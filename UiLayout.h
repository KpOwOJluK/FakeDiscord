#pragma once

#include "Win32Config.h"
#include <windows.h>

namespace ui
{
    struct PromptGeometry
    {
        RECT panel{};
        RECT label{};
        RECT edit{};
        RECT ok{};
        RECT back{};
    };

    struct TerminalGridSize
    {
        short columns = 100;
        short rows = 30;
    };

    /// Масштабирует логический размер с учётом DPI конкретного окна.
    int Scale(HWND window, int value);

    /// Возвращает высоту верхней панели приложения в физических пикселях.
    int HeaderHeight(HWND window);

    /// Возвращает базовый внешний отступ интерфейса с учётом DPI.
    int Margin(HWND window);

    /// Рассчитывает положение элементов формы ввода.
    PromptGeometry CalculatePromptGeometry(
        HWND window,
        int clientWidth,
        int clientHeight);

    /// Возвращает область, занимаемую терминалом под верхней панелью.
    RECT CalculateTerminalRect(HWND window);

    /// Рассчитывает число колонок и строк ConPTY по размеру клиентской области.
    TerminalGridSize CalculateTerminalGrid(
        HWND window,
        int cellWidth,
        int cellHeight);
}
