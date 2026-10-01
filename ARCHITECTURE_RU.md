# Архитектура FakeDiscord

Этот файл описывает структуру нативного Win32-клиента после рефакторинга.

## Общий принцип

Проект разделён по ответственности. `FakeDiscord.cpp` содержит только точку входа.
Состояние приложения хранится в `AppState`, а orchestration выполняет `FakeDiscordApp`.
Tincan запускается как дочерний процесс внутри ConPTY, его VT-вывод разбирается
`TerminalBuffer`, а затем рисуется GDI-рендерером.

## Главный слой приложения

- **FakeDiscord.cpp** — `wWinMain`, DPI awareness, обработка ошибок запуска.
- **FakeDiscordApp.h** — интерфейс главного контроллера с русской документацией.
- **FakeDiscordApp.cpp** — создание окна и маршрутизация Win32-сообщений.
- **FakeDiscordAppUi.cpp** — создание контролов, layout и геометрия экранов.
- **FakeDiscordAppSession.cpp** — меню, формы, сценарии подключения и жизненный цикл Tincan-сессии.
- **FakeDiscordAppInput.cpp** — клавиатура, мышь, выделение и clipboard.
- **FakeDiscordAppWindow.cpp** — DPI, resize, paint и shutdown.
- **AppState.h** — всё изменяемое состояние одного экземпляра приложения.
- **AppTypes.h** — enum-ы, ID контролов и общие UI-константы.

## ConPTY и терминал

- **ConPtySession.h/.cpp** — создание ConPTY, запуск/остановка Tincan,
  pipes, reader-thread, resize и запись во входной поток.
- **TerminalBuffer.h** — общий контракт VT/ANSI-эмулятора и состояние терминальной сетки.
- **TerminalBuffer.cpp** — UTF-8/VT parser, базовое состояние и ESC-последовательности.
- **TerminalBufferCsi.cpp** — CSI, SGR, DEC private modes и terminal queries.
- **TerminalBufferScreen.cpp** — запись ячеек, delayed autowrap, scroll/erase/insert и alternate screen.
- **TerminalBufferColor.cpp** — ANSI/xterm-256 цвета и определение wide/combining Unicode.
- **TerminalInput.h/.cpp** — перевод Win32-клавиш в VT/xterm sequences.
- **TerminalRenderer.h/.cpp** — GDI-отрисовка снимка терминала.
- **TerminalSelection.h/.cpp** — mouse selection и копирование текста.

## UI и Windows-интеграция

- **UiLayout.h/.cpp** — DPI-aware геометрия интерфейса и терминальной сетки.
- **UiPainter.h/.cpp** — header, страницы и owner-draw кнопки.
- **UiResources.h/.cpp** — RAII для шрифтов, иконок и кистей.
- **DisconnectDialog.h/.cpp** — кастомное модальное подтверждение отключения.
- **Clipboard.h/.cpp** — copy/paste через Windows Clipboard.
- **AppPaths.h/.cpp** — APPDATA/LOCALAPPDATA, ник и извлечение tincan.exe.
- **TextUtil.h/.cpp** — UTF-8/UTF-16 и Unicode code point utilities.

## Правила зависимостей

UI не должен разбирать VT-последовательности.
`TerminalBuffer` не должен знать о Win32-окнах или кнопках.
`ConPtySession` отвечает только за процесс/потоки и передачу данных.
`FakeDiscordApp` координирует модули, но низкоуровневая логика должна
оставаться в специализированных классах.

## Сборка

Основная проверяемая сборка:

```bat
build_msvc.bat
```

Скрипт рассчитан на **x64 Native Tools Command Prompt for VS 2022** и собирает код с `/W4`.
Альтернативно проект можно собирать через `CMakeLists.txt`, где для MSVC также включён `/W4`.

## Поведение, которое нельзя ломать при будущих изменениях

- Tincan room передаётся позиционным аргументом, не через `--room`.
- `Tab` должен штатно уходить в Tincan для переключения каналов.
- `Ctrl+C` с выделением копирует; без выделения отключает и возвращает в меню.
- `Ctrl+V`, `Ctrl+Shift+V`, `Shift+Insert` вставляют текст.
- ПКМ копирует выделение, а без выделения вставляет текст из Clipboard.
- `Esc` в голосовом режиме показывает кастомное подтверждение отключения.
- Resize ConPTY должен совпадать с фактической сеткой терминального рендера.
- Delayed VT autowrap обязателен: немедленный перенос в последней колонке
  ломает TUI Tincan после полного redraw.

## Перед крупными изменениями

1. Убедиться, что текущая версия собирается.
2. Менять один слой за раз.
3. Пересобирать после изменения публичного API модуля.
4. Проверять минимум: запуск, quick join, Tab, Esc, Ctrl+C, clipboard и resize.

## Автообновление Windows

- **Updater.h/.cpp** — WinHTTP-загрузка manifest/EXE, проверка версии,
  размера и SHA-256 через Windows CNG/BCrypt, подготовка безопасной самозамены.
- **FakeDiscordAppSession.cpp** — UI-сценарий кнопки `Обновить`:
  проверка версии, подтверждение пользователя, скачивание и перезапуск.
- **AppTypes.h** — хранит `kAppVersion` и ID кнопки Update.
- Для сборки добавлены системные библиотеки `winhttp` и `bcrypt`.
