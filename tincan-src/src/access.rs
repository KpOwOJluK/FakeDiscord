//! Persistent device identity and server-side access control.
//!
//! Security model:
//! - the coordinator has a stable iroh secret key stored outside the binary;
//! - every client has its own stable iroh secret key stored outside the binary;
//! - a one-time invite contains the coordinator public key plus a random 256-bit token;
//! - after first use the token is removed and the client's PeerId is persisted;
//! - later connections are admitted by the QUIC-authenticated PeerId alone.

use std::fs::{self, OpenOptions};
use std::path::{Path, PathBuf};
use std::thread;
use std::time::{Duration, SystemTime};

use anyhow::{Context, Result, anyhow, bail, ensure};
use data_encoding::HEXLOWER;
use iroh::SecretKey;
use rand::Rng;
use serde::{Deserialize, Serialize};

use crate::invite::{Invite, InviteToken};
use crate::proto::PeerId;

const STATE_SCHEMA: u32 = 1;
const LOCK_RETRY: Duration = Duration::from_millis(20);
const LOCK_TRIES: usize = 100;
const STALE_LOCK: Duration = Duration::from_secs(30);

#[derive(Debug, Clone, Serialize, Deserialize)]
struct AuthorizedPeer {
    peer: String,
    label: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct PendingInvite {
    token: String,
    label: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct AccessFile {
    #[serde(default = "schema")]
    schema: u32,
    #[serde(default)]
    authorized: Vec<AuthorizedPeer>,
    #[serde(default)]
    invites: Vec<PendingInvite>,
}

impl Default for AccessFile {
    fn default() -> Self {
        Self {
            schema: STATE_SCHEMA,
            authorized: Vec::new(),
            invites: Vec::new(),
        }
    }
}

fn schema() -> u32 {
    STATE_SCHEMA
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct AuthorizedDevice {
    pub peer: PeerId,
    pub label: String,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Admission {
    Known,
    Enrolled { label: String },
    Denied,
}

#[derive(Debug, Clone)]
pub struct ServerAccess {
    path: PathBuf,
}

impl ServerAccess {
    pub fn default() -> Result<Self> {
        Self::at(state_root()?.join("server").join("access.toml"))
    }

    /// Opens an access store at an explicit path. Useful for isolated deployments and tests.
    pub fn at(path: PathBuf) -> Result<Self> {
        if !path.exists() {
            save_access(&path, &AccessFile::default())?;
        }
        Ok(Self { path })
    }

    pub fn create_invite(&self, coordinator: PeerId, label: &str) -> Result<Invite> {
        let label = label.trim();
        ensure!(!label.is_empty(), "invite label cannot be empty");
        ensure!(label.chars().count() <= 64, "invite label is too long");

        with_lock(&self.path, || {
            let mut state = load_access(&self.path)?;
            let mut token = [0u8; 32];

            loop {
                rand::rng().fill_bytes(&mut token);
                let encoded = encode_bytes(&token);
                if !state.invites.iter().any(|item| item.token == encoded) {
                    state.invites.push(PendingInvite {
                        token: encoded,
                        label: label.to_string(),
                    });
                    break;
                }
            }

            save_access(&self.path, &state)?;
            Ok(Invite { coordinator, token })
        })
    }

    pub fn admit(&self, peer: PeerId, token: Option<InviteToken>) -> Result<Admission> {
        with_lock(&self.path, || {
            let mut state = load_access(&self.path)?;
            let peer_text = peer.to_string();

            if state.authorized.iter().any(|item| item.peer == peer_text) {
                return Ok(Admission::Known);
            }

            let Some(token) = token else {
                return Ok(Admission::Denied);
            };
            let token_text = encode_bytes(&token);
            let Some(index) = state.invites.iter().position(|item| item.token == token_text) else {
                return Ok(Admission::Denied);
            };

            let pending = state.invites.remove(index);
            state.authorized.push(AuthorizedPeer {
                peer: peer_text,
                label: pending.label.clone(),
            });
            save_access(&self.path, &state)?;

            Ok(Admission::Enrolled {
                label: pending.label,
            })
        })
    }

    pub fn list_devices(&self) -> Result<Vec<AuthorizedDevice>> {
        let state = load_access(&self.path)?;
        state
            .authorized
            .into_iter()
            .map(|item| {
                Ok(AuthorizedDevice {
                    peer: decode_peer(&item.peer)?,
                    label: item.label,
                })
            })
            .collect()
    }

    pub fn revoke(&self, selector: &str) -> Result<AuthorizedDevice> {
        let selector = selector.trim();
        ensure!(!selector.is_empty(), "device selector cannot be empty");

        with_lock(&self.path, || {
            let mut state = load_access(&self.path)?;
            let needle = selector.to_ascii_lowercase();
            let matches: Vec<usize> = state
                .authorized
                .iter()
                .enumerate()
                .filter(|(_, item)| {
                    item.peer.starts_with(&needle)
                        || item.label.to_ascii_lowercase() == needle
                })
                .map(|(index, _)| index)
                .collect();

            match matches.as_slice() {
                [] => bail!("no authorized device matches '{selector}'"),
                [index] => {
                    let removed = state.authorized.remove(*index);
                    save_access(&self.path, &state)?;
                    Ok(AuthorizedDevice {
                        peer: decode_peer(&removed.peer)?,
                        label: removed.label,
                    })
                }
                _ => bail!("device selector '{selector}' is ambiguous"),
            }
        })
    }
}

pub fn load_or_create_server_identity() -> Result<SecretKey> {
    load_or_create_identity(&state_root()?.join("server").join("identity.key"))
}

pub fn load_or_create_client_identity() -> Result<SecretKey> {
    load_or_create_identity(&state_root()?.join("client").join("identity.key"))
}

pub fn load_pairing() -> Result<Option<PeerId>> {
    let path = pairing_path()?;
    if !path.exists() {
        return Ok(None);
    }

    let text = fs::read_to_string(&path)
        .with_context(|| format!("could not read pairing file: {}", path.display()))?;
    let pairing: PairingFile = toml::from_str(&text)
        .with_context(|| format!("invalid pairing file: {}", path.display()))?;
    Ok(Some(decode_peer(&pairing.coordinator)?))
}

pub fn save_pairing(coordinator: PeerId) -> Result<()> {
    let path = pairing_path()?;
    let text = toml::to_string_pretty(&PairingFile {
        coordinator: coordinator.to_string(),
    })
    .context("could not serialize pairing")?;
    write_private(&path, text.as_bytes())
}

pub fn clear_pairing() -> Result<()> {
    let path = pairing_path()?;
    if path.exists() {
        fs::remove_file(&path)
            .with_context(|| format!("could not remove pairing: {}", path.display()))?;
    }
    Ok(())
}

#[derive(Debug, Serialize, Deserialize)]
struct PairingFile {
    coordinator: String,
}

fn pairing_path() -> Result<PathBuf> {
    Ok(state_root()?.join("client").join("pairing.toml"))
}

fn state_root() -> Result<PathBuf> {
    if let Some(explicit) = std::env::var_os("FAKEDISCORD_STATE_DIR") {
        return Ok(PathBuf::from(explicit));
    }

    if cfg!(windows) {
        if let Some(appdata) = std::env::var_os("APPDATA") {
            return Ok(PathBuf::from(appdata).join("FakeDiscord"));
        }
        if let Some(profile) = std::env::var_os("USERPROFILE") {
            return Ok(PathBuf::from(profile)
                .join("AppData")
                .join("Roaming")
                .join("FakeDiscord"));
        }
    }

    if let Some(xdg) = std::env::var_os("XDG_CONFIG_HOME") {
        return Ok(PathBuf::from(xdg).join("fakediscord"));
    }
    if let Some(home) = std::env::var_os("HOME") {
        return Ok(PathBuf::from(home).join(".config").join("fakediscord"));
    }

    bail!("could not determine a directory for FakeDiscord security state")
}

fn load_or_create_identity(path: &Path) -> Result<SecretKey> {
    if path.exists() {
        let text = fs::read_to_string(path)
            .with_context(|| format!("could not read identity: {}", path.display()))?;
        let bytes = decode_32(text.trim())
            .with_context(|| format!("invalid identity file: {}", path.display()))?;
        return Ok(SecretKey::from_bytes(&bytes));
    }

    let secret = SecretKey::generate();
    write_private(path, encode_bytes(&secret.to_bytes()).as_bytes())?;
    Ok(secret)
}

fn load_access(path: &Path) -> Result<AccessFile> {
    if !path.exists() {
        return Ok(AccessFile::default());
    }
    let text = fs::read_to_string(path)
        .with_context(|| format!("could not read access list: {}", path.display()))?;
    let state: AccessFile = toml::from_str(&text)
        .with_context(|| format!("invalid access list: {}", path.display()))?;
    ensure!(
        state.schema == STATE_SCHEMA,
        "unsupported access-list schema {}",
        state.schema
    );
    Ok(state)
}

fn save_access(path: &Path, state: &AccessFile) -> Result<()> {
    let text = toml::to_string_pretty(state).context("could not serialize access list")?;
    write_private(path, text.as_bytes())
}

fn write_private(path: &Path, bytes: &[u8]) -> Result<()> {
    let parent = path
        .parent()
        .ok_or_else(|| anyhow!("path has no parent: {}", path.display()))?;
    fs::create_dir_all(parent)
        .with_context(|| format!("could not create directory: {}", parent.display()))?;

    let temp = path.with_extension("tmp");
    fs::write(&temp, bytes)
        .with_context(|| format!("could not write temporary file: {}", temp.display()))?;
    set_private_permissions(&temp)?;

    if path.exists() {
        fs::remove_file(path)
            .with_context(|| format!("could not replace file: {}", path.display()))?;
    }
    fs::rename(&temp, path)
        .with_context(|| format!("could not install file: {}", path.display()))?;
    set_private_permissions(path)?;
    Ok(())
}

#[cfg(unix)]
fn set_private_permissions(path: &Path) -> Result<()> {
    use std::os::unix::fs::PermissionsExt;
    fs::set_permissions(path, fs::Permissions::from_mode(0o600))
        .with_context(|| format!("could not protect file: {}", path.display()))
}

#[cfg(not(unix))]
fn set_private_permissions(_path: &Path) -> Result<()> {
    Ok(())
}

fn encode_bytes(bytes: &[u8]) -> String {
    HEXLOWER.encode(bytes)
}

fn decode_32(text: &str) -> Result<[u8; 32]> {
    let bytes = HEXLOWER
        .decode(text.as_bytes())
        .map_err(|_| anyhow!("expected 64 lowercase hexadecimal characters"))?;
    bytes
        .as_slice()
        .try_into()
        .map_err(|_| anyhow!("expected exactly 32 bytes"))
}

fn decode_peer(text: &str) -> Result<PeerId> {
    Ok(PeerId(decode_32(text)?))
}

struct FileLock {
    path: PathBuf,
}

impl Drop for FileLock {
    fn drop(&mut self) {
        let _ = fs::remove_file(&self.path);
    }
}

fn with_lock<T>(path: &Path, operation: impl FnOnce() -> Result<T>) -> Result<T> {
    let _lock = acquire_lock(path)?;
    operation()
}

fn acquire_lock(path: &Path) -> Result<FileLock> {
    let parent = path
        .parent()
        .ok_or_else(|| anyhow!("path has no parent: {}", path.display()))?;
    fs::create_dir_all(parent)?;

    let lock_path = path.with_extension("lock");
    for _ in 0..LOCK_TRIES {
        match OpenOptions::new()
            .write(true)
            .create_new(true)
            .open(&lock_path)
        {
            Ok(_) => return Ok(FileLock { path: lock_path }),
            Err(err) if err.kind() == std::io::ErrorKind::AlreadyExists => {
                if lock_is_stale(&lock_path) {
                    let _ = fs::remove_file(&lock_path);
                    continue;
                }
                thread::sleep(LOCK_RETRY);
            }
            Err(err) => {
                return Err(err)
                    .with_context(|| format!("could not lock access list: {}", path.display()));
            }
        }
    }

    bail!("access list is busy; try again")
}

fn lock_is_stale(path: &Path) -> bool {
    fs::metadata(path)
        .and_then(|meta| meta.modified())
        .ok()
        .and_then(|modified| SystemTime::now().duration_since(modified).ok())
        .is_some_and(|age| age >= STALE_LOCK)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn test_root(name: &str) -> PathBuf {
        std::env::temp_dir().join(format!("fakediscord-access-{name}-{}", std::process::id()))
    }

    #[test]
    fn invite_is_consumed_and_device_survives_reload() {
        let dir = test_root("consume");
        let path = dir.join("access.toml");
        let access = ServerAccess { path: path.clone() };
        save_access(&path, &AccessFile::default()).unwrap();

        let coordinator = PeerId([1; 32]);
        let device = PeerId([2; 32]);
        let invite = access.create_invite(coordinator, "Alice").unwrap();

        assert_eq!(
            access.admit(device, Some(invite.token)).unwrap(),
            Admission::Enrolled {
                label: "Alice".into()
            }
        );
        assert_eq!(access.admit(PeerId([3; 32]), Some(invite.token)).unwrap(), Admission::Denied);

        let reopened = ServerAccess { path };
        assert_eq!(reopened.admit(device, None).unwrap(), Admission::Known);
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn unknown_device_without_invite_is_denied() {
        let dir = test_root("deny");
        let path = dir.join("access.toml");
        let access = ServerAccess { path: path.clone() };
        save_access(&path, &AccessFile::default()).unwrap();

        assert_eq!(access.admit(PeerId([9; 32]), None).unwrap(), Admission::Denied);
        let _ = fs::remove_dir_all(dir);
    }

    #[test]
    fn revoked_device_is_denied_again() {
        let dir = test_root("revoke");
        let path = dir.join("access.toml");
        let access = ServerAccess { path: path.clone() };
        save_access(&path, &AccessFile::default()).unwrap();

        let coordinator = PeerId([1; 32]);
        let device = PeerId([4; 32]);
        let invite = access.create_invite(coordinator, "Bob").unwrap();
        access.admit(device, Some(invite.token)).unwrap();

        let removed = access.revoke("Bob").unwrap();
        assert_eq!(removed.peer, device);
        assert_eq!(access.admit(device, None).unwrap(), Admission::Denied);
        let _ = fs::remove_dir_all(dir);
    }
}
