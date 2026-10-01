#!/usr/bin/env bash
set -euo pipefail

rm -f "$HOME/.local/bin/fakediscord"
rm -f "$HOME/.local/bin/fakediscord-gui"
rm -f "$HOME/.local/lib/fakediscord/tincan"
rmdir "$HOME/.local/lib/fakediscord" 2>/dev/null || true
rm -f "$HOME/.local/share/applications/fakediscord.desktop"
rm -f "$HOME/.local/share/icons/hicolor/256x256/apps/fakediscord.png"

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1 || true
fi

echo "FakeDiscord удалён. Настройки и ник оставлены в ~/.config/fakediscord."
