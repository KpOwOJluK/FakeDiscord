#include "Win32Config.h"
#include "Updater.h"

#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cwctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <tuple>
#include <vector>

#include "AppTypes.h"
#include "TextUtil.h"

namespace fs = std::filesystem;

namespace
{
constexpr wchar_t kDefaultUpdateManifestUrl[] =
    L"https://github.com/KpOwOJluK/FakeDiscord/releases/latest/download/manifest.json";
constexpr std::uint64_t kMaxManifestBytes = 256 * 1024;
constexpr std::uint64_t kMaxUpdateBytes = 256ull * 1024ull * 1024ull;
class InternetHandle
{
public:
    explicit InternetHandle(HINTERNET value = nullptr) : value_(value) {}
    ~InternetHandle()
    {
        if (value_)
            WinHttpCloseHandle(value_);
    }

    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;

    HINTERNET Get() const
    {
        return value_;
    }

    explicit operator bool() const
    {
        return value_ != nullptr;
    }

private:
    HINTERNET value_ = nullptr;
};

std::wstring Win32ErrorText(DWORD code)
{
    wchar_t* buffer = nullptr;
    const DWORD flags =
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
        FORMAT_MESSAGE_FROM_SYSTEM |
        FORMAT_MESSAGE_IGNORE_INSERTS;

    const DWORD length = FormatMessageW(
        flags,
        nullptr,
        code,
        0,
        reinterpret_cast<wchar_t*>(&buffer),
        0,
        nullptr);

    std::wstring text =
        length && buffer ? std::wstring(buffer, length) : L"";

    if (buffer)
        LocalFree(buffer);

    while (!text.empty() &&
           (text.back() == L'\r' || text.back() == L'\n' ||
            text.back() == L' '))
    {
        text.pop_back();
    }

    return text;
}
bool FailWinHttp(
    const wchar_t* action,
    std::wstring& error)
{
    const DWORD code = GetLastError();
    error = action;
    error += L": ";
    error += Win32ErrorText(code);
    error += L" (";
    error += std::to_wstring(code);
    error += L")";
    return false;
}

std::wstring ManifestUrl()
{
    std::wstring url = GetEnvironmentString(L"FD_UPDATE_MANIFEST_URL");
    if (url.empty())
        url = kDefaultUpdateManifestUrl;

    while (!url.empty() && std::iswspace(url.front()))
        url.erase(url.begin());

    while (!url.empty() && std::iswspace(url.back()))
        url.pop_back();

    return url;
}

fs::path TempManifestPath()
{
    wchar_t directory[MAX_PATH + 1]{};
    const DWORD length = GetTempPathW(MAX_PATH, directory);
    if (length == 0 || length > MAX_PATH)
        return {};

    fs::path path(directory);
    path /= L"FakeDiscord-manifest-" +
        std::to_wstring(GetCurrentProcessId()) +
        L".json";
    return path;
}

bool DownloadUrl(
    const std::wstring& url,
    const fs::path& destination,
    std::uint64_t maxBytes,
    std::uint64_t& written,
    std::wstring& error)
{
    std::array<wchar_t, 512> host{};
    std::array<wchar_t, 4096> path{};
    std::array<wchar_t, 2048> extra{};

    URL_COMPONENTSW parts{};
    parts.dwStructSize = sizeof(parts);
    parts.lpszHostName = host.data();
    parts.dwHostNameLength = static_cast<DWORD>(host.size());
    parts.lpszUrlPath = path.data();
    parts.dwUrlPathLength = static_cast<DWORD>(path.size());
    parts.lpszExtraInfo = extra.data();
    parts.dwExtraInfoLength = static_cast<DWORD>(extra.size());

    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
        return FailWinHttp(L"Некорректный URL обновления", error);

    const std::wstring object =
        std::wstring(path.data(), parts.dwUrlPathLength) +
        std::wstring(extra.data(), parts.dwExtraInfoLength);

    InternetHandle session(WinHttpOpen(
        L"FakeDiscord-Updater/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0));

    if (!session)
        return FailWinHttp(L"WinHttpOpen", error);

    WinHttpSetTimeouts(session.Get(), 5000, 5000, 10000, 10000);

    InternetHandle connection(
        WinHttpConnect(
            session.Get(),
            host.data(),
            parts.nPort,
            0));
    if (!connection)
        return FailWinHttp(L"WinHttpConnect", error);

    const DWORD requestFlags =
        parts.nScheme == INTERNET_SCHEME_HTTPS
            ? WINHTTP_FLAG_SECURE
            : 0;

    InternetHandle request(WinHttpOpenRequest(
        connection.Get(),
        L"GET",
        object.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        requestFlags));

    if (!request)
        return FailWinHttp(L"WinHttpOpenRequest", error);

    if (!WinHttpSendRequest(
            request.Get(),
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0))
    {
        return FailWinHttp(L"Не удалось отправить запрос обновления", error);
    }
    if (!WinHttpReceiveResponse(request.Get(), nullptr))
        return FailWinHttp(L"Не удалось получить ответ сервера", error);

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.Get(),
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &statusSize,
            WINHTTP_NO_HEADER_INDEX))
    {
        return FailWinHttp(L"Не удалось прочитать HTTP-статус", error);
    }

    if (status != 200)
    {
        error = L"GitHub Releases вернул HTTP ";
        error += std::to_wstring(status);
        return false;
    }

    std::ofstream file(destination, std::ios::binary | std::ios::trunc);
    if (!file)
    {
        error = L"Не удалось создать временный файл обновления.";
        return false;
    }
    written = 0;
    std::array<char, 64 * 1024> buffer{};

    for (;;)
    {
        DWORD read = 0;
        if (!WinHttpReadData(
                request.Get(),
                buffer.data(),
                static_cast<DWORD>(buffer.size()),
                &read))
        {
            file.close();
            fs::remove(destination);
            return FailWinHttp(L"Ошибка загрузки обновления", error);
        }

        if (read == 0)
            break;

        written += read;
        if (written > maxBytes)
        {
            file.close();
            fs::remove(destination);
            error = L"Файл обновления превышает допустимый размер.";
            return false;
        }

        file.write(buffer.data(), static_cast<std::streamsize>(read));
        if (!file)
        {
            file.close();
            fs::remove(destination);
            error = L"Ошибка записи временного файла обновления.";
            return false;
        }
    }

    file.close();
    return true;
}

bool ReadTextFile(
    const fs::path& path,
    std::string& text,
    std::wstring& error)
{
    std::ifstream file(path, std::ios::binary);
    if (!file)
    {
        error = L"Не удалось прочитать manifest обновления.";
        return false;
    }

    text.assign(
        std::istreambuf_iterator<char>(file),
        std::istreambuf_iterator<char>());
    return true;
}
bool JsonString(
    const std::string& json,
    const std::string& key,
    std::string& value,
    std::size_t start = 0)
{
    const std::string token = "\"" + key + "\"";
    std::size_t pos = json.find(token, start);
    if (pos == std::string::npos)
        return false;

    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos)
        return false;

    pos = json.find('"', pos + 1);
    if (pos == std::string::npos)
        return false;

    ++pos;
    const std::size_t end = json.find('"', pos);
    if (end == std::string::npos)
        return false;

    value = json.substr(pos, end - pos);
    return true;
}

bool JsonUInt64(
    const std::string& json,
    const std::string& key,
    std::uint64_t& value,
    std::size_t start)
{
    const std::string token = "\"" + key + "\"";
    std::size_t pos = json.find(token, start);
    if (pos == std::string::npos)
        return false;

    pos = json.find(':', pos + token.size());
    if (pos == std::string::npos)
        return false;

    ++pos;
    while (pos < json.size() &&
           std::isspace(static_cast<unsigned char>(json[pos])))
    {
        ++pos;
    }

    std::size_t end = pos;
    while (end < json.size() &&
           std::isdigit(static_cast<unsigned char>(json[end])))
    {
        ++end;
    }

    if (end == pos)
        return false;
    try
    {
        value = std::stoull(json.substr(pos, end - pos));
        return true;
    }
    catch (...)
    {
        return false;
    }
}

std::wstring ResolveArtifactUrl(
    const std::wstring& manifestUrl,
    const std::wstring& artifact)
{
    if (artifact.rfind(L"https://", 0) == 0 ||
        artifact.rfind(L"http://", 0) == 0)
    {
        return artifact;
    }

    const std::size_t slash = manifestUrl.find_last_of(L'/');
    if (slash == std::wstring::npos)
        return artifact;

    return manifestUrl.substr(0, slash + 1) + artifact;
}

bool ParseVersion(
    const std::wstring& text,
    std::tuple<unsigned, unsigned, unsigned, unsigned>& out)
{
    unsigned major = 0;
    unsigned minor = 0;
    unsigned patch = 0;
    unsigned fd = 0;
    wchar_t extra = 0;

    const int matched = swscanf_s(
        text.c_str(),
        L"%u.%u.%u-fd%u%c",
        &major,
        &minor,
        &patch,
        &fd,
        &extra,
        1u);

    if (matched != 4)
        return false;

    out = std::make_tuple(major, minor, patch, fd);
    return true;
}

int CompareVersions(
    const std::wstring& current,
    const std::wstring& remote)
{
    std::tuple<unsigned, unsigned, unsigned, unsigned> a{};
    std::tuple<unsigned, unsigned, unsigned, unsigned> b{};
    if (!ParseVersion(current, a) || !ParseVersion(remote, b))
        return 2;

    if (b > a)
        return 1;
    if (b < a)
        return -1;
    return 0;
}

bool Sha256File(
    const fs::path& path,
    std::wstring& digest,
    std::wstring& error)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectSize = 0;
    DWORD resultSize = 0;

    NTSTATUS status = BCryptOpenAlgorithmProvider(
        &algorithm,
        BCRYPT_SHA256_ALGORITHM,
        nullptr,
        0);

    if (status < 0)
    {
        error = L"Не удалось инициализировать SHA-256.";
        return false;
    }
    status = BCryptGetProperty(
        algorithm,
        BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectSize),
        sizeof(objectSize),
        &resultSize,
        0);

    std::vector<UCHAR> object(objectSize);
    std::array<UCHAR, 32> output{};

    if (status >= 0)
    {
        status = BCryptCreateHash(
            algorithm,
            &hash,
            object.data(),
            static_cast<ULONG>(object.size()),
            nullptr,
            0,
            0);
    }

    std::ifstream file(path, std::ios::binary);
    std::array<char, 64 * 1024> buffer{};

    while (status >= 0 && file)
    {
        file.read(buffer.data(), buffer.size());
        const std::streamsize count = file.gcount();
        if (count <= 0)
            break;

        status = BCryptHashData(
            hash,
            reinterpret_cast<PUCHAR>(buffer.data()),
            static_cast<ULONG>(count),
            0);
    }

    if (status >= 0)
    {
        status = BCryptFinishHash(
            hash,
            output.data(),
            static_cast<ULONG>(output.size()),
            0);
    }

    if (hash)
        BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);

    if (status < 0 || (!file.eof() && file.fail()))
    {
        error = L"Не удалось вычислить SHA-256 обновления.";
        return false;
    }
    std::wostringstream hex;
    hex << std::hex << std::setfill(L'0');
    for (const UCHAR byte : output)
        hex << std::setw(2) << static_cast<unsigned>(byte);

    digest = hex.str();
    return true;
}

std::wstring Lower(std::wstring text)
{
    std::transform(
        text.begin(),
        text.end(),
        text.begin(),
        [](wchar_t ch)
        {
            return static_cast<wchar_t>(towlower(ch));
        });
    return text;
}

std::wstring EscapePowerShellLiteral(std::wstring text)
{
    std::size_t pos = 0;
    while ((pos = text.find(L'\'', pos)) != std::wstring::npos)
    {
        text.insert(pos, 1, L'\'');
        pos += 2;
    }
    return text;
}

bool WriteUtf8Bom(
    const fs::path& path,
    const std::wstring& text)
{
    const std::string utf8 = WideToUtf8(text);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;

    const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    file.write(
        reinterpret_cast<const char*>(bom),
        sizeof(bom));
    file.write(utf8.data(), static_cast<std::streamsize>(utf8.size()));
    return static_cast<bool>(file);
}
}

namespace updater
{
const wchar_t* CurrentVersion()
{
    return app::kAppVersion;
}

CheckResult CheckForUpdate()
{
    CheckResult result;
    const std::wstring manifestUrl = ManifestUrl();
    const fs::path manifestPath = TempManifestPath();

    if (manifestPath.empty())
    {
        result.error = L"Не удалось определить временный каталог Windows.";
        return result;
    }

    std::uint64_t manifestSize = 0;
    if (!DownloadUrl(
            manifestUrl,
            manifestPath,
            kMaxManifestBytes,
            manifestSize,
            result.error))
    {
        return result;
    }

    std::string json;
    if (!ReadTextFile(manifestPath, json, result.error))
    {
        fs::remove(manifestPath);
        return result;
    }

    fs::remove(manifestPath);
    std::string version;
    const std::size_t platform = json.find("\"windows-x64\"");

    if (!JsonString(json, "version", version) ||
        platform == std::string::npos)
    {
        result.error = L"Manifest не содержит Windows-релиз.";
        return result;
    }

    std::string url;
    std::string sha256;
    std::uint64_t size = 0;

    if (!JsonString(json, "url", url, platform) ||
        !JsonString(json, "sha256", sha256, platform) ||
        !JsonUInt64(json, "size", size, platform))
    {
        result.error = L"Некорректное описание Windows-артефакта.";
        return result;
    }

    result.update.version = Utf8ToWide(version);
    result.update.downloadUrl =
        ResolveArtifactUrl(manifestUrl, Utf8ToWide(url));
    result.update.sha256 = Lower(Utf8ToWide(sha256));
    result.update.size = size;

    const bool validHash =
        result.update.sha256.size() == 64 &&
        std::all_of(
            result.update.sha256.begin(),
            result.update.sha256.end(),
            [](wchar_t ch) { return std::iswxdigit(ch) != 0; });

    if (!validHash || size == 0 || size > kMaxUpdateBytes)
    {
        result.error = L"Manifest содержит некорректные параметры артефакта.";
        return result;
    }

    const int comparison =
        CompareVersions(CurrentVersion(), result.update.version);

    if (comparison == 2)
    {
        result.error = L"Неподдерживаемый формат версии в manifest.";
        return result;
    }

    result.success = true;

    if (comparison < 0)
    {
        result.error =
            L"На сервере опубликована более старая версия " +
            result.update.version +
            L".";
        result.success = false;
        return result;
    }

    result.updateAvailable = comparison > 0;
    return result;
}

bool DownloadAndVerify(
    const UpdateInfo& info,
    const fs::path& destination,
    std::wstring& error)
{
    std::error_code ec;
    fs::remove(destination, ec);

    std::uint64_t written = 0;
    if (!DownloadUrl(
            info.downloadUrl,
            destination,
            kMaxUpdateBytes,
            written,
            error))
    {
        return false;
    }

    if (written != info.size)
    {
        fs::remove(destination, ec);
        error =
            L"Размер загруженного файла не совпадает с manifest.";
        return false;
    }

    std::wstring actualHash;
    if (!Sha256File(destination, actualHash, error))
    {
        fs::remove(destination, ec);
        return false;
    }

    if (Lower(actualHash) != Lower(info.sha256))
    {
        fs::remove(destination, ec);
        error =
            L"SHA-256 загруженного FakeDiscord.exe не совпадает с manifest.";
        return false;
    }

    return true;
}

fs::path CurrentExecutablePath()
{
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));

    if (length == 0 || length >= buffer.size())
        return {};

    return fs::path(std::wstring(buffer.data(), length));
}
bool ScheduleReplacement(
    const fs::path& downloadedExe,
    std::wstring& error)
{
    const fs::path currentExe = CurrentExecutablePath();
    if (currentExe.empty())
    {
        error = L"Не удалось определить путь FakeDiscord.exe.";
        return false;
    }

    wchar_t tempDirectory[MAX_PATH + 1]{};
    const DWORD tempLength =
        GetTempPathW(MAX_PATH, tempDirectory);

    if (tempLength == 0 || tempLength > MAX_PATH)
    {
        error = L"Не удалось определить временный каталог.";
        return false;
    }

    fs::path scriptPath(tempDirectory);
    scriptPath /=
        L"FakeDiscord-update-" +
        std::to_wstring(GetCurrentProcessId()) +
        L".ps1";

    const std::wstring source =
        EscapePowerShellLiteral(downloadedExe.wstring());
    const std::wstring target =
        EscapePowerShellLiteral(currentExe.wstring());

    std::wstring script;
    script += L"$ErrorActionPreference = 'Stop'\r\n";
    script += L"$pidToWait = " +
        std::to_wstring(GetCurrentProcessId()) + L"\r\n";
    script += L"while (Get-Process -Id $pidToWait -ErrorAction SilentlyContinue) { ";
    script += L"Start-Sleep -Milliseconds 250 }\r\n";
    script += L"$source = '" + source + L"'\r\n";
    script += L"$target = '" + target + L"'\r\n";
    script += L"Move-Item -LiteralPath $source -Destination $target -Force\r\n";
    script += L"Start-Process -FilePath $target\r\n";
    script += L"Remove-Item -LiteralPath $PSCommandPath -Force\r\n";

    if (!WriteUtf8Bom(scriptPath, script))
    {
        error = L"Не удалось создать скрипт установки обновления.";
        return false;
    }

    std::wstring command =
        L"powershell.exe -NoProfile -NonInteractive "
        L"-ExecutionPolicy Bypass -WindowStyle Hidden -File \"" +
        scriptPath.wstring() +
        L"\"";

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};

    if (!CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &startup,
            &process))
    {
        fs::remove(scriptPath);
        error = L"Не удалось запустить установщик обновления: ";
        error += Win32ErrorText(GetLastError());
        return false;
    }

    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}
}
