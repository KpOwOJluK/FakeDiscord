#include "Clipboard.h"
#include "ConPtySession.h"
#include "TextUtil.h"

#include <string>

namespace
{
    /// Пытается открыть Clipboard несколько раз: его может кратковременно удерживать другой процесс.
    bool OpenClipboardWithRetry(HWND owner)
    {
        for (int attempt = 0; attempt < 5; ++attempt)
        {
            if (OpenClipboard(owner))
                return true;
            Sleep(10);
        }
        return false;
    }
}

bool ClipboardHasUnicodeText()
{
    return IsClipboardFormatAvailable(
               CF_UNICODETEXT) != FALSE;
}

bool SetClipboardUnicodeText(
    HWND owner,
    const std::wstring& value)
{
    if (!OpenClipboardWithRetry(owner))
        return false;

    if (!EmptyClipboard())
    {
        CloseClipboard();
        return false;
    }

    const SIZE_T bytes =
        (value.size() + 1) * sizeof(wchar_t);

    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (!memory)
    {
        CloseClipboard();
        return false;
    }

    void* destination = GlobalLock(memory);
    if (!destination)
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    CopyMemory(destination, value.c_str(), bytes);
    GlobalUnlock(memory);

    if (!SetClipboardData(CF_UNICODETEXT, memory))
    {
        GlobalFree(memory);
        CloseClipboard();
        return false;
    }

    CloseClipboard();
    return true;
}

bool PasteClipboardToTerminal(
    HWND owner,
    ConPtySession* session)
{
    if (!session)
        return false;

    if (!OpenClipboardWithRetry(owner))
    {
        MessageBeep(MB_ICONWARNING);
        return false;
    }

    HANDLE data = GetClipboardData(CF_UNICODETEXT);
    if (!data)
    {
        CloseClipboard();
        MessageBeep(MB_ICONWARNING);
        return false;
    }

    const wchar_t* clipboardText =
        static_cast<const wchar_t*>(GlobalLock(data));

    if (!clipboardText)
    {
        CloseClipboard();
        MessageBeep(MB_ICONWARNING);
        return false;
    }

    std::wstring source(clipboardText);
    GlobalUnlock(data);
    CloseClipboard();

    if (source.empty())
        return true;

    std::wstring normalized;
    normalized.reserve(source.size());

    for (size_t i = 0; i < source.size(); ++i)
    {
        const wchar_t ch = source[i];

        if (ch == L'\r')
        {
            normalized.push_back(L'\r');
            if (i + 1 < source.size() && source[i + 1] == L'\n')
                ++i;
            continue;
        }

        if (ch == L'\n')
        {
            normalized.push_back(L'\r');
            continue;
        }

        normalized.push_back(ch);
    }

    const std::string utf8 = WideToUtf8(normalized);
    if (utf8.empty() && !normalized.empty())
    {
        MessageBeep(MB_ICONWARNING);
        return false;
    }

    return session->Write(utf8);
}
