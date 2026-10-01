#include "TerminalInput.h"

#include "ConPtySession.h"
#include "TextUtil.h"

#include <cwctype>

namespace terminal_input
{
    std::string KeySequence(UINT virtualKey)
    {
        switch (virtualKey)
        {
        case VK_F1: return "\x1bOP";
        case VK_F2: return "\x1bOQ";
        case VK_F3: return "\x1bOR";
        case VK_F4: return "\x1bOS";
        case VK_F5: return "\x1b[15~";
        case VK_F6: return "\x1b[17~";
        case VK_F7: return "\x1b[18~";
        case VK_F8: return "\x1b[19~";
        case VK_F9: return "\x1b[20~";
        case VK_F10: return "\x1b[21~";
        case VK_F11: return "\x1b[23~";
        case VK_F12: return "\x1b[24~";

        case VK_UP: return "\x1b[A";
        case VK_DOWN: return "\x1b[B";
        case VK_RIGHT: return "\x1b[C";
        case VK_LEFT: return "\x1b[D";

        case VK_HOME: return "\x1b[H";
        case VK_END: return "\x1b[F";
        case VK_INSERT: return "\x1b[2~";
        case VK_DELETE: return "\x1b[3~";
        case VK_PRIOR: return "\x1b[5~";
        case VK_NEXT: return "\x1b[6~";
        case VK_ESCAPE: return "\x1b";
        default: return {};
        }
    }

    void SendCtrlCode(
        ConPtySession& session,
        wchar_t character)
    {
        const wchar_t upper =
            static_cast<wchar_t>(
                towupper(character));

        if (upper < L'A' || upper > L'Z')
            return;

        const char control =
            static_cast<char>(
                upper - L'A' + 1);

        session.Write(
            std::string(1, control));
    }

    void SendCharacter(
        ConPtySession& session,
        wchar_t character)
    {
        if (character == L'\r')
        {
            session.Write("\r");
            return;
        }

        if (character == L'\b')
        {
            session.Write("\x7f");
            return;
        }

        if (character == L'\t')
        {
            session.Write("\t");
            return;
        }

        if (character < 0x20)
            return;

        session.Write(
            WideToUtf8(
                std::wstring(1, character)));
    }
}
