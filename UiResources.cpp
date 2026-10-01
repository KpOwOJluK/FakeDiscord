#include "UiResources.h"

#include "resource.h"

#include <algorithm>

UiResources::~UiResources()
{
    DestroyFonts();

    if (editBrush_)
        DeleteObject(editBrush_);

    if (bigIcon_)
        DestroyIcon(bigIcon_);

    if (smallIcon_)
        DestroyIcon(smallIcon_);
}

bool UiResources::Initialize(
    HWND window,
    HINSTANCE instance)
{
    bigIcon_ =
        static_cast<HICON>(
            LoadImageW(
                instance,
                MAKEINTRESOURCEW(IDI_APP_ICON),
                IMAGE_ICON,
                48,
                48,
                LR_DEFAULTCOLOR));

    smallIcon_ =
        static_cast<HICON>(
            LoadImageW(
                instance,
                MAKEINTRESOURCEW(IDI_APP_ICON),
                IMAGE_ICON,
                20,
                20,
                LR_DEFAULTCOLOR));

    editBrush_ =
        CreateSolidBrush(
            RGB(45, 45, 52));

    RecreateFonts(window);

    return uiFont_ &&
           titleFont_ &&
           terminalFont_ &&
           emojiFont_ &&
           editBrush_;
}

void UiResources::RecreateFonts(HWND window)
{
    DestroyFonts();

    UINT dpi = GetDpiForWindow(window);
    if (dpi == 0)
        dpi = 96;

    const int uiHeight =
        -MulDiv(16, static_cast<int>(dpi), 96);
    const int titleHeight =
        -MulDiv(22, static_cast<int>(dpi), 96);
    const int terminalHeight =
        -MulDiv(16, static_cast<int>(dpi), 96);

    uiFont_ =
        CreateFontW(
            uiHeight,
            0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");

    titleFont_ =
        CreateFontW(
            titleHeight,
            0, 0, 0,
            FW_SEMIBOLD,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");

    terminalFont_ =
        CreateFontW(
            terminalHeight,
            0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            FIXED_PITCH | FF_MODERN,
            L"Cascadia Mono");

    emojiFont_ =
        CreateFontW(
            terminalHeight,
            0, 0, 0,
            FW_NORMAL,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI Emoji");

    HDC dc = GetDC(window);
    if (!dc || !terminalFont_)
        return;

    HGDIOBJ oldFont =
        SelectObject(dc, terminalFont_);

    TEXTMETRICW metrics{};
    if (GetTextMetricsW(dc, &metrics))
    {
        cellWidth_ =
            std::max(
                7,
                static_cast<int>(
                    metrics.tmAveCharWidth));

        cellHeight_ =
            std::max(
                14,
                static_cast<int>(
                    metrics.tmHeight +
                    metrics.tmExternalLeading));
    }

    SelectObject(dc, oldFont);
    ReleaseDC(window, dc);
}

void UiResources::ApplyWindowIcons(HWND window) const
{
    if (bigIcon_)
    {
        SendMessageW(
            window,
            WM_SETICON,
            ICON_BIG,
            reinterpret_cast<LPARAM>(bigIcon_));
    }

    if (smallIcon_)
    {
        SendMessageW(
            window,
            WM_SETICON,
            ICON_SMALL,
            reinterpret_cast<LPARAM>(smallIcon_));
    }
}

void UiResources::DestroyFonts()
{
    if (uiFont_)
        DeleteObject(uiFont_);

    if (titleFont_)
        DeleteObject(titleFont_);

    if (terminalFont_)
        DeleteObject(terminalFont_);

    if (emojiFont_)
        DeleteObject(emojiFont_);

    uiFont_ = nullptr;
    titleFont_ = nullptr;
    terminalFont_ = nullptr;
    emojiFont_ = nullptr;
}
