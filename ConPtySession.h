#pragma once

#include "Win32Config.h"
#include <windows.h>

#include <atomic>
#include <string>
#include <thread>
#include <vector>

class TerminalBuffer;

/// Управляет жизненным циклом одного дочернего процесса Tincan,
/// запущенного внутри Windows ConPTY.
class ConPtySession
{
public:
    /// Связывает сессию с терминальным буфером и флагом необходимости перерисовки.
    ConPtySession(
        TerminalBuffer& terminal,
        std::atomic<bool>& terminalDirty,
        UINT exitMessage);

    /// Гарантированно завершает reader-thread и освобождает Win32/ConPTY ресурсы.
    ~ConPtySession();

    ConPtySession(const ConPtySession&) = delete;
    ConPtySession& operator=(const ConPtySession&) = delete;

    /// Создаёт ConPTY, запускает дочерний процесс и начинает чтение его вывода.
    bool Start(
        HWND notifyWindow,
        const std::wstring& executable,
        const std::vector<std::wstring>& args,
        const std::wstring& workingDirectory,
        short cols,
        short rows);

    /// Изменяет размер уже созданной псевдоконсоли.
    void Resize(short cols, short rows);

    /// Отправляет сырые байты во входной поток дочернего терминала.
    bool Write(const std::string& bytes);

    /// Останавливает сессию; при terminateChild=true принудительно завершает Tincan.
    void Stop(bool terminateChild);

private:
    /// Экранирует один аргумент по правилам командной строки Windows.
    static std::wstring QuoteArg(const std::wstring& arg);

    /// Формирует полную mutable-командную строку для CreateProcessW.
    static std::wstring BuildCommandLine(
        const std::wstring& executable,
        const std::vector<std::wstring>& args);

    TerminalBuffer& terminal_;
    std::atomic<bool>& terminalDirty_;
    UINT exitMessage_ = 0;

    HPCON pseudoConsole_ = nullptr;
    HANDLE inputWrite_ = INVALID_HANDLE_VALUE;
    HANDLE outputRead_ = INVALID_HANDLE_VALUE;
    HANDLE process_ = nullptr;
    HANDLE thread_ = nullptr;

    std::thread reader_;
    std::atomic<bool> running_{false};
    HWND notifyWindow_ = nullptr;
};
