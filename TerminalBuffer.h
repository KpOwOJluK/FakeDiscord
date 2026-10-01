#pragma once

#include "Win32Config.h"
#include <windows.h>

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

/// Одна визуальная ячейка терминальной сетки.
struct TerminalCell
{
    uint32_t codepoint = L' ';
    COLORREF fg = RGB(220, 220, 220);
    COLORREF bg = RGB(12, 12, 12);
    bool bold = false;
    bool continuation = false;
};

/// Неизменяемый снимок терминала для безопасной отрисовки из UI-потока.
struct TerminalSnapshot
{
    int rows = 0;
    int cols = 0;
    int cursorRow = 0;
    int cursorCol = 0;
    bool cursorVisible = true;
    std::vector<TerminalCell> cells;
};

    /// VT/ANSI-эмулятор, принимающий UTF-8 поток Tincan и поддерживающий
    /// экранный буфер, курсор, цвета, scroll region и alternate screen.
class TerminalBuffer
{
public:
    /// Создаёт терминал указанного размера.
    explicit TerminalBuffer(int rows = 30, int cols = 100);

    /// Изменяет размер сетки с сохранением доступного содержимого.
    void Resize(int rows, int cols);

    /// Полностью возвращает терминал в начальное состояние.
    void Reset();

    /// Передаёт очередной блок байтов UTF-8/VT в парсер терминала.
    void Feed(const char* data, size_t size);

    /// Возвращает потокобезопасный снимок текущего экрана.
    TerminalSnapshot Snapshot() const;

    /// Забирает накопленные ответы на запросы терминала, например CSI 6 n.
    std::vector<std::string> TakeResponses();

private:
    /// Состояния конечного автомата VT-парсера.
    enum class ParserState
    {
        Normal,
        Escape,
        Csi,
        Osc,
        OscEscape,
        Dcs,
        DcsEscape,
        Charset
    };

    /// Текущие графические атрибуты SGR.
    struct Attr
    {
        COLORREF fg = RGB(220, 220, 220);
        COLORREF bg = RGB(12, 12, 12);
        bool bold = false;
        bool inverse = false;
    };

    mutable std::mutex mutex_;

    int rows_ = 30;
    int cols_ = 100;
    int row_ = 0;
    int col_ = 0;

    int savedRow_ = 0;
    int savedCol_ = 0;

    int scrollTop_ = 0;
    int scrollBottom_ = 29;

    bool cursorVisible_ = true;
    bool alternateScreen_ = false;
    bool autoWrap_ = true;
    bool originMode_ = false;
    bool wrapPending_ = false;

    Attr attr_{};
    Attr savedAttr_{};

    std::vector<TerminalCell> cells_;
    std::vector<TerminalCell> primaryBackup_;
    int primaryBackupRow_ = 0;
    int primaryBackupCol_ = 0;

    ParserState state_ = ParserState::Normal;
    std::string csi_;
    std::string osc_;

    uint32_t utf8Codepoint_ = 0;
    uint32_t utf8Min_ = 0;
    int utf8Remaining_ = 0;

    std::vector<std::string> responses_;

    /// Создаёт пустую ячейку с текущими цветами.
    TerminalCell BlankCell() const;

    /// Возвращает линейный индекс ячейки.
    size_t Index(int row, int col) const;

    /// Обрабатывает один входной байт и состояние UTF-8/VT парсера.
    void ProcessByte(unsigned char byte);

    /// Обрабатывает уже декодированный Unicode code point.
    void ProcessCodepoint(uint32_t cp);

    /// Обрабатывает последовательность ESC.
    void HandleEscape(uint32_t cp);

    /// Обрабатывает завершённую CSI-последовательность.
    void HandleCsi(char finalChar);

    /// Печатает Unicode-символ с корректным delayed autowrap.
    void PutCodepoint(uint32_t cp);

    /// Выполняет перевод строки внутри активного scroll region.
    void LineFeed();

    /// Выполняет reverse index и при необходимости прокрутку вниз.
    void ReverseIndex();

    /// Прокручивает диапазон строк вверх.
    void ScrollUp(int top, int bottom, int count = 1);

    /// Прокручивает диапазон строк вниз.
    void ScrollDown(int top, int bottom, int count = 1);

    /// Очищает весь экран.
    void ClearAll();

    /// Выполняет CSI J.
    void EraseDisplay(int mode);

    /// Выполняет CSI K.
    void EraseLine(int mode);

    /// Стирает указанное количество ячеек от курсора.
    void EraseChars(int count);

    /// Вставляет пустые ячейки в текущую строку.
    void InsertChars(int count);

    /// Удаляет ячейки из текущей строки.
    void DeleteChars(int count);

    /// Вставляет строки внутри scroll region.
    void InsertLines(int count);

    /// Удаляет строки внутри scroll region.
    void DeleteLines(int count);

    /// Применяет параметры Select Graphic Rendition.
    void ApplySgr(const std::vector<int>& params);

    /// Включает или выключает поддерживаемые DEC private modes.
    void SetPrivateMode(bool enabled, const std::vector<int>& params);

    /// Переходит на alternate screen, сохраняя основной буфер.
    void EnterAlternateScreen();

    /// Возвращается с alternate screen на основной буфер.
    void LeaveAlternateScreen();

    /// Разбирает параметры текущей CSI-последовательности.
    std::vector<int> ParseParams(bool& privateMode) const;

    /// Возвращает один из стандартных 16 ANSI-цветов.
    static COLORREF BasicColor(int index, bool bright);

    /// Преобразует индекс палитры xterm-256 в COLORREF.
    static COLORREF Color256(int index);

    /// Определяет приблизительно двойную ширину символа.
    static bool IsWide(uint32_t cp);

    /// Определяет combining code point, не занимающий отдельную ячейку.
    static bool IsCombining(uint32_t cp);
};
