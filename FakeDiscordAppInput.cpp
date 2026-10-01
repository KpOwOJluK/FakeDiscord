#include "FakeDiscordApp.h"

#include <shellapi.h>

#include <string>
#include <vector>
#include <windowsx.h>

#include "AppTypes.h"
#include "Clipboard.h"
#include "DisconnectDialog.h"
#include "PttKey.h"
#include "TerminalInput.h"
#include "TextUtil.h"

namespace
{
    enum class DropCommandKind
    {
        NewSend,
        ContinueSend,
        ContinueSendTo,
        BusyFileCommand
    };

    struct DropCommandContext
    {
        DropCommandKind kind = DropCommandKind::NewSend;
        std::wstring recipientToken;
        bool needsSeparator = false;
    };

    /// Извлекает координаты мыши из LPARAM клиентского сообщения Win32.
    POINT MousePoint(LPARAM value)
    {
        return POINT{GET_X_LPARAM(value), GET_Y_LPARAM(value)};
    }

    std::wstring CurrentTerminalLine(const TerminalBuffer& terminal)
    {
        const TerminalSnapshot snapshot = terminal.Snapshot();
        if (snapshot.rows <= 0 || snapshot.cols <= 0 ||
            snapshot.cursorRow < 0 || snapshot.cursorRow >= snapshot.rows)
        {
            return {};
        }

        const int limit = (std::min)(snapshot.cursorCol, snapshot.cols);
        std::wstring line;
        line.reserve(static_cast<size_t>(limit));

        for (int column = 0; column < limit; ++column)
        {
            const TerminalCell& cell =
                snapshot.cells[static_cast<size_t>(snapshot.cursorRow * snapshot.cols + column)];

            if (cell.continuation || cell.codepoint == 0)
                continue;

            if (cell.codepoint <= 0xFFFF)
                line.push_back(static_cast<wchar_t>(cell.codepoint));
            else
                line.push_back(L' ');
        }

        return line;
    }

    DropCommandContext DetectDropCommand(const TerminalBuffer& terminal)
    {
        const std::wstring line = CurrentTerminalLine(terminal);

        if (line.size() >= 7 &&
            line.compare(line.size() - 7, 7, L"/sendto") == 0)
        {
            return {DropCommandKind::BusyFileCommand, {}, false};
        }

        const size_t sendToPos = line.rfind(L"/sendto ");
        if (sendToPos != std::wstring::npos)
        {
            const std::wstring rest = line.substr(sendToPos + 8);
            if (rest.empty())
                return {DropCommandKind::BusyFileCommand, {}, false};

            std::wstring recipientToken;
            size_t pathStart = std::wstring::npos;

            if (rest.front() == L'"')
            {
                bool escaped = false;
                size_t closingQuote = std::wstring::npos;

                for (size_t index = 1; index < rest.size(); ++index)
                {
                    const wchar_t ch = rest[index];
                    if (escaped)
                    {
                        escaped = false;
                        continue;
                    }
                    if (ch == L'\\')
                    {
                        escaped = true;
                        continue;
                    }
                    if (ch == L'"')
                    {
                        closingQuote = index;
                        break;
                    }
                }

                if (closingQuote == std::wstring::npos)
                    return {DropCommandKind::BusyFileCommand, {}, false};

                recipientToken = rest.substr(0, closingQuote + 1);
                pathStart = closingQuote + 1;
            }
            else
            {
                const size_t separator = rest.find_first_of(L" \t");
                if (separator == std::wstring::npos)
                {
                    return {
                        DropCommandKind::ContinueSendTo,
                        rest,
                        true};
                }

                recipientToken = rest.substr(0, separator);
                pathStart = separator;
            }

            const std::wstring pathPart = rest.substr(pathStart);
            if (pathPart.find_first_not_of(L" \t") == std::wstring::npos)
            {
                const bool needsSeparator =
                    !rest.empty() &&
                    !iswspace(rest.back());

                return {
                    DropCommandKind::ContinueSendTo,
                    recipientToken,
                    needsSeparator};
            }

            return {DropCommandKind::BusyFileCommand, {}, false};
        }

        if (line.size() >= 5 &&
            line.compare(line.size() - 5, 5, L"/send") == 0)
        {
            return {DropCommandKind::ContinueSend, {}, true};
        }

        const size_t sendPos = line.rfind(L"/send ");
        if (sendPos != std::wstring::npos)
        {
            const std::wstring rest = line.substr(sendPos + 6);
            if (rest.find_first_not_of(L" \t") == std::wstring::npos)
                return {DropCommandKind::ContinueSend, {}, false};

            return {DropCommandKind::BusyFileCommand, {}, false};
        }

        return {DropCommandKind::NewSend, {}, false};
    }
}

/// Регистрирует raw keyboard input для получения PTT даже у свёрнутого окна.
bool FakeDiscordApp::RegisterGlobalPttInput()
{
    RAWINPUTDEVICE device{};
    device.usUsagePage = 0x01; // Generic Desktop Controls.
    device.usUsage = 0x06;     // Keyboard.
    device.dwFlags = RIDEV_INPUTSINK;
    device.hwndTarget = state_.window;

    return RegisterRawInputDevices(&device, 1, sizeof(device)) == TRUE;
}

/// Применяет состояние PTT независимо от того, находится ли FakeDiscord в фокусе.
bool FakeDiscordApp::HandlePttKeyState(WPARAM virtualKey, bool pressed)
{
    if (state_.view != app::ViewMode::Terminal ||
        !state_.session ||
        !IsPttKey(virtualKey))
    {
        return false;
    }

    if (pressed)
    {
        if (!state_.pttHeld)
        {
            state_.pttHeld = true;
            state_.session->Write("\x1b[23;8~"); // Ctrl+Alt+Shift+F11 = внутренний PTT-press.
        }
    }
    else
    {
        ReleasePtt();
    }

    return true;
}

/// Принимает системный raw keyboard input, включая события при свёрнутом окне.
bool FakeDiscordApp::HandleRawInput(LPARAM rawInputHandle)
{
    UINT size = 0;
    if (GetRawInputData(
            reinterpret_cast<HRAWINPUT>(rawInputHandle),
            RID_INPUT,
            nullptr,
            &size,
            sizeof(RAWINPUTHEADER)) == static_cast<UINT>(-1) ||
        size == 0)
    {
        return false;
    }

    std::vector<BYTE> buffer(size);
    if (GetRawInputData(
            reinterpret_cast<HRAWINPUT>(rawInputHandle),
            RID_INPUT,
            buffer.data(),
            &size,
            sizeof(RAWINPUTHEADER)) != size)
    {
        return false;
    }

    const RAWINPUT* input = reinterpret_cast<const RAWINPUT*>(buffer.data());
    if (input->header.dwType != RIM_TYPEKEYBOARD)
        return false;

    const RAWKEYBOARD& keyboard = input->data.keyboard;
    if (keyboard.VKey == 0 || keyboard.VKey == 255)
        return false;

    const bool pressed = (keyboard.Flags & RI_KEY_BREAK) == 0;
    return HandlePttKeyState(static_cast<WPARAM>(keyboard.VKey), pressed);
}

/// Проверяет совпадение Win32 virtual-key с сохранённой PTT-клавишей.
bool FakeDiscordApp::IsPttKey(WPARAM virtualKey) const
{
    if (!state_.settings.pttEnabled)
        return false;

    const UINT configured = ptt_key::VirtualKey(state_.settings.pttKey);
    return configured != 0 && configured == static_cast<UINT>(virtualKey);
}

/// Закрывает PTT и посылает core отдельное событие отпускания.
void FakeDiscordApp::ReleasePtt()
{
    if (!state_.pttHeld)
        return;

    state_.pttHeld = false;
    if (state_.session)
        state_.session->Write("\x1b[24;8~"); // Ctrl+Alt+Shift+F12 = внутренний PTT-release.
}

/// Обрабатывает отпускание настроенной PTT-клавиши.
bool FakeDiscordApp::HandleKeyUp(WPARAM virtualKey)
{
    return HandlePttKeyState(virtualKey, false);
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

    if (HandlePttKeyState(virtualKey, true))
        return true;

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
/// Команда вводится напрямую: F5 в Tincan переключает Deafen и не должен эмулироваться.
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

    const DropCommandContext context =
        DetectDropCommand(state_.terminal);

    DragFinish(drop);

    if (files.empty() ||
        context.kind == DropCommandKind::BusyFileCommand)
    {
        MessageBeep(MB_ICONWARNING);
        return;
    }

    std::string commands;

    for (size_t index = 0; index < files.size(); ++index)
    {
        const std::wstring& path = files[index];
        const bool first = index == 0;
        std::wstring command;

        if (first &&
            context.kind == DropCommandKind::ContinueSend)
        {
            if (context.needsSeparator)
                command += L" ";

            command += L"\"" + path + L"\"\r";
        }
        else if (first &&
                 context.kind == DropCommandKind::ContinueSendTo)
        {
            if (context.needsSeparator)
                command += L" ";

            command += L"\"" + path + L"\"\r";
        }
        else if (context.kind == DropCommandKind::ContinueSendTo)
        {
            command =
                L"/sendto " +
                context.recipientToken +
                L" \"" +
                path +
                L"\"\r";
        }
        else
        {
            command =
                L"/send \"" +
                path +
                L"\"\r";
        }

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
