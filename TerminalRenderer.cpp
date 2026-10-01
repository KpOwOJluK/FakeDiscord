#include "TerminalRenderer.h"
#include "TerminalBuffer.h"
#include "TerminalSelection.h"
#include "TextUtil.h"

#include <algorithm>
#include <cstdint>
#include <string>

namespace
{
    /// Определяет символы, для которых предпочтительнее шрифт Segoe UI Emoji.
    bool IsEmojiLike(uint32_t cp)
    {
        return cp >= 0x1F000 ||
               (cp >= 0x2600 && cp <= 0x27BF);
    }
}

/// Рисует полный снимок терминальной сетки с учётом выделения и курсора.
void DrawTerminalView(
    HDC hdc,
    const RECT& terminalRect,
    const TerminalBuffer& terminal,
    const TerminalSelection& selection,
    HFONT terminalFont,
    HFONT emojiFont,
    int cellWidth,
    int cellHeight)
{
    HBRUSH bg = CreateSolidBrush(RGB(12, 12, 12));
    FillRect(hdc, &terminalRect, bg);
    DeleteObject(bg);

    const TerminalSnapshot snap = terminal.Snapshot();
    SelectObject(hdc, terminalFont);
    SetBkMode(hdc, OPAQUE);

    const int left = static_cast<int>(terminalRect.left);
    const int top = static_cast<int>(terminalRect.top);
    const int right = static_cast<int>(terminalRect.right);
    const int bottom = static_cast<int>(terminalRect.bottom);

    for (int row = 0; row < snap.rows; ++row)
    {
        const int y = top + row * cellHeight;
        if (y >= bottom)
            break;

        for (int col = 0; col < snap.cols; ++col)
        {
            const int x = left + col * cellWidth;
            if (x >= right)
                break;

            const auto& cell = snap.cells[
                static_cast<size_t>(row * snap.cols + col)];

            RECT cellRect{
                static_cast<LONG>(x),
                static_cast<LONG>(y),
                static_cast<LONG>(std::min(right, x + cellWidth)),
                static_cast<LONG>(std::min(bottom, y + cellHeight))
            };

            const bool selected =
                selection.IsCellSelected(row, col, snap.cols);

            SetTextColor(
                hdc,
                selected ? RGB(255, 255, 255) : cell.fg);
            SetBkColor(
                hdc,
                selected ? RGB(72, 68, 118) : cell.bg);

            if (cell.continuation)
            {
                ExtTextOutW(
                    hdc,
                    x,
                    y,
                    ETO_OPAQUE,
                    &cellRect,
                    L"",
                    0,
                    nullptr);
                continue;
            }

            const std::wstring glyph = CodepointToWide(cell.codepoint);
            HFONT font = IsEmojiLike(cell.codepoint)
                ? emojiFont
                : terminalFont;
            SelectObject(hdc, font);

            ExtTextOutW(
                hdc,
                x,
                y,
                ETO_OPAQUE | ETO_CLIPPED,
                &cellRect,
                glyph.c_str(),
                static_cast<UINT>(glyph.size()),
                nullptr);
        }
    }

    if (snap.cursorVisible &&
        snap.cursorRow >= 0 &&
        snap.cursorRow < snap.rows &&
        snap.cursorCol >= 0 &&
        snap.cursorCol < snap.cols)
    {
        const int x = left + snap.cursorCol * cellWidth;
        const int y = top + snap.cursorRow * cellHeight;

        RECT cursor{
            static_cast<LONG>(x),
            static_cast<LONG>(y + cellHeight - 2),
            static_cast<LONG>(x + cellWidth),
            static_cast<LONG>(y + cellHeight)
        };

        HBRUSH cursorBrush = CreateSolidBrush(RGB(230, 230, 230));
        FillRect(hdc, &cursor, cursorBrush);
        DeleteObject(cursorBrush);
    }
}
