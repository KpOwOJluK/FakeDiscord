#include "Win32Config.h"
#include "AppPaths.h"
#include "TextUtil.h"
#include "PttKey.h"
#include "resource.h"

#include <windows.h>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace fs = std::filesystem;

namespace
{
bool FileMatchesEmbedded(
    const fs::path& path,
    const void* data,
    DWORD size)
{
    std::error_code ec;
    if (!fs::exists(path, ec) || ec)
        return false;

    const auto existingSize = fs::file_size(path, ec);
    if (ec || existingSize != size)
        return false;

    std::ifstream file(path, std::ios::binary);
    if (!file)
        return false;

    constexpr std::size_t chunkSize = 64 * 1024;
    std::vector<char> buffer(chunkSize);
    const auto* expected =
        static_cast<const unsigned char*>(data);

    std::size_t offset = 0;
    while (offset < static_cast<std::size_t>(size))
    {
        const std::size_t remaining =
            static_cast<std::size_t>(size) - offset;
        const std::size_t count =
            std::min(chunkSize, remaining);

        file.read(
            buffer.data(),
            static_cast<std::streamsize>(count));

        if (file.gcount() !=
            static_cast<std::streamsize>(count))
        {
            return false;
        }

        if (std::memcmp(
                buffer.data(),
                expected + offset,
                count) != 0)
        {
            return false;
        }

        offset += count;
    }

    return true;
}
}

AppPaths InitAppPaths()
{
    const std::wstring appData =
        GetEnvironmentString(L"APPDATA");
    const std::wstring localAppData =
        GetEnvironmentString(L"LOCALAPPDATA");

    if (appData.empty() || localAppData.empty())
    {
        throw std::runtime_error(
            "APPDATA/LOCALAPPDATA not available");
    }

    AppPaths paths;
    paths.configDir = fs::path(appData) / L"FakeDiscord";
    paths.nickFile = paths.configDir / L"nick.txt";
    paths.settingsFile = paths.configDir / L"launcher-settings.conf";
    paths.cacheDir = fs::path(localAppData) / L"FakeDiscord";
    paths.tincanPath = paths.cacheDir / L"tincan.exe";

    fs::create_directories(paths.configDir);
    fs::create_directories(paths.cacheDir);
    return paths;
}

void EnsureTincanExtracted(const AppPaths& paths)
{
    HMODULE module = GetModuleHandleW(nullptr);
    HRSRC resource = FindResourceW(
        module,
        MAKEINTRESOURCEW(IDR_TINCAN),
        RT_RCDATA);

    if (!resource)
        throw std::runtime_error("Embedded tincan.exe resource not found");

    HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded)
        throw std::runtime_error("Cannot load embedded tincan.exe");

    const DWORD size = SizeofResource(module, resource);
    const void* data = LockResource(loaded);
    if (!data || size == 0)
        throw std::runtime_error("Embedded tincan.exe is empty");

    if (FileMatchesEmbedded(paths.tincanPath, data, size))
        return;

    std::error_code ec;

    fs::path temp = paths.tincanPath;
    temp += L".new";

    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file)
            throw std::runtime_error("Cannot extract tincan.exe");

        file.write(
            static_cast<const char*>(data),
            static_cast<std::streamsize>(size));

        if (!file)
            throw std::runtime_error("Cannot write tincan.exe");
    }

    fs::remove(paths.tincanPath, ec);
    ec.clear();
    fs::rename(temp, paths.tincanPath, ec);

    if (ec)
    {
        fs::remove(temp, ec);
        throw std::runtime_error("Cannot replace extracted tincan.exe");
    }
}

std::wstring LoadNick(const AppPaths& paths)
{
    if (!fs::exists(paths.nickFile))
        return {};

    std::ifstream file(paths.nickFile, std::ios::binary);
    if (!file)
        return {};

    const std::string bytes(
        (std::istreambuf_iterator<char>(file)),
        std::istreambuf_iterator<char>());

    return Utf8ToWide(bytes);
}

void SaveNick(const AppPaths& paths, const std::wstring& nick)
{
    fs::create_directories(paths.configDir);

    std::ofstream file(
        paths.nickFile,
        std::ios::binary | std::ios::trunc);
    if (!file)
        return;

    const std::string utf8 = WideToUtf8(nick);
    file.write(
        utf8.data(),
        static_cast<std::streamsize>(utf8.size()));
}

LauncherSettings LoadLauncherSettings(const AppPaths& paths)
{
    LauncherSettings settings;
    std::ifstream file(paths.settingsFile, std::ios::binary);
    if (!file)
        return settings;

    std::string line;
    while (std::getline(file, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto split = line.find('=');
        if (split == std::string::npos)
            continue;

        const std::string key = line.substr(0, split);
        const std::string value = line.substr(split + 1);
        if (key == "language")
            settings.english = value == "en";
        else if (key == "notifications")
            settings.notifications = value != "0";
        else if (key == "ptt_enabled")
            settings.pttEnabled = value == "1";
        else if (key == "ptt_key")
        {
            const std::wstring normalized = ptt_key::Normalize(Utf8ToWide(value));
            settings.pttKey = normalized.empty() ? L"F4" : normalized;
        }
        else if (key == "max_file_gib")
        {
            try { settings.maxFileGiB = std::clamp(std::stoi(value), 1, 16); }
            catch (...) { settings.maxFileGiB = 8; }
        }
        else if (key == "server_name" && !value.empty())
            settings.serverName = Utf8ToWide(value);
        else if (key == "channels" && !value.empty())
            settings.channels = Utf8ToWide(value);
    }
    return settings;
}

void SaveLauncherSettings(const AppPaths& paths, const LauncherSettings& settings)
{
    fs::create_directories(paths.configDir);
    std::ofstream file(paths.settingsFile, std::ios::binary | std::ios::trunc);
    if (!file)
        return;

    file << "language=" << (settings.english ? "en" : "ru") << '\n';
    file << "notifications=" << (settings.notifications ? "1" : "0") << '\n';
    file << "ptt_enabled=" << (settings.pttEnabled ? "1" : "0") << '\n';
    file << "ptt_key=" << WideToUtf8(ptt_key::Normalize(settings.pttKey).empty() ? L"F4" : ptt_key::Normalize(settings.pttKey)) << '\n';
    file << "max_file_gib=" << std::clamp(settings.maxFileGiB, 1, 16) << '\n';
    file << "server_name=" << WideToUtf8(settings.serverName) << '\n';
    file << "channels=" << WideToUtf8(settings.channels) << '\n';
}
