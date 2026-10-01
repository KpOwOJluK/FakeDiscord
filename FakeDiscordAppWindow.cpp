#include "FakeDiscordApp.h"

#include <algorithm>

#include "AppTypes.h"
#include "TerminalRenderer.h"
#include "UiLayout.h"
#include "UiPainter.h"

/// Применяет рекомендованный Windows размер окна и пересоздаёт DPI-зависимые шрифты.
void FakeDiscordApp::HandleDpiChanged(LPARAM lParam)
{
    const RECT* suggested =
        reinterpret_cast<const RECT*>(lParam);

    if (suggested)
    {
        SetWindowPos(
            state_.window,
            nullptr,
            suggested->left,
            suggested->top,
            suggested->right - suggested->left,
            suggested->bottom - suggested->top,
            SWP_NOZORDER | SWP_NOACTIVATE);
    }
    state_.resources.RecreateFonts(state_.window);

    for (HWND control : state_.controls)
    {
        SendMessageW(
            control,
            WM_SETFONT,
            reinterpret_cast<WPARAM>(
                state_.resources.UiFont()),
            TRUE);
    }

    LayoutNow();
    InvalidateRect(state_.window, nullptr, FALSE);
}

/// Сбрасывает выделение и обновляет layout после изменения размера окна.
void FakeDiscordApp::HandleResize()
{
    if (app::IsTerminalView(state_.view))
    {
        state_.selection.Clear();

        if (GetCapture() == state_.window)
            ReleaseCapture();
    }

    LayoutNow();
    InvalidateRect(state_.window, nullptr, TRUE);
}

/// Рисует весь кадр во вспомогательный bitmap, затем копирует его в окно.
void FakeDiscordApp::Paint()
{
    PAINTSTRUCT paint{};
    HDC targetDc = BeginPaint(state_.window, &paint);

    RECT client{};
    GetClientRect(state_.window, &client);

    const int width =
        std::max(
            1,
            static_cast<int>(
                client.right - client.left));

    const int height =
        std::max(
            1,
            static_cast<int>(
                client.bottom - client.top));

    HDC bufferDc = CreateCompatibleDC(targetDc);
    HBITMAP bitmap =
        CreateCompatibleBitmap(targetDc, width, height);

    if (!bufferDc || !bitmap)
    {
        if (bitmap)
            DeleteObject(bitmap);
        if (bufferDc)
            DeleteDC(bufferDc);

        EndPaint(state_.window, &paint);
        return;
    }

    HGDIOBJ oldBitmap =
        SelectObject(bufferDc, bitmap);

    HBRUSH background =
        CreateSolidBrush(RGB(17, 17, 20));

    FillRect(bufferDc, &client, background);
    DeleteObject(background);
    ui::DrawHeader(
        bufferDc,
        state_.window,
        client,
        state_.resources,
        state_.view,
        state_.nickname,
        state_.settings.english);

    if (app::IsTerminalView(state_.view))
    {
        DrawTerminalView(
            bufferDc,
            TerminalRect(),
            state_.terminal,
            state_.selection,
            state_.resources.TerminalFont(),
            state_.resources.EmojiFont(),
            state_.resources.CellWidth(),
            state_.resources.CellHeight());
    }
    else
    {
        ui::DrawPageBackground(
            bufferDc,
            state_.window,
            client,
            state_.resources,
            state_.view,
            state_.promptAction,
            state_.promptLabel,
            state_.settings.english);
    }

    BitBlt(
        targetDc,
        paint.rcPaint.left,
        paint.rcPaint.top,
        paint.rcPaint.right - paint.rcPaint.left,
        paint.rcPaint.bottom - paint.rcPaint.top,
        bufferDc,
        paint.rcPaint.left,
        paint.rcPaint.top,
        SRCCOPY);

    SelectObject(bufferDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(bufferDc);

    EndPaint(state_.window, &paint);
}

/// Завершает таймер, ConPTY-сессию и дочерние контролы перед выходом.
void FakeDiscordApp::Shutdown()
{
    if (state_.window)
        KillTimer(state_.window, app::kTerminalTimerId);

    StopSession();
    DestroyControls();
}
