#pragma once

#include "Win32Config.h"
#include <windows.h>

class TerminalBuffer;
class TerminalSelection;

/// Рисует текущий снимок терминала в указанной области.
/// Функция учитывает ANSI-цвета, wide/emoji-глифы, курсор
/// и визуальную подсветку выделенного диапазона.
void DrawTerminalView(
    HDC hdc,
    const RECT& terminalRect,
    const TerminalBuffer& terminal,
    const TerminalSelection& selection,
    HFONT terminalFont,
    HFONT emojiFont,
    int cellWidth,
    int cellHeight);
