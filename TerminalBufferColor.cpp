#include "TerminalBuffer.h"

#include <algorithm>

// Цветовая палитра xterm и упрощённая оценка ширины Unicode-символов.

COLORREF TerminalBuffer::BasicColor(int index, bool bright)
{
    static const COLORREF normal[8] = {
        RGB(0, 0, 0),
        RGB(205, 49, 49),
        RGB(13, 188, 121),
        RGB(229, 229, 16),
        RGB(36, 114, 200),
        RGB(188, 63, 188),
        RGB(17, 168, 205),
        RGB(229, 229, 229)
    };

    static const COLORREF high[8] = {
        RGB(102, 102, 102),
        RGB(241, 76, 76),
        RGB(35, 209, 139),
        RGB(245, 245, 67),
        RGB(59, 142, 234),
        RGB(214, 112, 214),
        RGB(41, 184, 219),
        RGB(255, 255, 255)
    };

    index = std::clamp(index, 0, 7);
    return bright ? high[index] : normal[index];
}

COLORREF TerminalBuffer::Color256(int index)
{
    index = std::clamp(index, 0, 255);

    if (index < 8)
        return BasicColor(index, false);

    if (index < 16)
        return BasicColor(index - 8, true);

    if (index >= 232)
    {
        const int gray = 8 + (index - 232) * 10;
        return RGB(gray, gray, gray);
    }

    const int n = index - 16;
    const int r = n / 36;
    const int g = (n / 6) % 6;
    const int b = n % 6;

    auto component = [](int v)
    {
        return v == 0 ? 0 : 55 + v * 40;
    };

    return RGB(component(r), component(g), component(b));
}

bool TerminalBuffer::IsCombining(uint32_t cp)
{
    return
        (cp >= 0x0300 && cp <= 0x036F) ||
        (cp >= 0x1AB0 && cp <= 0x1AFF) ||
        (cp >= 0x1DC0 && cp <= 0x1DFF) ||
        (cp >= 0x20D0 && cp <= 0x20FF) ||
        (cp >= 0xFE20 && cp <= 0xFE2F);
}

bool TerminalBuffer::IsWide(uint32_t cp)
{
    return
        (cp >= 0x1100 && cp <= 0x115F) ||
        cp == 0x2329 ||
        cp == 0x232A ||
        (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
        (cp >= 0xAC00 && cp <= 0xD7A3) ||
        (cp >= 0xF900 && cp <= 0xFAFF) ||
        (cp >= 0xFE10 && cp <= 0xFE19) ||
        (cp >= 0xFE30 && cp <= 0xFE6F) ||
        (cp >= 0xFF00 && cp <= 0xFF60) ||
        (cp >= 0xFFE0 && cp <= 0xFFE6) ||
        (cp >= 0x1F300 && cp <= 0x1FAFF) ||
        (cp >= 0x20000 && cp <= 0x3FFFD);
}
