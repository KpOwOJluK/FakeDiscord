# FakeDiscord File Transfer Experimental — fd3

Отдельная экспериментальная ветка проекта. Исходная папка `FakeDiscord_Native_CPP` не изменена.

## Что реализовано

- общий модифицированный Tincan `0.3.2-fd3` для Windows и Linux;
- новый control protocol `tincan/control/2` с объявлениями файлов;
- отдельный прямой file plane `tincan/file/0` поверх iroh/QUIC;
- содержимое файла не идёт через координатора комнаты;
- потоковая передача без загрузки всего файла в RAM;
- BLAKE2s-256 проверка целостности перед финальным сохранением;
- временный `.part` файл и атомарный rename после успешной проверки;
- защита от перезаписи существующих файлов;
- очистка удалённого имени файла от каталогов;
- предел одного файла: 8 GiB;
- прогресс отправки/получения в status line;
- команды `/send`, `/files`, `/get`;
- Windows Native/ConPTY launcher с новым core внутри;
- Arch Linux build/install/launcher scripts.

## Команды передачи файлов

```text
/send <path>
/files
/get <id>
```

Windows downloads: `%USERPROFILE%\Downloads\FakeDiscord`.
Linux downloads: `~/Downloads/FakeDiscord`.
Оба пути можно переопределить через `FAKEDISCORD_DOWNLOAD_DIR`.

## Проверки

- `cargo check` — успешно;
- полный Rust test suite — успешно;
- unit tests: 310 passed, 0 failed, 2 ignored;
- CLI tests: 4 passed;
- control-plane tests: 14 passed, 0 failed, 2 ignored (internet discovery tests);
- voice mesh: 7 passed;
- новый file-transfer E2E: 1 passed;
- Windows MSVC build — успешно;
- runtime cache check: extracted Tincan reports `0.3.2-fd3`; SHA-256 matches embedded build.

## Важная совместимость

`fd3` несовместим с обычным Tincan 0.3.2 на уровне control ALPN. Все участники одной комнаты должны использовать экспериментальный Windows/Arch build из этой папки.

## Arch Linux

Инструкция: `arch-linux/README_ARCH_RU.md`.

```bash
sudo pacman -S --needed base-devel rust pkgconf alsa-lib opus
bash arch-linux/build_arch.sh
bash arch-linux/install_arch.sh
fakediscord
```

## Новое в fd2: быстрый выбор файла

- Windows FakeDiscord принимает drag'n'drop файлов прямо в окно активной Tincan-сессии;
- при drop launcher автоматически возвращает Tincan на Chat (F5) и отправляет `/send "<полный путь>"`;
- можно бросить несколько файлов одновременно — для каждого создаётся отдельная `/send` команда;
- каталоги при drag'n'drop игнорируются;
- `/send` + `Tab` теперь включает файловое автодополнение в самом Tincan;
- обычный `Tab` без `/send` по-прежнему переключает текстовые каналы;
- автодополнение работает и в Windows, и в Arch Linux, потому что находится в общем Rust-core;
- пустой `/send ` начинает просмотр с домашнего каталога;
- поддерживаются абсолютные и относительные пути, `~`, каталоги, Unicode и пробелы;
- при единственном совпадении путь дополняется целиком;
- при нескольких совпадениях дополняется общий префикс, а варианты показываются в status-line;
- каталоги получают завершающий системный разделитель, поэтому можно продолжать нажимать Tab глубже по дереву.

Примеры:

```text
/send
<Tab>

/send D:\Dow
<Tab>

/send ~/Down
<Tab>
```

## Новое в fd3: /get + Tab

- `/get` + `Tab` показывает файлы, доступные в текущем текстовом канале;
- `/get <префикс-id>` + `Tab` дополняет transfer ID;
- `/get <часть имени>` + `Tab` ищет файл по имени без учёта регистра;
- при единственном совпадении строка автоматически становится `/get <короткий-id>`;
- при нескольких совпадениях варианты показываются в status-line;
- поиск работает одинаково на Windows и Arch Linux.

## Исправление Windows drag'n'drop

- устранён баг, при котором отправитель уходил в Deafen/Mute после перетаскивания файла;
- причина: launcher перед `/send` эмулировал F5, а в актуальном Tincan F5 переключает Deafen;
- синтетический F5 удалён, `/send` теперь передаётся напрямую в активную Tincan-сессию;
- Windows launcher пересобран успешно.

## Windows автообновление

В главное меню добавлена кнопка `Обновить с GitHub`.

Алгоритм:
- manifest: `https://github.com/KpOwOJluK/FakeDiscord/releases/latest/download/manifest.json`;
- для отладки полный URL manifest можно переопределить через `FD_UPDATE_MANIFEST_URL`;
- WinHTTP загружает manifest и Windows-артефакт непосредственно из GitHub Releases;
- новая версия определяется по формату `0.3.2-fdN`;
- размер и SHA-256 обязательны и проверяются до установки;
- неверный hash удаляет временный EXE и оставляет текущую версию нетронутой;
- после успешной проверки создаётся временный PowerShell installer;
- installer ждёт завершения текущего PID, заменяет `FakeDiscord.exe`, запускает новую версию и удаляет себя.

Проверено локальным smoke-тестом:
- обнаружение fd3 -> fd4;
- загрузка полного EXE;
- успешная SHA-256 проверка;
- блокировка неверного SHA-256;
- отсутствие скачивания при одинаковой версии;
- самозамена одноразового тестового EXE после завершения процесса.
