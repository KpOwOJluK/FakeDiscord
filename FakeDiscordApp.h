#pragma once

#include "Win32Config.h"
#include <windows.h>

#include <string>
#include <vector>

#include "AppState.h"
#include "UiLayout.h"

/// Главный контроллер приложения FakeDiscord.
/// Класс владеет состоянием окна, маршрутизирует Win32-сообщения
/// и связывает UI, ConPTY и терминальный эмулятор.
class FakeDiscordApp
{
public:
    /// Создаёт контроллер для указанного экземпляра приложения.
    explicit FakeDiscordApp(HINSTANCE instance);

    /// Подготавливает пути, регистрирует класс окна и создаёт главное окно.
    bool Initialize(int showCommand);

    /// Запускает основной цикл обработки сообщений Windows.
    int RunMessageLoop();

private:
    /// Статический оконный обработчик, привязывающий HWND к экземпляру класса.
    static LRESULT CALLBACK WindowProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    /// Обрабатывает сообщение, уже привязанное к конкретному экземпляру приложения.
    LRESULT HandleMessage(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam);

    /// Регистрирует Win32-класс главного окна FakeDiscord.
    bool RegisterWindowClass();

    /// Создаёт главное окно и показывает его пользователю.
    bool CreateMainWindow(int showCommand);

    /// Создаёт кнопку текущего экрана и добавляет её в список контролов.
    HWND CreateButton(const wchar_t* text, int id);

    /// Создаёт обычное текстовое поле ввода.
    HWND CreateEdit();

    /// Создаёт read-only выпадающий список для параметров настроек.
    HWND CreateComboBox(int id);

    /// Удаляет все дочерние контролы текущего экрана.
    void DestroyControls();

    /// Пересчитывает положение контролов по текущему размеру окна.
    void LayoutNow();

    /// Выполняет layout для заданной ширины и высоты клиентской области.
    void LayoutControls(int width, int height);

    /// Перерисовывает текущий экран после изменения состояния.
    void RefreshView();

    /// Возвращает область клиентского окна, отведённую под терминал.
    RECT TerminalRect() const;

    /// Рассчитывает размер сетки ConPTY в колонках и строках.
    ui::TerminalGridSize TerminalGrid() const;

    /// Показывает главное меню и завершает активную Tincan-сессию.
    void ShowLauncher();

    /// Показывает настройки лаунчера и создания приватного сервера.
    void ShowSettings();

    /// Добавляет к запуску Tincan пользовательский лимит файлов и режим уведомлений.
    void AppendSessionSettings(std::vector<std::wstring>& args) const;

    /// Показывает форму ввода для выбранного сценария.
    void ShowPrompt(
        app::PromptAction action,
        const wchar_t* label);

    /// Возвращается из формы ввода на логически предыдущий экран.
    void CancelPrompt();

    /// Читает введённое значение и выполняет действие текущей формы.
    void SubmitPrompt();

    /// Запускает интерактивную Tincan-сессию с переданными аргументами.
    void StartTerminal(const std::vector<std::wstring>& args);

    /// Запускает Tincan в режиме отображения аудиоустройств.
    void StartDevices();

    /// Проверяет сервер обновлений и при согласии устанавливает новую версию.
    void CheckForUpdates();

    /// Завершает и освобождает текущую ConPTY-сессию.
    void StopSession();

    /// Обрабатывает нажатие кнопки или уведомление дочернего контрола.
    void HandleCommand(int id, int notificationCode);

    /// Регистрирует системный Raw Input для фонового PTT даже без фокуса окна.
    bool RegisterGlobalPttInput();

    /// Обрабатывает системное Raw Input событие клавиатуры.
    bool HandleRawInput(LPARAM rawInputHandle);

    /// Возвращает true, если клавиша совпадает с настроенной PTT-клавишей.
    bool IsPttKey(WPARAM virtualKey) const;

    /// Применяет состояние нажатия/отпускания PTT независимо от фокуса окна.
    bool HandlePttKeyState(WPARAM virtualKey, bool pressed);

    /// Обрабатывает нажатие клавиши и начало удержания PTT.
    bool HandleKeyDown(WPARAM virtualKey);

    /// Обрабатывает отпускание настроенной PTT-клавиши.
    bool HandleKeyUp(WPARAM virtualKey);

    /// Принудительно закрывает PTT при потере фокуса или завершении сессии.
    void ReleasePtt();

    /// Обрабатывает текстовый ввод WM_CHAR.
    bool HandleCharacter(WPARAM character);

    /// Преобразует перетащенные в окно файлы в команды /send.
    void HandleDroppedFiles(WPARAM dropHandle);

    /// Обрабатывает начало выделения текста терминала мышью.
    bool BeginTerminalSelection(LPARAM mousePosition);

    /// Обновляет выделение терминала при перемещении мыши.
    bool UpdateTerminalSelection(LPARAM mousePosition);

    /// Завершает выделение терминала.
    bool EndTerminalSelection(LPARAM mousePosition);

    /// Копирует выделение либо вставляет текст по правой кнопке мыши.
    bool HandleTerminalRightClick();

    /// Копирует активное выделение в Clipboard и обновляет подсветку.
    bool CopySelectionToClipboard();

    /// Обрабатывает изменение DPI и пересоздаёт зависящие от DPI ресурсы.
    void HandleDpiChanged(LPARAM lParam);

    /// Обрабатывает изменение размера окна и синхронизирует размер ConPTY.
    void HandleResize();

    /// Отрисовывает главное окно через двойную буферизацию.
    void Paint();

    /// Останавливает фоновые ресурсы перед уничтожением окна.
    void Shutdown();

    AppState state_;
};

