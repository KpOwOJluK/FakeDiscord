#include "TerminalBuffer.h"

#include <algorithm>

// Операции экранной сетки: печать, delayed autowrap, scroll/erase и alternate screen.

void TerminalBuffer::PutCodepoint(uint32_t cp)
{
    if (IsCombining(cp))
    {
        // Рендерер работает по ячейкам, поэтому отдельные combining-знаки
        // игнорируем, чтобы не ломать геометрию курсора.
        return;
    }

    const int width =
        IsWide(cp)
            ? 2
            : 1;

    // В VT/xterm перенос после последней колонки отложенный:
    // переход на новую строку происходит только перед следующим печатным символом.
    // Полноэкранные TUI, включая Tincan, рассчитывают именно на это поведение.
    if (wrapPending_)
    {
        if (autoWrap_)
        {
            col_ = 0;
            LineFeed();
        }
        else
        {
            col_ = cols_ - 1;
        }

        wrapPending_ = false;
    }

    // Двухколоночный символ не может начинаться в последней ячейке.
    // При autowrap переносим его на следующую строку, иначе оставляем курсор у края.
    if (width == 2 &&
        col_ == cols_ - 1)
    {
        if (autoWrap_)
        {
            col_ = 0;
            LineFeed();
        }
        else
        {
            return;
        }
    }

    TerminalCell cell;
    cell.codepoint = cp;
    cell.fg =
        attr_.inverse
            ? attr_.bg
            : attr_.fg;
    cell.bg =
        attr_.inverse
            ? attr_.fg
            : attr_.bg;
    cell.bold = attr_.bold;
    cell.continuation = false;

    cells_[Index(row_, col_)] =
        cell;

    if (width == 2 &&
        col_ + 1 < cols_)
    {
        TerminalCell continuation =
            cell;

        continuation.codepoint = 0;
        continuation.continuation = true;

        cells_[Index(
            row_,
            col_ + 1)] =
                continuation;
    }

    if (width == 1)
    {
        if (col_ == cols_ - 1)
        {
            if (autoWrap_)
                wrapPending_ = true;
        }
        else
        {
            ++col_;
        }

        return;
    }

    // Ниже остаётся только случай двухколоночного символа.
    if (col_ + 1 == cols_ - 1)
    {
        col_ = cols_ - 1;

        if (autoWrap_)
            wrapPending_ = true;
    }
    else
    {
        col_ += 2;
    }
}

void TerminalBuffer::LineFeed()
{
    if (row_ == scrollBottom_)
    {
        ScrollUp(scrollTop_, scrollBottom_, 1);
    }
    else
    {
        row_ = std::min(rows_ - 1, row_ + 1);
    }
}

void TerminalBuffer::ReverseIndex()
{
    if (row_ == scrollTop_)
    {
        ScrollDown(scrollTop_, scrollBottom_, 1);
    }
    else
    {
        row_ = std::max(0, row_ - 1);
    }
}

void TerminalBuffer::ScrollUp(int top, int bottom, int count)
{
    if (top < 0 || bottom >= rows_ || top > bottom)
        return;

    count = std::clamp(count, 1, bottom - top + 1);

    for (int r = top; r <= bottom - count; ++r)
    {
        for (int c = 0; c < cols_; ++c)
            cells_[Index(r, c)] = cells_[Index(r + count, c)];
    }

    for (int r = bottom - count + 1; r <= bottom; ++r)
    {
        for (int c = 0; c < cols_; ++c)
            cells_[Index(r, c)] = BlankCell();
    }
}

void TerminalBuffer::ScrollDown(int top, int bottom, int count)
{
    if (top < 0 || bottom >= rows_ || top > bottom)
        return;

    count = std::clamp(count, 1, bottom - top + 1);

    for (int r = bottom; r >= top + count; --r)
    {
        for (int c = 0; c < cols_; ++c)
            cells_[Index(r, c)] = cells_[Index(r - count, c)];
    }

    for (int r = top; r < top + count; ++r)
    {
        for (int c = 0; c < cols_; ++c)
            cells_[Index(r, c)] = BlankCell();
    }
}

void TerminalBuffer::ClearAll()
{
    std::fill(cells_.begin(), cells_.end(), BlankCell());
}

void TerminalBuffer::EraseDisplay(int mode)
{
    if (mode == 2 || mode == 3)
    {
        ClearAll();
        return;
    }

    if (mode == 0)
    {
        for (int c = col_; c < cols_; ++c)
            cells_[Index(row_, c)] = BlankCell();

        for (int r = row_ + 1; r < rows_; ++r)
            for (int c = 0; c < cols_; ++c)
                cells_[Index(r, c)] = BlankCell();
    }
    else if (mode == 1)
    {
        for (int r = 0; r < row_; ++r)
            for (int c = 0; c < cols_; ++c)
                cells_[Index(r, c)] = BlankCell();

        for (int c = 0; c <= col_; ++c)
            cells_[Index(row_, c)] = BlankCell();
    }
}

void TerminalBuffer::EraseLine(int mode)
{
    if (mode == 2)
    {
        for (int c = 0; c < cols_; ++c)
            cells_[Index(row_, c)] = BlankCell();
    }
    else if (mode == 0)
    {
        for (int c = col_; c < cols_; ++c)
            cells_[Index(row_, c)] = BlankCell();
    }
    else if (mode == 1)
    {
        for (int c = 0; c <= col_; ++c)
            cells_[Index(row_, c)] = BlankCell();
    }
}

void TerminalBuffer::EraseChars(int count)
{
    count = std::max(1, count);

    for (int c = col_; c < std::min(cols_, col_ + count); ++c)
        cells_[Index(row_, c)] = BlankCell();
}

void TerminalBuffer::InsertChars(int count)
{
    count = std::clamp(count, 1, cols_ - col_);

    for (int c = cols_ - 1; c >= col_ + count; --c)
        cells_[Index(row_, c)] = cells_[Index(row_, c - count)];

    for (int c = col_; c < col_ + count; ++c)
        cells_[Index(row_, c)] = BlankCell();
}

void TerminalBuffer::DeleteChars(int count)
{
    count = std::clamp(count, 1, cols_ - col_);

    for (int c = col_; c < cols_ - count; ++c)
        cells_[Index(row_, c)] = cells_[Index(row_, c + count)];

    for (int c = cols_ - count; c < cols_; ++c)
        cells_[Index(row_, c)] = BlankCell();
}

void TerminalBuffer::InsertLines(int count)
{
    if (row_ < scrollTop_ || row_ > scrollBottom_)
        return;

    count = std::clamp(count, 1, scrollBottom_ - row_ + 1);

    for (int r = scrollBottom_; r >= row_ + count; --r)
    {
        for (int c = 0; c < cols_; ++c)
            cells_[Index(r, c)] = cells_[Index(r - count, c)];
    }

    for (int r = row_; r < row_ + count; ++r)
    {
        for (int c = 0; c < cols_; ++c)
            cells_[Index(r, c)] = BlankCell();
    }
}

void TerminalBuffer::DeleteLines(int count)
{
    if (row_ < scrollTop_ || row_ > scrollBottom_)
        return;

    count = std::clamp(count, 1, scrollBottom_ - row_ + 1);

    for (int r = row_; r <= scrollBottom_ - count; ++r)
    {
        for (int c = 0; c < cols_; ++c)
            cells_[Index(r, c)] = cells_[Index(r + count, c)];
    }

    for (int r = scrollBottom_ - count + 1; r <= scrollBottom_; ++r)
    {
        for (int c = 0; c < cols_; ++c)
            cells_[Index(r, c)] = BlankCell();
    }
}


void TerminalBuffer::EnterAlternateScreen()
{
    if (alternateScreen_)
        return;

    primaryBackup_ = cells_;
    primaryBackupRow_ = row_;
    primaryBackupCol_ = col_;

    alternateScreen_ = true;
    wrapPending_ = false;
    row_ = 0;
    col_ = 0;
    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;

    ClearAll();
}

void TerminalBuffer::LeaveAlternateScreen()
{
    if (!alternateScreen_)
        return;

    alternateScreen_ = false;
    wrapPending_ = false;

    if (primaryBackup_.size() == cells_.size())
        cells_ = primaryBackup_;
    else
        ClearAll();

    primaryBackup_.clear();

    row_ = std::clamp(primaryBackupRow_, 0, rows_ - 1);
    col_ = std::clamp(primaryBackupCol_, 0, cols_ - 1);

    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;
}

