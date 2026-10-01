#pragma once

#include "Win32Config.h"
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <string>

namespace ptt_key
{
    /// Приводит пользовательское имя PTT-клавиши к каноническому виду.
    inline std::wstring Normalize(std::wstring value)
    {
        const auto notSpace = [](wchar_t ch) { return !iswspace(ch); };
        const auto first = std::find_if(value.begin(), value.end(), notSpace);
        const auto last = std::find_if(value.rbegin(), value.rend(), notSpace).base();
        if (first >= last)
            return {};

        value = std::wstring(first, last);
        std::transform(
            value.begin(),
            value.end(),
            value.begin(),
            [](wchar_t ch) { return static_cast<wchar_t>(towupper(ch)); });

        if (value == L" " || value == L"SPACE" || value == L"ПРОБЕЛ")
            return L"SPACE";
        if (value == L"CAPS" || value == L"CAPSLOCK" || value == L"CAPS LOCK")
            return L"CAPSLOCK";
        if (value == L"MOUSE4" || value == L"MOUSE 4" || value == L"XBUTTON1")
            return L"MOUSE4";
        if (value == L"MOUSE5" || value == L"MOUSE 5" || value == L"XBUTTON2")
            return L"MOUSE5";

        if (value.size() == 1)
        {
            const wchar_t ch = value.front();
            if ((ch >= L'A' && ch <= L'Z') ||
                (ch >= L'0' && ch <= L'9'))
            {
                return value;
            }
        }

        if (value.size() >= 2 && value.front() == L'F')
        {
            int number = 0;
            for (size_t i = 1; i < value.size(); ++i)
            {
                if (value[i] < L'0' || value[i] > L'9')
                    return {};
                number = number * 10 + static_cast<int>(value[i] - L'0');
            }
            if (number >= 1 && number <= 12)
                return L"F" + std::to_wstring(number);
        }

        return {};
    }

    /// Возвращает Win32 virtual-key для поддерживаемой PTT-клавиши.
    inline UINT VirtualKey(const std::wstring& value)
    {
        const std::wstring key = Normalize(value);
        if (key.empty())
            return 0;

        if (key == L"SPACE")
            return VK_SPACE;
        if (key == L"CAPSLOCK")
            return VK_CAPITAL;
        if (key == L"MOUSE4")
            return VK_XBUTTON1;
        if (key == L"MOUSE5")
            return VK_XBUTTON2;
        if (key.size() == 1)
            return static_cast<UINT>(key.front());

        if (key.front() == L'F')
        {
            const int number = std::stoi(key.substr(1));
            return static_cast<UINT>(VK_F1 + number - 1);
        }

        return 0;
    }

    /// Набор, который одинаково понимают Windows launcher и Tincan core.
    inline bool IsSupported(const std::wstring& value)
    {
        return !Normalize(value).empty();
    }
}
