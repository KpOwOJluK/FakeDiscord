#include "FakeDiscordApp.h"

#include <shellapi.h>

#include <string>
#include <vector>
#include <windowsx.h>

#include "AppTypes.h"
#include "Clipboard.h"
#include "DisconnectDialog.h"
#include "TerminalInput.h"
#include "TextUtil.h"

namespace
{
    /// Извлекает координаты мыши из LPARAM клиентского сообщения Win32.
    POINT MousePoint(LPARAM value)
    {
        return POINT{GET_X_LPARAM(value), GET_Y_LPARAM(value)};
    }
}

/// Обрабатывает клавиатуру терминала и горячие клавиши приложения.
bool FakeDiscordApp::HandleKeyDown(WPARAM virtualKey)
{
    if (app::IsTerminalView(state_.view) &&
        (GetKeyState(VK_CONTROL) & 0x8000) != 0 &&
        virtualKey == L'C' &&
        state_.selection.Active())
    {
        CopySelectionToClipboard();
        return true;
    }
    if (state_.view == app::ViewMode::Devices &&
        (virtualKey == VK_RETURN ||
         virtualKey == VK_ESCAPE))
    {
        ShowLauncher();
        return true;
    }

    if (state_.view == app::ViewMode::Terminal &&
        virtualKey == VK_ESCAPE)
    {
        if (ShowDisconnectConfirm(
                state_.window,
                state_.instance,
                state_.resources.BigIcon(),
                state_.resources.SmallIcon(),
                state_.resources.UiFont(),
                state_.resources.TitleFont()))
        {
            ShowLauncher();
        }
        else
        {
            SetFocus(state_.window);
        }

        return true;
    }

    if (state_.view != app::ViewMode::Terminal ||
        !state_.session)
    {
        return false;
    }
    const bool ctrl =
        (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    const bool alt =
        (GetKeyState(VK_MENU) & 0x8000) != 0;

    const bool shift =
        (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    if (ctrl && virtualKey == L'V')
    {
        PasteClipboardToTerminal(
            state_.window,
            state_.session.get());
        return true;
    }

    if (shift && virtualKey == VK_INSERT)
    {
        PasteClipboardToTerminal(
            state_.window,
            state_.session.get());
        return true;
    }

    if (ctrl && virtualKey == L'C')
    {
        ShowLauncher();
        return true;
    }

    if (ctrl &&
        virtualKey >= L'A' &&
        virtualKey <= L'Z')
    {
        terminal_input::SendCtrlCode(
            *state_.session,
            static_cast<wchar_t>(virtualKey));
        return true;
    }

    std::string sequence =
        terminal_input::KeySequence(
            static_cast<UINT>(virtualKey));

    if (sequence.empty())
        return false;

    if (alt)
        sequence.insert(sequence.begin(), '\x1b');

    state_.session->Write(sequence);
    return true;
}

/// Передаёт текстовый ввод терминалу либо подтверждает/отменяет форму.
bool FakeDiscordApp::HandleCharacter(WPARAM character)
{
    if (state_.view == app::ViewMode::Prompt &&
        character == VK_ESCAPE)
    {
        CancelPrompt();
        return true;
    }

    if (state_.view == app::ViewMode::Terminal &&
        state_.session)
    {
        terminal_input::SendCharacter(
            *state_.session,
            static_cast<wchar_t>(character));
        return true;
    }

    if (state_.view == app::ViewMode::Prompt &&
        character == VK_RETURN)
    {
        SubmitPrompt();
        return true;
    }

    return false;
}

/// Отправляет каждый перетащенный файл через существующую команду /send.
/// Перед вводом принудительно возвращает Tincan на экран чата клавишей F5.
void FakeDiscordApp::HandleDroppedFiles(WPARAM dropHandle)
{
    HDROP drop =
        reinterpret_cast<HDROP>(dropHandle);

    if (!drop)
        return;

    const UINT count =
        DragQueryFileW(
            drop,
            0xFFFFFFFF,
            nullptr,
            0);

    if (state_.view != app::ViewMode::Terminal ||
        !state_.session)
    {
        DragFinish(drop);
        MessageBeep(MB_ICONWARNING);
        return;
    }

    std::vector<std::wstring> files;
    files.reserve(count);

    for (UINT index = 0; index < count; ++index)
    {
        const UINT length =
            DragQueryFileW(
                drop,
                index,
                nullptr,
                0);

        std::wstring path(
            static_cast<size_t>(length + 1),
            L'\0');

        if (DragQueryFileW(
                drop,
                index,
                path.data(),
                length + 1) == 0)
        {
            continue;
        }

        path.resize(length);

        const DWORD attributes =
            GetFileAttributesW(path.c_str());

        if (attributes == INVALID_FILE_ATTRIBUTES ||
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
        {
            continue;
        }

        files.push_back(std::move(path));
    }

    DragFinish(drop);

    if (files.empty())
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    const std::string chatSequence =
        terminal_input::KeySequence(VK_F5);

    if (!chatSequence.empty())
        state_.session->Write(chatSequence);

    std::string commands;

    for (const std::wstring& path : files)
    {
        std::wstring command =
            L"/send \"" +
            path +
            L"\"\r";

        commands += WideToUtf8(command);
    }

    state_.session->Write(commands);
    SetFocus(state_.window);
}

/// Начинает выделение текста, если ЛКМ нажата внутри терминала.
bool FakeDiscordApp::BeginTerminalSelection(LPARAM mousePosition)
{
    if (!app::IsTerminalView(state_.view))
        return false;

    const RECT terminal = TerminalRect();

    if (!state_.selection.Begin(
            MousePoint(mousePosition),
            terminal,
            state_.resources.CellWidth(),
            state_.resources.CellHeight(),
            state_.terminal))
    {
        return false;
    }

    SetFocus(state_.window);
    SetCapture(state_.window);
    InvalidateRect(state_.window, &terminal, FALSE);
    return true;
}

/// Обновляет конец выделения во время drag-операции.
bool FakeDiscordApp::UpdateTerminalSelection(LPARAM mousePosition)
{
    if (!state_.selection.Selecting() ||
        GetCapture() != state_.window ||
        !app::IsTerminalView(state_.view))
    {
        return false;
    }

    const RECT terminal = TerminalRect();

    if (state_.selection.Update(
            MousePoint(mousePosition),
            terminal,
            state_.resources.CellWidth(),
            state_.resources.CellHeight(),
            state_.terminal))
    {
        InvalidateRect(state_.window, &terminal, FALSE);
    }

    return true;
}

/// Завершает drag-выделение и освобождает mouse capture.
bool FakeDiscordApp::EndTerminalSelection(LPARAM mousePosition)
{
    if (!state_.selection.Selecting())
        return false;

    if (GetCapture() == state_.window)
        ReleaseCapture();

    const RECT terminal = TerminalRect();

    state_.selection.End(
        MousePoint(mousePosition),
        terminal,
        state_.resources.CellWidth(),
        state_.resources.CellHeight(),
        state_.terminal);

    InvalidateRect(state_.window, &terminal, FALSE);
    return true;
}

/// Реализует контекстное поведение ПКМ: копирование выделения или вставку.
bool FakeDiscordApp::HandleTerminalRightClick()
{
    if (!app::IsTerminalView(state_.view))
        return false;

    if (state_.selection.Active())
    {
        CopySelectionToClipboard();
        SetFocus(state_.window);
        return true;
    }

    if (state_.view == app::ViewMode::Terminal &&
        state_.session &&
        ClipboardHasUnicodeText())
    {
        PasteClipboardToTerminal(
            state_.window,
            state_.session.get());
    }
    SetFocus(state_.window);
    return true;
}

/// Копирует текущее выделение и снимает визуальную подсветку после успеха.
bool FakeDiscordApp::CopySelectionToClipboard()
{
    const RECT terminal = TerminalRect();

    const bool copied =
        state_.selection.CopyToClipboard(
            state_.window,
            state_.terminal);

    if (copied)
        InvalidateRect(state_.window, &terminal, FALSE);

    return copied;
}
