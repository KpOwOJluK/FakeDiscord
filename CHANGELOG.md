# Changelog

This changelog summarizes the FakeDiscord fork milestones.

## 0.3.2-fd15

- Windows PTT hotkey and file-size selections now use themed dropdown menus that match the FakeDiscord launcher style.
- Closed selectors use the same owner-draw blue buttons as the rest of Settings; popup menus use the dark FakeDiscord palette.
- PTT choices include F1-F12, A-Z, 0-9, Space, CapsLock, Mouse4 and Mouse5.
- File-size choices cover all supported limits from 1 to 16 GiB and save immediately.
- Updater can detect a rebuilt artifact with the same version by comparing the local executable SHA-256 with the release manifest.

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
