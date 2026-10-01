#include "UiPainter.h"

#include "UiLayout.h"
#include "UiResources.h"

#include <algorithm>

namespace ui
{
    void DrawHeader(
        HDC dc,
        HWND window,
        const RECT& client,
        const UiResources& resources,
        app::ViewMode view,
        const std::wstring& nickname,
        bool english)
    {
        const int headerHeight = HeaderHeight(window);
        const int margin = Margin(window);

        RECT header{
            0,
            0,
            client.right,
            static_cast<LONG>(headerHeight)
        };

        HBRUSH brush = CreateSolidBrush(RGB(28, 28, 32));
        FillRect(dc, &header, brush);
        DeleteObject(brush);

        const int iconSize = Scale(window, 46);
        const int iconX = margin;
        const int iconY =
            std::max(0, (headerHeight - iconSize) / 2);

        if (resources.BigIcon())
        {
            DrawIconEx(
                dc,
                iconX,
                iconY,
                resources.BigIcon(),
                iconSize,
                iconSize,
                0,
                nullptr,
                DI_NORMAL);
        }

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(245, 245, 245));
        SelectObject(dc, resources.TitleFont());

        const int titleLeft =
            iconX + iconSize + Scale(window, 14);

        RECT titleRect{
            static_cast<LONG>(titleLeft),
            0,
            std::max(
                static_cast<LONG>(titleLeft),
                client.right -
                    static_cast<LONG>(
                        Scale(window, 540))),
            static_cast<LONG>(headerHeight)
        };

        DrawTextW(
            dc,
            L"FakeDiscord",
            -1,
            &titleRect,
            DT_SINGLELINE |
                DT_VCENTER |
                DT_LEFT |
                DT_END_ELLIPSIS |
                DT_NOPREFIX);

        SelectObject(dc, resources.UiFont());
        SetTextColor(dc, RGB(180, 180, 186));

        RECT infoRect{
            std::max(
                static_cast<LONG>(
                    titleLeft + Scale(window, 180)),
                client.right -
                    static_cast<LONG>(
                        Scale(window, 560))),
            0,
            client.right -
                static_cast<LONG>(margin),
            static_cast<LONG>(headerHeight)
        };

        const wchar_t* hint = nullptr;
        std::wstring dynamicHint;

        if (view == app::ViewMode::Terminal)
        {
            hint = english
                ? L"F2 Talk · F3 Mute · F5 Deafen · F6 Audio · Esc — disconnect"
                : L"F2 Talk · F3 Mute · F5 Deafen · F6 Audio · Esc — отключиться";
        }
        else if (view == app::ViewMode::Devices)
        {
            hint = english ? L"Enter / Esc — back" : L"Enter / Esc — назад";
        }
        else
        {
            dynamicHint =
                (english ? L"Nick: " : L"Ник: ") +
                (nickname.empty()
                    ? std::wstring(english ? L"(not set)" : L"(не задан)")
                    : nickname);

            hint = dynamicHint.c_str();
        }

        DrawTextW(
            dc,
            hint,
            -1,
            &infoRect,
            DT_SINGLELINE |
                DT_VCENTER |
                DT_RIGHT |
                DT_END_ELLIPSIS |
                DT_NOPREFIX);
    }

    void DrawPageBackground(
        HDC dc,
        HWND window,
        const RECT& client,
        const UiResources& resources,
        app::ViewMode view,
        app::PromptAction promptAction,
        const std::wstring& promptLabel,
        bool english)
    {
        RECT body = client;
        body.top =
            static_cast<LONG>(
                HeaderHeight(window));

        HBRUSH background =
            CreateSolidBrush(
                RGB(17, 17, 20));

        FillRect(dc, &body, background);
        DeleteObject(background);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(210, 210, 216));
        SelectObject(dc, resources.UiFont());

        const wchar_t* pageText = nullptr;

        if (view == app::ViewMode::Launcher)
        {
            pageText = english
                ? L"Private P2P client • persistent device identity"
                : L"Приватный P2P-клиент • постоянная identity устройства";
        }
        else if (view == app::ViewMode::Settings)
        {
            pageText = english
                ? L"Launcher and private server settings"
                : L"Настройки лаунчера и приватного сервера";
        }
        else if (view == app::ViewMode::Prompt)
        {
            if (promptAction == app::PromptAction::ChangeNick)
                pageText = english ? L"Change nickname" : L"Смена ника";
            else if (promptAction == app::PromptAction::SettingsServerName ||
                     promptAction == app::PromptAction::SettingsChannels ||
                     promptAction == app::PromptAction::SettingsMaxFile)
                pageText = english ? L"Settings" : L"Настройки";
            else
                pageText = english
                    ? L"Connect with a one-time invite"
                    : L"Подключение по одноразовому приглашению";
        }

        if (pageText)
        {
            const LONG margin =
                static_cast<LONG>(
                    Margin(window));

            RECT textRect{
                margin,
                static_cast<LONG>(
                    HeaderHeight(window) +
                    Scale(window, 10)),
                client.right - margin,
                static_cast<LONG>(
                    HeaderHeight(window) +
                    Scale(window, 50))
            };

            DrawTextW(
                dc,
                pageText,
                -1,
                &textRect,
                DT_SINGLELINE |
                    DT_LEFT |
                    DT_VCENTER |
                    DT_END_ELLIPSIS |
                    DT_NOPREFIX);
        }

        if (view != app::ViewMode::Prompt)
            return;

        const int width =
            static_cast<int>(
                client.right - client.left);

        const int height =
            static_cast<int>(
                client.bottom - client.top);

        const PromptGeometry geometry =
            CalculatePromptGeometry(
                window,
                width,
                height);

        HBRUSH panelBrush =
            CreateSolidBrush(
                RGB(24, 24, 29));

        FillRect(
            dc,
            &geometry.panel,
            panelBrush);

        DeleteObject(panelBrush);

        HPEN border =
            CreatePen(
                PS_SOLID,
                1,
                RGB(55, 55, 64));

        HGDIOBJ oldPen =
            SelectObject(dc, border);

        HGDIOBJ oldBrush =
            SelectObject(
                dc,
                GetStockObject(NULL_BRUSH));

        Rectangle(
            dc,
            geometry.panel.left,
            geometry.panel.top,
            geometry.panel.right,
            geometry.panel.bottom);

        SelectObject(dc, oldBrush);
        SelectObject(dc, oldPen);
        DeleteObject(border);

        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(232, 232, 238));
        SelectObject(dc, resources.UiFont());

        RECT labelRect = geometry.label;

        DrawTextW(
            dc,
            promptLabel.c_str(),
            -1,
            &labelRect,
            DT_SINGLELINE |
                DT_LEFT |
                DT_VCENTER |
                DT_END_ELLIPSIS |
                DT_NOPREFIX);
    }

    bool DrawButton(
        const DRAWITEMSTRUCT& item,
        HWND window,
        HFONT font)
    {
        if (item.CtlType != ODT_BUTTON)
            return false;

        wchar_t caption[256]{};

        GetWindowTextW(
            item.hwndItem,
            caption,
            256);

        const bool pressed =
            (item.itemState & ODS_SELECTED) != 0;

        const bool disabled =
            (item.itemState & ODS_DISABLED) != 0;

        COLORREF background =
            pressed
                ? RGB(76, 67, 170)
                : RGB(61, 55, 138);

        if (disabled)
            background = RGB(70, 70, 76);

        HBRUSH backgroundBrush =
            CreateSolidBrush(background);

        FillRect(
            item.hDC,
            &item.rcItem,
            backgroundBrush);

        DeleteObject(backgroundBrush);

        HPEN borderPen =
            CreatePen(
                PS_SOLID,
                1,
                pressed
                    ? RGB(160, 150, 255)
                    : RGB(103, 94, 210));

        HGDIOBJ oldPen =
            SelectObject(
                item.hDC,
                borderPen);

        HGDIOBJ oldBrush =
            SelectObject(
                item.hDC,
                GetStockObject(NULL_BRUSH));

        Rectangle(
            item.hDC,
            item.rcItem.left,
            item.rcItem.top,
            item.rcItem.right,
            item.rcItem.bottom);

        SelectObject(item.hDC, oldBrush);
        SelectObject(item.hDC, oldPen);
        DeleteObject(borderPen);

        SetBkMode(item.hDC, TRANSPARENT);

        SetTextColor(
            item.hDC,
            disabled
                ? RGB(155, 155, 160)
                : RGB(245, 245, 250));

        SelectObject(item.hDC, font);

        RECT textRect = item.rcItem;
        const bool choiceButton =
            item.CtlID == app::SetPttKey ||
            item.CtlID == app::SetMaxFile;

        if (choiceButton)
            textRect.right -= Scale(window, 44);

        DrawTextW(
            item.hDC,
            caption,
            -1,
            &textRect,
            DT_SINGLELINE |
                DT_CENTER |
                DT_VCENTER |
                DT_END_ELLIPSIS |
                DT_NOPREFIX);

        if (choiceButton)
        {
            RECT arrowRect = item.rcItem;
            arrowRect.left = arrowRect.right - Scale(window, 44);
            DrawTextW(
                item.hDC,
                L"▼",
                -1,
                &arrowRect,
                DT_SINGLELINE |
                    DT_CENTER |
                    DT_VCENTER |
                    DT_NOPREFIX);
        }

        if ((item.itemState & ODS_FOCUS) != 0)
        {
            RECT focus = item.rcItem;

            InflateRect(
                &focus,
                -Scale(window, 4),
                -Scale(window, 4));

            DrawFocusRect(
                item.hDC,
                &focus);
        }

        return true;
    }

    bool MeasureChoiceMenuItem(
        MEASUREITEMSTRUCT& item,
        HWND window)
    {
        if (item.CtlType != ODT_MENU || item.itemData == 0)
            return false;

        item.itemWidth = static_cast<UINT>(Scale(window, 500));
        item.itemHeight = static_cast<UINT>(Scale(window, 42));
        return true;
    }

    bool DrawChoiceMenuItem(
        const DRAWITEMSTRUCT& item,
        HWND window,
        HFONT font)
    {
        if (item.CtlType != ODT_MENU || item.itemData == 0)
            return false;

        const auto* text =
            reinterpret_cast<const wchar_t*>(item.itemData);
        const bool selected =
            (item.itemState & ODS_SELECTED) != 0;
        const bool checked =
            (item.itemState & ODS_CHECKED) != 0;

        HBRUSH background = CreateSolidBrush(
            selected ? RGB(61, 55, 138) : RGB(24, 24, 29));
        FillRect(item.hDC, &item.rcItem, background);
        DeleteObject(background);

        HPEN separator = CreatePen(
            PS_SOLID,
            1,
            selected ? RGB(103, 94, 210) : RGB(48, 48, 56));
        HGDIOBJ oldPen = SelectObject(item.hDC, separator);
        MoveToEx(
            item.hDC,
            item.rcItem.left,
            item.rcItem.bottom - 1,
            nullptr);
        LineTo(
            item.hDC,
            item.rcItem.right,
            item.rcItem.bottom - 1);
        SelectObject(item.hDC, oldPen);
        DeleteObject(separator);

        SetBkMode(item.hDC, TRANSPARENT);
        SetTextColor(item.hDC, RGB(245, 245, 250));
        SelectObject(item.hDC, font);

        RECT textRect = item.rcItem;
        textRect.left += Scale(window, 42);
        textRect.right -= Scale(window, 18);

        if (checked)
        {
            RECT checkRect = item.rcItem;
            checkRect.left += Scale(window, 12);
            checkRect.right = checkRect.left + Scale(window, 22);
            DrawTextW(
                item.hDC,
                L"✓",
                -1,
                &checkRect,
                DT_SINGLELINE |
                    DT_CENTER |
                    DT_VCENTER |
                    DT_NOPREFIX);
        }

        DrawTextW(
            item.hDC,
            text,
            -1,
            &textRect,
            DT_SINGLELINE |
                DT_LEFT |
                DT_VCENTER |
                DT_END_ELLIPSIS |
                DT_NOPREFIX);
        return true;
    }
}
