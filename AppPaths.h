#pragma once

#include <filesystem>
#include <string>

/// Настройки нативного лаунчера. Здесь нет адресов, паролей или invite-кодов.
struct LauncherSettings
{
    bool english = false;
    bool notifications = true;
    bool pttEnabled = false;
    std::wstring pttKey = L"F4";
    int maxFileGiB = 8;
    std::wstring serverName = L"Private";
    std::wstring channels = L"general,gaming,music";
};

/// Набор путей, используемых FakeDiscord для конфигурации и кеша.
struct AppPaths
{
    std::filesystem::path configDir;
    std::filesystem::path nickFile;
    std::filesystem::path settingsFile;
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

/// Загружает настройки лаунчера; отсутствующий/повреждённый файл даёт безопасные defaults.
LauncherSettings LoadLauncherSettings(const AppPaths& paths);

/// Сохраняет настройки лаунчера в UTF-8 key=value без секретов доступа.
void SaveLauncherSettings(const AppPaths& paths, const LauncherSettings& settings);
