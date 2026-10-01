# Changelog

This changelog summarizes the FakeDiscord fork milestones.

## 0.3.2-fd15

- Windows settings now use dropdown lists for the PTT hotkey and file-size limit.
- PTT dropdown includes F1-F12, A-Z, 0-9, Space, CapsLock, Mouse4 and Mouse5.
- File-size dropdown exposes all supported limits from 1 to 16 GiB and saves the selection immediately.
- Windows launcher release version bumped for GitHub updater delivery.

## 0.3.2-fd14

- Removed Tailscale/Funnel from the project and update path.
- Windows updater now targets GitHub Releases.
- Windows launcher button renamed to **Update from GitHub**.
- Arch Qt launcher includes a GitHub update/release button.
- Repository metadata prepared for `KpOwOJluK/FakeDiscord`.
- Release packaging updated to include Arch core, terminal launcher, Qt launcher, icon and installer.

## 0.3.2-fd13

- Added Qt 6 graphical launcher for Arch Linux while preserving terminal mode.
- Added distinct microphone-open and microphone-close notification sounds.
- PTT sounds follow actual microphone state to avoid duplicate cues.
- KDE desktop integration and application icon support.

## 0.3.2-fd12

- Added true background PTT while FakeDiscord is unfocused.
- Windows: global keyboard state through Raw Input / `RIDEV_INPUTSINK`.
- Linux/KDE Wayland: read-only evdev keyboard listener.
- Added one-time udev access setup for Linux global PTT.
## 0.3.2-fd11

- Fixed Arch/kitty PTT release handling.
- Enabled full keyboard enhancement events for ordinary keys, Space and CapsLock.
- Added regression coverage for configurable PTT keys.

## 0.3.2-fd10

- Fixed F1 / `/invite` session crash caused by host-only commands reaching an unreachable serialization path.
- Host now processes invite/list/revoke commands locally.
- Clients receive a normal host-only notice instead of crashing.
- Reworked native Windows PTT forwarding.

## 0.3.2-fd9

- Added configurable push-to-talk hotkey.
- Added PTT settings persistence.
- Added hold-to-talk press/release behavior.
- Added supported key mapping for F1-F12, A-Z, 0-9, Space and CapsLock.

## Earlier fork milestones

- Persistent private-server coordinator identity and device pairing.
- One-time invites and host allowlist/revocation.
- Direct P2P file transfer over a dedicated iroh/QUIC protocol.
- Public/private file offers, image preview and transfer integrity validation.
- `/send`, `/sendto`, `/get` completion.
- Windows drag-and-drop support and the F5/deafen regression fix.
- Russian/English launcher settings and configurable file-size limits.
