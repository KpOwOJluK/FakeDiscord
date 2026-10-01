#include "TerminalBuffer.h"

#include <algorithm>

// Базовая часть эмулятора: жизненный цикл буфера, UTF-8 parser и ESC-последовательности.

TerminalBuffer::TerminalBuffer(int rows, int cols)
{
    rows_ = std::max(1, rows);
    cols_ = std::max(1, cols);
    scrollBottom_ = rows_ - 1;
    cells_.assign(static_cast<size_t>(rows_ * cols_), BlankCell());
}

TerminalCell TerminalBuffer::BlankCell() const
{
    TerminalCell cell;
    cell.codepoint = L' ';
    cell.fg = attr_.inverse ? attr_.bg : attr_.fg;
    cell.bg = attr_.inverse ? attr_.fg : attr_.bg;
    cell.bold = attr_.bold;
    cell.continuation = false;
    return cell;
}

size_t TerminalBuffer::Index(int row, int col) const
{
    return static_cast<size_t>(row * cols_ + col);
}

void TerminalBuffer::Resize(int rows, int cols)
{
    std::lock_guard lock(mutex_);

    rows = std::max(1, rows);
    cols = std::max(1, cols);

    if (rows == rows_ && cols == cols_)
        return;

    std::vector<TerminalCell> next(
        static_cast<size_t>(rows * cols),
        BlankCell());

    const int copyRows = std::min(rows_, rows);
    const int copyCols = std::min(cols_, cols);

    for (int r = 0; r < copyRows; ++r)
    {
        for (int c = 0; c < copyCols; ++c)
        {
            next[static_cast<size_t>(r * cols + c)] =
                cells_[Index(r, c)];
        }
    }

    rows_ = rows;
    cols_ = cols;
    cells_.swap(next);

    row_ = std::clamp(row_, 0, rows_ - 1);
    col_ = std::clamp(col_, 0, cols_ - 1);
    wrapPending_ = false;

    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;

    if (!primaryBackup_.empty())
    {
        std::vector<TerminalCell> resizedBackup(
            static_cast<size_t>(rows_ * cols_),
            BlankCell());

        const int oldRows =
            static_cast<int>(primaryBackup_.size()) /
            std::max(1, copyCols);

        const int br = std::min(rows_, oldRows);
        const int bc = std::min(cols_, copyCols);

        // Резервный экран при resize сохраняется по возможности: TUI всё равно перерисуется после resize.
        for (int r = 0; r < br; ++r)
        {
            for (int c = 0; c < bc; ++c)
            {
                const size_t oldIndex =
                    static_cast<size_t>(r * copyCols + c);

                if (oldIndex < primaryBackup_.size())
                {
                    resizedBackup[
                        static_cast<size_t>(r * cols_ + c)] =
                            primaryBackup_[oldIndex];
                }
            }
        }

        primaryBackup_.swap(resizedBackup);
    }
}

void TerminalBuffer::Reset()
{
    std::lock_guard lock(mutex_);

    attr_ = Attr{};
    savedAttr_ = Attr{};

    row_ = 0;
    col_ = 0;
    savedRow_ = 0;
    savedCol_ = 0;

    scrollTop_ = 0;
    scrollBottom_ = rows_ - 1;

    cursorVisible_ = true;
    alternateScreen_ = false;

    autoWrap_ = true;
    originMode_ = false;
    wrapPending_ = false;

    primaryBackup_.clear();

    state_ = ParserState::Normal;
    csi_.clear();
    osc_.clear();

    utf8Codepoint_ = 0;
    utf8Min_ = 0;
    utf8Remaining_ = 0;

    cells_.assign(static_cast<size_t>(rows_ * cols_), BlankCell());
}

void TerminalBuffer::Feed(const char* data, size_t size)
{
    std::lock_guard lock(mutex_);

    for (size_t i = 0; i < size; ++i)
        ProcessByte(static_cast<unsigned char>(data[i]));
}

TerminalSnapshot TerminalBuffer::Snapshot() const
{
    std::lock_guard lock(mutex_);

    TerminalSnapshot snap;
    snap.rows = rows_;
    snap.cols = cols_;
    snap.cursorRow = row_;
    snap.cursorCol = col_;
    snap.cursorVisible = cursorVisible_;
    snap.cells = cells_;
    return snap;
}

std::vector<std::string> TerminalBuffer::TakeResponses()
{
    std::lock_guard lock(mutex_);
    std::vector<std::string> out;
    out.swap(responses_);
    return out;
}

void TerminalBuffer::ProcessByte(unsigned char byte)
{
    if (utf8Remaining_ > 0)
    {
        if ((byte & 0xC0) != 0x80)
        {
            utf8Remaining_ = 0;
            utf8Codepoint_ = 0;
            ProcessCodepoint(0xFFFD);
            ProcessByte(byte);
            return;
        }

        utf8Codepoint_ =
            (utf8Codepoint_ << 6) |
            static_cast<uint32_t>(byte & 0x3F);

        --utf8Remaining_;

        if (utf8Remaining_ == 0)
        {
            const uint32_t cp = utf8Codepoint_;

            if (cp < utf8Min_ ||
                cp > 0x10FFFF ||
                (cp >= 0xD800 && cp <= 0xDFFF))
            {
                ProcessCodepoint(0xFFFD);
            }
            else
            {
                ProcessCodepoint(cp);
            }

            utf8Codepoint_ = 0;
            utf8Min_ = 0;
        }

        return;
    }

    if (byte < 0x80)
    {
        ProcessCodepoint(byte);
        return;
    }

    if ((byte & 0xE0) == 0xC0)
    {
        utf8Codepoint_ = byte & 0x1F;
        utf8Remaining_ = 1;
        utf8Min_ = 0x80;
        return;
    }

    if ((byte & 0xF0) == 0xE0)
    {
        utf8Codepoint_ = byte & 0x0F;
        utf8Remaining_ = 2;
        utf8Min_ = 0x800;
        return;
    }

    if ((byte & 0xF8) == 0xF0)
    {
        utf8Codepoint_ = byte & 0x07;
        utf8Remaining_ = 3;
        utf8Min_ = 0x10000;
        return;
    }

    ProcessCodepoint(0xFFFD);
}

void TerminalBuffer::ProcessCodepoint(uint32_t cp)
{
    switch (state_)
    {
    case ParserState::Normal:
        if (cp == 0x1B)
        {
            state_ = ParserState::Escape;
            return;
        }

        if (cp == L'\r')
        {
            wrapPending_ = false;
            col_ = 0;
            return;
        }

        if (cp == L'\n' || cp == L'\v' || cp == L'\f')
        {
            wrapPending_ = false;
            LineFeed();
            return;
        }

        if (cp == L'\b')
        {
            wrapPending_ = false;
            col_ = std::max(0, col_ - 1);
            return;
        }

        if (cp == L'\t')
        {
            wrapPending_ = false;
            const int next = ((col_ / 8) + 1) * 8;
            col_ = std::min(cols_ - 1, next);
            return;
        }

        if (cp < 0x20 || cp == 0x7F)
            return;

        PutCodepoint(cp);
        return;

    case ParserState::Escape:
        HandleEscape(cp);
        return;

    case ParserState::Csi:
        if (cp >= 0x40 && cp <= 0x7E)
        {
            HandleCsi(static_cast<char>(cp));
            csi_.clear();
            state_ = ParserState::Normal;
            return;
        }

        if (cp <= 0x7F && csi_.size() < 256)
            csi_.push_back(static_cast<char>(cp));
        else
            state_ = ParserState::Normal;
        return;

    case ParserState::Osc:
        if (cp == 0x07)
        {
            osc_.clear();
            state_ = ParserState::Normal;
            return;
        }

        if (cp == 0x1B)
        {
            state_ = ParserState::OscEscape;
            return;
        }

        if (cp <= 0x7F && osc_.size() < 1024)
            osc_.push_back(static_cast<char>(cp));
        return;

    case ParserState::OscEscape:
        if (cp == L'\\')
        {
            osc_.clear();
            state_ = ParserState::Normal;
        }
        else
        {
            state_ = ParserState::Osc;
        }
        return;

    case ParserState::Dcs:
        if (cp == 0x1B)
            state_ = ParserState::DcsEscape;
        return;

    case ParserState::DcsEscape:
        if (cp == L'\\')
            state_ = ParserState::Normal;
        else
            state_ = ParserState::Dcs;
        return;

    case ParserState::Charset:
        state_ = ParserState::Normal;
        return;
    }
}

void TerminalBuffer::HandleEscape(uint32_t cp)
{
    state_ = ParserState::Normal;

    switch (cp)
    {
    case L'[':
        csi_.clear();
        state_ = ParserState::Csi;
        break;

    case L']':
        osc_.clear();
        state_ = ParserState::Osc;
        break;

    case L'P':
    case L'^':
    case L'_':
        state_ = ParserState::Dcs;
        break;

    case L'(':
    case L')':
    case L'*':
    case L'+':
        state_ = ParserState::Charset;
        break;

    case L'7':
        wrapPending_ = false;
        savedRow_ = row_;
        savedCol_ = col_;
        savedAttr_ = attr_;
        break;

    case L'8':
        wrapPending_ = false;
        row_ = std::clamp(savedRow_, 0, rows_ - 1);
        col_ = std::clamp(savedCol_, 0, cols_ - 1);
        attr_ = savedAttr_;
        break;

    case L'D':
        wrapPending_ = false;
        LineFeed();
        break;

    case L'E':
        wrapPending_ = false;
        col_ = 0;
        LineFeed();
        break;

    case L'M':
        wrapPending_ = false;
        ReverseIndex();
        break;

    case L'c':
        attr_ = Attr{};
        row_ = 0;
        col_ = 0;
        scrollTop_ = 0;
        scrollBottom_ = rows_ - 1;
        cursorVisible_ = true;
        autoWrap_ = true;
        originMode_ = false;
        wrapPending_ = false;
        ClearAll();
        break;

    case L'Z':
        responses_.push_back("\x1b[?1;2c");
        break;

    default:
        break;
    }
}

