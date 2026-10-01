#include "FakeDiscordApp.h"

#include <filesystem>
#include <memory>

#include "AppPaths.h"
#include "AppTypes.h"
#include "Updater.h"

namespace
{
    constexpr wchar_t kDefaultRoom[] = L"FakeDiscord";
    constexpr wchar_t kDefaultPassphrase[] = L"HelldiversDissidents";
}

/// Принудительно завершает активный Tincan и освобождает объект ConPTY-сессии.
void FakeDiscordApp::StopSession()
{
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

    CreateButton(L"Создать комнату", app::Create);
    CreateButton(
        L"Подключиться к FakeDiscord",
        app::QuickJoin);
    CreateButton(L"Подключиться", app::Join);
    CreateButton(L"Сменить ник", app::ChangeNick);
    CreateButton(L"Аудиоустройства", app::Devices);
    CreateButton(L"Обновить с GitHub", app::Update);
    CreateButton(L"Выход", app::Exit);

    RefreshView();
}

/// Показывает варианты создания комнаты.
void FakeDiscordApp::ShowCreateMenu()
{
    state_.view = app::ViewMode::CreateMenu;
    state_.promptAction = app::PromptAction::None;

    DestroyControls();

    CreateButton(
        L"По имени комнаты + passphrase",
        app::CreateNamed);
    CreateButton(
        L"Создать по invite-коду",
        app::CreateInvite);
    CreateButton(L"Назад", app::SubmenuBack);

    RefreshView();
}

/// Показывает варианты подключения к комнате.
void FakeDiscordApp::ShowJoinMenu()
{
    state_.view = app::ViewMode::JoinMenu;
    state_.promptAction = app::PromptAction::None;

    DestroyControls();

    CreateButton(L"По invite-коду", app::JoinInvite);
    CreateButton(
        L"По имени комнаты + passphrase",
        app::JoinNamed);
    CreateButton(L"Назад", app::SubmenuBack);

    RefreshView();
}

/// Открывает унифицированную форму ввода.
void FakeDiscordApp::ShowPrompt(
    app::PromptAction action,
    const wchar_t* label,
    bool password)
{
    state_.view = app::ViewMode::Prompt;
    state_.promptAction = action;
    state_.promptLabel = label ? label : L"";

    DestroyControls();

    state_.promptEdit = CreateEdit(password);
    CreateButton(L"Продолжить", app::PromptOk);
    CreateButton(L"Назад", app::PromptCancel);

    RefreshView();

    if (state_.promptEdit)
        SetFocus(state_.promptEdit);
}

/// Возвращает форму ввода на соответствующий предыдущий экран.
void FakeDiscordApp::CancelPrompt()
{
    switch (state_.promptAction)
    {
    case app::PromptAction::HostRoom:
    case app::PromptAction::HostPassphrase:
        ShowCreateMenu();
        break;

    case app::PromptAction::JoinCode:
    case app::PromptAction::JoinRoom:
    case app::PromptAction::JoinPassphrase:
        ShowJoinMenu();
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
    case app::PromptAction::HostRoom:
        state_.pendingRoom = value;
        ShowPrompt(
            app::PromptAction::HostPassphrase,
            L"Введите passphrase",
            true);
        break;

    case app::PromptAction::HostPassphrase:
        StartTerminal({
            L"host",
            state_.pendingRoom,
            L"--name",
            state_.nickname,
            L"-p",
            value});
        break;

    case app::PromptAction::JoinCode:
        StartTerminal({
            L"join",
            value,
            L"--name",
            state_.nickname});
        break;

    case app::PromptAction::JoinRoom:
        state_.pendingRoom = value;
        ShowPrompt(
            app::PromptAction::JoinPassphrase,
            L"Введите passphrase",
            true);
        break;

    case app::PromptAction::JoinPassphrase:
        StartTerminal({
            L"join",
            state_.pendingRoom,
            L"--name",
            state_.nickname,
            L"-p",
            value});
        break;

    case app::PromptAction::ChangeNick:
        state_.nickname = value;
        SaveNick(state_.paths, state_.nickname);
        ShowLauncher();
        break;

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
            L"Не удалось запустить Tincan через ConPTY.",
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
            L"Не удалось получить список аудиоустройств.",
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
    SetCursor(LoadCursorW(nullptr, IDC_WAIT));
    const updater::CheckResult check = updater::CheckForUpdate();
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));

    if (!check.success)
    {
        std::wstring message =
            L"Не удалось проверить обновления.\n\n" + check.error;
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
            L"Установлена актуальная версия: ";
        message += updater::CurrentVersion();

        MessageBoxW(
            state_.window,
            message.c_str(),
            app::kTitle,
            MB_OK | MB_ICONINFORMATION);
        return;
    }

    std::wstring question =
        L"Доступна версия " + check.update.version +
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
            L"Не удалось определить путь текущего FakeDiscord.exe.",
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
            L"Обновление не установлено.\n\n" + error;
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
            L"Обновление загружено, но не удалось запустить установку.\n\n" +
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
        L"Обновление проверено по SHA-256.\n"
        L"FakeDiscord сейчас перезапустится.",
        app::kTitle,
        MB_OK | MB_ICONINFORMATION);

    DestroyWindow(state_.window);
}

/// Выполняет действие, соответствующее идентификатору кнопки.
void FakeDiscordApp::HandleCommand(int id)
{
    switch (id)
    {
    case app::Create:
        ShowCreateMenu();
        break;
    case app::QuickJoin:
        StartTerminal({
            L"join",
            kDefaultRoom,
            L"--name",
            state_.nickname,
            L"-p",
            kDefaultPassphrase});
        break;

    case app::Join:
        ShowJoinMenu();
        break;

    case app::ChangeNick:
        ShowPrompt(
            app::PromptAction::ChangeNick,
            L"Введите новый ник",
            false);
        break;

    case app::Devices:
        StartDevices();
        break;

    case app::Update:
        CheckForUpdates();
        break;

    case app::CreateNamed:
        ShowPrompt(
            app::PromptAction::HostRoom,
            L"Введите имя комнаты",
            false);
        break;

    case app::CreateInvite:
        StartTerminal({
            L"host",
            L"--name",
            state_.nickname});
        break;

    case app::JoinInvite:
        ShowPrompt(
            app::PromptAction::JoinCode,
            L"Введите invite-код",
            false);
        break;

    case app::JoinNamed:
        ShowPrompt(
            app::PromptAction::JoinRoom,
            L"Введите имя комнаты",
            false);
        break;

    case app::SubmenuBack:
        ShowLauncher();
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
