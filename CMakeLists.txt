cmake_minimum_required(VERSION 3.20)

project(FakeDiscordConPTY LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(FakeDiscord WIN32
    FakeDiscord.cpp
    FakeDiscordApp.cpp
    FakeDiscordAppUi.cpp
    FakeDiscordAppSession.cpp
    FakeDiscordAppInput.cpp
    FakeDiscordAppWindow.cpp
    TerminalBuffer.cpp
    TerminalBufferCsi.cpp
    TerminalBufferScreen.cpp
    TerminalBufferColor.cpp
    ConPtySession.cpp
    Clipboard.cpp
    TerminalSelection.cpp
    TerminalRenderer.cpp
    TerminalInput.cpp
    DisconnectDialog.cpp
    AppPaths.cpp
    TextUtil.cpp
    UiLayout.cpp
    UiResources.cpp
    UiPainter.cpp
    Updater.cpp
    FakeDiscord.rc
)

target_compile_definitions(FakeDiscord PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    _WIN32_WINNT=0x0A00
)

if(MSVC)
    target_compile_options(FakeDiscord PRIVATE
        /utf-8
        /W4
        /O2
        /GL
        /Gy
    )

    set_property(
        TARGET FakeDiscord
        PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )

    target_link_options(FakeDiscord PRIVATE
        /LTCG
        /OPT:REF
        /OPT:ICF
    )
endif()

target_link_libraries(FakeDiscord PRIVATE
    user32
    gdi32
    kernel32
    shell32
    winhttp
    bcrypt
)
