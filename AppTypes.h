#pragma once

#include "Win32Config.h"
#include <windows.h>

namespace app
{
    inline constexpr wchar_t kWindowClass[] = L"FakeDiscordConPtyWindow";
    inline constexpr wchar_t kTitle[] = L"FakeDiscord";
    inline constexpr wchar_t kAppVersion[] = L"0.3.2-fd15";

    inline constexpr int kHeaderHeight = 74;
    inline constexpr int kMargin = 18;

    inline constexpr UINT kTerminalExitMessage = WM_APP + 2;
    inline constexpr UINT_PTR kTerminalTimerId = 1;
    inline constexpr UINT kTerminalFrameMs = 33;

    enum class ViewMode
    {
        Launcher,
        Settings,
        Prompt,
        Terminal,
        Devices
    };

    enum class PromptAction
    {
        None,
        JoinCode,
        ChangeNick,
        SettingsServerName,
        SettingsChannels,
        SettingsMaxFile,
        SettingsPttKey
    };

    enum ControlId : int
    {
        Create = 1001,
        Join,
        JoinInvite,
        ChangeNick,
        Devices,
        Settings,
        Update,
        Exit,

        ToggleLanguage,
        ToggleNotifications,
        TogglePtt,
        SetPttKey,
        SetMaxFile,
        SetServerName,
        SetChannels,
        SettingsBack,

        PromptEdit = 1101,
        PromptOk,
        PromptCancel
    };

    /// Возвращает true для экранов, содержимое которых рисуется терминальным рендерером.
    inline bool IsTerminalView(ViewMode view)
    {
        return view == ViewMode::Terminal ||
               view == ViewMode::Devices;
    }
}
