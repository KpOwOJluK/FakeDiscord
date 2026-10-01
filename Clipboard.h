#pragma once

#include "Win32Config.h"
#include <windows.h>

#include <string>

class ConPtySession;

/// Проверяет, содержит ли Windows Clipboard текст в формате UTF-16.
bool ClipboardHasUnicodeText();

/// Полностью заменяет текстовое содержимое Clipboard указанной UTF-16 строкой.
bool SetClipboardUnicodeText(
    HWND owner,
    const std::wstring& value);

/// Читает Unicode-текст из Clipboard, нормализует переводы строк
/// и отправляет результат в активную ConPTY-сессию как UTF-8.
bool PasteClipboardToTerminal(
    HWND owner,
    ConPtySession* session);
