#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace updater
{
struct UpdateInfo
{
    std::wstring version;
    std::wstring downloadUrl;
    std::wstring sha256;
    std::uint64_t size = 0;
};

struct CheckResult
{
    bool success = false;
    bool updateAvailable = false;
    UpdateInfo update;
    std::wstring error;
};

/// Возвращает версию текущей сборки FakeDiscord.
const wchar_t* CurrentVersion();

/// Запрашивает manifest и определяет наличие новой или пересобранной Windows-сборки.
CheckResult CheckForUpdate();

/// Скачивает бинарник и проверяет размер и SHA-256 до установки.
bool DownloadAndVerify(
    const UpdateInfo& info,
    const std::filesystem::path& destination,
    std::wstring& error);

/// Возвращает полный путь к запущенному FakeDiscord.exe.
std::filesystem::path CurrentExecutablePath();

/// Планирует замену EXE после завершения текущего процесса и перезапуск.
bool ScheduleReplacement(
    const std::filesystem::path& downloadedExe,
    std::wstring& error);
}
