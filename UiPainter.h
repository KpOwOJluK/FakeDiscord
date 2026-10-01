#pragma once

#include "Win32Config.h"
#include <windows.h>

#include <string>

#include "AppTypes.h"

class UiResources;

namespace ui
{
    /// Рисует верхнюю панель приложения: иконку, заголовок и контекстную подсказку.
    void DrawHeader(
        HDC dc,
        HWND window,
        const RECT& client,
        const UiResources& resources,
        app::ViewMode view,
        const std::wstring& nickname);

    /// Рисует фон экранов меню и, при необходимости, карточку формы ввода.
    void DrawPageBackground(
        HDC dc,
        HWND window,
        const RECT& client,
        const UiResources& resources,
        app::ViewMode view,
        app::PromptAction promptAction,
        const std::wstring& promptLabel);

    /// Выполняет owner-draw отрисовку кнопки в едином стиле FakeDiscord.
    bool DrawButton(
        const DRAWITEMSTRUCT& item,
        HWND window,
        HFONT font);
}
