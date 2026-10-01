#pragma once

#include "Win32Config.h"
#include <windows.h>

/// Показывает модальное окно подтверждения отключения в стиле FakeDiscord.
/// Возвращает true только если пользователь явно выбрал «Да».
bool ShowDisconnectConfirm(
    HWND owner,
    HINSTANCE instance,
    HICON iconBig,
    HICON iconSmall,
    HFONT uiFont,
    HFONT titleFont);
