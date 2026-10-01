//! One-time enrollment invitations.
//!
//! The invitation is not a reusable room credential. It carries only the coordinator
//! public identity and a random 256-bit token. The server consumes that token on the
//! first successful enrollment and persists the joining device's PeerId.

use anyhow::{Result, anyhow, bail};
use data_encoding::BASE32_NOPAD;

use crate::proto::PeerId;

pub type InviteToken = [u8; 32];

const PREFIX: &str = "fd1";
const PART_CHARS: usize = 52;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Invite {
    pub coordinator: PeerId,
    pub token: InviteToken,
}

pub fn encode(invite: &Invite) -> String {
    format!(
        "{PREFIX}.{}.{}",
        encode_part(&invite.coordinator.0),
        encode_part(&invite.token)
    )
}

pub fn decode(text: &str) -> Result<Invite> {
    let normalized = text.trim().to_ascii_lowercase();
    let mut parts = normalized.split('.');

    let Some(prefix) = parts.next() else {
        bail!("invite is empty");
    };
    let Some(coordinator) = parts.next() else {
        bail!("invite is incomplete");
    };
    let Some(token) = parts.next() else {
        bail!("invite is incomplete");
    };
    if parts.next().is_some() {
        bail!("invite has too many parts");
    }
    if prefix != PREFIX {
        bail!("unsupported invite format");
    }

    Ok(Invite {
        coordinator: PeerId(decode_part(coordinator)?),
        token: decode_part(token)?,
    })
}

pub fn looks_like_invite(text: &str) -> bool {
    text.trim()
        .get(..PREFIX.len() + 1)
        .is_some_and(|head| head.eq_ignore_ascii_case("fd1."))
}

fn encode_part(bytes: &[u8; 32]) -> String {
    BASE32_NOPAD.encode(bytes).to_ascii_lowercase()
}

fn decode_part(text: &str) -> Result<[u8; 32]> {
    if text.len() != PART_CHARS {
        bail!(
            "invite component must be {PART_CHARS} characters, got {}",
            text.len()
        );
    }

    let upper = text.to_ascii_uppercase();
    let bytes = BASE32_NOPAD
        .decode(upper.as_bytes())
        .map_err(|_| anyhow!("invite contains an invalid character"))?;

    bytes
        .as_slice()
        .try_into()
        .map_err(|_| anyhow!("invite component did not decode to 32 bytes"))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn round_trip() {
        let invite = Invite {
            coordinator: PeerId([7; 32]),
            token: [9; 32],
        };
        assert_eq!(decode(&encode(&invite)).unwrap(), invite);
    }

    #[test]
    fn decode_is_case_insensitive_and_trims_outer_space() {
        let invite = Invite {
            coordinator: PeerId([1; 32]),
            token: [2; 32],
        };
        let upper = encode(&invite).to_ascii_uppercase();
        assert_eq!(decode(&format!("  {upper}\n")).unwrap(), invite);
    }

    #[test]
    fn random_text_is_not_mistaken_for_an_invite() {
        assert!(!looks_like_invite("general"));
        assert!(!looks_like_invite("abcd-efgh"));
        assert!(looks_like_invite("FD1.abc.def"));
    }

    #[test]
    fn malformed_invites_are_rejected() {
        assert!(decode("").is_err());
        assert!(decode("fd1.short.short").is_err());
        assert!(decode("fd2.aaaaaaaa.bbbbbbbb").is_err());
    }
}
