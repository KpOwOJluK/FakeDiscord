//! Networking layer: the iroh endpoint, the control plane and the voice mesh.

pub mod control;
pub mod endpoint;
pub mod file;

use std::path::PathBuf;

use tokio::sync::mpsc;

use crate::proto::{ChannelId, ChatLine, FileOffer, PeerId, PeerInfo, RoomSnapshot, TransferId};

/// User actions coming from the interface.
///
/// The host and the joiner send the same commands; the only difference is which
/// constructor built the `Session`. The interface never has to know who is hosting.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Command {
    SwitchChannel(Option<ChannelId>),
    /// The channel travels explicitly: the user can switch channels while typing, and
    /// the message must land in the channel it was written in.
    Chat {
        channel: ChannelId,
        text: String,
    },
    /// Подготавливает локальный файл и публикует его метаданные в комнате.
    ShareFile {
        channel: ChannelId,
        recipient: Option<PeerId>,
        path: PathBuf,
    },
    /// Скачивает ранее объявленный файл напрямую у отправителя.
    DownloadFile {
        offer: FileOffer,
        channel: ChannelId,
    },
    SetMuted(bool),
    SetDeafened(bool),
    SetAfk(bool),
    /// Host-only: creates a fresh one-time enrollment invite.
    CreateInvite { label: String },
    /// Host-only: lists devices persisted in the server allowlist.
    ListAuthorized,
    /// Host-only: removes one device from the persistent allowlist.
    RevokeAuthorized { selector: String },
    Quit,
}

/// Events going out to the interface.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Event {
    Welcome {
        me: PeerId,
        room: RoomSnapshot,
    },
    Roster(Vec<PeerInfo>),
    Chat(ChatLine),
    /// Метаданные файла, который можно скачать напрямую у отправителя.
    FileOffer(FileOffer),
    /// Текущий прогресс файловой передачи.
    FileProgress {
        id: TransferId,
        name: String,
        transferred: u64,
        total: u64,
        receiving: bool,
    },
    /// Файл полностью получен и атомарно перемещён на итоговый путь.
    FileSaved {
        id: TransferId,
        name: String,
        path: PathBuf,
    },
    /// Локальная ошибка подготовки/передачи файла.
    FileFailed {
        id: Option<TransferId>,
        message: String,
    },
    Notice(String),
    /// The session is over — the coordinator shut down, the link dropped, or we were
    /// rejected.
    Disconnected(String),
}

/// Where the interface holds on to the control plane.
pub struct Session {
    pub me: PeerId,
    /// The invite code — our own when hosting, the room we joined otherwise.
    pub invite_code: String,
    pub commands: mpsc::Sender<Command>,
    pub events: mpsc::Receiver<Event>,
}

/// The clock the coordinator uses to order chat.
pub(crate) fn now() -> u64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map(|d| d.as_secs())
        .unwrap_or(0)
}
pub mod voice;
