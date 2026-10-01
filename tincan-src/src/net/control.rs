//! The control plane: the coordinator server and the joining client.
//!
//! The crucial point of the design: **the host's own actions go through the same
//! function as actions arriving from the network** (`apply`). With a shortcut just for
//! the host, the room the host sees and the room everyone else sees would drift apart.
//!
//! The flow:
//! ```text
//! coordinator                            joiner
//!     │── Ready ──────────────────────────▶│
//!     │◀── Hello{name, one-time token?} ───│
//!     │── Welcome{you, room} ─────────────▶│   (or Rejected)
//!     │── Roster / Chat / Notice ─────────▶│   (broadcast)
//!     │◀── SwitchChannel / Chat / Leave ───│
//! ```

use std::sync::Arc;
use std::time::Duration;

use anyhow::{Context, Result, bail, ensure};
use iroh::endpoint::{Connection, RecvStream, SendStream};
use iroh::{Endpoint, EndpointAddr};
use serde::Serialize;
use serde::de::DeserializeOwned;
use tokio::sync::{Mutex, broadcast, mpsc};
use tracing::{debug, warn};

use super::endpoint::to_peer_id;
use super::file::FileService;
use super::voice::VoiceMesh;
use super::{Command, Event, Session, now};
use crate::access::{Admission as AccessAdmission, ServerAccess};
use crate::clipboard;
use crate::invite::{self, InviteToken};
use crate::proto::{
    self, FileOffer, MAX_FILE_BYTES, MAX_FILE_NAME_CHARS, MAX_MESSAGE_BYTES, PeerId, ToCoordinator,
    ToPeer,
};
use crate::room::Room;

/// Depth of the broadcast channel. A slow client that falls this far behind is
/// resynchronised with a full roster to keep it consistent.
const BROADCAST_DEPTH: usize = 512;
/// The interface event queue.
const EVENT_DEPTH: usize = 256;
/// How long `join --retry` waits between attempts to reach the room.
const RETRY_PAUSE: Duration = Duration::from_secs(2);
/// What a join that never reached the room says.
const UNREACHABLE: &str =
    "could not reach the saved server — it may be offline or the pairing may be stale";

// ── Framing ─────────────────────────────────────────────────────────────────────

async fn write_msg<T: Serialize>(stream: &mut SendStream, message: &T) -> Result<()> {
    let framed = proto::encode(message)?;
    stream
        .write_all(&framed)
        .await
        .context("could not write to the stream")?;
    Ok(())
}

async fn read_msg<T: DeserializeOwned>(stream: &mut RecvStream) -> Result<T> {
    let mut header = [0u8; 4];
    stream
        .read_exact(&mut header)
        .await
        .context("the stream closed")?;
    let len = u32::from_le_bytes(header) as usize;
    if len > MAX_MESSAGE_BYTES {
        bail!("the other side announced a {len} byte message — over the limit");
    }
    let mut body = vec![0u8; len];
    stream
        .read_exact(&mut body)
        .await
        .context("the message was cut short")?;
    proto::decode(&body)
}

// ── Coordinator ────────────────────────────────────────────────────────────────

pub(crate) struct Shared {
    room: Mutex<Room>,
    /// The broadcast that reaches every connected peer **and** the host's own
    /// interface.
    broadcast: broadcast::Sender<ToPeer>,
    access: ServerAccess,
}

impl Shared {
    /// The single point of truth: every action is applied to the room here and the
    /// result is broadcast.
    async fn apply(&self, from: PeerId, message: ToCoordinator) -> Result<()> {
        let mut room = self.room.lock().await;
        match message {
            ToCoordinator::Hello { .. } => bail!("the handshake is already complete"),

            ToCoordinator::SwitchChannel { channel } => {
                room.switch_channel(&from, channel)?;
                let _ = self.broadcast.send(ToPeer::Roster {
                    peers: room.roster(),
                });
            }

            ToCoordinator::Chat { channel, text } => {
                let line = room.post_chat(&from, channel, &text, now())?;
                let _ = self.broadcast.send(ToPeer::Chat(line));
            }

            ToCoordinator::OfferFile {
                channel,
                id,
                recipient,
                name,
                size,
                preview,
                digest,
            } => {
                ensure!(room.get(&from).is_some(), "you are not in the room");
                ensure!(
                    room.channels().get(channel.0 as usize).is_some(),
                    "no such channel: {}",
                    channel.0
                );
                ensure!(size <= MAX_FILE_BYTES, "file is over the size limit");
                match &preview {
                    crate::proto::FilePreview::Generic { kind } => {
                        ensure!(kind.chars().count() <= 16, "file preview kind is too long");
                    }
                    crate::proto::FilePreview::Image {
                        thumb_width,
                        thumb_height,
                        rgb,
                        ..
                    } => {
                        ensure!(
                            *thumb_width <= 24 && *thumb_height <= 12,
                            "image preview dimensions are too large"
                        );
                        let expected = usize::from(*thumb_width)
                            .saturating_mul(usize::from(*thumb_height))
                            .saturating_mul(3);
                        ensure!(rgb.len() == expected, "image preview payload is malformed");
                    }
                }
                if let Some(target) = recipient {
                    ensure!(
                        room.get(&target).is_some(),
                        "the recipient is not in the room"
                    );
                    ensure!(
                        target != from,
                        "sending a private file to yourself is not useful"
                    );
                }

                let clean_name: String = name
                    .trim()
                    .chars()
                    .filter(|character| !character.is_control())
                    .map(|character| match character {
                        '/' | '\\' => '_',
                        other => other,
                    })
                    .take(MAX_FILE_NAME_CHARS)
                    .collect();
                ensure!(!clean_name.is_empty(), "file name is empty");

                let offer = FileOffer {
                    id,
                    channel,
                    from,
                    recipient,
                    name: clean_name,
                    size,
                    preview,
                    digest,
                };
                if offer.recipient.is_none() {
                    room.remember_public_file(offer.clone());
                }
                let _ = self.broadcast.send(ToPeer::FileOffer(offer));
            }

            ToCoordinator::SetMuted { muted } => {
                room.set_muted(&from, muted)?;
                let _ = self.broadcast.send(ToPeer::Roster {
                    peers: room.roster(),
                });
            }

            ToCoordinator::SetDeafened { deafened } => {
                room.set_deafened(&from, deafened)?;
                let _ = self.broadcast.send(ToPeer::Roster {
                    peers: room.roster(),
                });
            }

            ToCoordinator::SetAfk { afk } => {
                room.set_afk(&from, afk)?;
                let _ = self.broadcast.send(ToPeer::Roster {
                    peers: room.roster(),
                });
            }

            ToCoordinator::Leave => {
                if let Some(peer) = room.leave(&from) {
                    let _ = self.broadcast.send(ToPeer::Notice {
                        text: format!("{} left the room", peer.name),
                    });
                    let _ = self.broadcast.send(ToPeer::Roster {
                        peers: room.roster(),
                    });
                }
            }
        }
        Ok(())
    }
}

pub struct Coordinator;

impl Coordinator {
    /// Opens the room and starts accepting incoming connections.
    pub async fn spawn(
        endpoint: Endpoint,
        room: Room,
        access: ServerAccess,
        host_name: &str,
        voice: Option<VoiceMesh>,
    ) -> Result<Session> {
        let me = to_peer_id(endpoint.id());

        let mut room = room;
        room.join(me, host_name)
            .context("the host nickname is invalid")?;

        let (broadcast_tx, _) = broadcast::channel(BROADCAST_DEPTH);
        let shared = Arc::new(Shared {
            room: Mutex::new(room),
            broadcast: broadcast_tx,
            access,
        });

        let (event_tx, event_rx) = mpsc::channel(EVENT_DEPTH);
        let (command_tx, command_rx) = mpsc::channel(EVENT_DEPTH);

        // The host's interface listens to the same broadcast as everyone else.
        let snapshot = shared.room.lock().await.snapshot();
        event_tx
            .send(Event::Welcome { me, room: snapshot })
            .await
            .ok();
        tokio::spawn(pump_broadcast_to_ui(
            shared.broadcast.subscribe(),
            event_tx.clone(),
            me,
        ));

        let files = FileService::new(event_tx.clone());

        tokio::spawn(accept_loop(
            endpoint.clone(),
            Some(shared.clone()),
            voice,
            files.clone(),
        ));
        tokio::spawn(host_commands(
            shared, command_rx, event_tx, endpoint, me, files,
        ));

        Ok(Session {
            me,
            invite_code: String::new(),
            commands: command_tx,
            events: event_rx,
        })
    }
}

/// Обрабатывает локальные команды координатора.
/// Файловые операции запускаются отдельными задачами, чтобы хеширование и I/O
/// не блокировали чат, голос и UI.
async fn host_commands(
    shared: Arc<Shared>,
    mut commands: mpsc::Receiver<Command>,
    events: mpsc::Sender<Event>,
    endpoint: Endpoint,
    me: PeerId,
    files: FileService,
) {
    while let Some(command) = commands.recv().await {
        match command {
            Command::Quit => {
                let _ = shared.broadcast.send(ToPeer::Notice {
                    text: "the room is closing — the server stopped".into(),
                });
                tokio::time::sleep(std::time::Duration::from_millis(150)).await;
                super::endpoint::close_and_retract(&endpoint).await;
                let _ = events
                    .send(Event::Disconnected("the room was closed".into()))
                    .await;
                break;
            }
            Command::ShareFile {
                channel,
                recipient,
                path,
            } => {
                spawn_share(
                    files.clone(),
                    shared.clone(),
                    events.clone(),
                    me,
                    channel,
                    recipient,
                    path,
                );
            }
            Command::DownloadFile { offer, channel } => {
                spawn_download(
                    files.clone(),
                    endpoint.clone(),
                    events.clone(),
                    offer,
                    channel,
                );
            }
            Command::CreateInvite { label } => {
                match shared.access.create_invite(me, &label) {
                    Ok(invitation) => {
                        let code = invite::encode(&invitation);
                        let copied = clipboard::copy(&code);
                        let _ = events
                            .send(Event::Notice(format!(
                                "one-time invite for {}:",
                                label.trim()
                            )))
                            .await;
                        let _ = events.send(Event::Notice(code)).await;
                        if copied {
                            let _ = events
                                .send(Event::Notice("copied to clipboard".into()))
                                .await;
                        }
                    }
                    Err(err) => {
                        let _ = events
                            .send(Event::Notice(format!("could not create invite: {err}")))
                            .await;
                    }
                }
            }
            Command::ListAuthorized => match shared.access.list_devices() {
                Ok(devices) if devices.is_empty() => {
                    let _ = events
                        .send(Event::Notice("no authorized devices".into()))
                        .await;
                }
                Ok(devices) => {
                    let _ = events
                        .send(Event::Notice("authorized devices:".into()))
                        .await;
                    for device in devices {
                        let _ = events
                            .send(Event::Notice(format!(
                                "{}  {}  {}",
                                device.peer.short(), device.peer, device.label
                            )))
                            .await;
                    }
                }
                Err(err) => {
                    let _ = events
                        .send(Event::Notice(format!("could not list authorized devices: {err}")))
                        .await;
                }
            },
            Command::RevokeAuthorized { selector } => match shared.access.revoke(&selector) {
                Ok(device) => {
                    let _ = events
                        .send(Event::Notice(format!(
                            "revoked {} [{}]",
                            device.label,
                            device.peer.short()
                        )))
                        .await;
                }
                Err(err) => {
                    let _ = events
                        .send(Event::Notice(format!("could not revoke device: {err}")))
                        .await;
                }
            },
            other => {
                let Some(wire) = into_wire(other) else {
                    let _ = events
                        .send(Event::Notice("unsupported local command".into()))
                        .await;
                    continue;
                };
                if let Err(err) = shared.apply(me, wire).await {
                    let _ = events
                        .send(Event::Notice(format!("that did not work: {err}")))
                        .await;
                }
            }
        }
    }
    endpoint.close().await;
}

fn spawn_share(
    files: FileService,
    shared: Arc<Shared>,
    events: mpsc::Sender<Event>,
    me: PeerId,
    channel: crate::proto::ChannelId,
    recipient: Option<PeerId>,
    path: std::path::PathBuf,
) {
    tokio::spawn(async move {
        match files.prepare_offer(path, me, channel, recipient).await {
            Ok(offer) => {
                let id = offer.id;
                let wire = ToCoordinator::OfferFile {
                    channel: offer.channel,
                    id: offer.id,
                    recipient: offer.recipient,
                    name: offer.name,
                    size: offer.size,
                    preview: offer.preview,
                    digest: offer.digest,
                };
                if let Err(err) = shared.apply(me, wire).await {
                    let _ = events
                        .send(Event::FileFailed {
                            id: Some(id),
                            message: format!("could not publish file: {err}"),
                        })
                        .await;
                }
            }
            Err(err) => {
                let _ = events
                    .send(Event::FileFailed {
                        id: None,
                        message: format!("could not prepare file: {err}"),
                    })
                    .await;
            }
        }
    });
}

fn spawn_download(
    files: FileService,
    endpoint: Endpoint,
    events: mpsc::Sender<Event>,
    offer: FileOffer,
    channel: crate::proto::ChannelId,
) {
    tokio::spawn(async move {
        let id = offer.id;
        if let Err(err) = files.download(endpoint, offer, channel, None).await {
            let _ = events
                .send(Event::FileFailed {
                    id: Some(id),
                    message: format!("download failed: {err}"),
                })
                .await;
        }
    });
}

/// Routes incoming connections by their ALPN.
///
/// Один accept-loop обслуживает control, voice и file ALPN.
/// На обычном участнике `control` отсутствует: он принимает только voice/file.
pub(crate) async fn accept_loop(
    endpoint: Endpoint,
    control: Option<Arc<Shared>>,
    voice: Option<VoiceMesh>,
    files: FileService,
) {
    while let Some(incoming) = endpoint.accept().await {
        let control = control.clone();
        let voice = voice.clone();
        let files = files.clone();
        tokio::spawn(async move {
            let mut accepting = match incoming.accept() {
                Ok(accepting) => accepting,
                Err(err) => {
                    debug!("could not accept an incoming connection: {err:#}");
                    return;
                }
            };
            let alpn = match accepting.alpn().await {
                Ok(alpn) => alpn,
                Err(err) => {
                    debug!("could not read the ALPN: {err:#}");
                    return;
                }
            };
            let conn = match accepting.await {
                Ok(conn) => conn,
                Err(err) => {
                    debug!("an incoming connection failed to establish: {err:#}");
                    return;
                }
            };

            if alpn == proto::VOICE_ALPN {
                match voice {
                    Some(mesh) => mesh.accept(conn),
                    None => debug!("a voice connection arrived but audio is not enabled"),
                }
                return;
            }

            if alpn == proto::FILE_ALPN {
                if let Err(err) = files.accept(conn).await {
                    debug!("file connection ended with an error: {err:#}");
                }
                return;
            }

            let Some(shared) = control else {
                debug!("a control connection arrived but we are not the coordinator");
                return;
            };
            let peer = to_peer_id(conn.remote_id());
            if let Err(err) = serve_peer(shared.clone(), conn, peer).await {
                debug!("{} ile oturum bitti: {err:#}", peer.short());
            }
            // However the connection ends (gracefully or not), the roster is cleaned.
            let _ = shared.apply(peer, ToCoordinator::Leave).await;
        });
    }
}

/// Turns an applicant away, with a reason.
///
/// To **make sure the reason arrives**, the stream is finished and we wait for it to be
/// read; otherwise the message is lost as the connection closes and the user sees a
/// meaningless transport error instead of the authorization reason.
async fn reject(mut send: SendStream, reason: &str) -> Result<()> {
    write_msg(
        &mut send,
        &ToPeer::Rejected {
            reason: reason.to_string(),
        },
    )
    .await?;
    send.finish().ok();
    let _ = tokio::time::timeout(std::time::Duration::from_secs(5), send.stopped()).await;
    Ok(())
}

async fn serve_peer(shared: Arc<Shared>, conn: Connection, peer: PeerId) -> Result<()> {
    let (mut send, mut recv) = conn
        .open_bi()
        .await
        .context("could not open the control stream")?;

    // A server-opened QUIC stream is only observable by the peer after the first write.
    // Ready carries no secret; authentication of the remote device already comes from QUIC.
    write_msg(&mut send, &ToPeer::Ready).await?;

    let hello: ToCoordinator = read_msg(&mut recv).await?;
    let ToCoordinator::Hello { name, invite } = hello else {
        bail!("a different message arrived instead of the handshake");
    };

    // Do not burn a one-time token for a malformed nickname. Room::join has the
    // same normalization rule; duplicate valid names are disambiguated, not rejected.
    let normalized_name: String = name
        .trim()
        .chars()
        .filter(|character| !character.is_control())
        .take(crate::proto::MAX_NAME_CHARS)
        .collect();
    if normalized_name.trim().is_empty() {
        return reject(send, "a nickname cannot be empty").await;
    }

    match shared.access.admit(peer, invite)? {
        AccessAdmission::Known => {}
        AccessAdmission::Enrolled { ref label } => {
            debug!("{} enrolled as {label}", peer.short());
        }
        AccessAdmission::Denied => {
            warn!("unauthorized device {} tried to connect", peer.short());
            return reject(
                send,
                "this device is not authorized; ask the host for a new one-time invite",
            )
            .await;
        }
    }

    // Subscribe before taking the room snapshot. Any offer created while this
    // handshake is being written will then be queued rather than lost.
    let mut updates = shared.broadcast.subscribe();
    let (display_name, snapshot, public_files) = {
        let mut room = shared.room.lock().await;
        match room.join(peer, &name) {
            Ok(display_name) => (display_name, room.snapshot(), room.public_files()),
            Err(err) => {
                let reason = err.to_string();
                drop(room);
                return reject(send, &reason).await;
            }
        }
    };

    write_msg(
        &mut send,
        &ToPeer::Welcome {
            you: peer,
            room: snapshot,
        },
    )
    .await?;

    // Stream durable public history one offer per control frame. This keeps the
    // Welcome frame small even after a room has accumulated thousands of files.
    for offer in public_files {
        write_msg(&mut send, &ToPeer::FileOffer(offer)).await?;
    }
    let _ = shared.broadcast.send(ToPeer::Notice {
        text: format!("{display_name} joined the room"),
    });
    {
        let room = shared.room.lock().await;
        let _ = shared.broadcast.send(ToPeer::Roster {
            peers: room.roster(),
        });
    }

    // The write side that carries the broadcast to this peer.
    let writer = tokio::spawn(async move {
        loop {
            match updates.recv().await {
                Ok(message) => {
                    if !message_visible_to(&message, peer) {
                        continue;
                    }
                    if write_msg(&mut send, &message).await.is_err() {
                        return;
                    }
                }
                Err(broadcast::error::RecvError::Lagged(skipped)) => {
                    // Safer to make a lagging client rebuild its state than to send
                    // it an incomplete stream of messages.
                    debug!("{} fell {skipped} messages behind", peer.short());
                    let notice = ToPeer::Notice {
                        text: "your connection slowed down, some messages were skipped".into(),
                    };
                    if write_msg(&mut send, &notice).await.is_err() {
                        return;
                    }
                }
                Err(broadcast::error::RecvError::Closed) => return,
            }
        }
    });

    // The read side: handle commands until the peer goes away.
    let result = async {
        loop {
            let message: ToCoordinator = read_msg(&mut recv).await?;
            let leaving = matches!(message, ToCoordinator::Leave);
            if let Err(err) = shared.apply(peer, message).await {
                debug!("{} komutu reddedildi: {err:#}", peer.short());
            }
            if leaving {
                return Ok(());
            }
        }
    }
    .await;

    writer.abort();
    result
}

// ── Joining client ─────────────────────────────────────────────────────────────

pub struct Client;

impl Client {
    /// Connects to the coordinator and completes device authorization.
    /// A one-time token is only present on the first enrollment.
    ///
    /// In normal use the target is just an identity and discovery finds its address.
    /// Tests skip discovery by passing a full `EndpointAddr`.
    pub async fn connect(
        endpoint: Endpoint,
        target: impl Into<EndpointAddr>,
        invite: Option<InviteToken>,
        name: &str,
        voice: Option<VoiceMesh>,
    ) -> Result<Session> {
        Self::connect_patiently(endpoint, target, invite, name, voice, Duration::ZERO, |_| {}).await
    }

    /// [`connect`](Self::connect), but a room that cannot be reached yet is tried again
    /// until `patience` runs out, with `waiting` told how long is left before each new
    /// attempt. This is `join --retry`: a script that starts the host and the joiner
    /// together should not have to know which one comes up first.
    ///
    /// Only reaching the room is retried. A room that answers and turns you away has
    /// said all it will say, and asking again would not change it.
    pub async fn connect_patiently(
        endpoint: Endpoint,
        target: impl Into<EndpointAddr>,
        invite: Option<InviteToken>,
        name: &str,
        voice: Option<VoiceMesh>,
        patience: Duration,
        mut waiting: impl FnMut(Duration),
    ) -> Result<Session> {
        let target: EndpointAddr = target.into();
        let deadline = tokio::time::Instant::now() + patience;
        let conn = loop {
            let attempt = endpoint.connect(target.clone(), proto::ALPN);
            // One attempt at a room that is gone takes iroh's own 30 s to give up, which
            // would carry a short `--retry` well past its end.
            let reached = if patience.is_zero() {
                attempt.await.map_err(anyhow::Error::from)
            } else {
                match tokio::time::timeout_at(deadline, attempt).await {
                    Ok(reached) => reached.map_err(anyhow::Error::from),
                    Err(_) => Err(anyhow::anyhow!("gave up after {} s", patience.as_secs())),
                }
            };
            match reached {
                Ok(conn) => break conn,
                Err(err) => {
                    let left = deadline.saturating_duration_since(tokio::time::Instant::now());
                    if left <= RETRY_PAUSE {
                        return Err(err).context(UNREACHABLE);
                    }
                    debug!("the room is not reachable yet: {err:#}");
                    waiting(left);
                    tokio::time::sleep(RETRY_PAUSE).await;
                }
            }
        };

        let (mut send, mut recv) = conn
            .accept_bi()
            .await
            .context("could not establish the control stream")?;

        let greeting: ToPeer = read_msg(&mut recv).await?;
        if !matches!(greeting, ToPeer::Ready) {
            bail!("unexpected greeting message");
        }

        write_msg(
            &mut send,
            &ToCoordinator::Hello {
                name: name.to_string(),
                invite,
            },
        )
        .await?;

        let (me, snapshot) = match read_msg::<ToPeer>(&mut recv).await? {
            ToPeer::Welcome { you, room } => (you, room),
            ToPeer::Rejected { reason } => bail!("you were not let into the room: {reason}"),
            _ => bail!("unexpected reply"),
        };

        let (event_tx, event_rx) = mpsc::channel(EVENT_DEPTH);
        let (command_tx, command_rx) = mpsc::channel(EVENT_DEPTH);

        event_tx
            .send(Event::Welcome { me, room: snapshot })
            .await
            .ok();

        // Joiners use the same accept-loop shape as the coordinator, only without
        // a control server: incoming voice and file links are both peer-to-peer.
        let files = FileService::new(event_tx.clone());
        tokio::spawn(accept_loop(endpoint.clone(), None, voice, files.clone()));
        tokio::spawn(client_reader(recv, event_tx.clone(), endpoint.clone(), me));
        tokio::spawn(client_writer(
            send, command_rx, conn, endpoint, event_tx, me, files,
        ));

        Ok(Session {
            me,
            invite_code: String::new(),
            commands: command_tx,
            events: event_rx,
        })
    }
}

async fn client_reader(
    mut recv: RecvStream,
    events: mpsc::Sender<Event>,
    endpoint: Endpoint,
    me: PeerId,
) {
    loop {
        match read_msg::<ToPeer>(&mut recv).await {
            Ok(message) => {
                if !message_visible_to(&message, me) {
                    continue;
                }
                if let Some(event) = wire_to_event(message)
                    && events.send(event).await.is_err()
                {
                    break;
                }
            }
            Err(_) => {
                // The QUIC-level reason for a drop tells the user nothing; in
                // practice it only ever means the room has closed.
                let _ = events
                    .send(Event::Disconnected(
                        "lost contact with the room — the server may have stopped".into(),
                    ))
                    .await;
                break;
            }
        }
    }
    endpoint.close().await;
}

async fn client_writer(
    mut send: SendStream,
    mut commands: mpsc::Receiver<Command>,
    conn: Connection,
    endpoint: Endpoint,
    events: mpsc::Sender<Event>,
    me: PeerId,
    files: FileService,
) {
    while let Some(command) = commands.recv().await {
        match command {
            Command::ShareFile {
                channel,
                recipient,
                path,
            } => {
                let offer = match files.prepare_offer(path, me, channel, recipient).await {
                    Ok(offer) => offer,
                    Err(err) => {
                        let _ = events
                            .send(Event::FileFailed {
                                id: None,
                                message: format!("could not prepare file: {err}"),
                            })
                            .await;
                        continue;
                    }
                };

                let wire = ToCoordinator::OfferFile {
                    channel: offer.channel,
                    id: offer.id,
                    recipient: offer.recipient,
                    name: offer.name,
                    size: offer.size,
                    preview: offer.preview,
                    digest: offer.digest,
                };

                if write_msg(&mut send, &wire).await.is_err() {
                    let _ = events
                        .send(Event::Disconnected("cannot reach the server".into()))
                        .await;
                    break;
                }
            }
            Command::DownloadFile { offer, channel } => {
                spawn_download(
                    files.clone(),
                    endpoint.clone(),
                    events.clone(),
                    offer,
                    channel,
                );
            }
            Command::CreateInvite { .. }
            | Command::ListAuthorized
            | Command::RevokeAuthorized { .. } => {
                let _ = events
                    .send(Event::Notice(
                        "this command is available only on the private-server host".into(),
                    ))
                    .await;
            }
            other => {
                let quitting = matches!(other, Command::Quit);
                let Some(wire) = into_wire(other) else {
                    let _ = events
                        .send(Event::Notice("unsupported local command".into()))
                        .await;
                    continue;
                };
                if write_msg(&mut send, &wire).await.is_err() {
                    let _ = events
                        .send(Event::Disconnected("cannot reach the server".into()))
                        .await;
                    break;
                }
                if quitting {
                    let _ = send.finish();
                    conn.close(0u32.into(), b"ayrildi");
                    let _ = events
                        .send(Event::Disconnected("you left the room".into()))
                        .await;
                    break;
                }
            }
        }
    }
    endpoint.close().await;
}

// ── Conversions ────────────────────────────────────────────────────────────────

fn into_wire(command: Command) -> Option<ToCoordinator> {
    match command {
        Command::SwitchChannel(channel) => Some(ToCoordinator::SwitchChannel { channel }),
        Command::Chat { channel, text } => Some(ToCoordinator::Chat { channel, text }),
        Command::ShareFile { .. }
        | Command::DownloadFile { .. }
        | Command::CreateInvite { .. }
        | Command::ListAuthorized
        | Command::RevokeAuthorized { .. } => None,
        Command::SetMuted(muted) => Some(ToCoordinator::SetMuted { muted }),
        Command::SetDeafened(deafened) => Some(ToCoordinator::SetDeafened { deafened }),
        Command::SetAfk(afk) => Some(ToCoordinator::SetAfk { afk }),
        Command::Quit => Some(ToCoordinator::Leave),
    }
}

fn message_visible_to(message: &ToPeer, peer: PeerId) -> bool {
    match message {
        ToPeer::FileOffer(offer) => {
            offer.recipient.is_none() || offer.from == peer || offer.recipient == Some(peer)
        }
        _ => true,
    }
}

fn wire_to_event(message: ToPeer) -> Option<Event> {
    match message {
        ToPeer::Roster { peers } => Some(Event::Roster(peers)),
        ToPeer::Chat(line) => Some(Event::Chat(line)),
        ToPeer::FileOffer(offer) => Some(Event::FileOffer(offer)),
        ToPeer::Notice { text } => Some(Event::Notice(text)),
        ToPeer::Rejected { reason } => Some(Event::Disconnected(reason)),
        // Welcome and Ready are only meaningful during the handshake.
        ToPeer::Welcome { .. } | ToPeer::Ready => None,
    }
}

async fn pump_broadcast_to_ui(
    mut updates: broadcast::Receiver<ToPeer>,
    events: mpsc::Sender<Event>,
    me: PeerId,
) {
    loop {
        match updates.recv().await {
            Ok(message) => {
                if !message_visible_to(&message, me) {
                    continue;
                }
                if let Some(event) = wire_to_event(message)
                    && events.send(event).await.is_err()
                {
                    return;
                }
            }
            Err(broadcast::error::RecvError::Lagged(_)) => continue,
            Err(broadcast::error::RecvError::Closed) => return,
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::proto::{ChannelId, FilePreview, TransferId};

    fn offer(recipient: Option<PeerId>) -> ToPeer {
        ToPeer::FileOffer(FileOffer {
            id: TransferId([7; 16]),
            channel: ChannelId(1),
            from: PeerId([1; 32]),
            recipient,
            name: "secret.bin".into(),
            size: 12,
            preview: FilePreview::Generic { kind: "BIN".into() },
            digest: [9; 32],
        })
    }

    #[test]
    fn host_only_commands_never_reach_wire_serialization() {
        assert!(into_wire(Command::CreateInvite { label: "guest".into() }).is_none());
        assert!(into_wire(Command::ListAuthorized).is_none());
        assert!(into_wire(Command::RevokeAuthorized { selector: "guest".into() }).is_none());
    }

    #[test]
    fn private_file_offer_is_only_visible_to_sender_and_recipient() {
        let recipient = PeerId([2; 32]);
        let stranger = PeerId([3; 32]);
        let private = offer(Some(recipient));

        assert!(message_visible_to(&private, PeerId([1; 32])));
        assert!(message_visible_to(&private, recipient));
        assert!(!message_visible_to(&private, stranger));

        let public = offer(None);
        assert!(message_visible_to(&public, stranger));
    }
}
