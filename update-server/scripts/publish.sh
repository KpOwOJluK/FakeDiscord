#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/../.." && pwd)"
PUBLIC_DIR="$PROJECT_ROOT/update-server/public/stable"

VERSION="${1:-$(sed -n 's/.*kAppVersion\[\].*L"\([^"]*\)".*/\1/p' "$PROJECT_ROOT/AppTypes.h" | head -n1 | tr -d '\r')}"
WIN_SOURCE="$PROJECT_ROOT/FakeDiscord.exe"
LINUX_CORE="$PROJECT_ROOT/arch-linux/dist/tincan"
LINUX_LAUNCHER="$PROJECT_ROOT/arch-linux/fakediscord"
LINUX_GUI="$PROJECT_ROOT/arch-linux/dist/fakediscord-gui"
LINUX_ICON="$PROJECT_ROOT/arch-linux/dist/fakediscord.png"
LINUX_INSTALLER="$PROJECT_ROOT/arch-linux/install_arch.sh"

for file in "$WIN_SOURCE" "$LINUX_CORE" "$LINUX_LAUNCHER" "$LINUX_GUI" "$LINUX_ICON" "$LINUX_INSTALLER"; do
    if [[ ! -f "$file" ]]; then
        echo "Не найден обязательный файл: $file" >&2
        exit 1
    fi
done

mkdir -p "$PUBLIC_DIR"
cp -f "$WIN_SOURCE" "$PUBLIC_DIR/FakeDiscord.exe"

stage="$(mktemp -d)"
trap 'rm -rf "$stage"' EXIT
mkdir -p "$stage/dist"
cp -f "$LINUX_CORE" "$stage/dist/tincan"
cp -f "$LINUX_GUI" "$stage/dist/fakediscord-gui"
cp -f "$LINUX_ICON" "$stage/dist/fakediscord.png"
cp -f "$LINUX_LAUNCHER" "$stage/fakediscord"
cp -f "$LINUX_INSTALLER" "$stage/install_arch.sh"
chmod 0755 "$stage/dist/tincan" "$stage/fakediscord" "$stage/dist/fakediscord-gui" "$stage/install_arch.sh"
tar -C "$stage" -czf "$PUBLIC_DIR/FakeDiscord-arch-x86_64.tar.gz"     dist fakediscord install_arch.sh

export VERSION PUBLIC_DIR
python3 - <<'PY'
import hashlib, json, os
from datetime import datetime, timezone
from pathlib import Path

root = Path(os.environ["PUBLIC_DIR"])
version = os.environ["VERSION"]

def artifact(name):
    path = root / name
    digest = hashlib.sha256(path.read_bytes()).hexdigest()
    return {"file": name, "url": name, "size": path.stat().st_size, "sha256": digest}

manifest = {
    "schema": 2,
    "channel": "stable",
    "version": version,
    "published_at": datetime.now(timezone.utc).isoformat().replace("+00:00", "Z"),
    "artifacts": {
        "windows-x64": artifact("FakeDiscord.exe"),
        "linux-x86_64": artifact("FakeDiscord-arch-x86_64.tar.gz"),
    },
}
(root / "manifest.json").write_text(
    json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
    encoding="utf-8",
)
PY

echo "Опубликован релиз $VERSION"
sha256sum "$PUBLIC_DIR/FakeDiscord.exe" "$PUBLIC_DIR/FakeDiscord-arch-x86_64.tar.gz"
echo "Manifest: $PUBLIC_DIR/manifest.json"
