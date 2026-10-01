#include "Win32Config.h"
#include "TextUtil.h"
#include <windows.h>

std::wstring GetEnvironmentString(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0);
    if (size == 0)
        return {};

    std::wstring value(size, L'\0');
    const DWORD written = GetEnvironmentVariableW(
        name,
        value.data(),
        size);

    if (written == 0)
        return {};

    value.resize(written);
    return value;
}

std::string WideToUtf8(const std::wstring& text)
{
    if (text.empty())
        return {};

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0,
        nullptr,
        nullptr);

    if (size <= 0)
        return {};

    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        out.data(),
        size,
        nullptr,
        nullptr);
    return out;
}

std::wstring Utf8ToWide(const std::string& text)
{
    if (text.empty())
        return {};

    const int size = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);

    if (size <= 0)
        return {};

    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        out.data(),
        size);
    return out;
}

std::wstring CodepointToWide(uint32_t codepoint)
{
    if (codepoint == 0)
        return {};

    if (codepoint <= 0xFFFF)
        return std::wstring(1, static_cast<wchar_t>(codepoint));

    codepoint -= 0x10000;

    const wchar_t high =
        static_cast<wchar_t>(
            0xD800 + ((codepoint >> 10) & 0x3FF));

    const wchar_t low =
        static_cast<wchar_t>(
            0xDC00 + (codepoint & 0x3FF));

    std::wstring result;
    result.push_back(high);
    result.push_back(low);
    return result;
}
