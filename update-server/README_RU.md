# FakeDiscord — GitHub Releases

Локальная папка `update-server` теперь используется только для подготовки артефактов GitHub Release.
Локальные tunnel/reverse-proxy сервисы больше не используются.

## Структура

- `public/stable/manifest.json` — manifest последнего stable-релиза;
- `public/stable/FakeDiscord.exe` — Windows x64;
- `public/stable/FakeDiscord-arch-x86_64.tar.gz` — Arch/Linux x86_64;
- `scripts/publish.sh` — собирает локальный набор файлов для GitHub Release.

## Подготовка новой версии

Из корня проекта:

```bash
./update-server/scripts/publish.sh
```

Версия автоматически берётся из `AppTypes.h`.
Её также можно передать вручную:

```bash
./update-server/scripts/publish.sh 0.3.2-fd14
```
После выполнения в `public/stable/` должны находиться три файла:

```text
manifest.json
FakeDiscord.exe
FakeDiscord-arch-x86_64.tar.gz
```

Их нужно прикрепить к GitHub Release репозитория:

```text
KpOwOJluK/FakeDiscord
```

Windows updater обращается напрямую к:

```text
https://github.com/KpOwOJluK/FakeDiscord/releases/latest/download/manifest.json
```

Относительные URL внутри manifest разрешаются относительно того же GitHub Release,
поэтому `FakeDiscord.exe` загружается напрямую из GitHub.

Для локальной отладки полный URL manifest можно временно переопределить переменной:

```text
FD_UPDATE_MANIFEST_URL
```
