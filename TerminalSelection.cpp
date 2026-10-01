#include "TerminalSelection.h"
#include "Clipboard.h"
#include "TerminalBuffer.h"
#include "TextUtil.h"

#include <algorithm>
#include <cstdint>
#include <string>

void TerminalSelection::Clear()
{
    selecting_ = false;
    active_ = false;
    anchorRow_ = anchorCol_ = endRow_ = endCol_ = 0;
}

void TerminalSelection::CancelDrag()
{
    selecting_ = false;
}

void TerminalSelection::Normalize(
    int cols,
    int& startRow,
    int& startCol,
    int& endRow,
    int& endCol) const
{
    const long long anchor =
        static_cast<long long>(anchorRow_) * cols + anchorCol_;
    const long long tail =
        static_cast<long long>(endRow_) * cols + endCol_;

    if (anchor <= tail)
    {
        startRow = anchorRow_;
        startCol = anchorCol_;
        endRow = endRow_;
        endCol = endCol_;
    }
    else
    {
        startRow = endRow_;
        startCol = endCol_;
        endRow = anchorRow_;
        endCol = anchorCol_;
    }
}

bool TerminalSelection::CellFromPoint(
    POINT point,
    const RECT& terminalRect,
    int cellWidth,
    int cellHeight,
    const TerminalBuffer& terminal,
    int& row,
    int& col)
{
    const int left = static_cast<int>(terminalRect.left);
    const int top = static_cast<int>(terminalRect.top);
    const int right = static_cast<int>(terminalRect.right);
    const int bottom = static_cast<int>(terminalRect.bottom);

    if (right <= left || bottom <= top)
        return false;

    const int pointX =
        std::clamp(
            static_cast<int>(point.x),
            left,
            right - 1);

    const int pointY =
        std::clamp(
            static_cast<int>(point.y),
            top,
            bottom - 1);

    point.x =
        static_cast<LONG>(
            pointX);

    point.y =
        static_cast<LONG>(
            pointY);

    const TerminalSnapshot snap = terminal.Snapshot();
    if (snap.rows <= 0 || snap.cols <= 0)
        return false;

    col =
        (pointX - left) /
        std::max(1, cellWidth);

    row =
        (pointY - top) /
        std::max(1, cellHeight);

    col = std::clamp(col, 0, snap.cols - 1);
    row = std::clamp(row, 0, snap.rows - 1);

    const auto& cell = snap.cells[
        static_cast<size_t>(row * snap.cols + col)];

    if (cell.continuation && col > 0)
        --col;

    return true;
}

bool TerminalSelection::Begin(
    POINT point,
    const RECT& terminalRect,
    int cellWidth,
    int cellHeight,
    const TerminalBuffer& terminal)
{
    if (!PtInRect(&terminalRect, point))
        return false;

    int row = 0;
    int col = 0;
    if (!CellFromPoint(
            point,
            terminalRect,
            cellWidth,
            cellHeight,
            terminal,
            row,
            col))
    {
        return false;
    }

    selecting_ = true;
    active_ = true;
    anchorRow_ = endRow_ = row;
    anchorCol_ = endCol_ = col;
    return true;
}

bool TerminalSelection::Update(
    POINT point,
    const RECT& terminalRect,
    int cellWidth,
    int cellHeight,
    const TerminalBuffer& terminal)
{
    if (!selecting_)
        return false;

    int row = 0;
    int col = 0;
    if (!CellFromPoint(
            point,
            terminalRect,
            cellWidth,
            cellHeight,
            terminal,
            row,
            col))
    {
        return false;
    }

    if (row == endRow_ && col == endCol_)
        return false;

    endRow_ = row;
    endCol_ = col;
    return true;
}

void TerminalSelection::End(
    POINT point,
    const RECT& terminalRect,
    int cellWidth,
    int cellHeight,
    const TerminalBuffer& terminal)
{
    if (!selecting_)
        return;

    int row = 0;
    int col = 0;
    if (CellFromPoint(
            point,
            terminalRect,
            cellWidth,
            cellHeight,
            terminal,
            row,
            col))
    {
        endRow_ = row;
        endCol_ = col;
    }

    selecting_ = false;
}

bool TerminalSelection::IsCellSelected(
    int row,
    int col,
    int cols) const
{
    if (!active_ || cols <= 0)
        return false;

    int startRow = 0;
    int startCol = 0;
    int endRow = 0;
    int endCol = 0;
    Normalize(cols, startRow, startCol, endRow, endCol);

    const long long current =
        static_cast<long long>(row) * cols + col;
    const long long first =
        static_cast<long long>(startRow) * cols + startCol;
    const long long last =
        static_cast<long long>(endRow) * cols + endCol;

    return current >= first && current <= last;
}

bool TerminalSelection::CopyToClipboard(
    HWND owner,
    const TerminalBuffer& terminal)
{
    if (!active_)
        return false;

    const TerminalSnapshot snap = terminal.Snapshot();
    if (snap.rows <= 0 || snap.cols <= 0)
        return false;

    int startRow = 0;
    int startCol = 0;
    int endRow = 0;
    int endCol = 0;
    Normalize(snap.cols, startRow, startCol, endRow, endCol);

    startRow = std::clamp(startRow, 0, snap.rows - 1);
    endRow = std::clamp(endRow, 0, snap.rows - 1);
    startCol = std::clamp(startCol, 0, snap.cols - 1);
    endCol = std::clamp(endCol, 0, snap.cols - 1);

    std::wstring result;

    for (int row = startRow; row <= endRow; ++row)
    {
        const int firstCol = row == startRow ? startCol : 0;
        const int lastCol = row == endRow ? endCol : snap.cols - 1;
        std::wstring line;

        for (int col = firstCol; col <= lastCol; ++col)
        {
            const auto& cell = snap.cells[
                static_cast<size_t>(row * snap.cols + col)];

            if (cell.continuation)
                continue;

            uint32_t cp = cell.codepoint;
            if (cp == 0)
                continue;
            if (cp < 0x20)
                cp = L' ';

            line += CodepointToWide(cp);
        }

        while (!line.empty() && line.back() == L' ')
            line.pop_back();

        result += line;
        if (row != endRow)
            result += L"\r\n";
    }

    if (result.empty())
        return false;

    const bool ok =
        SetClipboardUnicodeText(
            owner,
            result);

    if (!ok)
    {
        MessageBeep(MB_ICONWARNING);
        return false;
    }

    Clear();
    return true;
}
