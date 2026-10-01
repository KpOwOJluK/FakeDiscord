#pragma once

#include <filesystem>
#include <string>

/// Набор путей, используемых FakeDiscord для конфигурации и кеша.
struct AppPaths
{
    std::filesystem::path configDir;
    std::filesystem::path nickFile;
    std::filesystem::path cacheDir;
    std::filesystem::path tincanPath;
};

/// Определяет APPDATA/LOCALAPPDATA, создаёт каталоги и возвращает готовые пути.
AppPaths InitAppPaths();

/// Извлекает встроенный tincan.exe в кеш, если актуальной копии ещё нет.
void EnsureTincanExtracted(const AppPaths& paths);

/// Загружает сохранённый ник из UTF-8 файла. При отсутствии файла возвращает пустую строку.
std::wstring LoadNick(const AppPaths& paths);

/// Сохраняет ник пользователя в UTF-8 файл конфигурации.
void SaveNick(const AppPaths& paths, const std::wstring& nick);
