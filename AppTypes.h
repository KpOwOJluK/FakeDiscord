#pragma once

#include "Win32Config.h"
#include <windows.h>

namespace app
{
    inline constexpr wchar_t kWindowClass[] = L"FakeDiscordConPtyWindow";
    inline constexpr wchar_t kTitle[] = L"FakeDiscord";
    inline constexpr wchar_t kAppVersion[] = L"0.3.2-fd4";

    inline constexpr int kHeaderHeight = 74;
    inline constexpr int kMargin = 18;

    inline constexpr UINT kTerminalExitMessage = WM_APP + 2;
    inline constexpr UINT_PTR kTerminalTimerId = 1;
    inline constexpr UINT kTerminalFrameMs = 33;

    enum class ViewMode
    {
        Launcher,
        CreateMenu,
        JoinMenu,
        Prompt,
        Terminal,
        Devices
    };

    enum class PromptAction
    {
        None,
        HostRoom,
        HostPassphrase,
        JoinCode,
        JoinRoom,
        JoinPassphrase,
        ChangeNick
    };

    enum ControlId : int
    {
        Create = 1001,
        QuickJoin,
        Join,
        ChangeNick,
        Devices,
        Update,
        Exit,

        CreateNamed,
        CreateInvite,
        JoinInvite,
        JoinNamed,
        SubmenuBack,

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
