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
        const std::wstring& nickname,
        bool english);

    /// Рисует фон экранов меню и, при необходимости, карточку формы ввода.
    void DrawPageBackground(
        HDC dc,
        HWND window,
        const RECT& client,
        const UiResources& resources,
        app::ViewMode view,
        app::PromptAction promptAction,
        const std::wstring& promptLabel,
        bool english);

    /// Выполняет owner-draw отрисовку кнопки в едином стиле FakeDiscord.
    bool DrawButton(
        const DRAWITEMSTRUCT& item,
        HWND window,
        HFONT font);

    /// Задаёт размер пунктов фирменного выпадающего меню настроек.
    bool MeasureChoiceMenuItem(
        MEASUREITEMSTRUCT& item,
        HWND window);

    /// Рисует пункт выпадающего меню в цветах интерфейса FakeDiscord.
    bool DrawChoiceMenuItem(
        const DRAWITEMSTRUCT& item,
        HWND window,
        HFONT font);
}
