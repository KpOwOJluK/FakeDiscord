#include "UiLayout.h"

#include "AppTypes.h"

#include <algorithm>

namespace ui
{
    int Scale(HWND window, int value)
    {
        UINT dpi = 96;

        if (window)
        {
            const UINT currentDpi = GetDpiForWindow(window);
            if (currentDpi != 0)
                dpi = currentDpi;
        }

        return MulDiv(value, static_cast<int>(dpi), 96);
    }

    int HeaderHeight(HWND window)
    {
        return Scale(window, app::kHeaderHeight);
    }

    int Margin(HWND window)
    {
        return Scale(window, app::kMargin);
    }

    PromptGeometry CalculatePromptGeometry(
        HWND window,
        int clientWidth,
        int clientHeight)
    {
        const int margin = Margin(window);
        const int header = HeaderHeight(window);
        const int availableWidth =
            std::max(1, clientWidth - 2 * margin);
        const int contentWidth =
            std::min(Scale(window, 700), availableWidth);
        const int x =
            std::max(margin, (clientWidth - contentWidth) / 2);

        const int labelHeight = Scale(window, 28);
        const int editHeight = Scale(window, 46);
        const int buttonHeight = Scale(window, 46);
        const int buttonGap = Scale(window, 16);
        const int panelPadding = Scale(window, 26);
        const int labelGap = Scale(window, 12);
        const int buttonTopGap = Scale(window, 22);

        const int contentHeight =
            labelHeight + labelGap +
            editHeight + buttonTopGap +
            buttonHeight;

        const int panelHeight =
            contentHeight + 2 * panelPadding;

        const int minimumTop =
            header + Scale(window, 82);

        const int panelTop =
            std::max(
                minimumTop,
                (clientHeight - panelHeight) / 2);

        PromptGeometry geometry;
        geometry.panel = RECT{
            static_cast<LONG>(x - panelPadding),
            static_cast<LONG>(panelTop),
            static_cast<LONG>(x + contentWidth + panelPadding),
            static_cast<LONG>(panelTop + panelHeight)
        };

        const int contentTop = panelTop + panelPadding;
        geometry.label = RECT{
            static_cast<LONG>(x),
            static_cast<LONG>(contentTop),
            static_cast<LONG>(x + contentWidth),
            static_cast<LONG>(contentTop + labelHeight)
        };

        const int editTop =
            contentTop + labelHeight + labelGap;

        geometry.edit = RECT{
            static_cast<LONG>(x),
            static_cast<LONG>(editTop),
            static_cast<LONG>(x + contentWidth),
            static_cast<LONG>(editTop + editHeight)
        };

        const int buttonsTop =
            editTop + editHeight + buttonTopGap;

        const int leftButtonWidth =
            (contentWidth - buttonGap) / 2;

        geometry.ok = RECT{
            static_cast<LONG>(x),
            static_cast<LONG>(buttonsTop),
            static_cast<LONG>(x + leftButtonWidth),
            static_cast<LONG>(buttonsTop + buttonHeight)
        };

        geometry.back = RECT{
            static_cast<LONG>(x + leftButtonWidth + buttonGap),
            static_cast<LONG>(buttonsTop),
            static_cast<LONG>(x + contentWidth),
            static_cast<LONG>(buttonsTop + buttonHeight)
        };

        return geometry;
    }

    RECT CalculateTerminalRect(HWND window)
    {
        RECT rect{};
        GetClientRect(window, &rect);
        rect.top =
            static_cast<LONG>(
                HeaderHeight(window));
        return rect;
    }

    TerminalGridSize CalculateTerminalGrid(
        HWND window,
        int cellWidth,
        int cellHeight)
    {
        const RECT rect =
            CalculateTerminalRect(window);

        const int width =
            std::max(
                1,
                static_cast<int>(
                    rect.right - rect.left));

        const int height =
            std::max(
                1,
                static_cast<int>(
                    rect.bottom - rect.top));

        TerminalGridSize size;
        size.columns =
            static_cast<short>(
                std::clamp(
                    width / std::max(1, cellWidth),
                    20,
                    300));

        size.rows =
            static_cast<short>(
                std::clamp(
                    height / std::max(1, cellHeight),
                    8,
                    120));

        return size;
    }
}
