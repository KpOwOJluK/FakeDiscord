//! Cross-platform filesystem completion for the /send command.

use std::ffi::OsString;
use std::fs;
use std::path::{MAIN_SEPARATOR, Path, PathBuf};

use crate::proto::{FileOffer, PeerInfo};

/// Result of one Tab completion attempt.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Completion {
    /// Full replacement for the TUI input line.
    pub input: String,
    /// Optional short hint shown in the status line.
    pub hint: Option<String>,
}

#[derive(Debug)]
struct Candidate {
    name: String,
    is_dir: bool,
}

/// Completes the path after "/send ".
///
/// Returns None when the current input is not a /send command, so Tab can retain
/// its normal channel-switching meaning everywhere else.
pub fn complete_send_input(input: &str) -> Option<Completion> {
    if input == "/send" {
        return Some(Completion {
            input: "/send ".into(),
            hint: Some("file path completion enabled".into()),
        });
    }

    let argument = input.strip_prefix("/send ")?;
    Some(complete_argument(argument))
}

/// Completes /sendto in two stages: participant nickname first, filesystem path second.
///
/// Nicknames are matched case-insensitively. Names containing whitespace are quoted
/// automatically, so a completed command stays unambiguous before path completion.
pub fn complete_sendto_input(
    input: &str,
    peers: &[PeerInfo],
    me: crate::proto::PeerId,
) -> Option<Completion> {
    if input == "/sendto" {
        return Some(Completion {
            input: "/sendto ".into(),
            hint: Some(
                participant_hint(peers, me, "").unwrap_or_else(|| "no other participants".into()),
            ),
        });
    }

    let rest = input.strip_prefix("/sendto ")?;

    // A quoted nickname is either still being typed or already closed and followed
    // by the filesystem argument.
    if let Some(quoted) = rest.strip_prefix('"') {
        if let Some(end) = find_unescaped_quote(quoted) {
            let token_end = end + 2; // opening quote + body + closing quote
            let recipient_token = &rest[..token_end];
            let path = rest[token_end..].trim_start();
            let mut completion = complete_argument(path);
            completion.input =
                completion
                    .input
                    .replacen("/send ", &format!("/sendto {recipient_token} "), 1);
            return Some(completion);
        }

        let query = unescape_recipient_token(quoted);
        return Some(complete_recipient_query(peers, me, &query, true));
    }

    if let Some(split) = rest.find(char::is_whitespace) {
        let recipient_token = rest[..split].trim();

        // Once the first token is an exact nickname, the following text belongs to
        // the path. Nicknames containing whitespace are emitted quoted by Tab.
        let exact_recipient = peers
            .iter()
            .any(|peer| peer.id != me && peer.name.eq_ignore_ascii_case(recipient_token));
        if exact_recipient {
            let path = rest[split..].trim_start();
            let mut completion = complete_argument(path);
            completion.input =
                completion
                    .input
                    .replacen("/send ", &format!("/sendto {recipient_token} "), 1);
            return Some(completion);
        }

        // Otherwise whitespace can still be part of an unfinished nickname:
        // "John D<Tab>" may resolve "John Doe".
        if matching_peers(peers, me, rest).next().is_some() {
            return Some(complete_recipient_query(peers, me, rest, false));
        }

        let recipient_token = rest[..split].trim();
        if recipient_token.is_empty() {
            return Some(Completion {
                input: "/sendto ".into(),
                hint: Some(
                    participant_hint(peers, me, "")
                        .unwrap_or_else(|| "no other participants".into()),
                ),
            });
        }

        let path = rest[split..].trim_start();
        let mut completion = complete_argument(path);
        completion.input =
            completion
                .input
                .replacen("/send ", &format!("/sendto {recipient_token} "), 1);
        return Some(completion);
    }

    Some(complete_recipient_query(peers, me, rest.trim(), false))
}

fn complete_recipient_query(
    peers: &[PeerInfo],
    me: crate::proto::PeerId,
    query: &str,
    forced_quote: bool,
) -> Completion {
    let mut matches = matching_peers(peers, me, query).collect::<Vec<_>>();
    matches.sort_by_key(|peer| peer.name.to_ascii_lowercase());

    if matches.is_empty() {
        return Completion {
            input: if forced_quote {
                format!("/sendto \"{query}")
            } else {
                format!("/sendto {query}")
            },
            hint: Some("no matching participant".into()),
        };
    }

    if matches.len() == 1 {
        let nickname = &matches[0].name;
        return Completion {
            input: format!("/sendto {} ", quote_recipient(nickname)),
            hint: Some(format!("recipient: {nickname} · now type or drop a file")),
        };
    }

    let names = matches
        .iter()
        .map(|peer| peer.name.as_str())
        .collect::<Vec<_>>();
    let common = common_prefix(&names);
    let completed = if common.chars().count() > query.chars().count() {
        common
    } else {
        query.to_string()
    };

    let needs_open_quote = forced_quote || completed.chars().any(char::is_whitespace);
    Completion {
        input: if needs_open_quote {
            format!("/sendto \"{}", escape_recipient(&completed))
        } else {
            format!("/sendto {completed}")
        },
        hint: Some(participant_hint(peers, me, query).unwrap_or_default()),
    }
}

fn matching_peers<'a>(
    peers: &'a [PeerInfo],
    me: crate::proto::PeerId,
    query: &'a str,
) -> impl Iterator<Item = &'a PeerInfo> {
    let lower = query.to_ascii_lowercase();
    peers.iter().filter(move |peer| {
        peer.id != me && (query.is_empty() || peer.name.to_ascii_lowercase().starts_with(&lower))
    })
}

fn quote_recipient(name: &str) -> String {
    if name.chars().any(char::is_whitespace) || name.contains('"') || name.contains('\\') {
        format!("\"{}\"", escape_recipient(name))
    } else {
        name.to_string()
    }
}

fn escape_recipient(name: &str) -> String {
    name.replace('\\', "\\\\").replace('"', "\\\"")
}

fn unescape_recipient_token(token: &str) -> String {
    let mut result = String::new();
    let mut escaped = false;
    for ch in token.chars() {
        if escaped {
            result.push(ch);
            escaped = false;
        } else if ch == '\\' {
            escaped = true;
        } else {
            result.push(ch);
        }
    }
    if escaped {
        result.push('\\');
    }
    result
}

fn find_unescaped_quote(text: &str) -> Option<usize> {
    let mut escaped = false;
    for (index, ch) in text.char_indices() {
        if escaped {
            escaped = false;
            continue;
        }
        if ch == '\\' {
            escaped = true;
        } else if ch == '"' {
            return Some(index);
        }
    }
    None
}

fn participant_hint(peers: &[PeerInfo], me: crate::proto::PeerId, query: &str) -> Option<String> {
    let mut names = matching_peers(peers, me, query)
        .map(|peer| peer.name.clone())
        .collect::<Vec<_>>();
    names.sort_by_key(|name| name.to_ascii_lowercase());

    if names.is_empty() {
        None
    } else {
        Some(format!(
            "participants: {}",
            names.into_iter().take(8).collect::<Vec<_>>().join("  ")
        ))
    }
}

/// Completes /get against file offers visible in the current text channel.
/// A hex prefix matches transfer IDs; other text searches file names.
pub fn complete_get_input(input: &str, offers: &[FileOffer]) -> Option<Completion> {
    if input != "/get" && !input.starts_with("/get ") {
        return None;
    }

    let query = input.strip_prefix("/get").unwrap_or_default().trim();

    let query_lower = query.to_lowercase();
    let looks_like_id =
        !query.is_empty() && query.chars().all(|character| character.is_ascii_hexdigit());

    let mut matches = offers
        .iter()
        .filter(|offer| {
            if query.is_empty() {
                return true;
            }

            if looks_like_id && offer.id.to_string().starts_with(&query_lower) {
                return true;
            }

            offer.name.to_lowercase().contains(&query_lower)
        })
        .collect::<Vec<_>>();

    matches.reverse();

    if matches.is_empty() {
        return Some(Completion {
            input: if query.is_empty() {
                "/get ".into()
            } else {
                format!("/get {query}")
            },
            hint: Some("no matching shared file".into()),
        });
    }

    if matches.len() == 1 {
        let offer = matches[0];
        return Some(Completion {
            input: format!("/get {}", offer.id.short()),
            hint: Some(format!("{} · {} bytes", offer.name, offer.size)),
        });
    }

    let completed_input = if looks_like_id {
        let ids = matches
            .iter()
            .map(|offer| offer.id.to_string())
            .collect::<Vec<_>>();
        let refs = ids.iter().map(String::as_str).collect::<Vec<_>>();
        let common = common_prefix(&refs);

        if common.len() > query.len() {
            format!("/get {common}")
        } else {
            format!("/get {query}")
        }
    } else if query.is_empty() {
        "/get ".into()
    } else {
        format!("/get {query}")
    };

    let mut labels = matches
        .iter()
        .take(6)
        .map(|offer| format!("{} {}", offer.id.short(), offer.name))
        .collect::<Vec<_>>();

    if matches.len() > labels.len() {
        labels.push(format!("+{} more", matches.len() - labels.len()));
    }

    Some(Completion {
        input: completed_input,
        hint: Some(format!("files: {}", labels.join("  "))),
    })
}

fn complete_argument(argument: &str) -> Completion {
    let raw = argument.trim_start();
    let had_quote = raw.starts_with('"');
    let mut path_text = if had_quote { &raw[1..] } else { raw };

    if path_text.ends_with('"') {
        path_text = &path_text[..path_text.len() - 1];
    }

    if path_text == "~" {
        let mut completed = path_text.to_string();
        completed.push(preferred_separator(path_text));
        return Completion {
            input: input_for(&completed, had_quote, false),
            hint: Some("home directory".into()),
        };
    }

    let lookup_path = expand_home(path_text);
    let ends_with_separator = path_text.ends_with('/') || path_text.ends_with('\\');
    let browse_home = path_text.is_empty() && home_dir().is_some();

    let (lookup_dir, typed_prefix) = if path_text.is_empty() {
        (
            home_dir().unwrap_or_else(|| PathBuf::from(".")),
            String::new(),
        )
    } else if ends_with_separator {
        (lookup_path.clone(), String::new())
    } else {
        let parent = lookup_path
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
            .unwrap_or_else(|| Path::new("."))
            .to_path_buf();

        let prefix = lookup_path
            .file_name()
            .map(|name| name.to_string_lossy().into_owned())
            .unwrap_or_default();

        (parent, prefix)
    };

    let mut candidates = match list_candidates(&lookup_dir, &typed_prefix) {
        Ok(candidates) => candidates,
        Err(error) => {
            return Completion {
                input: input_for(path_text, had_quote, false),
                hint: Some(error),
            };
        }
    };

    if candidates.is_empty() {
        return Completion {
            input: input_for(path_text, had_quote, false),
            hint: Some("no matching file or directory".into()),
        };
    }

    candidates.sort_by(|left, right| {
        right
            .is_dir
            .cmp(&left.is_dir)
            .then_with(|| compare_names(&left.name, &right.name))
    });
    let display_parent = if browse_home {
        PathBuf::from("~")
    } else {
        display_parent(path_text, ends_with_separator)
    };

    if candidates.len() == 1 {
        let candidate = &candidates[0];
        let mut completed = join_display_path(&display_parent, &candidate.name);

        if candidate.is_dir {
            completed.push(preferred_separator(path_text));
        }

        return Completion {
            input: input_for(&completed, had_quote, !candidate.is_dir),
            hint: Some(if candidate.is_dir {
                format!("directory: {}", candidate.name)
            } else {
                format!("file: {}", candidate.name)
            }),
        };
    }

    let common = common_prefix(
        &candidates
            .iter()
            .map(|candidate| candidate.name.as_str())
            .collect::<Vec<_>>(),
    );

    let completed = if common.chars().count() > typed_prefix.chars().count() {
        join_display_path(&display_parent, &common)
    } else {
        path_text.to_string()
    };

    let mut names = candidates
        .iter()
        .take(6)
        .map(|candidate| {
            if candidate.is_dir {
                format!("{}{MAIN_SEPARATOR}", candidate.name)
            } else {
                candidate.name.clone()
            }
        })
        .collect::<Vec<_>>();

    if candidates.len() > names.len() {
        names.push(format!("+{} more", candidates.len() - names.len()));
    }

    Completion {
        input: input_for(&completed, had_quote, false),
        hint: Some(format!("matches: {}", names.join("  "))),
    }
}
fn list_candidates(directory: &Path, prefix: &str) -> Result<Vec<Candidate>, String> {
    let entries = fs::read_dir(directory)
        .map_err(|error| format!("cannot read {}: {error}", directory.display()))?;

    let mut candidates = Vec::new();

    for entry in entries.flatten() {
        let name: OsString = entry.file_name();
        let name = name.to_string_lossy().into_owned();

        if !starts_with_name(&name, prefix) {
            continue;
        }

        let is_dir = entry.file_type().map(|kind| kind.is_dir()).unwrap_or(false);

        candidates.push(Candidate { name, is_dir });
    }

    Ok(candidates)
}

fn starts_with_name(name: &str, prefix: &str) -> bool {
    #[cfg(windows)]
    {
        name.to_lowercase().starts_with(&prefix.to_lowercase())
    }

    #[cfg(not(windows))]
    {
        name.starts_with(prefix)
    }
}

fn compare_names(left: &str, right: &str) -> std::cmp::Ordering {
    #[cfg(windows)]
    {
        left.to_lowercase().cmp(&right.to_lowercase())
    }

    #[cfg(not(windows))]
    {
        left.cmp(right)
    }
}
fn common_prefix(names: &[&str]) -> String {
    let Some(first) = names.first() else {
        return String::new();
    };

    let mut prefix = first.to_string();

    for name in &names[1..] {
        let mut bytes = 0usize;

        for (left, right) in prefix.chars().zip(name.chars()) {
            let equal = if cfg!(windows) {
                left.to_lowercase().to_string() == right.to_lowercase().to_string()
            } else {
                left == right
            };

            if !equal {
                break;
            }

            bytes += left.len_utf8();
        }

        prefix.truncate(bytes);
        if prefix.is_empty() {
            break;
        }
    }

    prefix
}

fn display_parent(path_text: &str, ends_with_separator: bool) -> PathBuf {
    if path_text.is_empty() {
        return PathBuf::new();
    }

    let display = PathBuf::from(path_text);

    if ends_with_separator {
        display
    } else {
        display
            .parent()
            .unwrap_or_else(|| Path::new(""))
            .to_path_buf()
    }
}

fn join_display_path(parent: &Path, name: &str) -> String {
    if parent.as_os_str().is_empty() {
        name.to_string()
    } else {
        parent.join(name).to_string_lossy().into_owned()
    }
}
fn preferred_separator(path_text: &str) -> char {
    #[cfg(windows)]
    {
        if path_text.contains('/') && !path_text.contains('\\') {
            '/'
        } else {
            '\\'
        }
    }

    #[cfg(not(windows))]
    {
        let _ = path_text;
        '/'
    }
}

fn input_for(path_text: &str, had_quote: bool, final_file: bool) -> String {
    let needs_quote = had_quote || path_text.chars().any(char::is_whitespace);

    if needs_quote {
        if final_file {
            format!("/send \"{path_text}\"")
        } else {
            format!("/send \"{path_text}")
        }
    } else {
        format!("/send {path_text}")
    }
}

fn expand_home(path: &str) -> PathBuf {
    if path == "~" {
        return home_dir().unwrap_or_else(|| PathBuf::from(path));
    }

    if let Some(rest) = path.strip_prefix("~/").or_else(|| path.strip_prefix("~\\")) {
        if let Some(home) = home_dir() {
            return home.join(rest);
        }
    }

    PathBuf::from(path)
}

fn home_dir() -> Option<PathBuf> {
    #[cfg(windows)]
    {
        std::env::var_os("USERPROFILE").map(PathBuf::from)
    }

    #[cfg(not(windows))]
    {
        std::env::var_os("HOME").map(PathBuf::from)
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_dir() -> PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        std::env::temp_dir().join(format!("tincan-complete-{}-{nonce}", std::process::id()))
    }

    #[test]
    fn tab_after_send_enables_path_mode() {
        let result = complete_send_input("/send").unwrap();
        assert_eq!(result.input, "/send ");
    }

    #[test]
    fn completes_a_unique_file_and_quotes_spaces() {
        let root = temp_dir();
        fs::create_dir_all(&root).unwrap();
        fs::write(root.join("my photo.png"), b"x").unwrap();

        let input = format!("/send {}{}my p", root.display(), MAIN_SEPARATOR);
        let result = complete_send_input(&input).unwrap();

        assert!(result.input.starts_with("/send \""));
        assert!(result.input.ends_with("my photo.png\""));

        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn completes_directory_then_nested_file() {
        let root = temp_dir();
        let folder = root.join("folder");
        fs::create_dir_all(&folder).unwrap();
        fs::write(folder.join("nested.txt"), b"x").unwrap();

        let first = format!("/send {}{}fol", root.display(), MAIN_SEPARATOR);
        let first = complete_send_input(&first).unwrap();
        assert!(first.input.ends_with(&format!("folder{MAIN_SEPARATOR}")));

        let second = format!("{}nes", first.input);
        let second = complete_send_input(&second).unwrap();
        assert!(second.input.ends_with("nested.txt"));

        let _ = fs::remove_dir_all(root);
    }

    fn offer(id_byte: u8, name: &str) -> FileOffer {
        FileOffer {
            id: crate::proto::TransferId([id_byte; 16]),
            channel: crate::proto::ChannelId(0),
            from: crate::proto::PeerId([9; 32]),
            recipient: None,
            name: name.into(),
            size: 42,
            preview: crate::proto::FilePreview::Generic {
                kind: "FILE".into(),
            },
            digest: [7; 32],
        }
    }

    fn peer(byte: u8, name: &str) -> PeerInfo {
        PeerInfo {
            id: crate::proto::PeerId([byte; 32]),
            name: name.into(),
            channel: None,
            muted: false,
            deafened: false,
            afk: false,
        }
    }

    #[test]
    fn sendto_tab_completes_nickname_case_insensitively() {
        let me = crate::proto::PeerId([1; 32]);
        let peers = vec![peer(1, "Me"), peer(2, "Alice"), peer(3, "Bob")];

        let result = complete_sendto_input("/sendto al", &peers, me).unwrap();

        assert_eq!(result.input, "/sendto Alice ");
        assert!(result.hint.unwrap().contains("Alice"));
    }

    #[test]
    fn sendto_tab_completes_common_nickname_prefix() {
        let me = crate::proto::PeerId([1; 32]);
        let peers = vec![peer(1, "Me"), peer(2, "Johnny"), peer(3, "John Doe")];

        let result = complete_sendto_input("/sendto Jo", &peers, me).unwrap();

        assert_eq!(result.input, "/sendto John");
        let hint = result.hint.unwrap();
        assert!(hint.contains("Johnny"));
        assert!(hint.contains("John Doe"));
    }

    #[test]
    fn sendto_tab_quotes_nickname_with_spaces() {
        let me = crate::proto::PeerId([1; 32]);
        let peers = vec![peer(1, "Me"), peer(2, "John Doe")];

        let result = complete_sendto_input("/sendto John D", &peers, me).unwrap();

        assert_eq!(result.input, "/sendto \"John Doe\" ");
        assert!(result.hint.unwrap().contains("John Doe"));
    }

    #[test]
    fn sendto_tab_switches_from_nickname_to_path_completion() {
        let me = crate::proto::PeerId([1; 32]);
        let peers = vec![peer(1, "Me"), peer(2, "Alice")];
        let root = temp_dir();
        fs::create_dir_all(&root).unwrap();
        fs::write(root.join("photo.png"), b"x").unwrap();

        let input = format!("/sendto Alice {}{}pho", root.display(), MAIN_SEPARATOR);
        let result = complete_sendto_input(&input, &peers, me).unwrap();

        assert!(result.input.starts_with("/sendto Alice "));
        assert!(result.input.ends_with("photo.png"));

        let _ = fs::remove_dir_all(root);
    }

    #[test]
    fn sendto_completion_never_captures_plain_send() {
        let me = crate::proto::PeerId([1; 32]);
        let peers = vec![peer(1, "Me"), peer(2, "Alice")];

        assert!(complete_sendto_input("/send C:\\temp", &peers, me).is_none());
    }

    #[test]
    fn get_tab_lists_visible_offers() {
        let offers = vec![offer(0x11, "photo.png"), offer(0x22, "archive.zip")];

        let result = complete_get_input("/get", &offers).unwrap();

        assert_eq!(result.input, "/get ");
        let hint = result.hint.unwrap();
        assert!(hint.contains("photo.png"));
        assert!(hint.contains("archive.zip"));
    }

    #[test]
    fn get_tab_completes_unique_id_prefix() {
        let offers = vec![offer(0x11, "photo.png"), offer(0x22, "archive.zip")];

        let result = complete_get_input("/get 1111", &offers).unwrap();

        assert_eq!(result.input, "/get 11111111");
        assert!(result.hint.unwrap().contains("photo.png"));
    }

    #[test]
    fn get_tab_finds_file_by_name_fragment() {
        let offers = vec![offer(0x11, "holiday-photo.png"), offer(0x22, "archive.zip")];

        let result = complete_get_input("/get holiday", &offers).unwrap();

        assert_eq!(result.input, "/get 11111111");
        assert!(result.hint.unwrap().contains("holiday-photo.png"));
    }
}
