//! tincan — serverless voice chat that runs in your terminal.

use std::io::{IsTerminal as _, Write as _};
use std::path::PathBuf;

use anyhow::{Context, Result};
use clap::{CommandFactory, Parser, Subcommand};
use iroh::Endpoint;
use tincan::access::{self, ServerAccess};
use tincan::audio;
use tincan::audio::device::Wanted;
use tincan::clipboard;
use tincan::config::Config;
use tincan::invite;
use tincan::net::Command;
use tincan::net::control::{Client, Coordinator};
use tincan::net::endpoint;
use tincan::net::voice::VoiceMesh;
use tincan::proto::PeerId;
use tincan::room::Room;
use tincan::ui::{self, VoiceControl};

/// Channels created by default on the private coordinator.
const DEFAULT_CHANNELS: &str = "general,gaming,music";
/// Generic default; a deployment-specific name is stored only in the user's launcher config.
const PRIVATE_ROOM: &str = "Private";
const DEFAULT_MAX_FILE_GIB: u64 = 8;

#[derive(Parser)]
#[command(
    name = "tincan",
    version,
    about = "Serverless voice chat in your terminal",
    before_help = tincan::logo::BANNER
)]
struct Cli {
    #[command(subcommand)]
    command: Sub,
}

#[derive(Subcommand)]
enum Sub {
    /// Start the private coordinator with its persistent identity and allowlist.
    Host {
        /// The nickname shown for the server owner.
        #[arg(long, short)]
        name: Option<String>,
        /// Display name advertised to connected clients.
        #[arg(long, default_value = PRIVATE_ROOM)]
        server_name: String,
        /// Comma-separated list of channels.
        #[arg(long, default_value = DEFAULT_CHANNELS)]
        channels: String,
        /// Maximum size of a file this client may send, 1..=16 GiB.
        #[arg(long, default_value_t = DEFAULT_MAX_FILE_GIB, value_parser = clap::value_parser!(u64).range(1..=16))]
        max_file_gib: u64,
        /// Disable message/join notification sounds while keeping interface feedback sounds.
        #[arg(long)]
        no_notifications: bool,
        #[command(flatten)]
        audio: AudioArgs,
    },
    /// Join the saved coordinator, or enroll this device with a one-time invite.
    Join {
        /// One-time invite. Omit it after this device has already been enrolled.
        invite: Option<String>,
        /// The nickname you appear under in the room.
        #[arg(long, short)]
        name: Option<String>,
        /// Keep trying to reach the server for up to this many seconds.
        #[arg(long, value_name = "SECS")]
        retry: Option<u64>,
        /// Maximum size of a file this client may send, 1..=16 GiB.
        #[arg(long, default_value_t = DEFAULT_MAX_FILE_GIB, value_parser = clap::value_parser!(u64).range(1..=16))]
        max_file_gib: u64,
        /// Disable message/join notification sounds while keeping interface feedback sounds.
        #[arg(long)]
        no_notifications: bool,
        #[command(flatten)]
        audio: AudioArgs,
    },
    /// Create a one-time invite without opening another server process.
    Invite {
        /// Human label stored with the device after enrollment.
        label: String,
    },
    /// List devices in the persistent server allowlist.
    Peers,
    /// Remove one device by label or PeerId prefix.
    Revoke {
        selector: String,
    },
    /// Forget the locally saved coordinator while keeping this device identity.
    ResetPairing,
    /// List the audio devices tincan can see.
    Devices {
        /// Include what the device picker leaves out: ALSA plugins and each card's raw PCMs.
        #[arg(long)]
        all: bool,
    },
    /// Generate shell auto-completion scripts.
    Completions {
        /// Shell to generate completions for.
        shell: clap_complete::Shell,
    },
}

/// Flags shared by the commands that use audio.
#[derive(clap::Args, Clone)]
struct AudioArgs {
    /// Skip audio entirely; text chat only.
    #[arg(long)]
    no_voice: bool,
    /// Microphone to use (a distinctive part of its name is enough).
    #[arg(long)]
    input: Option<String>,
    /// Speaker to use (a distinctive part of its name is enough).
    #[arg(long)]
    output: Option<String>,
    /// Push-to-talk: the microphone only opens while the configured key is held.
    #[arg(long)]
    ptt: bool,
    /// Key used for push-to-talk. The native launcher forwards press/release reliably.
    #[arg(long, default_value = "F4")]
    ptt_key: String,
}

#[tokio::main]
async fn main() -> Result<()> {
    // Parsed first: `--help` and a bad argument both exit here, and neither should
    // leave a log file behind.
    let command = Cli::parse().command;
    if let Sub::Completions { shell } = command {
        clap_complete::generate(shell, &mut Cli::command(), "tincan", &mut std::io::stdout());
        return Ok(());
    }
    let log = start_logging();

    let result = run(command).await;
    report_log(log);
    result
}

/// Sends this run's log somewhere it cannot land on top of the interface.
///
/// The interface draws on the terminal and the alternate screen does not capture
/// stderr, so one warning printed straight over the room — which is exactly what it
/// did. A redirected stderr is left alone: `2>tincan.log` has always meant "put the
/// log there", and it still does.
fn start_logging() -> Option<PathBuf> {
    let filter =
        tracing_subscriber::EnvFilter::try_from_default_env().unwrap_or_else(|_| "warn".into());

    if !std::io::stderr().is_terminal() {
        tracing_subscriber::fmt()
            .with_writer(std::io::stderr)
            .with_env_filter(filter)
            .init();
        return None;
    }

    // Nowhere to write is a reason to stay quiet, not a reason to scribble on the
    // interface: without `init` the macros do nothing at all.
    let path = log_path()?;
    // Appending, because the interface points stderr at this same file while it is up
    // (see `tincan::stderr`), and two writers at their own offsets overwrite each other.
    let file = std::fs::OpenOptions::new()
        .create(true)
        .append(true)
        .open(&path)
        .ok()?;
    let _ = file.set_len(0);
    if let Ok(copy) = file.try_clone() {
        tincan::stderr::sink_into(copy);
    }
    tracing_subscriber::fmt()
        .with_writer(std::sync::Arc::new(file))
        .with_ansi(false)
        .with_env_filter(filter)
        .init();
    Some(path)
}

fn log_path() -> Option<PathBuf> {
    let base = std::env::var_os("XDG_CACHE_HOME")
        .map(PathBuf::from)
        .or_else(|| std::env::var_os("HOME").map(|home| PathBuf::from(home).join(".cache")))?;
    let dir = base.join("tincan");
    std::fs::create_dir_all(&dir).ok()?;
    // One file per run. Two tincans on one machine is an ordinary thing to do while
    // testing, and they must not write over each other.
    Some(dir.join(format!("{}.log", std::process::id())))
}

/// Says where the log is, but only when there is something in it.
///
/// A clean run should end in silence, and a bad one in a single line — not in a wall
/// of text arriving at the moment you decided to stop reading. Whatever mattered to
/// the user was already said in the room while it happened.
fn report_log(path: Option<PathBuf>) {
    let Some(path) = path else {
        return;
    };
    let empty = std::fs::metadata(&path)
        .map(|file| file.len() == 0)
        .unwrap_or(true);
    if empty {
        let _ = std::fs::remove_file(&path);
        return;
    }
    let lines = std::fs::read_to_string(&path)
        .map(|log| log.lines().count())
        .unwrap_or(0);
    let plural = if lines == 1 { "" } else { "s" };
    eprintln!(
        "\n  {lines} log line{plural} from this session: {}",
        path.display()
    );
}

async fn run(command: Sub) -> Result<()> {
    match command {
        Sub::Host {
            name,
            server_name,
            channels,
            max_file_gib,
            no_notifications,
            audio,
        } => host(
            name,
            server_name,
            channels,
            max_file_gib,
            no_notifications,
            audio,
        )
        .await,
        Sub::Join {
            invite,
            name,
            retry,
            max_file_gib,
            no_notifications,
            audio,
        } => join(
            invite,
            name,
            retry,
            max_file_gib,
            no_notifications,
            audio,
        )
        .await,
        Sub::Invite { label } => create_invite(label),
        Sub::Peers => list_authorized(),
        Sub::Revoke { selector } => revoke_authorized(selector),
        Sub::ResetPairing => {
            access::clear_pairing()?;
            println!("saved server removed; device identity was kept");
            Ok(())
        }
        Sub::Devices { all } => {
            println!("{}", audio::device::describe_devices(all)?);
            Ok(())
        }
        Sub::Completions { shell } => {
            clap_complete::generate(shell, &mut Cli::command(), "tincan", &mut std::io::stdout());
            Ok(())
        }
    }
}

fn create_invite(label: String) -> Result<()> {
    let identity = access::load_or_create_server_identity()?;
    let coordinator = endpoint::to_peer_id(identity.public());
    let server_access = ServerAccess::default()?;
    let invitation = server_access.create_invite(coordinator, &label)?;
    let code = invite::encode(&invitation);
    let copied = clipboard::copy(&code);

    println!("one-time invite for {}:", label.trim());
    println!("{code}");
    if copied {
        println!("copied to clipboard");
    }
    Ok(())
}

fn list_authorized() -> Result<()> {
    let devices = ServerAccess::default()?.list_devices()?;
    if devices.is_empty() {
        println!("no authorized devices");
        return Ok(());
    }

    println!("authorized devices:");
    for device in devices {
        println!("{}  {}  {}", device.peer.short(), device.peer, device.label);
    }
    Ok(())
}

fn revoke_authorized(selector: String) -> Result<()> {
    let device = ServerAccess::default()?.revoke(&selector)?;
    println!("revoked {} [{}]", device.label, device.peer.short());
    Ok(())
}

async fn host(
    name: Option<String>,
    server_name: String,
    channels: String,
    max_file_gib: u64,
    no_notifications: bool,
    audio: AudioArgs,
) -> Result<()> {
    tincan::logo::print_banner();
    tincan::net::file::set_send_limit_gib(max_file_gib)?;
    let channels: Vec<String> = channels
        .split(',')
        .map(|c| c.trim().to_string())
        .filter(|c| !c.is_empty())
        .collect();

    let identity = access::load_or_create_server_identity()?;
    let server_access = ServerAccess::default()?;
    let room = Room::new(server_name.trim(), channels)?;

    println!("{}", tincan::logo::heading("  connecting to the network…"));
    let endpoint = endpoint::bind(Some(identity)).await?;
    let me = endpoint::to_peer_id(endpoint.id());
    let (mesh, control) = setup_voice(&endpoint, me, &audio);

    let mut session =
        Coordinator::spawn(endpoint, room, server_access, &nickname(name), mesh).await?;

    println!(
        "\n{}",
        tincan::logo::heading(
            "  private server is online. create one-time invites with /invite <name>."
        )
    );
    println!(
        "{}",
        tincan::logo::heading(
            "  /auth lists enrolled devices; /revoke <name-or-peer-prefix> removes access."
        )
    );

    print!(
        "\n{}",
        tincan::logo::heading(
            "  press enter to open the room. invitations can be created from inside the room."
        )
    );
    std::io::stdout().flush().ok();

    if let Leaving::Interrupted = wait_at_the_prompt().await {
        println!();
        let _ = session.commands.send(Command::Quit).await;
        let _ =
            tokio::time::timeout(std::time::Duration::from_secs(4), session.events.recv()).await;
        return Ok(());
    }

    ui::run(session, control, audio.ptt, &audio.ptt_key, !no_notifications).await
}

async fn join(
    invite_text: Option<String>,
    name: Option<String>,
    retry: Option<u64>,
    max_file_gib: u64,
    no_notifications: bool,
    audio: AudioArgs,
) -> Result<()> {
    tincan::logo::print_banner();
    tincan::net::file::set_send_limit_gib(max_file_gib)?;

    let (coordinator, token) = match invite_text {
        Some(text) => {
            let invitation = invite::decode(&text).context("could not read the one-time invite")?;
            (invitation.coordinator, Some(invitation.token))
        }
        None => {
            let coordinator = access::load_pairing()?.context(
                "this device is not paired yet; use 'join <one-time-invite>' first",
            )?;
            (coordinator, None)
        }
    };

    println!("{}", tincan::logo::heading("  connecting to the private server…"));
    let identity = access::load_or_create_client_identity()?;
    let endpoint = endpoint::bind(Some(identity)).await?;
    let me = endpoint::to_peer_id(endpoint.id());
    let (mesh, control) = setup_voice(&endpoint, me, &audio);

    let target = endpoint::to_endpoint_id(&coordinator)?;
    let patience = std::time::Duration::from_secs(retry.unwrap_or(0));
    let waiting = |left: std::time::Duration| {
        println!(
            "{}",
            tincan::logo::heading(&format!(
                "  server is not reachable yet — trying again ({} s left)",
                left.as_secs()
            ))
        );
    };

    let session = Client::connect_patiently(
        endpoint,
        target,
        token,
        &nickname(name),
        mesh,
        patience,
        waiting,
    )
    .await?;

    // Only remember the coordinator after the server actually accepted this device.
    access::save_pairing(coordinator)?;
    ui::run(session, control, audio.ptt, &audio.ptt_key, !no_notifications).await
}

/// How the wait at the prompt ended.
enum Leaving {
    Enter,
    Interrupted,
}

/// Waits for Enter, or for the user to give up on the room.
///
/// The line is read on a plain thread rather than through `tokio::io::stdin`, which is
/// backed by the runtime's blocking pool. A blocking read cannot be cancelled: dropping
/// the future on ctrl+c leaves the thread sitting on `read`, and the runtime will not
/// finish shutting down until it returns — so the program printed its goodbye and then
/// waited forever for a keypress that was never coming. A detached thread is something
/// the process is allowed to walk away from. `ui::spawn_key_reader` reads keys the same
/// way, for the same reason.
async fn wait_at_the_prompt() -> Leaving {
    let (tx, mut rx) = tokio::sync::mpsc::channel::<()>(1);
    std::thread::spawn(move || {
        let mut line = String::new();
        let _ = std::io::stdin().read_line(&mut line);
        let _ = tx.blocking_send(());
    });

    tokio::select! {
        _ = rx.recv() => Leaving::Enter,
        _ = tokio::signal::ctrl_c() => Leaving::Interrupted,
    }
}

/// Brings up the audio hardware and the mesh.
///
/// If audio cannot start (no microphone permission, device is not 48 kHz) the app must
/// not die: text chat keeps working and the user sees the reason in the interface.
fn setup_voice(
    endpoint: &Endpoint,
    me: PeerId,
    args: &AudioArgs,
) -> (Option<VoiceMesh>, Option<VoiceControl>) {
    if args.no_voice {
        return (None, None);
    }
    let config = Config::load();
    let choice = audio::device::DeviceChoice {
        input: Wanted::pick(args.input.clone(), config.input_device),
        output: Wanted::pick(args.output.clone(), config.output_device),
    };
    match audio::start(me, &choice) {
        Ok(io) => {
            let mesh = VoiceMesh::start(endpoint.clone(), me, io.incoming.clone(), io.outgoing);
            let control = VoiceControl {
                mesh: mesh.clone(),
                speaking: io.speaking,
                mic_open: io.mic_open,
                hearing: io.hearing,
                mic_level: io.mic_level,
                peer_levels: io.peer_levels,
                peer_gains: io.peer_gains,
                mic_test: io.mic_test,
                gate: io.gate,
                denoise: io.denoise,
                health: io.health,
                blip_tx: io.blip_tx,
                devices: io.devices,
            };
            (Some(mesh), Some(control))
        }
        Err(err) => {
            eprintln!("\n  audio could not start, so this is a text-only session: {err:#}\n");
            std::thread::sleep(std::time::Duration::from_millis(2500));
            (None, None)
        }
    }
}

/// Falls back to the system username when no nickname is given.
fn nickname(explicit: Option<String>) -> String {
    explicit
        .or_else(|| std::env::var("USER").ok())
        .unwrap_or_else(|| "guest".to_string())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn host_has_no_deployment_credential_argument() {
        let cli = Cli::try_parse_from(["tincan", "host", "--name", "alice"]).unwrap();
        assert!(matches!(cli.command, Sub::Host { name: Some(ref value), .. } if value == "alice"));

        assert!(
            Cli::try_parse_from(["tincan", "host", "private-room-name"]).is_err(),
            "host must not accept a room address or secret on the command line"
        );
    }

    #[test]
    fn join_invite_is_optional_after_pairing() {
        let cli = Cli::try_parse_from(["tincan", "join"]).unwrap();
        assert!(matches!(cli.command, Sub::Join { invite: None, .. }));

        let code = "fd1.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";
        let cli = Cli::try_parse_from(["tincan", "join", code]).unwrap();
        assert!(matches!(cli.command, Sub::Join { invite: Some(ref value), .. } if value == code));
    }

    #[test]
    fn invite_admin_command_requires_only_a_label() {
        let cli = Cli::try_parse_from(["tincan", "invite", "Alice laptop"]).unwrap();
        assert!(matches!(cli.command, Sub::Invite { ref label } if label == "Alice laptop"));
    }

    #[test]
    fn completions_generate_for_supported_shells() {
        use clap_complete::Shell;

        for shell in [Shell::Bash, Shell::Zsh, Shell::Fish, Shell::PowerShell] {
            let mut buf = Vec::new();
            clap_complete::generate(shell, &mut Cli::command(), "tincan", &mut buf);
            let script = String::from_utf8(buf).expect("completion script should be valid utf-8");
            assert!(
                !script.is_empty(),
                "completions for {shell:?} must not be empty"
            );
            assert!(
                script.contains("host"),
                "must mention host command for {shell:?}"
            );
            assert!(
                script.contains("join"),
                "must mention join command for {shell:?}"
            );
            assert!(
                script.contains("completions"),
                "must mention completions command for {shell:?}"
            );
        }
    }
}
