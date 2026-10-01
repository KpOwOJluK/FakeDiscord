#pragma once

#include "Win32Config.h"
#include <windows.h>

class TerminalBuffer;

/// Хранит состояние выделения текста мышью поверх терминальной сетки.
class TerminalSelection
{
public:
    /// Полностью очищает выделение и состояние drag-операции.
    void Clear();

    /// Завершает только текущую drag-операцию, сохраняя выделенный диапазон.
    void CancelDrag();

    /// Возвращает true, если в терминале есть активное выделение.
    bool Active() const { return active_; }

    /// Возвращает true, пока пользователь удерживает ЛКМ и изменяет выделение.
    bool Selecting() const { return selecting_; }

    /// Начинает новое выделение с ячейки под указателем мыши.
    bool Begin(
        POINT point,
        const RECT& terminalRect,
        int cellWidth,
        int cellHeight,
        const TerminalBuffer& terminal);

    /// Обновляет конечную ячейку выделения; возвращает true при изменении диапазона.
    bool Update(
        POINT point,
        const RECT& terminalRect,
        int cellWidth,
        int cellHeight,
        const TerminalBuffer& terminal);

    /// Завершает выделение и фиксирует конечную ячейку.
    void End(
        POINT point,
        const RECT& terminalRect,
        int cellWidth,
        int cellHeight,
        const TerminalBuffer& terminal);

    /// Проверяет, входит ли конкретная ячейка в текущий диапазон выделения.
    bool IsCellSelected(
        int row,
        int col,
        int cols) const;

    /// Копирует выделенный текст в Clipboard и снимает выделение при успехе.
    bool CopyToClipboard(
        HWND owner,
        const TerminalBuffer& terminal);

private:
    /// Преобразует координату мыши в координату терминальной ячейки.
    static bool CellFromPoint(
        POINT point,
        const RECT& terminalRect,
        int cellWidth,
        int cellHeight,
        const TerminalBuffer& terminal,
        int& row,
        int& col);

    /// Нормализует направление выделения к диапазону start <= end.
    void Normalize(
        int cols,
        int& startRow,
        int& startCol,
        int& endRow,
        int& endCol) const;

    bool selecting_ = false;
    bool active_ = false;

    int anchorRow_ = 0;
    int anchorCol_ = 0;
    int endRow_ = 0;
    int endCol_ = 0;
};
