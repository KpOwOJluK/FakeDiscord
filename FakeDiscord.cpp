#include "Win32Config.h"
#include <windows.h>

#include <exception>
#include <string>

#include "AppTypes.h"
#include "FakeDiscordApp.h"
#include "TextUtil.h"

/// Точка входа приложения.
/// Включает Per-Monitor DPI awareness, инициализирует контроллер
/// и передаёт ему управление циклом Win32-сообщений.
int WINAPI wWinMain(
    HINSTANCE instance,
    HINSTANCE,
    PWSTR,
    int showCommand)
{
    SetProcessDpiAwarenessContext(
        DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    try
    {
        FakeDiscordApp app(instance);

        if (!app.Initialize(showCommand))
        {
            MessageBoxW(
                nullptr,
                L"Не удалось создать главное окно FakeDiscord.",
                app::kTitle,
                MB_OK | MB_ICONERROR);
            return 1;
        }
        return app.RunMessageLoop();
    }
    catch (const std::exception& error)
    {
        std::wstring message =
            L"Ошибка подготовки FakeDiscord:\n";

        message += Utf8ToWide(error.what());

        MessageBoxW(
            nullptr,
            message.c_str(),
            app::kTitle,
            MB_OK | MB_ICONERROR);

        return 1;
    }
}
