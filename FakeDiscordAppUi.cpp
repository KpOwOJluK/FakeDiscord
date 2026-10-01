#include "FakeDiscordApp.h"

#include <algorithm>

#include "AppTypes.h"
#include "UiLayout.h"

/// Создаёт owner-draw кнопку текущего экрана и подключает общий UI-шрифт.
HWND FakeDiscordApp::CreateButton(const wchar_t* text, int id)
{
    HWND control = CreateWindowExW(
        0,
        L"BUTTON",
        text,
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0,
        0,
        100,
        34,
        state_.window,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(id)),
        state_.instance,
        nullptr);

    if (!control)
        return nullptr;

    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(
            state_.resources.UiFont()),
        TRUE);

    state_.controls.push_back(control);
    return control;
}

/// Создаёт обычное поле ввода и настраивает его отступы.
HWND FakeDiscordApp::CreateEdit()
{
    const DWORD style =
        WS_CHILD |
        WS_VISIBLE |
        WS_TABSTOP |
        WS_BORDER |
        ES_AUTOHSCROLL;

    HWND control = CreateWindowExW(
        0,
        L"EDIT",
        L"",
        style,
        0,
        0,
        100,
        30,
        state_.window,
        reinterpret_cast<HMENU>(
            static_cast<INT_PTR>(app::PromptEdit)),
        state_.instance,
        nullptr);

    if (!control)
        return nullptr;

    SendMessageW(
        control,
        WM_SETFONT,
        reinterpret_cast<WPARAM>(
            state_.resources.UiFont()),
        TRUE);

    SendMessageW(
        control,
        EM_SETMARGINS,
        EC_LEFTMARGIN | EC_RIGHTMARGIN,
        MAKELPARAM(
            ui::Scale(state_.window, 12),
            ui::Scale(state_.window, 12)));

    state_.controls.push_back(control);
    return control;
}

/// Удаляет все дочерние HWND, созданные для текущего экрана.
void FakeDiscordApp::DestroyControls()
{
    if (state_.window)
    {
        HWND child = GetWindow(state_.window, GW_CHILD);

        while (child)
        {
            HWND next = GetWindow(child, GW_HWNDNEXT);
            DestroyWindow(child);
            child = next;
        }
    }

    state_.controls.clear();
    state_.promptEdit = nullptr;
}

/// Пересчитывает расположение элементов по реальной клиентской области.
void FakeDiscordApp::LayoutNow()
{
    if (!state_.window)
        return;

    RECT client{};
    GetClientRect(state_.window, &client);

    LayoutControls(
        static_cast<int>(client.right - client.left),
        static_cast<int>(client.bottom - client.top));
}

/// Обновляет положение контролов или размер терминальной сетки.
void FakeDiscordApp::LayoutControls(int width, int height)
{
    const int margin = ui::Margin(state_.window);
    const int headerHeight = ui::HeaderHeight(state_.window);

    if (state_.view == app::ViewMode::Launcher ||
        state_.view == app::ViewMode::Settings)
    {
        const int availableWidth =
            std::max(1, width - 2 * margin);

        const int buttonWidth =
            std::min(
                ui::Scale(state_.window, 500),
                availableWidth);

        const int x =
            std::max(
                margin,
                (width - buttonWidth) / 2);

        const int startOffset =
            state_.view == app::ViewMode::Settings ? 58 : 70;

        const int gap = ui::Scale(state_.window, 12);
        const int buttonHeight =
            ui::Scale(state_.window, 50);

        int y =
            headerHeight +
            ui::Scale(state_.window, startOffset);

        for (HWND control : state_.controls)
        {
            MoveWindow(
                control,
                x,
                y,
                buttonWidth,
                buttonHeight,
                TRUE);

            y += buttonHeight + gap;
        }

        return;
    }

    if (state_.view == app::ViewMode::Prompt)
    {
        if (state_.controls.size() < 3)
            return;

        const ui::PromptGeometry geometry =
            ui::CalculatePromptGeometry(
                state_.window,
                width,
                height);

        const auto moveTo = [](HWND control, const RECT& rect)
        {
            MoveWindow(
                control,
                rect.left,
                rect.top,
                rect.right - rect.left,
                rect.bottom - rect.top,
                TRUE);
        };

        moveTo(state_.controls[0], geometry.edit);
        moveTo(state_.controls[1], geometry.ok);
        moveTo(state_.controls[2], geometry.back);
        return;
    }

    if (!app::IsTerminalView(state_.view))
        return;

    const ui::TerminalGridSize grid = TerminalGrid();
    const TerminalSnapshot snapshot = state_.terminal.Snapshot();

    if (snapshot.rows == grid.rows &&
        snapshot.cols == grid.columns)
    {
        return;
    }

    state_.terminal.Reset();
    state_.terminal.Resize(grid.rows, grid.columns);
    state_.terminalDirty = true;

    if (state_.session)
        state_.session->Resize(grid.columns, grid.rows);
}

/// Пересчитывает layout и синхронно перерисовывает окно.
void FakeDiscordApp::RefreshView()
{
    LayoutNow();

    RedrawWindow(
        state_.window,
        nullptr,
        nullptr,
        RDW_INVALIDATE |
            RDW_ERASE |
            RDW_FRAME |
            RDW_ALLCHILDREN |
            RDW_ERASENOW |
            RDW_UPDATENOW);

    UpdateWindow(state_.window);
}

/// Возвращает прямоугольник терминала под верхней панелью.
RECT FakeDiscordApp::TerminalRect() const
{
    return ui::CalculateTerminalRect(state_.window);
}

/// Рассчитывает размер терминальной сетки по метрикам текущего шрифта.
ui::TerminalGridSize FakeDiscordApp::TerminalGrid() const
{
    return ui::CalculateTerminalGrid(
        state_.window,
        state_.resources.CellWidth(),
        state_.resources.CellHeight());
}

/// Останавливает текущую Tincan-сессию и освобождает объект ConPTY.
