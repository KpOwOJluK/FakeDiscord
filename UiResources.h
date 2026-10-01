#pragma once

#include "Win32Config.h"
#include <windows.h>

class UiResources
{
public:
    UiResources() = default;
    ~UiResources();

    UiResources(const UiResources&) = delete;
    UiResources& operator=(const UiResources&) = delete;

    /// Загружает иконки, кисти и шрифты главного окна.
    bool Initialize(HWND window, HINSTANCE instance);

    /// Пересоздаёт шрифты и метрики терминала после изменения DPI.
    void RecreateFonts(HWND window);

    /// Устанавливает большую и маленькую иконки окну приложения.
    void ApplyWindowIcons(HWND window) const;

    HICON BigIcon() const { return bigIcon_; }
    HICON SmallIcon() const { return smallIcon_; }

    HFONT UiFont() const { return uiFont_; }
    HFONT TitleFont() const { return titleFont_; }
    HFONT TerminalFont() const { return terminalFont_; }
    HFONT EmojiFont() const { return emojiFont_; }

    HBRUSH EditBrush() const { return editBrush_; }

    int CellWidth() const { return cellWidth_; }
    int CellHeight() const { return cellHeight_; }

private:
    /// Освобождает только GDI-шрифты, не затрагивая остальные ресурсы.
    void DestroyFonts();

    HICON bigIcon_ = nullptr;
    HICON smallIcon_ = nullptr;

    HFONT uiFont_ = nullptr;
    HFONT titleFont_ = nullptr;
    HFONT terminalFont_ = nullptr;
    HFONT emojiFont_ = nullptr;

    HBRUSH editBrush_ = nullptr;

    int cellWidth_ = 9;
    int cellHeight_ = 18;
};
