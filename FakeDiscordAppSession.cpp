#include "FakeDiscordApp.h"

#include <filesystem>
#include <memory>
#include <cwchar>

#include "AppPaths.h"
#include "AppTypes.h"
#include "PttKey.h"
#include "Updater.h"

namespace
{
constexpr int kPttChoiceCount = 52;

std::wstring PttKeyAt(int index)
{
    if (index >= 0 && index < 12)
        return L"F" + std::to_wstring(index + 1);

    index -= 12;
    if (index >= 0 && index < 26)
        return std::wstring(1, static_cast<wchar_t>(L'A' + index));

    index -= 26;
    if (index >= 0 && index < 10)
        return std::wstring(1, static_cast<wchar_t>(L'0' + index));

    switch (index - 10)
    {
    case 0: return L"SPACE";
    case 1: return L"CAPSLOCK";
    case 2: return L"MOUSE4";
    case 3: return L"MOUSE5";
    default: return L"";
    }
}

int PttKeyIndex(const std::wstring& key)
{
    for (int index = 0; index < kPttChoiceCount; ++index)
    {
        if (PttKeyAt(index) == key)
            return index;
    }
    return 3; // F4
}

std::wstring PttChoiceText(bool english, const std::wstring& key)
{
    return (english ? L"PTT key: " : L"Клавиша PTT: ") + key;
}

std::wstring FileLimitChoiceText(bool english, int gib)
{
    return (english ? L"File limit: " : L"Лимит файла: ") +
        std::to_wstring(gib) + (english ? L" GiB" : L" ГиБ");
}

int ShowChoiceMenu(
    HWND owner,
    HWND anchor,
    const std::vector<std::wstring>& labels,
    int selectedIndex)
{
    if (!owner || !anchor || labels.empty())
        return -1;

    HMENU menu = CreatePopupMenu();
    if (!menu)
        return -1;

    for (size_t index = 0; index < labels.size(); ++index)
    {
        MENUITEMINFOW item{};
        item.cbSize = sizeof(item);
        item.fMask = MIIM_ID | MIIM_FTYPE | MIIM_DATA | MIIM_STATE;
        item.fType = MFT_OWNERDRAW;
        item.fState =
            static_cast<int>(index) == selectedIndex
                ? MFS_CHECKED
                : MFS_ENABLED;
        item.wID = static_cast<UINT>(index + 1);
        item.dwItemData = reinterpret_cast<ULONG_PTR>(labels[index].c_str());
        InsertMenuItemW(menu, static_cast<UINT>(index), TRUE, &item);
    }

    RECT anchorRect{};
    GetWindowRect(anchor, &anchorRect);

    const UINT command = TrackPopupMenuEx(
        menu,
        TPM_LEFTALIGN |
            TPM_TOPALIGN |
            TPM_RETURNCMD |
            TPM_RIGHTBUTTON,
        anchorRect.left,
        anchorRect.bottom + 2,
        owner,
        nullptr);

    DestroyMenu(menu);
    return command > 0 ? static_cast<int>(command - 1) : -1;
}
}

/// Принудительно завершает активный Tincan и освобождает объект ConPTY-сессии.
void FakeDiscordApp::StopSession()
{
    ReleasePtt();
    if (!state_.session)
        return;
    state_.session->Stop(true);
    state_.session.reset();
}

/// Показывает главное меню приложения.
void FakeDiscordApp::ShowLauncher()
{
    StopSession();

    state_.view = app::ViewMode::Launcher;
    state_.promptAction = app::PromptAction::None;
    state_.selection.Clear();

    DestroyControls();

    const bool en = state_.settings.english;
    CreateButton(en ? L"Start private server" : L"Запустить приватный сервер", app::Create);
    CreateButton(en ? L"Connect" : L"Подключиться", app::Join);
    CreateButton(
        en ? L"Connect with one-time invite" : L"Подключиться по одноразовому приглашению",
        app::JoinInvite);
    CreateButton(en ? L"Change nickname" : L"Сменить ник", app::ChangeNick);
    CreateButton(en ? L"Audio devices" : L"Аудиоустройства", app::Devices);
    CreateButton(en ? L"Settings" : L"Настройки", app::Settings);
    CreateButton(en ? L"Update from GitHub" : L"Обновить с GitHub", app::Update);
    CreateButton(en ? L"Exit" : L"Выход", app::Exit);

    RefreshView();
}

/// Показывает настройки лаунчера и параметры создания сервера.
void FakeDiscordApp::ShowSettings()
{
    StopSession();
    state_.view = app::ViewMode::Settings;
    state_.promptAction = app::PromptAction::None;
    state_.selection.Clear();
    DestroyControls();

    const bool en = state_.settings.english;
    const std::wstring language =
        (en ? L"Language: English" : L"Язык: Русский");
    const std::wstring notifications =
        en
            ? std::wstring(L"Notifications: ") + (state_.settings.notifications ? L"On" : L"Off")
            : std::wstring(L"Уведомления: ") + (state_.settings.notifications ? L"Вкл" : L"Выкл");
    const std::wstring pttMode =
        en
            ? std::wstring(L"Push-to-talk: ") + (state_.settings.pttEnabled ? L"On" : L"Off")
            : std::wstring(L"Push-to-talk: ") + (state_.settings.pttEnabled ? L"Вкл" : L"Выкл");
    const std::wstring serverName =
        (en ? L"Server name: " : L"Имя сервера: ") + state_.settings.serverName;
    const std::wstring channels =
        (en ? L"Channels: " : L"Каналы: ") + state_.settings.channels;

    CreateButton(language.c_str(), app::ToggleLanguage);
    CreateButton(notifications.c_str(), app::ToggleNotifications);
    CreateButton(pttMode.c_str(), app::TogglePtt);

    const std::wstring pttKey =
        PttChoiceText(en, state_.settings.pttKey);
    const std::wstring fileLimit =
        FileLimitChoiceText(en, state_.settings.maxFileGiB);

    CreateButton(pttKey.c_str(), app::SetPttKey);
    CreateButton(fileLimit.c_str(), app::SetMaxFile);
    CreateButton(serverName.c_str(), app::SetServerName);
    CreateButton(channels.c_str(), app::SetChannels);
    CreateButton(en ? L"Back" : L"Назад", app::SettingsBack);
    RefreshView();
}

/// Добавляет настройки, одинаковые для host и join.
void FakeDiscordApp::AppendSessionSettings(std::vector<std::wstring>& args) const
{
    args.push_back(L"--max-file-gib");
    args.push_back(std::to_wstring(state_.settings.maxFileGiB));
    if (!state_.settings.notifications)
        args.push_back(L"--no-notifications");
    if (state_.settings.pttEnabled)
    {
        args.push_back(L"--ptt");
        args.push_back(L"--ptt-key");
        args.push_back(state_.settings.pttKey);
    }
}

/// Открывает унифицированную форму ввода.
void FakeDiscordApp::ShowPrompt(
    app::PromptAction action,
    const wchar_t* label)
{
    state_.view = app::ViewMode::Prompt;
    state_.promptAction = action;
    state_.promptLabel = label ? label : L"";

    DestroyControls();

    state_.promptEdit = CreateEdit();
    const bool en = state_.settings.english;
    CreateButton(en ? L"Continue" : L"Продолжить", app::PromptOk);
    CreateButton(en ? L"Back" : L"Назад", app::PromptCancel);

    RefreshView();

    if (state_.promptEdit)
        SetFocus(state_.promptEdit);
}

/// Возвращает форму ввода на соответствующий предыдущий экран.
void FakeDiscordApp::CancelPrompt()
{
    switch (state_.promptAction)
    {
    case app::PromptAction::SettingsServerName:
    case app::PromptAction::SettingsChannels:
    case app::PromptAction::SettingsMaxFile:
    case app::PromptAction::SettingsPttKey:
        ShowSettings();
        break;
    default:
        ShowLauncher();
        break;
    }
}

/// Обрабатывает значение текущего поля ввода.
void FakeDiscordApp::SubmitPrompt()
{
    if (!state_.promptEdit)
        return;

    const int length =
        GetWindowTextLengthW(state_.promptEdit);

    std::wstring value(
        static_cast<size_t>(length + 1),
        L'\0');

    GetWindowTextW(
        state_.promptEdit,
        value.data(),
        length + 1);

    value.resize(static_cast<size_t>(length));

    if (value.empty())
    {
        MessageBeep(MB_ICONWARNING);
        SetFocus(state_.promptEdit);
        return;
    }

    switch (state_.promptAction)
    {
    case app::PromptAction::JoinCode:
    {
        std::vector<std::wstring> args{
            L"join",
            value,
            L"--name",
            state_.nickname};
        AppendSessionSettings(args);
        StartTerminal(args);
        break;
    }

    case app::PromptAction::ChangeNick:
        state_.nickname = value;
        SaveNick(state_.paths, state_.nickname);
        ShowLauncher();
        break;

    case app::PromptAction::SettingsServerName:
        state_.settings.serverName = value;
        SaveLauncherSettings(state_.paths, state_.settings);
        ShowSettings();
        break;

    case app::PromptAction::SettingsChannels:
        state_.settings.channels = value;
        SaveLauncherSettings(state_.paths, state_.settings);
        ShowSettings();
        break;

    case app::PromptAction::SettingsPttKey:
    {
        const std::wstring normalized = ptt_key::Normalize(value);
        if (normalized.empty())
        {
            MessageBoxW(
                state_.window,
                state_.settings.english
                    ? L"Supported PTT keys: F1-F12, A-Z, 0-9, Space, CapsLock, Mouse4, Mouse5."
                    : L"Поддерживаются PTT-клавиши: F1-F12, A-Z, 0-9, Пробел, CapsLock, Mouse4, Mouse5.",
                app::kTitle,
                MB_OK | MB_ICONWARNING);
            SetFocus(state_.promptEdit);
            return;
        }

        state_.settings.pttKey = normalized;
        SaveLauncherSettings(state_.paths, state_.settings);
        ShowSettings();
        break;
    }

    case app::PromptAction::SettingsMaxFile:
    {
        wchar_t* end = nullptr;
        const long parsed = std::wcstol(value.c_str(), &end, 10);
        if (!end || *end != L'\0' || parsed < 1 || parsed > 16)
        {
            MessageBoxW(
                state_.window,
                state_.settings.english
                    ? L"Enter a whole number from 1 to 16 GiB."
                    : L"Введите целое число от 1 до 16 ГиБ.",
                app::kTitle,
                MB_OK | MB_ICONWARNING);
            SetFocus(state_.promptEdit);
            return;
        }

        state_.settings.maxFileGiB = static_cast<int>(parsed);
        SaveLauncherSettings(state_.paths, state_.settings);
        ShowSettings();
        break;
    }

    default:
        ShowLauncher();
        break;
    }
}

/// Запускает Tincan через ConPTY и переводит приложение в терминальный режим.
void FakeDiscordApp::StartTerminal(
    const std::vector<std::wstring>& args)
{
    EnsureTincanExtracted(state_.paths);
    StopSession();

    state_.view = app::ViewMode::Terminal;
    state_.promptAction = app::PromptAction::None;
    state_.selection.Clear();

    DestroyControls();

    const ui::TerminalGridSize grid = TerminalGrid();

    state_.terminalDirty = true;
    state_.terminal.Reset();
    state_.terminal.Resize(grid.rows, grid.columns);

    state_.session =
        std::make_unique<ConPtySession>(
            state_.terminal,
            state_.terminalDirty,
            app::kTerminalExitMessage);

    if (!state_.session->Start(
            state_.window,
            state_.paths.tincanPath.wstring(),
            args,
            state_.paths.cacheDir.wstring(),
            grid.columns,
            grid.rows))
    {
        state_.session.reset();

        MessageBoxW(
            state_.window,
            state_.settings.english
                ? L"Could not start Tincan through ConPTY."
                : L"Не удалось запустить Tincan через ConPTY.",
            app::kTitle,
            MB_OK | MB_ICONERROR);

        ShowLauncher();
        return;
    }

    RefreshView();
    SetFocus(state_.window);
}

/// Запускает одноразовый экран списка аудиоустройств Tincan.
void FakeDiscordApp::StartDevices()
{
    EnsureTincanExtracted(state_.paths);
    StopSession();

    state_.view = app::ViewMode::Devices;
    state_.promptAction = app::PromptAction::None;
    state_.selection.Clear();

    DestroyControls();

    const ui::TerminalGridSize grid = TerminalGrid();

    state_.terminalDirty = true;
    state_.terminal.Reset();
    state_.terminal.Resize(grid.rows, grid.columns);

    state_.session =
        std::make_unique<ConPtySession>(
            state_.terminal,
            state_.terminalDirty,
            app::kTerminalExitMessage);

    if (!state_.session->Start(
            state_.window,
            state_.paths.tincanPath.wstring(),
            {L"devices"},
            state_.paths.cacheDir.wstring(),
            grid.columns,
            grid.rows))
    {
        state_.session.reset();

        MessageBoxW(
            state_.window,
            state_.settings.english
                ? L"Could not get the audio device list."
                : L"Не удалось получить список аудиоустройств.",
            app::kTitle,
            MB_OK | MB_ICONERROR);

        ShowLauncher();
        return;
    }

    RefreshView();
    SetFocus(state_.window);
}

/// Проверяет manifest, скачивает новый EXE и запускает безопасную самозамену.
void FakeDiscordApp::CheckForUpdates()
{
    const bool en = state_.settings.english;
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const updater::CheckResult check = updater::CheckForUpdate();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    if (!check.success)
    {
        std::wstring message =
            (en ? L"Could not check for updates.\n\n" : L"Не удалось проверить обновления.\n\n") + check.error;
        MessageBoxW(
            state_.window,
            message.c_str(),
            app::kTitle,
            MB_OK | MB_ICONERROR);
        return;
    }

    if (!check.updateAvailable)
    {
        std::wstring message =
            en ? L"The current version is installed: " : L"Установлена актуальная версия: ";
        message += updater::CurrentVersion();

        MessageBoxW(
            state_.window,
            message.c_str(),
            app::kTitle,
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring question = en
        ? L"Version " + check.update.version +
            L" is available.\nCurrent version: " + updater::CurrentVersion() +
            L".\n\nDownload the update and restart FakeDiscord?"
        : L"Доступна версия " + check.update.version +
            L".\nТекущая версия: " + updater::CurrentVersion() +
            L".\n\nСкачать обновление и перезапустить FakeDiscord?";

    if (MessageBoxW(
            state_.window,
            question.c_str(),
            app::kTitle,
            MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON1) != IDYES)
    {
        return;
    }

    const auto currentExe = updater::CurrentExecutablePath();
    if (currentExe.empty())
    {
        MessageBoxW(
            state_.window,
            en ? L"Could not determine the path of the current FakeDiscord.exe." : L"Не удалось определить путь текущего FakeDiscord.exe.",
            app::kTitle,
            MB_OK | MB_ICONERROR);
        return;
    }

    auto downloadedExe = currentExe;
    downloadedExe += L".update.new";

    for (HWND control : state_.controls)
        EnableWindow(control, FALSE);

    SetCursor(LoadCursorW(nullptr, IDC_WAIT));

    std::wstring error;
    const bool downloaded = updater::DownloadAndVerify(
        check.update,
        downloadedExe,
        error);

    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    if (!downloaded)
    {
        for (HWND control : state_.controls)
            EnableWindow(control, TRUE);

        std::wstring message =
            (en ? L"The update was not installed.\n\n" : L"Обновление не установлено.\n\n") + error;
        MessageBoxW(
            state_.window,
            message.c_str(),
            app::kTitle,
            MB_OK | MB_ICONERROR);
        return;
    }

    if (!updater::ScheduleReplacement(downloadedExe, error))
    {
        std::error_code ec;
        std::filesystem::remove(downloadedExe, ec);

        for (HWND control : state_.controls)
            EnableWindow(control, TRUE);

        std::wstring message =
            (en
                ? L"The update was downloaded, but installation could not be started.\n\n"
                : L"Обновление загружено, но не удалось запустить установку.\n\n") +
            error;
        MessageBoxW(
            state_.window,
            message.c_str(),
            app::kTitle,
            MB_OK | MB_ICONERROR);
        return;
    }

    MessageBoxW(
        state_.window,
        en
            ? L"The update was verified with SHA-256.\nFakeDiscord will restart now."
            : L"Обновление проверено по SHA-256.\nFakeDiscord сейчас перезапустится.",
        app::kTitle,
        MB_OK | MB_ICONINFORMATION);

    DestroyWindow(state_.window);
}

/// Выполняет действие, соответствующее идентификатору кнопки.
void FakeDiscordApp::HandleCommand(int id, int notificationCode)
{
    switch (id)
    {
    case app::Create:
    {
        std::vector<std::wstring> args{
            L"host",
            L"--name",
            state_.nickname,
            L"--server-name",
            state_.settings.serverName,
            L"--channels",
            state_.settings.channels};
        AppendSessionSettings(args);
        StartTerminal(args);
        break;
    }

    case app::Join:
    {
        std::vector<std::wstring> args{
            L"join",
            L"--name",
            state_.nickname};
        AppendSessionSettings(args);
        StartTerminal(args);
        break;
    }

    case app::JoinInvite:
        ShowPrompt(
            app::PromptAction::JoinCode,
            state_.settings.english
                ? L"Enter one-time invite"
                : L"Введите одноразовое приглашение");
        break;

    case app::ChangeNick:
        ShowPrompt(
            app::PromptAction::ChangeNick,
            state_.settings.english ? L"Enter new nickname" : L"Введите новый ник");
        break;

    case app::Devices:
        StartDevices();
        break;

    case app::Settings:
        ShowSettings();
        break;

    case app::ToggleLanguage:
        state_.settings.english = !state_.settings.english;
        SaveLauncherSettings(state_.paths, state_.settings);
        ShowSettings();
        break;

    case app::ToggleNotifications:
        state_.settings.notifications = !state_.settings.notifications;
        SaveLauncherSettings(state_.paths, state_.settings);
        ShowSettings();
        break;

    case app::TogglePtt:
        state_.settings.pttEnabled = !state_.settings.pttEnabled;
        SaveLauncherSettings(state_.paths, state_.settings);
        ShowSettings();
        break;

    case app::SetPttKey:
        if (notificationCode == BN_CLICKED)
        {
            std::vector<std::wstring> labels;
            labels.reserve(kPttChoiceCount);
            for (int index = 0; index < kPttChoiceCount; ++index)
                labels.push_back(PttKeyAt(index));

            const int selected = ShowChoiceMenu(
                state_.window,
                GetDlgItem(state_.window, app::SetPttKey),
                labels,
                PttKeyIndex(state_.settings.pttKey));

            const std::wstring key = PttKeyAt(selected);
            if (!key.empty())
            {
                state_.settings.pttKey = key;
                SaveLauncherSettings(state_.paths, state_.settings);
                ShowSettings();
            }
        }
        break;

    case app::SetMaxFile:
        if (notificationCode == BN_CLICKED)
        {
            std::vector<std::wstring> labels;
            labels.reserve(16);
            for (int gib = 1; gib <= 16; ++gib)
            {
                labels.push_back(
                    std::to_wstring(gib) +
                    (state_.settings.english ? L" GiB" : L" ГиБ"));
            }

            const int selected = ShowChoiceMenu(
                state_.window,
                GetDlgItem(state_.window, app::SetMaxFile),
                labels,
                state_.settings.maxFileGiB - 1);

            if (selected >= 0 && selected < 16)
            {
                state_.settings.maxFileGiB = selected + 1;
                SaveLauncherSettings(state_.paths, state_.settings);
                ShowSettings();
            }
        }
        break;

    case app::SetServerName:
        ShowPrompt(
            app::PromptAction::SettingsServerName,
            state_.settings.english ? L"Private server display name" : L"Отображаемое имя приватного сервера");
        SetWindowTextW(state_.promptEdit, state_.settings.serverName.c_str());
        break;

    case app::SetChannels:
        ShowPrompt(
            app::PromptAction::SettingsChannels,
            state_.settings.english
                ? L"Channels separated by commas"
                : L"Каналы через запятую");
        SetWindowTextW(state_.promptEdit, state_.settings.channels.c_str());
        break;

    case app::SettingsBack:
        ShowLauncher();
        break;

    case app::Update:
        CheckForUpdates();
        break;

    case app::Exit:
        DestroyWindow(state_.window);
        break;

    case app::PromptOk:
        SubmitPrompt();
        break;

    case app::PromptCancel:
        CancelPrompt();
        break;

    default:
        break;
    }
}
