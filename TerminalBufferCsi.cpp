#include "TerminalBuffer.h"

#include <algorithm>
#include <cstdlib>
#include <sstream>

// CSI/SGR часть эмулятора: курсор, цвета, DEC modes и ответы терминалу.

namespace
{
    constexpr COLORREF kDefaultFg = RGB(220, 220, 220);
    constexpr COLORREF kDefaultBg = RGB(12, 12, 12);

    /// Возвращает CSI-параметр либо значение по умолчанию.
    int ParamOr(const std::vector<int>& params, size_t index, int fallback)
    {
        if (index >= params.size() || params[index] < 0)
            return fallback;
        return params[index];
    }
}

std::vector<int> TerminalBuffer::ParseParams(bool& privateMode) const
{
    privateMode = false;

    std::string text = csi_;

    if (!text.empty() && (text.front() == '?' || text.front() == '>'))
    {
        privateMode = text.front() == '?';
        text.erase(text.begin());
    }

    std::vector<int> params;

    if (text.empty())
    {
        params.push_back(-1);
        return params;
    }

    size_t start = 0;

    while (start <= text.size())
    {
        size_t end = text.find(';', start);

        std::string part =
            text.substr(
                start,
                end == std::string::npos
                    ? std::string::npos
                    : end - start);

        if (part.empty())
        {
            params.push_back(-1);
        }
        else
        {
            // Подпараметры вида 4:3 пока не поддерживаются — используем только первую часть.
            size_t colon = part.find(':');
            if (colon != std::string::npos)
                part.resize(colon);

            char* parseEnd = nullptr;
            long value = std::strtol(part.c_str(), &parseEnd, 10);

            if (parseEnd == part.c_str())
                params.push_back(-1);
            else
                params.push_back(static_cast<int>(value));
        }

        if (end == std::string::npos)
            break;

        start = end + 1;
    }

    return params;
}

void TerminalBuffer::HandleCsi(char finalChar)
{
    bool privateMode = false;
    const std::vector<int> p = ParseParams(privateMode);

    const int n = std::max(1, ParamOr(p, 0, 1));

    switch (finalChar)
    {
    case 'A':
    {
        wrapPending_ = false;
        const int top = originMode_ ? scrollTop_ : 0;
        row_ = std::max(top, row_ - n);
        break;
    }

    case 'B':
    {
        wrapPending_ = false;
        const int bottom = originMode_ ? scrollBottom_ : rows_ - 1;
        row_ = std::min(bottom, row_ + n);
        break;
    }

    case 'C':
        wrapPending_ = false;
        col_ = std::min(cols_ - 1, col_ + n);
        break;

    case 'D':
        wrapPending_ = false;
        col_ = std::max(0, col_ - n);
        break;

    case 'E':
    {
        wrapPending_ = false;
        const int bottom = originMode_ ? scrollBottom_ : rows_ - 1;
        row_ = std::min(bottom, row_ + n);
        col_ = 0;
        break;
    }

    case 'F':
    {
        wrapPending_ = false;
        const int top = originMode_ ? scrollTop_ : 0;
        row_ = std::max(top, row_ - n);
        col_ = 0;
        break;
    }

    case 'G':
    case '`':
        wrapPending_ = false;
        col_ = std::clamp(ParamOr(p, 0, 1) - 1, 0, cols_ - 1);
        break;

    case 'd':
    {
        wrapPending_ = false;
        const int requested = ParamOr(p, 0, 1) - 1;

        if (originMode_)
        {
            row_ = std::clamp(
                scrollTop_ + requested,
                scrollTop_,
                scrollBottom_);
        }
        else
        {
            row_ = std::clamp(
                requested,
                0,
                rows_ - 1);
        }
        break;
    }

    case 'H':
    case 'f':
    {
        wrapPending_ = false;

        const int requestedRow =
            ParamOr(p, 0, 1) - 1;

        const int requestedCol =
            ParamOr(p, 1, 1) - 1;

        if (originMode_)
        {
            row_ = std::clamp(
                scrollTop_ + requestedRow,
                scrollTop_,
                scrollBottom_);
        }
        else
        {
            row_ = std::clamp(
                requestedRow,
                0,
                rows_ - 1);
        }

        col_ = std::clamp(
            requestedCol,
            0,
            cols_ - 1);
        break;
    }

    case 'J':
        wrapPending_ = false;
        EraseDisplay(ParamOr(p, 0, 0));
        break;

    case 'K':
        wrapPending_ = false;
        EraseLine(ParamOr(p, 0, 0));
        break;

    case 'X':
        wrapPending_ = false;
        EraseChars(n);
        break;

    case '@':
        wrapPending_ = false;
        InsertChars(n);
        break;

    case 'P':
        wrapPending_ = false;
        DeleteChars(n);
        break;

    case 'L':
        wrapPending_ = false;
        InsertLines(n);
        break;

    case 'M':
        wrapPending_ = false;
        DeleteLines(n);
        break;

    case 'S':
        wrapPending_ = false;
        ScrollUp(scrollTop_, scrollBottom_, n);
        break;

    case 'T':
        wrapPending_ = false;
        ScrollDown(scrollTop_, scrollBottom_, n);
        break;

    case 'm':
        ApplySgr(p);
        break;

    case 'r':
    {
        const int top =
            std::clamp(ParamOr(p, 0, 1) - 1, 0, rows_ - 1);

        const int bottom =
            std::clamp(
                ParamOr(p, 1, rows_) - 1,
                0,
                rows_ - 1);

        if (top < bottom)
        {
            scrollTop_ = top;
            scrollBottom_ = bottom;
            wrapPending_ = false;
            row_ = originMode_ ? scrollTop_ : 0;
            col_ = 0;
        }
        break;
    }

    case 's':
        wrapPending_ = false;
        savedRow_ = row_;
        savedCol_ = col_;
        savedAttr_ = attr_;
        break;

    case 'u':
        wrapPending_ = false;
        row_ = std::clamp(savedRow_, 0, rows_ - 1);
        col_ = std::clamp(savedCol_, 0, cols_ - 1);
        attr_ = savedAttr_;
        break;

    case 'h':
        if (privateMode)
            SetPrivateMode(true, p);
        break;

    case 'l':
        if (privateMode)
            SetPrivateMode(false, p);
        break;

    case 'n':
    {
        const int query = ParamOr(p, 0, 0);

        if (query == 5)
        {
            responses_.push_back("\x1b[0n");
        }
        else if (query == 6)
        {
            std::ostringstream ss;
            ss << "\x1b[" << (row_ + 1) << ';' << (col_ + 1) << 'R';
            responses_.push_back(ss.str());
        }
        break;
    }

    case 'c':
        responses_.push_back("\x1b[?1;2c");
        break;

    default:
        break;
    }
}


void TerminalBuffer::ApplySgr(const std::vector<int>& params)
{
    std::vector<int> p = params;

    if (p.empty())
        p.push_back(0);

    for (size_t i = 0; i < p.size(); ++i)
    {
        const int code = p[i] < 0 ? 0 : p[i];

        switch (code)
        {
        case 0:
            attr_ = Attr{};
            break;

        case 1:
            attr_.bold = true;
            break;

        case 2:
            attr_.bold = false;
            break;

        case 7:
            attr_.inverse = true;
            break;

        case 22:
            attr_.bold = false;
            break;

        case 27:
            attr_.inverse = false;
            break;

        case 39:
            attr_.fg = kDefaultFg;
            break;

        case 49:
            attr_.bg = kDefaultBg;
            break;

        default:
            if (code >= 30 && code <= 37)
            {
                attr_.fg = BasicColor(code - 30, false);
            }
            else if (code >= 40 && code <= 47)
            {
                attr_.bg = BasicColor(code - 40, false);
            }
            else if (code >= 90 && code <= 97)
            {
                attr_.fg = BasicColor(code - 90, true);
            }
            else if (code >= 100 && code <= 107)
            {
                attr_.bg = BasicColor(code - 100, true);
            }
            else if (code == 38 || code == 48)
            {
                COLORREF* target =
                    code == 38 ? &attr_.fg : &attr_.bg;

                if (i + 2 < p.size() && p[i + 1] == 5)
                {
                    *target = Color256(p[i + 2]);
                    i += 2;
                }
                else if (i + 4 < p.size() && p[i + 1] == 2)
                {
                    const int r = std::clamp(p[i + 2], 0, 255);
                    const int g = std::clamp(p[i + 3], 0, 255);
                    const int b = std::clamp(p[i + 4], 0, 255);
                    *target = RGB(r, g, b);
                    i += 4;
                }
            }
            break;
        }
    }
}

void TerminalBuffer::SetPrivateMode(
    bool enabled,
    const std::vector<int>& params)
{
    for (int raw : params)
    {
        const int code = raw < 0 ? 0 : raw;

        switch (code)
        {
        case 6:
            originMode_ = enabled;
            wrapPending_ = false;
            row_ = originMode_ ? scrollTop_ : 0;
            col_ = 0;
            break;

        case 7:
            autoWrap_ = enabled;
            if (!enabled)
                wrapPending_ = false;
            break;

        case 25:
            cursorVisible_ = enabled;
            break;

        case 47:
        case 1047:
        case 1049:
            if (enabled)
                EnterAlternateScreen();
            else
                LeaveAlternateScreen();
            break;

        default:
            break;
        }
    }
}

