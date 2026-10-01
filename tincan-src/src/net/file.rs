//! Прямой P2P file plane поверх того же iroh Endpoint, который использует Tincan.
//!
//! Control plane сообщает только метаданные FileOffer. Получатель сам подключается
//! к отправителю по FILE_ALPN и забирает поток байтов напрямую.

use std::collections::HashMap;
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;

use anyhow::{Context, Result, bail, ensure};
use blake2::{Blake2s256, Digest};
use iroh::Endpoint;
use iroh::endpoint::{Connection, RecvStream, SendStream};
use rand::Rng;
use serde::Serialize;
use serde::de::DeserializeOwned;
use tokio::fs::{self, File};
use tokio::io::{AsyncReadExt, AsyncWriteExt};
use tokio::sync::{RwLock, mpsc};
use tracing::debug;

use super::Event;
use super::endpoint::{to_endpoint_id, to_peer_id};
use crate::proto::{
    self, ChannelId, FileOffer, FilePreview, FileRequest, FileResponse, MAX_FILE_BYTES,
    MAX_FILE_NAME_CHARS, MAX_MESSAGE_BYTES, PeerId, TransferId,
};

const COPY_BUFFER: usize = 64 * 1024;
const PROGRESS_STEP: u64 = 1024 * 1024;
const GIB: u64 = 1024 * 1024 * 1024;
const DEFAULT_SEND_LIMIT_BYTES: u64 = 8 * GIB;
static SEND_LIMIT_BYTES: AtomicU64 = AtomicU64::new(DEFAULT_SEND_LIMIT_BYTES);

/// Меняет локальный лимит отправки. Протокольный потолок остаётся 16 GiB.
pub fn set_send_limit_gib(gib: u64) -> Result<()> {
    ensure!((1..=16).contains(&gib), "file send limit must be between 1 and 16 GiB");
    SEND_LIMIT_BYTES.store(gib * GIB, Ordering::Relaxed);
    Ok(())
}

pub fn send_limit_bytes() -> u64 {
    SEND_LIMIT_BYTES.load(Ordering::Relaxed).min(MAX_FILE_BYTES)
}

/// Локальный файл, который этот клиент согласился раздавать участникам комнаты.
#[derive(Clone)]
struct SharedFile {
    path: PathBuf,
    offer: FileOffer,
}

/// Клонируемый сервис, обслуживающий исходящие и входящие файловые передачи.
#[derive(Clone)]
pub struct FileService {
    offered: Arc<RwLock<HashMap<TransferId, SharedFile>>>,
    events: mpsc::Sender<Event>,
}

impl FileService {
    /// Создаёт пустой каталог раздач и привязывает события к UI текущей сессии.
    pub fn new(events: mpsc::Sender<Event>) -> Self {
        Self {
            offered: Arc::new(RwLock::new(HashMap::new())),
            events,
        }
    }

    /// Проверяет файл, вычисляет его digest и сохраняет путь только локально.
    pub async fn prepare_offer(
        &self,
        path: PathBuf,
        from: PeerId,
        channel: ChannelId,
        recipient: Option<PeerId>,
    ) -> Result<FileOffer> {
        let metadata = fs::metadata(&path)
            .await
            .with_context(|| format!("cannot read {}", path.display()))?;
        ensure!(
            metadata.is_file(),
            "the selected path is not a regular file"
        );
        let send_limit = send_limit_bytes();
        ensure!(
            metadata.len() <= send_limit,
            "file is too large: {} bytes (your send limit is {})",
            metadata.len(),
            send_limit
        );

        let name = safe_file_name(&path)?;
        ensure!(
            name.chars().count() <= MAX_FILE_NAME_CHARS,
            "file name is too long"
        );

        let digest = hash_file(&path).await?;
        let preview_path = path.clone();
        let preview_name = name.clone();
        let preview =
            tokio::task::spawn_blocking(move || build_preview(&preview_path, &preview_name))
                .await
                .unwrap_or_else(|_| generic_preview(&name));
        let mut random = [0u8; 16];
        rand::rng().fill_bytes(&mut random);
        let id = TransferId(random);

        let offer = FileOffer {
            id,
            channel,
            from,
            recipient,
            name,
            size: metadata.len(),
            preview,
            digest,
        };

        self.offered.write().await.insert(
            id,
            SharedFile {
                path,
                offer: offer.clone(),
            },
        );

        Ok(offer)
    }

    /// Обслуживает одно входящее FILE_ALPN соединение.
    pub async fn accept(&self, connection: Connection) -> Result<()> {
        let remote = to_peer_id(connection.remote_id());
        let (mut send, mut recv) = connection
            .accept_bi()
            .await
            .context("could not accept the file stream")?;

        let request: FileRequest = read_message(&mut recv).await?;
        let shared = self.offered.read().await.get(&request.id).cloned();

        let Some(shared) = shared else {
            write_message(
                &mut send,
                &FileResponse::Rejected {
                    reason: "file offer is no longer available".into(),
                },
            )
            .await?;
            send.finish().ok();
            let _ = tokio::time::timeout(Duration::from_secs(5), send.stopped()).await;
            return Ok(());
        };

        let allowed = match shared.offer.recipient {
            Some(recipient) => remote == recipient,
            None => request.channel == shared.offer.channel,
        };
        if !allowed {
            write_message(
                &mut send,
                &FileResponse::Rejected {
                    reason: if shared.offer.recipient.is_some() {
                        "this private file was sent to another participant".into()
                    } else {
                        "open the channel where this file was posted before downloading it".into()
                    },
                },
            )
            .await?;
            send.finish().ok();
            let _ = tokio::time::timeout(Duration::from_secs(5), send.stopped()).await;
            return Ok(());
        }

        write_message(
            &mut send,
            &FileResponse::Accepted {
                name: shared.offer.name.clone(),
                size: shared.offer.size,
                digest: shared.offer.digest,
            },
        )
        .await?;

        let mut file = File::open(&shared.path)
            .await
            .with_context(|| format!("cannot open {}", shared.path.display()))?;
        let mut buffer = vec![0u8; COPY_BUFFER];
        let mut sent = 0u64;
        let mut last_report = 0u64;

        while sent < shared.offer.size {
            let remaining = shared.offer.size - sent;
            let wanted = buffer.len().min(remaining as usize);
            let count = file
                .read(&mut buffer[..wanted])
                .await
                .context("could not read the shared file")?;

            if count == 0 {
                bail!("the shared file became shorter while it was being sent");
            }

            send.write_all(&buffer[..count])
                .await
                .context("the file connection closed while sending")?;
            sent += count as u64;

            if sent == shared.offer.size || sent - last_report >= PROGRESS_STEP {
                last_report = sent;
                self.report_progress(&shared.offer, sent, false).await;
            }
        }

        send.finish().context("could not finish the file stream")?;

        // Не отпускаем последний Connection сразу после FIN: iroh закрывает
        // соединение при уничтожении handle, а получатель мог ещё не дочитать
        // данные из QUIC. Ждём, пока peer подтвердит завершение stream.
        let _ = tokio::time::timeout(Duration::from_secs(30), send.stopped()).await;

        debug!(
            "sent file {} ({}) to {}",
            shared.offer.name,
            shared.offer.id.short(),
            remote.short()
        );
        Ok(())
    }

    /// Скачивает FileOffer напрямую у его владельца и возвращает итоговый путь.
    pub async fn download(
        &self,
        endpoint: Endpoint,
        offer: FileOffer,
        channel: ChannelId,
        directory: Option<PathBuf>,
    ) -> Result<PathBuf> {
        ensure!(
            offer.from != to_peer_id(endpoint.id()),
            "this file is already local"
        );
        ensure!(
            offer.size <= MAX_FILE_BYTES,
            "announced file is over the size limit"
        );

        let target = to_endpoint_id(&offer.from)?;
        let connection = endpoint
            .connect(target, proto::FILE_ALPN)
            .await
            .with_context(|| format!("could not reach {}", offer.from.short()))?;
        let (mut send, mut recv) = connection
            .open_bi()
            .await
            .context("could not open the file stream")?;

        write_message(
            &mut send,
            &FileRequest {
                id: offer.id,
                channel,
            },
        )
        .await?;
        send.finish().context("could not finish the file request")?;

        let response: FileResponse = read_message(&mut recv).await?;
        match response {
            FileResponse::Rejected { reason } => bail!("sender refused the file: {reason}"),
            FileResponse::Accepted { name, size, digest } => {
                ensure!(name == offer.name, "sender changed the file name");
                ensure!(size == offer.size, "sender changed the file size");
                ensure!(digest == offer.digest, "sender changed the file digest");
            }
        }

        let directory = directory.unwrap_or_else(default_download_dir);
        fs::create_dir_all(&directory)
            .await
            .with_context(|| format!("cannot create {}", directory.display()))?;

        let final_path = unique_destination(&directory, &offer.name).await;
        let temp_path = partial_path(&final_path, offer.id);
        let result = self.receive_body(&mut recv, &offer, &temp_path).await;

        if let Err(err) = result {
            let _ = fs::remove_file(&temp_path).await;
            return Err(err);
        }

        fs::rename(&temp_path, &final_path)
            .await
            .with_context(|| format!("cannot finalize {}", final_path.display()))?;

        let _ = self
            .events
            .send(Event::FileSaved {
                id: offer.id,
                name: offer.name.clone(),
                path: final_path.clone(),
            })
            .await;

        Ok(final_path)
    }

    /// Принимает ровно заявленное число байтов и проверяет BLAKE2s-256.
    async fn receive_body(
        &self,
        recv: &mut RecvStream,
        offer: &FileOffer,
        temp_path: &Path,
    ) -> Result<()> {
        let mut output = File::create(temp_path)
            .await
            .with_context(|| format!("cannot create {}", temp_path.display()))?;
        let mut hasher = Blake2s256::new();
        let mut buffer = vec![0u8; COPY_BUFFER];
        let mut received = 0u64;
        let mut last_report = 0u64;

        while received < offer.size {
            let remaining = offer.size - received;
            let wanted = buffer.len().min(remaining as usize);
            recv.read_exact(&mut buffer[..wanted])
                .await
                .context("the file stream ended early")?;
            output
                .write_all(&buffer[..wanted])
                .await
                .context("could not write the downloaded file")?;
            hasher.update(&buffer[..wanted]);
            received += wanted as u64;

            if received == offer.size || received - last_report >= PROGRESS_STEP {
                last_report = received;
                self.report_progress(offer, received, true).await;
            }
        }

        output
            .flush()
            .await
            .context("could not flush the downloaded file")?;
        drop(output);

        let digest: [u8; 32] = hasher.finalize().into();
        ensure!(digest == offer.digest, "file digest mismatch");
        Ok(())
    }

    async fn report_progress(&self, offer: &FileOffer, transferred: u64, receiving: bool) {
        let _ = self
            .events
            .send(Event::FileProgress {
                id: offer.id,
                name: offer.name.clone(),
                transferred,
                total: offer.size,
                receiving,
            })
            .await;
    }
}

/// Читает небольшой postcard-frame перед бинарным телом файла.
async fn read_message<T: DeserializeOwned>(stream: &mut RecvStream) -> Result<T> {
    let mut header = [0u8; 4];
    stream
        .read_exact(&mut header)
        .await
        .context("file protocol header was cut short")?;

    let length = u32::from_le_bytes(header) as usize;
    ensure!(
        length <= MAX_MESSAGE_BYTES,
        "file protocol message is too large"
    );

    let mut body = vec![0u8; length];
    stream
        .read_exact(&mut body)
        .await
        .context("file protocol message was cut short")?;
    proto::decode(&body)
}

/// Записывает небольшой postcard-frame в служебную часть file protocol.
async fn write_message<T: Serialize>(stream: &mut SendStream, message: &T) -> Result<()> {
    let framed = proto::encode(message)?;
    stream
        .write_all(&framed)
        .await
        .context("could not write the file protocol message")?;
    Ok(())
}

const PREVIEW_W: u32 = 24;
const PREVIEW_H: u32 = 12;

fn generic_preview(name: &str) -> FilePreview {
    let kind = Path::new(name)
        .extension()
        .and_then(|ext| ext.to_str())
        .filter(|ext| !ext.is_empty())
        .map(|ext| ext.to_ascii_uppercase())
        .unwrap_or_else(|| "FILE".into());
    FilePreview::Generic { kind }
}

fn build_preview(path: &Path, name: &str) -> FilePreview {
    const MAX_PREVIEW_SOURCE_PIXELS: u64 = 16_000_000;

    let Ok((source_width, source_height)) = image::image_dimensions(path) else {
        return generic_preview(name);
    };
    if u64::from(source_width).saturating_mul(u64::from(source_height)) > MAX_PREVIEW_SOURCE_PIXELS
    {
        return generic_preview(name);
    }

    let Ok(reader) = image::ImageReader::open(path) else {
        return generic_preview(name);
    };
    let Ok(reader) = reader.with_guessed_format() else {
        return generic_preview(name);
    };
    let Ok(image) = reader.decode() else {
        return generic_preview(name);
    };
    let width = source_width;
    let height = source_height;
    let thumb = image
        .resize(PREVIEW_W, PREVIEW_H, image::imageops::FilterType::Triangle)
        .to_rgb8();
    FilePreview::Image {
        width,
        height,
        thumb_width: thumb.width() as u8,
        thumb_height: thumb.height() as u8,
        rgb: thumb.into_raw(),
    }
}

/// Вычисляет BLAKE2s-256, не загружая весь файл в память.
async fn hash_file(path: &Path) -> Result<[u8; 32]> {
    let mut file = File::open(path)
        .await
        .with_context(|| format!("cannot open {}", path.display()))?;
    let mut hasher = Blake2s256::new();
    let mut buffer = vec![0u8; COPY_BUFFER];

    loop {
        let count = file
            .read(&mut buffer)
            .await
            .context("could not hash the file")?;
        if count == 0 {
            break;
        }
        hasher.update(&buffer[..count]);
    }

    Ok(hasher.finalize().into())
}

/// Никогда не доверяет каталогу, пришедшему от удалённого клиента.
fn safe_file_name(path: &Path) -> Result<String> {
    let name = path
        .file_name()
        .and_then(|value| value.to_str())
        .map(str::trim)
        .filter(|value| !value.is_empty())
        .ok_or_else(|| anyhow::anyhow!("file has no usable UTF-8 name"))?;

    ensure!(name != "." && name != "..", "unsafe file name");
    Ok(name.to_string())
}

/// Каталог загрузок одинаково предсказуем на Windows и Arch Linux.
pub fn default_download_dir() -> PathBuf {
    if let Some(custom) = std::env::var_os("FAKEDISCORD_DOWNLOAD_DIR") {
        return PathBuf::from(custom);
    }

    #[cfg(windows)]
    if let Some(home) = std::env::var_os("USERPROFILE") {
        return PathBuf::from(home).join("Downloads").join("FakeDiscord");
    }

    #[cfg(unix)]
    if let Some(home) = std::env::var_os("HOME") {
        return PathBuf::from(home).join("Downloads").join("FakeDiscord");
    }

    std::env::current_dir()
        .unwrap_or_else(|_| PathBuf::from("."))
        .join("FakeDiscord Downloads")
}

async fn unique_destination(directory: &Path, name: &str) -> PathBuf {
    let original = directory.join(name);
    if fs::metadata(&original).await.is_err() {
        return original;
    }

    let source = Path::new(name);
    let stem = source
        .file_stem()
        .and_then(|value| value.to_str())
        .unwrap_or("file");
    let extension = source.extension().and_then(|value| value.to_str());

    for index in 1..10_000u32 {
        let candidate_name = match extension {
            Some(extension) => format!("{stem} ({index}).{extension}"),
            None => format!("{stem} ({index})"),
        };
        let candidate = directory.join(candidate_name);
        if fs::metadata(&candidate).await.is_err() {
            return candidate;
        }
    }

    directory.join(format!("{}-{}", TransferId([0xff; 16]).short(), name))
}

fn partial_path(final_path: &Path, id: TransferId) -> PathBuf {
    let mut name = final_path
        .file_name()
        .and_then(|value| value.to_str())
        .unwrap_or("download")
        .to_string();
    name.push_str(&format!(".{}.part", id.short()));
    final_path.with_file_name(name)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn strips_directories_from_remote_names() {
        assert_eq!(
            safe_file_name(Path::new("folder/example.png")).unwrap(),
            "example.png"
        );
    }

    #[test]
    fn transfer_id_has_short_and_full_forms() {
        let id = TransferId([0xab; 16]);
        assert_eq!(id.short(), "abababab");
        assert_eq!(id.to_string().len(), 32);
    }
}
