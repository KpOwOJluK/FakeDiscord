#include "ConPtySession.h"
#include "TerminalBuffer.h"

#include <algorithm>
#include <cstddef>
#include <memory>

ConPtySession::ConPtySession(
    TerminalBuffer& terminal,
    std::atomic<bool>& terminalDirty,
    UINT exitMessage)
    : terminal_(terminal),
      terminalDirty_(terminalDirty),
      exitMessage_(exitMessage)
{
}

ConPtySession::~ConPtySession()
{
    Stop(true);
}

std::wstring ConPtySession::QuoteArg(const std::wstring& arg)
{
    if (arg.empty())
        return L"\"\"";

    bool needQuotes = false;
    for (wchar_t ch : arg)
    {
        if (ch == L' ' || ch == L'\t' || ch == L'\"')
        {
            needQuotes = true;
            break;
        }
    }

    if (!needQuotes)
        return arg;

    std::wstring out = L"\"";
    size_t slashes = 0;

    for (wchar_t ch : arg)
    {
        if (ch == L'\\')
        {
            ++slashes;
            continue;
        }

        if (ch == L'\"')
        {
            out.append(slashes * 2 + 1, L'\\');
            out.push_back(L'\"');
            slashes = 0;
            continue;
        }

        out.append(slashes, L'\\');
        slashes = 0;
        out.push_back(ch);
    }

    out.append(slashes * 2, L'\\');
    out.push_back(L'\"');
    return out;
}

std::wstring ConPtySession::BuildCommandLine(
    const std::wstring& executable,
    const std::vector<std::wstring>& args)
{
    std::wstring line = QuoteArg(executable);

    for (const auto& arg : args)
    {
        line.push_back(L' ');
        line += QuoteArg(arg);
    }

    return line;
}

bool ConPtySession::Start(
    HWND notifyWindow,
    const std::wstring& executable,
    const std::vector<std::wstring>& args,
    const std::wstring& workingDirectory,
    short cols,
    short rows)
{
    Stop(true);
    notifyWindow_ = notifyWindow;

    HANDLE inputRead = INVALID_HANDLE_VALUE;
    HANDLE outputWrite = INVALID_HANDLE_VALUE;

    if (!CreatePipe(&inputRead, &inputWrite_, nullptr, 0))
        return false;

    if (!CreatePipe(&outputRead_, &outputWrite, nullptr, 0))
    {
        CloseHandle(inputRead);
        CloseHandle(inputWrite_);
        inputWrite_ = INVALID_HANDLE_VALUE;
        return false;
    }

    COORD size{
        static_cast<SHORT>(std::max<short>(20, cols)),
        static_cast<SHORT>(std::max<short>(8, rows))
    };

    const HRESULT hr = CreatePseudoConsole(
        size,
        inputRead,
        outputWrite,
        0,
        &pseudoConsole_);

    CloseHandle(inputRead);
    CloseHandle(outputWrite);

    if (FAILED(hr))
    {
        CloseHandle(inputWrite_);
        CloseHandle(outputRead_);
        inputWrite_ = INVALID_HANDLE_VALUE;
        outputRead_ = INVALID_HANDLE_VALUE;
        pseudoConsole_ = nullptr;
        return false;
    }

    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);

    auto attrBuffer = std::make_unique<std::byte[]>(attrSize);
    auto attrList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(
        attrBuffer.get());

    if (!InitializeProcThreadAttributeList(
            attrList, 1, 0, &attrSize))
    {
        ClosePseudoConsole(pseudoConsole_);
        pseudoConsole_ = nullptr;
        return false;
    }

    if (!UpdateProcThreadAttribute(
            attrList,
            0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
            pseudoConsole_,
            sizeof(pseudoConsole_),
            nullptr,
            nullptr))
    {
        DeleteProcThreadAttributeList(attrList);
        ClosePseudoConsole(pseudoConsole_);
        pseudoConsole_ = nullptr;
        return false;
    }

    STARTUPINFOEXW si{};
    si.StartupInfo.cb = sizeof(si);
    si.lpAttributeList = attrList;

    PROCESS_INFORMATION pi{};

    std::wstring commandLine = BuildCommandLine(executable, args);
    std::vector<wchar_t> mutableCommand(
        commandLine.begin(), commandLine.end());
    mutableCommand.push_back(L'\0');

    DWORD oldTermSize = GetEnvironmentVariableW(L"TERM", nullptr, 0);
    std::wstring oldTerm;
    if (oldTermSize > 0)
    {
        oldTerm.resize(oldTermSize);
        DWORD written = GetEnvironmentVariableW(
            L"TERM", oldTerm.data(), oldTermSize);
        if (written > 0)
            oldTerm.resize(written);
        else
            oldTerm.clear();
    }

    DWORD oldColorSize = GetEnvironmentVariableW(L"COLORTERM", nullptr, 0);
    std::wstring oldColorTerm;
    if (oldColorSize > 0)
    {
        oldColorTerm.resize(oldColorSize);
        DWORD written = GetEnvironmentVariableW(
            L"COLORTERM", oldColorTerm.data(), oldColorSize);
        if (written > 0)
            oldColorTerm.resize(written);
        else
            oldColorTerm.clear();
    }

    SetEnvironmentVariableW(L"TERM", L"xterm-256color");
    SetEnvironmentVariableW(L"COLORTERM", L"truecolor");

    const BOOL created = CreateProcessW(
        executable.c_str(),
        mutableCommand.data(),
        nullptr,
        nullptr,
        FALSE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT,
        nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &si.StartupInfo,
        &pi);

    if (oldTerm.empty())
        SetEnvironmentVariableW(L"TERM", nullptr);
    else
        SetEnvironmentVariableW(L"TERM", oldTerm.c_str());

    if (oldColorTerm.empty())
        SetEnvironmentVariableW(L"COLORTERM", nullptr);
    else
        SetEnvironmentVariableW(L"COLORTERM", oldColorTerm.c_str());

    DeleteProcThreadAttributeList(attrList);

    if (!created)
    {
        ClosePseudoConsole(pseudoConsole_);
        pseudoConsole_ = nullptr;
        CloseHandle(inputWrite_);
        CloseHandle(outputRead_);
        inputWrite_ = INVALID_HANDLE_VALUE;
        outputRead_ = INVALID_HANDLE_VALUE;
        return false;
    }

    process_ = pi.hProcess;
    thread_ = pi.hThread;
    running_ = true;

    reader_ = std::thread([this]()
    {
        char buffer[8192];

        while (running_)
        {
            DWORD read = 0;
            const BOOL ok = ReadFile(
                outputRead_,
                buffer,
                sizeof(buffer),
                &read,
                nullptr);

            if (!ok || read == 0)
                break;

            terminal_.Feed(buffer, static_cast<size_t>(read));

            const auto replies = terminal_.TakeResponses();
            for (const auto& reply : replies)
                Write(reply);

            terminalDirty_ = true;
        }

        running_ = false;

        if (notifyWindow_ && exitMessage_ != 0)
        {
            PostMessageW(
                notifyWindow_,
                exitMessage_,
                0,
                0);
        }
    });

    return true;
}

void ConPtySession::Resize(short cols, short rows)
{
    if (!pseudoConsole_)
        return;

    COORD size{
        static_cast<SHORT>(std::max<short>(20, cols)),
        static_cast<SHORT>(std::max<short>(8, rows))
    };

    ResizePseudoConsole(pseudoConsole_, size);
}

bool ConPtySession::Write(const std::string& bytes)
{
    if (inputWrite_ == INVALID_HANDLE_VALUE || bytes.empty())
        return false;

    DWORD written = 0;
    return WriteFile(
        inputWrite_,
        bytes.data(),
        static_cast<DWORD>(bytes.size()),
        &written,
        nullptr) != FALSE;
}

void ConPtySession::Stop(bool terminateChild)
{
    running_.exchange(false);

    if (terminateChild && process_ && process_ != INVALID_HANDLE_VALUE)
    {
        TerminateProcess(process_, 0);
        WaitForSingleObject(process_, 1000);
    }

    if (inputWrite_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(inputWrite_);
        inputWrite_ = INVALID_HANDLE_VALUE;
    }

    if (pseudoConsole_)
    {
        ClosePseudoConsole(pseudoConsole_);
        pseudoConsole_ = nullptr;
    }

    if (outputRead_ != INVALID_HANDLE_VALUE)
    {
        CloseHandle(outputRead_);
        outputRead_ = INVALID_HANDLE_VALUE;
    }

    if (reader_.joinable() &&
        reader_.get_id() != std::this_thread::get_id())
    {
        reader_.join();
    }

    if (thread_)
    {
        CloseHandle(thread_);
        thread_ = nullptr;
    }

    if (process_)
    {
        CloseHandle(process_);
        process_ = nullptr;
    }

    notifyWindow_ = nullptr;
}
