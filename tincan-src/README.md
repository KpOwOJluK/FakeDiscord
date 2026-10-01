# FakeDiscord — Tincan fork

FakeDiscord is a privacy-focused fork of [Tincan](https://github.com/bilalyazicioglu/tincan-cli).

This fork keeps Tincan's encrypted iroh/QUIC transport and peer-to-peer voice model, while adding a persistent private-server access model, direct file transfer, native Windows launcher support, and a simple Arch Linux launcher.

> Upstream documentation is preserved in [README_UPSTREAM.md](README_UPSTREAM.md).

## Current fork status

Current development version: **0.3.2-fd14**

Control protocol: `tincan/control/4`

File-transfer protocol: `tincan/file/1`

## What is different from upstream Tincan

The original Tincan room/password admission model is not used by this fork.

FakeDiscord uses:

- a persistent cryptographic identity for the private server;
- a persistent cryptographic identity for every client device;
- one-time enrollment invites;
- a persistent server-side allowlist;
- device revocation;
- no reusable room password;
- no deployment-specific server address, password, or invite baked into the public client binary.

The control plane is hosted by the person running the private server. Voice and file payloads remain peer-to-peer whenever the transport can establish a direct path.

## First connection

The server owner starts the private server:

```bash
tincan host --name alice
```

Create a one-time invite for a friend or device:

```bash
tincan invite "Bob laptop"
```

The joining device uses the invite once:

```bash
tincan join fd1.... --name bob
```

After successful enrollment, the invite is consumed and the device PeerId is stored in the server allowlist.

Future launches on the same device do not need another invite:

```bash
tincan join --name bob
```

The client keeps its own device identity on disk, and the server keeps the allowlist on disk, so normal client/server restarts do not require re-enrollment.

## Access administration

List enrolled devices:

```bash
tincan peers
```

Revoke a device by label or PeerId prefix:

```bash
tincan revoke "Bob laptop"
```

Inside the host UI the same operations are available as:

```text
/invite <device-label>
/auth
/revoke <device-label-or-peer-prefix>
```

## Private server settings

The host command supports:

```bash
tincan host \
  --name alice \
  --server-name "Friends" \
  --channels general,gaming,music \
  --max-file-gib 8
```

Relevant options:

| Option | Meaning |
| --- | --- |
| `--server-name <NAME>` | Display name shown to connected clients |
| `--channels <LIST>` | Comma-separated channel list |
| `--max-file-gib <1..16>` | Local maximum size of files this client may send |
| `--no-notifications` | Disable message/join notification sounds |
| `--no-voice` | Text/file mode without voice |
| `--input <DEVICE>` | Preferred microphone |
| `--output <DEVICE>` | Preferred output device |
| `--ptt` | Push-to-talk mode |
| `--ptt-key <KEY>` | PTT key: F1-F12, A-Z, 0-9, Space or CapsLock |

The protocol-level file ceiling is **16 GiB**. A user's local send limit may be set lower.

## Launchers

### Windows

The native Windows launcher provides:

- **Start private server / Запустить приватный сервер**
- connect using the saved pairing;
- first connection using a one-time invite;
- RU/EN launcher language;
- notifications on/off;
- configurable hold-to-talk PTT with a saved hotkey;
- outgoing file limit from 1 to 16 GiB;
- private-server display name;
- server channel list;
- nickname and audio-device controls;
- updater entry point.

Launcher settings are stored under:

```text
%APPDATA%\FakeDiscord\launcher-settings.conf
```

Device/server cryptographic state is stored separately from the executable.

### Arch Linux

Run:

```bash
fakediscord
```

The Arch launcher exposes the same main settings:

- Russian / English;
- notification sounds;
- configurable hold-to-talk PTT with a saved hotkey;
- outgoing file limit 1–16 GiB;
- private-server display name;
- channel list;
- saved pairing / one-time invite;
- access administration.

Launcher settings are stored under:

```text
~/.config/fakediscord/launcher-settings.conf
```

or under `$XDG_CONFIG_HOME/fakediscord/` when `XDG_CONFIG_HOME` is set.

## Security state

By default, FakeDiscord stores security state outside the binary.

Linux:

```text
~/.config/fakediscord/server/identity.key
~/.config/fakediscord/server/access.toml
~/.config/fakediscord/client/identity.key
~/.config/fakediscord/client/pairing.toml
```

Windows uses the equivalent `%APPDATA%\FakeDiscord\...` directories.

The server stores invite tokens only while they are unused. A valid one-time invite enrolls the connecting transport-authenticated PeerId and is then consumed.

Security does **not** rely on hiding the executable. A public build can be downloaded by someone who is not authorized; without a valid one-time invite or an already-authorized device identity, the private server rejects the connection.

## File transfer

Files are transferred directly between peers over the dedicated `tincan/file/1` ALPN.

Features include:

- direct P2P streaming;
- BLAKE2s-256 integrity verification;
- `.part` temporary files and atomic rename on success;
- filename sanitization;
- public file offers scoped to their channel;
- private file offers scoped to the intended participant;
- image previews;
- `/send`, `/sendto`, `/files`, and `/get`;
- path completion with Tab;
- maximum protocol size of 16 GiB.

## Architecture

The fork has two main traffic paths.

**Control plane:** the private server manages enrollment, the allowlist, roster, channels, chat metadata, and file-offer metadata.

**Peer-to-peer plane:** voice and file payloads are exchanged directly between peers when possible. iroh may use its relay infrastructure when NAT traversal cannot establish a direct path.

The private server is therefore an authorization and coordination point, not a media/file relay by design.

## Building this fork

### Arch Linux

```bash
bash arch-linux/build_arch.sh
bash arch-linux/install_arch.sh
```

### Windows

The repository includes the native ConPTY launcher project and `build_msvc.bat`.

Build the Rust core first, place the resulting Windows `tincan.exe` next to the native launcher sources, then run the MSVC build script from a Visual Studio x64 build environment.

## Upstream credit

FakeDiscord is based on Tincan by Bilal Yazicioglu and contributors.

Original project:

https://github.com/bilalyazicioglu/tincan-cli

The original README is retained in this fork as `README_UPSTREAM.md`.

See the repository's license files for the applicable license terms.
