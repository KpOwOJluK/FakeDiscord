#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
SRC_DIR="$PROJECT_ROOT/tincan-src"
DIST_DIR="$SCRIPT_DIR/dist"
GUI_SRC="$SCRIPT_DIR/gui_main.cpp"
GUI_BIN="$DIST_DIR/fakediscord-gui"
ICON_SRC="$PROJECT_ROOT/FakeDiscord.ico"
ICON_PNG="$DIST_DIR/fakediscord.png"

missing=()

command -v cargo >/dev/null 2>&1 || missing+=("rust")
command -v rustc >/dev/null 2>&1 || missing+=("rust")
command -v pkg-config >/dev/null 2>&1 || missing+=("pkgconf")
command -v c++ >/dev/null 2>&1 || missing+=("base-devel")

if command -v pkg-config >/dev/null 2>&1; then
    pkg-config --exists alsa 2>/dev/null || missing+=("alsa-lib")
    pkg-config --exists opus 2>/dev/null || missing+=("opus")
    pkg-config --exists Qt6Widgets 2>/dev/null || missing+=("qt6-base")
fi

if ((${#missing[@]} > 0)); then
    mapfile -t missing < <(printf '%s\n' "${missing[@]}" | sort -u)
    echo "Не хватает пакетов для сборки Arch Linux:"
    printf '  %s\n' "${missing[@]}"
    echo
    echo "Установи:"
    echo "  sudo pacman -S --needed base-devel rust pkgconf alsa-lib opus qt6-base"
    exit 1
fi

rust_version="$(rustc --version | awk '{print $2}')"
echo "Rust: $rust_version"

mkdir -p "$DIST_DIR"

echo "[1/4] cargo check"
cargo check --manifest-path "$SRC_DIR/Cargo.toml" --locked

echo "[2/4] file-transfer E2E test"
cargo test --manifest-path "$SRC_DIR/Cargo.toml" --locked --test file_transfer

echo "[3/4] release core build"
cargo build --manifest-path "$SRC_DIR/Cargo.toml" --locked --release

cp -f "$SRC_DIR/target/release/tincan" "$DIST_DIR/tincan"
chmod 0755 "$DIST_DIR/tincan"

if command -v strip >/dev/null 2>&1; then
    strip "$DIST_DIR/tincan" || true
fi

echo "[4/4] Qt launcher build"
c++ -std=c++20 -O2 -Wall -Wextra -fPIC     $(pkg-config --cflags Qt6Widgets)     "$GUI_SRC" -o "$GUI_BIN"     $(pkg-config --libs Qt6Widgets)
chmod 0755 "$GUI_BIN"

if [[ -f "$ICON_SRC" ]] && command -v magick >/dev/null 2>&1; then
    magick "$ICON_SRC[0]" -resize 256x256 "$ICON_PNG" || true
fi

"$DIST_DIR/tincan" --version
echo
echo "Готово:"
echo "  core: $DIST_DIR/tincan"
echo "  gui:  $GUI_BIN"
echo "Для установки: $SCRIPT_DIR/install_arch.sh"
