#include "DisconnectDialog.h"
#include <windowsx.h>

namespace
{
    constexpr wchar_t kConfirmWindowClass[] =
        L"FakeDiscordConfirmWindow";

    struct DialogResources
    {
        HINSTANCE instance = nullptr;
        HICON iconBig = nullptr;
        HICON iconSmall = nullptr;
        HFONT uiFont = nullptr;
        HFONT titleFont = nullptr;
    };

    struct ConfirmDialogState
    {
        bool done = false;
        bool accepted = false;
        int selected = 1;
        RECT yesRect{};
        RECT noRect{};
        DialogResources resources{};
    };

    /// Масштабирует логический размер относительно DPI модального окна.
    int Scale(HWND hwnd, int value)
    {
        UINT dpi = GetDpiForWindow(hwnd);
        if (dpi == 0)
            dpi = 96;
        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    /// Рисует одну из двух кнопок подтверждения с учётом текущего выбора.
    void DrawButton(
        HWND hwnd,
        HDC hdc,
        const RECT& rect,
        const wchar_t* text,
        bool selected,
        HFONT font)
    {
        const COLORREF fill = selected
            ? RGB(76, 68, 170)
            : RGB(48, 48, 56);
        const COLORREF border = selected
            ? RGB(126, 116, 235)
            : RGB(72, 72, 82);

        HBRUSH brush = CreateSolidBrush(fill);
        HPEN pen = CreatePen(PS_SOLID, 1, border);
        HGDIOBJ oldBrush = SelectObject(hdc, brush);
        HGDIOBJ oldPen = SelectObject(hdc, pen);

        const int radius = Scale(hwnd, 8);
        RoundRect(
            hdc,
            rect.left,
            rect.top,
            rect.right,
            rect.bottom,
            radius,
            radius);

        SelectObject(hdc, oldPen);
        SelectObject(hdc, oldBrush);
        DeleteObject(pen);
        DeleteObject(brush);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(245, 245, 250));
        SelectObject(hdc, font);

        RECT textRect = rect;
        DrawTextW(
            hdc,
            text,
            -1,
            &textRect,
            DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);
    }

    /// Обрабатывает ввод, hit-test и отрисовку кастомного окна подтверждения.
    LRESULT CALLBACK ConfirmWindowProc(
        HWND hwnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        auto* state = reinterpret_cast<ConfirmDialogState*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        if (message == WM_NCCREATE)
        {
            auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            state = static_cast<ConfirmDialogState*>(create->lpCreateParams);
            SetWindowLongPtrW(
                hwnd,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(state));
            return TRUE;
        }

        switch (message)
        {
        case WM_NCHITTEST:
        {
            const LRESULT hit = DefWindowProcW(hwnd, message, wParam, lParam);
            if (hit == HTCLIENT)
            {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hwnd, &pt);
                if (pt.y < Scale(hwnd, 54))
                    return HTCAPTION;
            }
            return hit;
        }

        case WM_KEYDOWN:
            if (!state)
                return 0;

            if (wParam == VK_ESCAPE)
            {
                state->accepted = false;
                state->done = true;
                DestroyWindow(hwnd);
                return 0;
            }

            if (wParam == VK_LEFT ||
                wParam == VK_RIGHT ||
                wParam == VK_TAB)
            {
                state->selected = state->selected == 0 ? 1 : 0;
                InvalidateRect(hwnd, nullptr, FALSE);
                return 0;
            }

            if (wParam == VK_RETURN)
            {
                state->accepted = state->selected == 0;
                state->done = true;
                DestroyWindow(hwnd);
                return 0;
            }
            break;

        case WM_LBUTTONUP:
            if (state)
            {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                if (PtInRect(&state->yesRect, pt))
                {
                    state->accepted = true;
                    state->done = true;
                    DestroyWindow(hwnd);
                    return 0;
                }
                if (PtInRect(&state->noRect, pt))
                {
                    state->accepted = false;
                    state->done = true;
                    DestroyWindow(hwnd);
                    return 0;
                }
            }
            break;

        case WM_MOUSEMOVE:
            if (state)
            {
                POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                int next = state->selected;
                if (PtInRect(&state->yesRect, pt))
                    next = 0;
                else if (PtInRect(&state->noRect, pt))
                    next = 1;

                if (next != state->selected)
                {
                    state->selected = next;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
            break;

        case WM_CLOSE:
            if (state)
            {
                state->accepted = false;
                state->done = true;
            }
            DestroyWindow(hwnd);
            return 0;

        case WM_PAINT:
        {
            PAINTSTRUCT ps{};
            HDC hdc = BeginPaint(hwnd, &ps);
            RECT client{};
            GetClientRect(hwnd, &client);

            HBRUSH bg = CreateSolidBrush(RGB(24, 24, 29));
            FillRect(hdc, &client, bg);
            DeleteObject(bg);

            HPEN border = CreatePen(PS_SOLID, 1, RGB(70, 70, 82));
            HGDIOBJ oldPen = SelectObject(hdc, border);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
            Rectangle(hdc, 0, 0, client.right, client.bottom);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(border);

            const int pad = Scale(hwnd, 24);
            const int iconSize = Scale(hwnd, 38);

            if (state && state->resources.iconBig)
            {
                DrawIconEx(
                    hdc,
                    pad,
                    Scale(hwnd, 18),
                    state->resources.iconBig,
                    iconSize,
                    iconSize,
                    0,
                    nullptr,
                    DI_NORMAL);
            }

            SetBkMode(hdc, TRANSPARENT);
            SetTextColor(hdc, RGB(245, 245, 250));
            if (state)
                SelectObject(hdc, state->resources.titleFont);

            RECT titleRect{
                static_cast<LONG>(pad + iconSize + Scale(hwnd, 12)),
                static_cast<LONG>(Scale(hwnd, 16)),
                client.right - static_cast<LONG>(pad),
                static_cast<LONG>(Scale(hwnd, 60))
            };
            DrawTextW(
                hdc,
                L"FakeDiscord",
                -1,
                &titleRect,
                DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_NOPREFIX);

            if (state)
                SelectObject(hdc, state->resources.uiFont);
            SetTextColor(hdc, RGB(230, 230, 236));

            RECT questionRect{
                static_cast<LONG>(pad),
                static_cast<LONG>(Scale(hwnd, 82)),
                client.right - static_cast<LONG>(pad),
                static_cast<LONG>(Scale(hwnd, 128))
            };
            DrawTextW(
                hdc,
                L"Отключиться?",
                -1,
                &questionRect,
                DT_SINGLELINE | DT_CENTER | DT_VCENTER | DT_NOPREFIX);

            if (state)
            {
                const int gap = Scale(hwnd, 14);
                const int buttonHeight = Scale(hwnd, 42);
                const int buttonWidth = Scale(hwnd, 150);
                const int totalWidth = buttonWidth * 2 + gap;
                const int startX =
                    (static_cast<int>(client.right) - totalWidth) / 2;
                const int buttonY = Scale(hwnd, 146);

                state->yesRect = RECT{
                    static_cast<LONG>(startX),
                    static_cast<LONG>(buttonY),
                    static_cast<LONG>(startX + buttonWidth),
                    static_cast<LONG>(buttonY + buttonHeight)};

                state->noRect = RECT{
                    static_cast<LONG>(startX + buttonWidth + gap),
                    static_cast<LONG>(buttonY),
                    static_cast<LONG>(startX + buttonWidth + gap + buttonWidth),
                    static_cast<LONG>(buttonY + buttonHeight)};

                DrawButton(
                    hwnd,
                    hdc,
                    state->yesRect,
                    L"Да",
                    state->selected == 0,
                    state->resources.uiFont);
                DrawButton(
                    hwnd,
                    hdc,
                    state->noRect,
                    L"Нет",
                    state->selected == 1,
                    state->resources.uiFont);
            }

            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_ERASEBKGND:
            return 1;
        }

        return DefWindowProcW(hwnd, message, wParam, lParam);
    }
}

/// Создаёт модальное окно «Отключиться?» и возвращает выбор пользователя.
bool ShowDisconnectConfirm(
    HWND owner,
    HINSTANCE instance,
    HICON iconBig,
    HICON iconSmall,
    HFONT uiFont,
    HFONT titleFont)
{
    static bool classRegistered = false;

    if (!classRegistered)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = ConfirmWindowProc;
        wc.hInstance = instance;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = iconSmall;
        wc.hIconSm = iconSmall;
        wc.lpszClassName = kConfirmWindowClass;

        if (!RegisterClassExW(&wc) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        classRegistered = true;
    }

    ConfirmDialogState state{};
    state.resources.instance = instance;
    state.resources.iconBig = iconBig;
    state.resources.iconSmall = iconSmall;
    state.resources.uiFont = uiFont;
    state.resources.titleFont = titleFont;

    RECT ownerRect{};
    GetWindowRect(owner, &ownerRect);

    UINT dpi = GetDpiForWindow(owner);
    if (dpi == 0)
        dpi = 96;

    const int width = MulDiv(430, static_cast<int>(dpi), 96);
    const int height = MulDiv(220, static_cast<int>(dpi), 96);
    const int x = ownerRect.left +
        ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top +
        ((ownerRect.bottom - ownerRect.top) - height) / 2;

    HWND dialog = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kConfirmWindowClass,
        L"FakeDiscord",
        WS_POPUP,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        instance,
        &state);

    if (!dialog)
        return false;

    EnableWindow(owner, FALSE);
    ShowWindow(dialog, SW_SHOW);
    UpdateWindow(dialog);
    SetForegroundWindow(dialog);
    SetFocus(dialog);

    MSG msg{};
    while (!state.done && IsWindow(dialog))
    {
        const BOOL result = GetMessageW(&msg, nullptr, 0, 0);
        if (result <= 0)
        {
            state.done = true;
            state.accepted = false;
            break;
        }

        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (IsWindow(dialog))
        DestroyWindow(dialog);

    EnableWindow(owner, TRUE);
    SetForegroundWindow(owner);
    SetFocus(owner);
    return state.accepted;
}
