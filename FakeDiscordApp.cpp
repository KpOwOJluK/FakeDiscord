#include "FakeDiscordApp.h"

#include <shellapi.h>

#include "AppPaths.h"
#include "AppTypes.h"
#include "UiPainter.h"
#include "resource.h"

/// Создаёт контроллер и сохраняет HINSTANCE для создания окон и загрузки ресурсов.
FakeDiscordApp::FakeDiscordApp(HINSTANCE instance)
{
    state_.instance = instance;
}

/// Подготавливает постоянные данные приложения и создаёт главное окно.
bool FakeDiscordApp::Initialize(int showCommand)
{
    state_.paths = InitAppPaths();
    EnsureTincanExtracted(state_.paths);
    state_.nickname = LoadNick(state_.paths);
    state_.settings = LoadLauncherSettings(state_.paths);

    return RegisterWindowClass() &&
           CreateMainWindow(showCommand);
}

/// Выполняет стандартный Win32 message loop.
int FakeDiscordApp::RunMessageLoop()
{
    MSG message{};

    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        if (state_.view == app::ViewMode::Prompt &&
            IsDialogMessageW(state_.window, &message))
        {
            continue;
        }

        const bool pttKeyMessage =
            state_.view == app::ViewMode::Terminal &&
            (message.message == WM_KEYDOWN || message.message == WM_KEYUP) &&
            IsPttKey(message.wParam);

        if (!pttKeyMessage)
            TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}

/// Регистрирует класс главного окна и назначает статический WindowProc.
bool FakeDiscordApp::RegisterWindowClass()
{
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &FakeDiscordApp::WindowProc;
    windowClass.hInstance = state_.instance;
    windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    windowClass.hbrBackground = nullptr;
    windowClass.lpszClassName = app::kWindowClass;
    windowClass.hIcon = LoadIconW(
        state_.instance,
        MAKEINTRESOURCEW(IDI_APP_ICON));
    windowClass.hIconSm = windowClass.hIcon;

    return RegisterClassExW(&windowClass) != 0 ||
           GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

/// Создаёт главное окно и передаёт this через lpParam для привязки экземпляра.
bool FakeDiscordApp::CreateMainWindow(int showCommand)
{
    const UINT dpi = GetDpiForSystem();

    HWND window = CreateWindowExW(
        0,
        app::kWindowClass,
        app::kTitle,
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        MulDiv(1000, static_cast<int>(dpi), 96),
        MulDiv(760, static_cast<int>(dpi), 96),
        nullptr,
        nullptr,
        state_.instance,
        this);

    if (!window)
        return false;

    ShowWindow(window, showCommand);
    UpdateWindow(window);
    return true;
}

/// Связывает HWND с объектом приложения и перенаправляет сообщения в HandleMessage.
LRESULT CALLBACK FakeDiscordApp::WindowProc(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    FakeDiscordApp* app = reinterpret_cast<FakeDiscordApp*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));

    if (message == WM_NCCREATE)
    {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        app = static_cast<FakeDiscordApp*>(create->lpCreateParams);

        SetWindowLongPtrW(
            window,
            GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(app));
    }

    if (!app)
        return DefWindowProcW(window, message, wParam, lParam);

    return app->HandleMessage(window, message, wParam, lParam);
}

/// Маршрутизирует сообщения главного окна по небольшим специализированным обработчикам.
LRESULT FakeDiscordApp::HandleMessage(
    HWND window,
    UINT message,
    WPARAM wParam,
    LPARAM lParam)
{
    switch (message)
    {
    case WM_CREATE:
        state_.window = window;

        if (!state_.resources.Initialize(window, state_.instance))
            return -1;

        state_.resources.ApplyWindowIcons(window);
        DragAcceptFiles(window, TRUE);
        state_.globalPttInputRegistered = RegisterGlobalPttInput();

        SetTimer(
            window,
            app::kTerminalTimerId,
            app::kTerminalFrameMs,
            nullptr);

        if (state_.nickname.empty())
        {
            ShowPrompt(
                app::PromptAction::ChangeNick,
                state_.settings.english ? L"Enter nickname" : L"Введите новый ник");
        }
        else
        {
            ShowLauncher();
        }
        return 0;

    case WM_DPICHANGED:
        HandleDpiChanged(lParam);
        return 0;

    case WM_SIZE:
        HandleResize();
        return 0;

    case WM_DRAWITEM:
    {
        const auto* item =
            reinterpret_cast<const DRAWITEMSTRUCT*>(lParam);

        if (item &&
            ui::DrawButton(
                *item,
                window,
                state_.resources.UiFont()))
        {
            return TRUE;
        }
        break;
    }

    case WM_CTLCOLOREDIT:
    {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetBkMode(dc, OPAQUE);
        SetBkColor(dc, RGB(45, 45, 52));
        SetTextColor(dc, RGB(242, 242, 245));
        return reinterpret_cast<LRESULT>(
            state_.resources.EditBrush());
    }

    case WM_TIMER:
        if (wParam == app::kTerminalTimerId &&
            app::IsTerminalView(state_.view) &&
            state_.terminalDirty.exchange(false))
        {
            const RECT terminal = TerminalRect();
            InvalidateRect(window, &terminal, FALSE);
        }
        return 0;

    case WM_COMMAND:
        HandleCommand(LOWORD(wParam), HIWORD(wParam));
        return 0;

    case WM_LBUTTONDOWN:
        if (BeginTerminalSelection(lParam))
            return 0;
        break;

    case WM_MOUSEMOVE:
        if (UpdateTerminalSelection(lParam))
            return 0;
        break;

    case WM_LBUTTONUP:
        if (EndTerminalSelection(lParam))
            return 0;
        break;

    case WM_RBUTTONUP:
        if (HandleTerminalRightClick())
            return 0;
        break;

    case WM_CAPTURECHANGED:
        state_.selection.CancelDrag();
        return 0;

    case WM_DROPFILES:
        HandleDroppedFiles(wParam);
        return 0;

    case WM_INPUT:
        HandleRawInput(lParam);
        return DefWindowProcW(window, message, wParam, lParam);

    case WM_KEYDOWN:
        if (HandleKeyDown(wParam))
            return 0;
        break;

    case WM_KEYUP:
        if (HandleKeyUp(wParam))
            return 0;
        break;

    case WM_KILLFOCUS:
        if (!state_.globalPttInputRegistered)
            ReleasePtt();
        break;

    case WM_CHAR:
        if (HandleCharacter(wParam))
            return 0;
        break;

    case app::kTerminalExitMessage:
        if (state_.view == app::ViewMode::Terminal)
        {
            ShowLauncher();
        }
        else if (state_.view == app::ViewMode::Devices)
        {
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_PAINT:
        Paint();
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_CLOSE:
        DestroyWindow(window);
        return 0;

    case WM_DESTROY:
        Shutdown();
        PostQuitMessage(0);
        return 0;
    }

    return DefWindowProcW(window, message, wParam, lParam);
}

/// Создаёт owner-draw кнопку с шрифтом приложения.
