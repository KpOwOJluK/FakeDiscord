#pragma once

#include "Win32Config.h"
#include <windows.h>

#include <string>

class ConPtySession;

namespace terminal_input
{
    /// Преобразует виртуальную клавишу Windows в последовательность VT/xterm.
    std::string KeySequence(UINT virtualKey);

    /// Отправляет в терминал управляющий код Ctrl+A..Ctrl+Z.
    void SendCtrlCode(
        ConPtySession& session,
        wchar_t character);

    /// Отправляет символ WM_CHAR в ConPTY с корректной обработкой Enter и Backspace.
    void SendCharacter(
        ConPtySession& session,
        wchar_t character);
}
