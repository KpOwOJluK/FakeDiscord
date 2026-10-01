//! End-to-end tests for the control plane: two real iroh endpoints, a real QUIC
//! connection, a real handshake — but with no relays and no discovery, entirely local.

use std::collections::HashMap;
use std::sync::{LazyLock, Mutex};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;

use anyhow::{Result, bail};
use iroh::EndpointAddr;
use tincan::access::ServerAccess;
use tincan::invite::InviteToken;
use tincan::net::control::{Client, Coordinator};
use tincan::net::endpoint::{bind_offline, bind_offline_as, to_peer_id};
use tincan::net::{Command, Event, Session};
use tincan::proto::{ChannelId, PeerInfo};
use tincan::room::Room;

/// The upper bound used when waiting for events, so tests cannot hang.
const PATIENCE: Duration = Duration::from_secs(10);

fn test_room() -> Room {
    Room::new("test room", vec!["general".into(), "gaming".into()]).unwrap()
}

static ACCESS_COUNTER: AtomicU64 = AtomicU64::new(1);
static TOKENS: LazyLock<Mutex<HashMap<(String, String), InviteToken>>> =
    LazyLock::new(|| Mutex::new(HashMap::new()));

/// Creates an isolated access store and one pending invite for the following client.
fn admits(addr: &EndpointAddr, label: &str) -> ServerAccess {
    let serial = ACCESS_COUNTER.fetch_add(1, Ordering::Relaxed);
    let path = std::env::temp_dir()
        .join(format!("tincan-control-{serial}-{}", std::process::id()))
        .join("access.toml");
    let access = ServerAccess::at(path).unwrap();
    let stored_label = if label.trim().is_empty() {
        "test-device"
    } else {
        label
    };
    let invitation = access
        .create_invite(to_peer_id(addr.id), stored_label)
        .unwrap();
    TOKENS
        .lock()
        .unwrap()
        .insert((addr.id.to_string(), label.to_string()), invitation.token);
    access
}

/// Returns the token created by `admits`; an unknown label intentionally yields junk.
fn key_for(addr: &EndpointAddr, label: &str) -> Option<InviteToken> {
    Some(
        TOKENS
            .lock()
            .unwrap()
            .get(&(addr.id.to_string(), label.to_string()))
            .copied()
            .unwrap_or([0xa5; 32]),
    )
}

/// Waits for the first event matching a predicate, swallowing the others on the way.
async fn wait_for<T>(
    session: &mut Session,
    what: &str,
    mut matcher: impl FnMut(Event) -> Option<T>,
) -> Result<T> {
    let deadline = tokio::time::Instant::now() + PATIENCE;
    loop {
        let event = match tokio::time::timeout_at(deadline, session.events.recv()).await {
            Ok(Some(event)) => event,
            Ok(None) => bail!("the event channel closed while waiting for: {what}"),
            Err(_) => bail!("timed out waiting for: {what}"),
        };
        if let Event::Disconnected(reason) = &event {
            bail!("beklenmedik kopma ({reason}), beklenen: {what}");
        }
        if let Some(found) = matcher(event) {
            return Ok(found);
        }
    }
}

async fn wait_for_roster(session: &mut Session, count: usize) -> Result<Vec<PeerInfo>> {
    wait_for(
        session,
        &format!("a roster of {count}"),
        |event| match event {
            Event::Roster(peers) if peers.len() == count => Some(peers),
            _ => None,
        },
    )
    .await
}

async fn wait_for_chat(session: &mut Session, text: &str) -> Result<()> {
    let text = text.to_string();
    wait_for(
        session,
        &format!("chat: {text}"),
        move |event| match event {
            Event::Chat(line) if line.text == text => Some(()),
            _ => None,
        },
    )
    .await
}

/// The host opens a room and a guest connects: both sides must see the same room.
#[tokio::test]
async fn peer_joins_and_both_sides_converge() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let mut host = Coordinator::spawn(
        host_ep,
        test_room(),
        admits(&host_addr, "password"),
        "alice",
        None,
    )
    .await?;

    let welcome = wait_for(&mut host, "host welcome", |e| match e {
        Event::Welcome { room, .. } => Some(room),
        _ => None,
    })
    .await?;
    assert_eq!(
        welcome.peers.len(),
        1,
        "the host must see itself in the room"
    );
    assert_eq!(welcome.channels, vec!["general", "gaming"]);

    let guest_ep = bind_offline().await?;
    let mut guest = Client::connect(
        guest_ep,
        host_addr.clone(),
        key_for(&host_addr, "password"),
        "bob",
        None,
    )
    .await?;

    let guest_welcome = wait_for(&mut guest, "guest welcome", |e| match e {
        Event::Welcome { room, .. } => Some(room),
        _ => None,
    })
    .await?;
    assert_eq!(guest_welcome.room_name, "test room");
    assert_eq!(
        guest_welcome.peers.len(),
        2,
        "the joiner must see everyone, itself included"
    );

    // The host side must see the newcomer too.
    let roster = wait_for_roster(&mut host, 2).await?;
    let names: Vec<&str> = roster.iter().map(|p| p.name.as_str()).collect();
    assert!(
        names.contains(&"alice") && names.contains(&"bob"),
        "{names:?}"
    );

    assert_ne!(host.me, guest.me, "the identities must differ");
    assert_eq!(
        host.invite_code, guest.invite_code,
        "the same room means the same code"
    );
    Ok(())
}

/// A device that is not yet in the allowlist must present a valid one-time token.
#[tokio::test]
async fn enrollment_token_is_required_once() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let access = admits(&host_addr, "bob");
    let token = key_for(&host_addr, "bob");
    let mut host = Coordinator::spawn(host_ep, test_room(), access, "alice", None).await?;

    let unknown = Client::connect(
        bind_offline().await?,
        host_addr.clone(),
        None,
        "mallory",
        None,
    )
    .await;
    assert!(unknown.is_err(), "an unknown device must be rejected without an invite");

    let device_identity = iroh::SecretKey::generate();
    let guest_ep = bind_offline_as(device_identity.clone()).await?;
    let first_peer = to_peer_id(guest_ep.id());
    let mut guest = Client::connect(guest_ep, host_addr.clone(), token, "bob", None).await?;
    wait_for(&mut guest, "enrolled welcome", |event| {
        matches!(event, Event::Welcome { .. }).then_some(())
    })
    .await?;
    wait_for_roster(&mut host, 2).await?;

    // Disconnect, recreate the endpoint with exactly the same device identity,
    // and reconnect without an invite. The persisted allowlist must recognize it.
    guest.commands.send(Command::Quit).await?;
    drop(guest);
    wait_for_roster(&mut host, 1).await?;

    let guest_ep = bind_offline_as(device_identity).await?;
    assert_eq!(to_peer_id(guest_ep.id()), first_peer);
    let mut guest = Client::connect(guest_ep, host_addr, None, "bob", None).await?;
    wait_for(&mut guest, "welcome after reconnect", |event| {
        matches!(event, Event::Welcome { .. }).then_some(())
    })
    .await?;
    wait_for_roster(&mut host, 2).await?;
    Ok(())
}
/// `join --retry` keeps asking for a room with no address yet (a host that has not come
/// up, or one whose record was taken down), says how long is left before each new
/// attempt, and gives up on time.
#[tokio::test]
async fn a_retry_asks_again_and_ends_on_time() -> Result<()> {
    let gone = bind_offline().await?;
    let gone_addr = gone.addr();
    gone.close().await;
    let no_address = EndpointAddr::from(gone_addr.id);

    let mut waits = Vec::new();
    let started = tokio::time::Instant::now();
    let result = Client::connect_patiently(
        bind_offline().await?,
        no_address,
        key_for(&gone_addr, ""),
        "bob",
        None,
        Duration::from_secs(5),
        |left| waits.push(left),
    )
    .await;

    let err = format!("{:#}", result.err().expect("nobody is there"));
    assert!(err.contains("could not reach the saved coordinator"), "{err}");
    assert!(
        started.elapsed() < Duration::from_secs(8),
        "took {:?}",
        started.elapsed()
    );
    assert!(waits.len() >= 2, "each new attempt is announced: {waits:?}");
    assert!(
        waits.windows(2).all(|w| w[0] > w[1]),
        "the time left counts down: {waits:?}"
    );
    Ok(())
}

/// A single attempt that hangs (an address nobody answers) is cut off at the deadline,
/// rather than running on for iroh's 30 s.
#[tokio::test]
async fn a_retry_cuts_a_hanging_attempt_off_at_the_deadline() -> Result<()> {
    let gone = bind_offline().await?;
    let gone_addr = gone.addr();
    gone.close().await;

    let started = tokio::time::Instant::now();
    let result = Client::connect_patiently(
        bind_offline().await?,
        gone_addr.clone(),
        key_for(&gone_addr, ""),
        "bob",
        None,
        Duration::from_secs(4),
        |_| {},
    )
    .await;

    let err = format!("{:#}", result.err().expect("nobody is there"));
    assert!(err.contains("could not reach the saved coordinator"), "{err}");
    assert!(
        started.elapsed() < Duration::from_secs(7),
        "took {:?}",
        started.elapsed()
    );
    Ok(())
}

/// A room that answers and turns the joiner away is not asked again.
#[tokio::test]
async fn a_retry_does_not_repeat_a_refusal() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let _host = Coordinator::spawn(
        host_ep,
        test_room(),
        admits(&host_addr, "authorized-device"),
        "alice",
        None,
    )
    .await?;

    let started = tokio::time::Instant::now();
    let mut waits = 0;
    let result = Client::connect_patiently(
        bind_offline().await?,
        host_addr.clone(),
        key_for(&host_addr, "unknown-device"),
        "uninvited",
        None,
        Duration::from_secs(30),
        |_| waits += 1,
    )
    .await;

    let err = result
        .err()
        .expect("an invalid enrollment token must not be accepted")
        .to_string();
    assert!(
        err.contains("not authorized"),
        "the error must explain that the device is not authorized: {err}"
    );
    assert_eq!(waits, 0);
    assert!(
        started.elapsed() < Duration::from_secs(5),
        "took {:?}",
        started.elapsed()
    );
    Ok(())
}

/// An attempt with the wrong password must be rejected during the handshake.
#[tokio::test]
async fn invalid_invite_is_refused() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let _host = Coordinator::spawn(
        host_ep,
        test_room(),
        admits(&host_addr, "authorized-device"),
        "alice",
        None,
    )
    .await?;

    let guest_ep = bind_offline().await?;
    let result = Client::connect(
        guest_ep,
        host_addr.clone(),
        key_for(&host_addr, "unknown-device"),
        "uninvited",
        None,
    )
    .await;

    let err = result
        .err()
        .expect("an invalid enrollment token must not be accepted")
        .to_string();
    assert!(
        err.contains("not authorized"),
        "the error must explain that the device is not authorized: {err}"
    );
    Ok(())
}

/// A chat message must reach the sender and everyone else in the same shape.
#[tokio::test]
async fn chat_reaches_everyone_including_the_sender() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let mut host =
        Coordinator::spawn(host_ep, test_room(), admits(&host_addr, ""), "alice", None).await?;
    wait_for(&mut host, "host welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let guest_ep = bind_offline().await?;
    let mut guest = Client::connect(
        guest_ep,
        host_addr.clone(),
        key_for(&host_addr, ""),
        "bob",
        None,
    )
    .await?;
    wait_for(&mut guest, "guest welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;
    wait_for_roster(&mut host, 2).await?;

    // From the joiner to the host.
    guest
        .commands
        .send(Command::Chat {
            channel: ChannelId(0),
            text: "merhaba herkese".into(),
        })
        .await?;
    wait_for_chat(&mut host, "merhaba herkese").await?;
    wait_for_chat(&mut guest, "merhaba herkese").await?;

    // From the host to the joiner — the host's own message takes the same path.
    host.commands
        .send(Command::Chat {
            channel: ChannelId(1),
            text: "welcome aboard".into(),
        })
        .await?;
    wait_for_chat(&mut guest, "welcome aboard").await?;
    Ok(())
}

/// A channel switch must show up in everyone's roster — this is what drives the
/// voice mesh.
#[tokio::test]
async fn channel_switch_is_visible_to_everyone() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let mut host =
        Coordinator::spawn(host_ep, test_room(), admits(&host_addr, ""), "alice", None).await?;
    wait_for(&mut host, "host welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let guest_ep = bind_offline().await?;
    let mut guest = Client::connect(
        guest_ep,
        host_addr.clone(),
        key_for(&host_addr, ""),
        "bob",
        None,
    )
    .await?;
    let guest_id = guest.me;
    wait_for(&mut guest, "guest welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;
    wait_for_roster(&mut host, 2).await?;

    guest
        .commands
        .send(Command::SwitchChannel(Some(ChannelId(1))))
        .await?;

    let in_channel = |peers: &[PeerInfo]| {
        peers
            .iter()
            .any(|p| p.id == guest_id && p.channel == Some(ChannelId(1)))
    };

    let host_view = wait_for(
        &mut host,
        "the channel switch in the host roster",
        |e| match e {
            Event::Roster(peers) if in_channel(&peers) => Some(peers),
            _ => None,
        },
    )
    .await?;
    assert_eq!(host_view.len(), 2);

    wait_for(
        &mut guest,
        "the channel switch in the guest roster",
        |e| match e {
            Event::Roster(peers) if in_channel(&peers) => Some(()),
            _ => None,
        },
    )
    .await?;
    Ok(())
}

/// Asking to switch to a channel that does not exist must neither corrupt the room
/// nor drop the connection.
#[tokio::test]
async fn invalid_channel_request_is_ignored_without_breaking_the_session() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let mut host =
        Coordinator::spawn(host_ep, test_room(), admits(&host_addr, ""), "alice", None).await?;
    wait_for(&mut host, "host welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let guest_ep = bind_offline().await?;
    let mut guest = Client::connect(
        guest_ep,
        host_addr.clone(),
        key_for(&host_addr, ""),
        "bob",
        None,
    )
    .await?;
    wait_for(&mut guest, "guest welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;
    wait_for_roster(&mut host, 2).await?;

    guest
        .commands
        .send(Command::SwitchChannel(Some(ChannelId(99))))
        .await?;

    // The session must survive: a chat sent afterwards must still work.
    guest
        .commands
        .send(Command::Chat {
            channel: ChannelId(0),
            text: "still here".into(),
        })
        .await?;
    wait_for_chat(&mut host, "still here").await?;
    Ok(())
}

/// When a joiner leaves it must drop out of the roster.
#[tokio::test]
async fn leaving_updates_the_roster() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let mut host =
        Coordinator::spawn(host_ep, test_room(), admits(&host_addr, ""), "alice", None).await?;
    wait_for(&mut host, "host welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let guest_ep = bind_offline().await?;
    let mut guest = Client::connect(
        guest_ep,
        host_addr.clone(),
        key_for(&host_addr, ""),
        "bob",
        None,
    )
    .await?;
    wait_for(&mut guest, "guest welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;
    wait_for_roster(&mut host, 2).await?;

    guest.commands.send(Command::Quit).await?;

    let roster = wait_for_roster(&mut host, 1).await?;
    assert_eq!(roster[0].name, "alice", "only the host may remain");
    Ok(())
}

/// A second person arriving under the same nickname must be disambiguated, not
/// turned away.
#[tokio::test]
async fn duplicate_nicknames_are_disambiguated_over_the_wire() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let mut host =
        Coordinator::spawn(host_ep, test_room(), admits(&host_addr, ""), "alice", None).await?;
    wait_for(&mut host, "host welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let guest_ep = bind_offline().await?;
    let mut guest = Client::connect(
        guest_ep,
        host_addr.clone(),
        key_for(&host_addr, ""),
        "alice",
        None,
    )
    .await?;
    wait_for(&mut guest, "guest welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let roster = wait_for_roster(&mut host, 2).await?;
    let names: Vec<&str> = roster.iter().map(|p| p.name.as_str()).collect();
    assert_eq!(names.len(), 2);
    assert_ne!(
        names[0], names[1],
        "the two 'alice's must be distinguishable: {names:?}"
    );
    Ok(())
}

/// Three people: the coordinator must also relay messages between the joiners.
#[tokio::test]
async fn three_participants_stay_in_sync() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let access = admits(&host_addr, "bob");
    let bob_token = key_for(&host_addr, "bob");
    let carol_token = Some(
        access
            .create_invite(to_peer_id(host_addr.id), "carol")?
            .token,
    );
    let mut host =
        Coordinator::spawn(host_ep, test_room(), access, "alice", None).await?;
    wait_for(&mut host, "host welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let mut first = Client::connect(
        bind_offline().await?,
        host_addr.clone(),
        bob_token,
        "bob",
        None,
    )
    .await?;
    wait_for(&mut first, "welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;
    wait_for_roster(&mut host, 2).await?;

    let mut second = Client::connect(
        bind_offline().await?,
        host_addr.clone(),
        carol_token,
        "carol",
        None,
    )
    .await?;
    wait_for(&mut second, "welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;
    wait_for_roster(&mut host, 3).await?;

    // A message from one joiner must reach the other through the coordinator.
    first
        .commands
        .send(Command::Chat {
            channel: ChannelId(0),
            text: "carol can you hear me".into(),
        })
        .await?;
    wait_for_chat(&mut second, "carol can you hear me").await?;
    wait_for_chat(&mut host, "carol can you hear me").await?;
    Ok(())
}

/// When the coordinator leaves, clients receive a disconnection notice and
/// their endpoints close gracefully without dropping unclosed.
#[tokio::test]
async fn host_quitting_notifies_and_gracefully_disconnects_guest() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let mut host =
        Coordinator::spawn(host_ep, test_room(), admits(&host_addr, ""), "alice", None).await?;
    wait_for(&mut host, "host welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let guest_ep = bind_offline().await?;
    let mut guest = Client::connect(
        guest_ep,
        host_addr.clone(),
        key_for(&host_addr, ""),
        "bob",
        None,
    )
    .await?;
    wait_for(&mut guest, "guest welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;
    wait_for_roster(&mut host, 2).await?;

    // Host quits.
    host.commands.send(Command::Quit).await?;

    // Guest should receive the notice and disconnection.
    let deadline = tokio::time::Instant::now() + PATIENCE;
    let mut saw_notice = false;
    let mut saw_disconnect = false;
    while let Ok(Some(event)) = tokio::time::timeout_at(deadline, guest.events.recv()).await {
        match event {
            Event::Notice(text) if text.contains("the coordinator left") => saw_notice = true,
            Event::Disconnected(_) => {
                saw_disconnect = true;
                break;
            }
            _ => {}
        }
    }
    assert!(saw_notice, "guest should have received the closing notice");
    assert!(
        saw_disconnect,
        "guest should have received Event::Disconnected"
    );

    // Dropping guest's commands should close client_writer and terminate cleanly.
    drop(guest.commands);
    tokio::time::sleep(Duration::from_millis(100)).await;
    Ok(())
}

/// Setting AFK status must broadcast the updated roster to all peers in the room.
#[tokio::test]
async fn afk_status_is_visible_to_everyone() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let mut host =
        Coordinator::spawn(host_ep, test_room(), admits(&host_addr, ""), "alice", None).await?;
    wait_for(&mut host, "host welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let guest_ep = bind_offline().await?;
    let mut guest = Client::connect(
        guest_ep,
        host_addr.clone(),
        key_for(&host_addr, ""),
        "bob",
        None,
    )
    .await?;
    wait_for(&mut guest, "guest welcome", |e| {
        matches!(e, Event::Welcome { .. }).then_some(())
    })
    .await?;
    wait_for_roster(&mut host, 2).await?;

    // Guest marks itself as AFK.
    guest.commands.send(Command::SetAfk(true)).await?;

    // Host should see Bob as AFK in the roster.
    wait_for(&mut host, "bob marked afk in host roster", |e| match e {
        Event::Roster(peers) => {
            let bob = peers.iter().find(|p| p.name == "bob")?;
            bob.afk.then_some(())
        }
        _ => None,
    })
    .await?;

    // Guest clears AFK.
    guest.commands.send(Command::SetAfk(false)).await?;

    // Host should see Bob as no longer AFK.
    wait_for(&mut host, "bob un-afk in host roster", |e| match e {
        Event::Roster(peers) => {
            let bob = peers.iter().find(|p| p.name == "bob")?;
            (!bob.afk).then_some(())
        }
        _ => None,
    })
    .await?;

    Ok(())
}

/// Public file metadata is durable for the lifetime of the room and is streamed to
/// participants that join after the original post.
#[tokio::test]
async fn late_joiner_receives_public_file_history() -> Result<()> {
    let host_ep = bind_offline().await?;
    let host_addr = host_ep.addr();
    let mut host = Coordinator::spawn(
        host_ep,
        test_room(),
        admits(&host_addr, "password"),
        "alice",
        None,
    )
    .await?;
    wait_for(&mut host, "host welcome", |event| {
        matches!(event, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let nonce = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let path =
        std::env::temp_dir().join(format!("tincan-history-{}-{nonce}.txt", std::process::id()));
    tokio::fs::write(&path, b"posted before bob joined").await?;

    host.commands
        .send(Command::ShareFile {
            channel: ChannelId(0),
            recipient: None,
            path: path.clone(),
        })
        .await
        .unwrap();

    let original = wait_for(&mut host, "host file offer", |event| match event {
        Event::FileOffer(offer) if offer.name.contains("tincan-history") => Some(offer),
        _ => None,
    })
    .await?;

    let guest_ep = bind_offline().await?;
    let mut guest = Client::connect(
        guest_ep,
        host_addr.clone(),
        key_for(&host_addr, "password"),
        "bob",
        None,
    )
    .await?;

    wait_for(&mut guest, "guest welcome", |event| {
        matches!(event, Event::Welcome { .. }).then_some(())
    })
    .await?;

    let historical = wait_for(
        &mut guest,
        "historical public file offer",
        |event| match event {
            Event::FileOffer(offer) if offer.id == original.id => Some(offer),
            _ => None,
        },
    )
    .await?;

    assert_eq!(historical.channel, ChannelId(0));
    assert_eq!(historical.recipient, None);
    assert_eq!(historical.digest, original.digest);

    let _ = tokio::fs::remove_file(path).await;
    Ok(())
}
