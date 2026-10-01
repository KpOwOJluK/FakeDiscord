#pragma once

#include <cstdint>
#include <string>

/// Читает строковую переменную окружения Windows.
std::wstring GetEnvironmentString(const wchar_t* name);

/// Преобразует UTF-16 строку Windows в UTF-8.
std::string WideToUtf8(const std::wstring& text);

/// Преобразует UTF-8 строку в UTF-16 строку Windows.
std::wstring Utf8ToWide(const std::string& text);

/// Преобразует Unicode code point в UTF-16, включая surrogate pair.
std::wstring CodepointToWide(uint32_t codepoint);
