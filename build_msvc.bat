@echo off
setlocal EnableExtensions
title Build FakeDiscord ConPTY

if not exist tincan.exe (
    echo ERROR: tincan.exe not found.
    echo.
    echo Put the Windows x64 tincan.exe next to this script.
    echo.
    if not defined NO_PAUSE pause
    exit /b 1
)

where cl.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: MSVC compiler cl.exe not found.
    echo Run this from the x64 Native Tools Command Prompt for VS 2022.
    if not defined NO_PAUSE pause
    exit /b 1
)

where rc.exe >nul 2>nul
if errorlevel 1 (
    echo ERROR: rc.exe not found.
    if not defined NO_PAUSE pause
    exit /b 1
)

del /q *.obj FakeDiscord.res FakeDiscord.exe 2>nul

echo [1/3] Compiling resources...
rc.exe /nologo /fo FakeDiscord.res FakeDiscord.rc
if errorlevel 1 goto :fail

echo [2/3] Compiling native modules...
cl.exe /nologo /utf-8 /std:c++17 /EHsc /W4 /O2 /GL /Gy /MT ^
    /DUNICODE /D_UNICODE /DNOMINMAX /D_WIN32_WINNT=0x0A00 ^
    /c FakeDiscord.cpp FakeDiscordApp.cpp FakeDiscordAppUi.cpp FakeDiscordAppSession.cpp FakeDiscordAppInput.cpp FakeDiscordAppWindow.cpp ^
       TerminalBuffer.cpp TerminalBufferCsi.cpp TerminalBufferScreen.cpp TerminalBufferColor.cpp ^
       ConPtySession.cpp Clipboard.cpp TerminalSelection.cpp TerminalRenderer.cpp TerminalInput.cpp ^
       DisconnectDialog.cpp AppPaths.cpp TextUtil.cpp UiLayout.cpp ^
       UiResources.cpp UiPainter.cpp Updater.cpp
if errorlevel 1 goto :fail

echo [3/3] Linking...
link.exe /nologo ^
    FakeDiscord.obj FakeDiscordApp.obj FakeDiscordAppUi.obj FakeDiscordAppSession.obj FakeDiscordAppInput.obj FakeDiscordAppWindow.obj ^
    TerminalBuffer.obj TerminalBufferCsi.obj TerminalBufferScreen.obj TerminalBufferColor.obj ^
    ConPtySession.obj Clipboard.obj TerminalSelection.obj TerminalRenderer.obj TerminalInput.obj ^
    DisconnectDialog.obj AppPaths.obj TextUtil.obj UiLayout.obj ^
    UiResources.obj UiPainter.obj Updater.obj FakeDiscord.res ^
    /OUT:FakeDiscord.exe ^
    /SUBSYSTEM:WINDOWS ^
    /LTCG /OPT:REF /OPT:ICF ^
    user32.lib gdi32.lib kernel32.lib shell32.lib winhttp.lib bcrypt.lib
if errorlevel 1 goto :fail

del /q *.obj FakeDiscord.res 2>nul

echo.
echo ==========================================
echo Build complete:
echo %CD%\FakeDiscord.exe
echo ==========================================
echo.
for %%F in (FakeDiscord.exe) do echo Size: %%~zF bytes
echo.
if not defined NO_PAUSE pause
exit /b 0

:fail
echo.
echo BUILD FAILED.
if not defined NO_PAUSE pause
exit /b 1
