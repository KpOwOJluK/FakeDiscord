#pragma once

#include "Win32Config.h"
#include <windows.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>

#include "AppPaths.h"
#include "AppTypes.h"
#include "ConPtySession.h"
#include "TerminalBuffer.h"
#include "TerminalSelection.h"
#include "UiResources.h"

/// Содержит всё изменяемое состояние главного окна FakeDiscord.
/// Вместо набора разрозненных глобальных переменных приложение использует
/// один контекст, который явно показывает жизненный цикл ресурсов.
struct AppState
{
    HINSTANCE instance = nullptr;
    HWND window = nullptr;

    UiResources resources;

    app::ViewMode view = app::ViewMode::Launcher;
    app::PromptAction promptAction = app::PromptAction::None;

    std::vector<HWND> controls;
    HWND promptEdit = nullptr;
    std::wstring promptLabel;

    std::wstring nickname;
    LauncherSettings settings;

    AppPaths paths;

    TerminalBuffer terminal{30, 100};
    std::atomic<bool> terminalDirty{false};
    TerminalSelection selection;

    std::unique_ptr<ConPtySession> session;
    bool pttHeld = false;
    bool globalPttInputRegistered = false;
};
