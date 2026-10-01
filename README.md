# FakeDiscord

**FakeDiscord** is a privacy-focused desktop voice/text chat application built around a heavily modified fork of [Tincan](https://github.com/bilalyazicioglu/tincan-cli).

It keeps Tincan's encrypted iroh/QUIC peer-to-peer transport while adding a native Windows launcher, an Arch Linux Qt launcher, persistent private servers, direct file transfer, global push-to-talk, safer device pairing, and a GitHub-based update path.

> Current development version: **0.3.2-fd14**

## Highlights

- Encrypted peer-to-peer voice and text chat over iroh/QUIC.
- Persistent private-server identity instead of reusable room passwords.
- One-time invite codes for first-time device registration.
- Persistent device allowlist with revoke support.
- Direct P2P file transfer without routing file contents through the coordinator.
- Public and recipient-only private file offers.
- Streaming transfers with BLAKE2s-256 integrity checks.
- Atomic `.part` download handling and overwrite protection.
- Image previews for supported offers.
- `/send`, `/sendto`, `/get` and path/offer completion.
- Native Windows drag-and-drop file sending.
- Configurable PTT key: F1-F12, A-Z, 0-9, Space and CapsLock.
- Background PTT while another application or game has focus.
- Windows global PTT through Raw Input.
- Linux/KDE Wayland global PTT through a read-only evdev listener.
- Separate microphone open/close notification sounds.
- Russian and English launcher UI.
- Notification, transfer-size, server-name and channel settings.
- Windows native launcher and Arch Linux Qt launcher.
- Terminal launcher remains available on Arch.
- GitHub Releases based update path; no Tailscale/Funnel dependency.

## Platforms

### Windows

The native Win32 launcher embeds the Tincan core and provides:
- graphical menus and settings;
- ConPTY-hosted chat UI;
- drag-and-drop file sending;
- background Raw Input PTT;
- self-update support from GitHub Releases.

### Arch Linux / KDE Wayland

FakeDiscord provides both:
- `fakediscord-gui` — Qt 6 graphical launcher;
- `fakediscord` — terminal launcher.
The Linux core can listen to keyboard input devices in read-only mode for PTT while the launcher is minimized or a game is focused.

## Private server model

FakeDiscord does **not** embed a reusable room password or a specific private-server secret in the public client binary.

A private server has a persistent cryptographic identity. New devices are admitted with a one-time invite and then stored in the host allowlist. Previously authorized devices can reconnect without receiving another invite. Access can be revoked by the host.

## File transfer

File contents flow directly between peers using a dedicated iroh/QUIC file protocol.

The coordinator is used for offer/control metadata, not as a bulk file relay. Downloads are streamed to temporary files, verified, and atomically renamed only after successful completion.

Default launcher limits can be configured from **1 GiB to 16 GiB**.

## Repository layout

- root — native Windows launcher;
- `tincan-src/` — FakeDiscord's modified Tincan core snapshot;
- `arch-linux/` — Arch build/install scripts and Qt launcher;
- `update-server/` — tooling for preparing GitHub Release assets;
- `ARCHITECTURE_RU.md` — architecture notes in Russian;
- `PROJECT_STATUS_RU.md` — implementation status/history.

The separately maintained modified Tincan fork lives at **KpOwOJluK/tincan-cli**.
## GitHub updates

The Windows updater checks:

`https://github.com/KpOwOJluK/FakeDiscord/releases/latest/download/manifest.json`

Release artifacts are validated by expected size and SHA-256 before replacement.

The old Tailscale/Funnel update path has been removed completely.

## Development with ChatGPT

All major FakeDiscord modifications in this repository were implemented with assistance from **ChatGPT by OpenAI**, under the direction and review of the repository owner.

ChatGPT was used throughout the project for implementation, refactoring, debugging, cross-platform work, test creation, build automation, documentation, and release preparation.

See [AI_ASSISTED_DEVELOPMENT.md](AI_ASSISTED_DEVELOPMENT.md) for details.

## Upstream and attribution

The networking/chat core started from Tincan by Bilal Yazicioglu and contributors. Tincan is distributed under the MIT License.

FakeDiscord preserves upstream attribution and documents the modified core separately. See `LICENSE-TINCAN-NOTICE.txt` and the modified Tincan fork for upstream license information.

## Status

This is an active personal/community project. APIs, commands and packaging may continue to change while the fork evolves.
