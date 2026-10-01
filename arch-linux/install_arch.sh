#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
DIST_BIN="$SCRIPT_DIR/dist/tincan"
DIST_GUI="$SCRIPT_DIR/dist/fakediscord-gui"
DIST_ICON="$SCRIPT_DIR/dist/fakediscord.png"
LAUNCHER_SRC="$SCRIPT_DIR/fakediscord"
BIN_DIR="$HOME/.local/bin"
LIB_DIR="$HOME/.local/lib/fakediscord"
APP_DIR="$HOME/.local/share/applications"
ICON_DIR="$HOME/.local/share/icons/hicolor/256x256/apps"
DESKTOP_FILE="$APP_DIR/fakediscord.desktop"

first_keyboard_event() {
    local event=""
    for event in /dev/input/event*; do
        [[ -e "$event" ]] || continue
        if udevadm info -q property -n "$event" 2>/dev/null | grep -q '^ID_INPUT_KEYBOARD=1$'; then
            printf '%s
' "$event"
            return 0
        fi
    done
    return 1
}

first_mouse_event() {
    local event=""
    for event in /dev/input/event*; do
        [[ -e "$event" ]] || continue
        if udevadm info -q property -n "$event" 2>/dev/null | grep -q '^ID_INPUT_MOUSE=1$'; then
            printf '%s\n' "$event"
            return 0
        fi
    done
    return 1
}

install_global_ptt_access() {
    local keyboard=""
    local mouse=""
    local temp_rule=""
    keyboard="$(first_keyboard_event || true)"
    mouse="$(first_mouse_event || true)"
    if [[ -n "$keyboard" && -r "$keyboard" ]] &&
       { [[ -z "$mouse" ]] || [[ -r "$mouse" ]]; }; then
        return 0
    fi

    temp_rule="$(mktemp)"
    printf '%s
' 'SUBSYSTEM=="input", KERNEL=="event*", ENV{ID_INPUT_KEYBOARD}=="1", TAG+="uaccess"' > "$temp_rule"
    printf '%s\n' 'SUBSYSTEM=="input", KERNEL=="event*", ENV{ID_INPUT_MOUSE}=="1", TAG+="uaccess"' >> "$temp_rule"

    echo "Настройка фонового PTT: требуется одноразовое разрешение на чтение клавиатуры и мыши."
    local install_cmd='install -m 0644 "$1" /etc/udev/rules.d/70-fakediscord-ptt.rules && udevadm control --reload-rules && udevadm trigger --subsystem-match=input --action=change'

    if command -v pkexec >/dev/null 2>&1; then
        pkexec /bin/sh -c "$install_cmd" sh "$temp_rule" || true
    elif command -v sudo >/dev/null 2>&1; then
        sudo /bin/sh -c "$install_cmd" sh "$temp_rule" || true
    fi
    rm -f "$temp_rule"

    keyboard="$(first_keyboard_event || true)"
    mouse="$(first_mouse_event || true)"
    if [[ -n "$keyboard" && -r "$keyboard" ]] &&
       { [[ -z "$mouse" ]] || [[ -r "$mouse" ]]; }; then
        echo "Фоновый PTT: доступ к клавиатуре и мыши настроен."
    else
        echo "Внимание: глобальный PTT пока недоступен; терминальный PTT останется резервным."
    fi
}

if [[ ! -x "$DIST_BIN" || ! -x "$DIST_GUI" ]]; then
    echo "Release binaries not found; building them first..."
    bash "$SCRIPT_DIR/build_arch.sh"
fi

mkdir -p "$BIN_DIR" "$LIB_DIR" "$APP_DIR" "$ICON_DIR"
install -m 0755 "$DIST_BIN" "$LIB_DIR/tincan"
install -m 0755 "$DIST_GUI" "$BIN_DIR/fakediscord-gui"
install -m 0755 "$LAUNCHER_SRC" "$BIN_DIR/fakediscord"
if [[ -f "$DIST_ICON" ]]; then
    install -m 0644 "$DIST_ICON" "$ICON_DIR/fakediscord.png"
fi

cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=FakeDiscord
Comment=P2P voice, text and file transfer
Exec=$BIN_DIR/fakediscord-gui
Icon=fakediscord
Terminal=false
Categories=Network;Chat;
StartupNotify=true
EOF

if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$APP_DIR" >/dev/null 2>&1 || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache "$HOME/.local/share/icons/hicolor" >/dev/null 2>&1 || true
fi

install_global_ptt_access

echo "Установлено:"
echo "  gui:      $BIN_DIR/fakediscord-gui"
echo "  terminal: $BIN_DIR/fakediscord"
echo "  core:     $LIB_DIR/tincan"
echo "  desktop:  $DESKTOP_FILE"
echo
if [[ ":$PATH:" != *":$BIN_DIR:"* ]]; then
    echo "Внимание: $BIN_DIR не найден в PATH."
    echo "Добавь в ~/.bashrc или ~/.zshrc:"
    echo '  export PATH="$HOME/.local/bin:$PATH"'
    echo
fi
echo "Запуск GUI:      fakediscord-gui"
echo "Запуск терминала: fakediscord"
