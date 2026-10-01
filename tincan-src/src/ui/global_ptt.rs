use std::collections::HashSet;
use std::fs::{File, OpenOptions};
use std::os::fd::{AsRawFd, RawFd};
use std::os::unix::fs::OpenOptionsExt;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};

use tokio::sync::mpsc;

use super::UiEvent;

const EV_KEY: u16 = 0x01;

#[repr(C)]
struct LinuxInputEvent {
    time: libc::timeval,
    event_type: u16,
    code: u16,
    value: i32,
}

pub(super) struct Guard {
    stop: Arc<AtomicBool>,
}

impl Drop for Guard {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Release);
    }
}
pub(super) fn spawn(
    configured: &str,
    tx: mpsc::Sender<UiEvent>,
) -> Result<Guard, String> {
    let code = linux_key_code(configured)
        .ok_or_else(|| format!("unsupported global PTT key: {configured}"))?;
    let files = open_keyboards()?;
    if files.is_empty() {
        return Err("no readable keyboard input devices".into());
    }

    let stop = Arc::new(AtomicBool::new(false));
    let worker_stop = stop.clone();

    std::thread::Builder::new()
        .name("fakediscord-global-ptt".into())
        .spawn(move || run_reader(files, code, tx, worker_stop))
        .map_err(|err| format!("could not start global PTT reader: {err}"))?;

    Ok(Guard { stop })
}

fn open_keyboards() -> Result<Vec<File>, String> {
    let entries = std::fs::read_dir("/dev/input")
        .map_err(|err| format!("cannot enumerate input devices: {err}"))?;
    let mut seen = HashSet::new();
    let mut files = Vec::new();
    let mut denied = false;
    for entry in entries.flatten() {
        let name = entry.file_name();
        if !name.to_string_lossy().starts_with("event") {
            continue;
        }

        let path = entry.path();
        if !seen.insert(path.clone()) {
            continue;
        }

        match OpenOptions::new()
            .read(true)
            .custom_flags(libc::O_NONBLOCK)
            .open(&path)
        {
            Ok(file) => files.push(file),
            Err(err) if err.kind() == std::io::ErrorKind::PermissionDenied => {
                denied = true;
            }
            Err(_) => {}
        }
    }

    if files.is_empty() && denied {
        return Err(
            "global PTT needs read access to keyboard input devices; reinstall the Arch launcher to install its udev rule"
                .into(),
        );
    }
    Ok(files)
}
fn run_reader(
    mut files: Vec<File>,
    key_code: u16,
    tx: mpsc::Sender<UiEvent>,
    stop: Arc<AtomicBool>,
) {
    let mut held = HashSet::<RawFd>::new();

    while !stop.load(Ordering::Acquire) && !files.is_empty() {
        let mut pollfds: Vec<libc::pollfd> = files
            .iter()
            .map(|file| libc::pollfd {
                fd: file.as_raw_fd(),
                events: libc::POLLIN,
                revents: 0,
            })
            .collect();

        let ready = unsafe {
            libc::poll(pollfds.as_mut_ptr(), pollfds.len() as libc::nfds_t, 250)
        };
        if ready <= 0 {
            continue;
        }

        let mut dead = Vec::new();
        for (index, pollfd) in pollfds.iter().enumerate() {
            if pollfd.revents & (libc::POLLERR | libc::POLLHUP | libc::POLLNVAL) != 0 {
                if held.remove(&pollfd.fd) && held.is_empty() {
                    let _ = tx.blocking_send(UiEvent::GlobalPtt(false));
                }
                dead.push(index);
                continue;
            }
            if pollfd.revents & libc::POLLIN == 0 {
                continue;
            }

            loop {
                let mut event: LinuxInputEvent = unsafe { std::mem::zeroed() };
                let bytes = unsafe {
                    libc::read(
                        pollfd.fd,
                        (&mut event as *mut LinuxInputEvent).cast(),
                        std::mem::size_of::<LinuxInputEvent>(),
                    )
                };

                if bytes as usize != std::mem::size_of::<LinuxInputEvent>() {
                    break;
                }
                if event.event_type != EV_KEY || event.code != key_code {
                    continue;
                }

                match event.value {
                    1 => {
                        if held.insert(pollfd.fd) && held.len() == 1 {
                            if tx.blocking_send(UiEvent::GlobalPtt(true)).is_err() {
                                return;
                            }
                        }
                    }
                    0 => {
                        if held.remove(&pollfd.fd)
                            && held.is_empty()
                            && tx.blocking_send(UiEvent::GlobalPtt(false)).is_err()
                        {
                            return;
                        }
                    }
                    _ => {}
                }
            }
        }

        for index in dead.into_iter().rev() {
            files.swap_remove(index);
        }
    }

    if !held.is_empty() {
        let _ = tx.blocking_send(UiEvent::GlobalPtt(false));
    }
}

fn linux_key_code(configured: &str) -> Option<u16> {
    let key = configured.trim().to_ascii_uppercase();
    match key.as_str() {
        "SPACE" => Some(57),
        "CAPSLOCK" => Some(58),
        "1" => Some(2), "2" => Some(3), "3" => Some(4), "4" => Some(5),
        "5" => Some(6), "6" => Some(7), "7" => Some(8), "8" => Some(9),
        "9" => Some(10), "0" => Some(11),
        "Q" => Some(16), "W" => Some(17), "E" => Some(18), "R" => Some(19),
        "T" => Some(20), "Y" => Some(21), "U" => Some(22), "I" => Some(23),
        "O" => Some(24), "P" => Some(25),
        "A" => Some(30), "S" => Some(31), "D" => Some(32), "F" => Some(33),
        "G" => Some(34), "H" => Some(35), "J" => Some(36), "K" => Some(37),
        "L" => Some(38),
        "Z" => Some(44), "X" => Some(45), "C" => Some(46), "V" => Some(47),
        "B" => Some(48), "N" => Some(49), "M" => Some(50),
        "F1" => Some(59), "F2" => Some(60), "F3" => Some(61), "F4" => Some(62),
        "F5" => Some(63), "F6" => Some(64), "F7" => Some(65), "F8" => Some(66),
        "F9" => Some(67), "F10" => Some(68), "F11" => Some(87), "F12" => Some(88),
        _ => None,
    }
}

#[cfg(test)]
mod tests {
    use super::linux_key_code;

    #[test]
    fn maps_supported_ptt_keys_to_linux_input_codes() {
        assert_eq!(linux_key_code("CAPSLOCK"), Some(58));
        assert_eq!(linux_key_code("SPACE"), Some(57));
        assert_eq!(linux_key_code("Q"), Some(16));
        assert_eq!(linux_key_code("F12"), Some(88));
    }
}
