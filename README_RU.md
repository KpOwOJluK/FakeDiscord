# FakeDiscord

**FakeDiscord** — кроссплатформенный приватный голосовой/текстовый чат на базе сильно модифицированного форка [Tincan](https://github.com/KpOwOJluK/tincan-cli).

Текущая версия разработки: **0.3.2-fd14**.

## Основные возможности

- зашифрованный P2P-голос и текст поверх iroh/QUIC;
- постоянный приватный сервер без переиспользуемого пароля комнаты;
- одноразовые приглашения для первого подключения устройства;
- постоянный allowlist устройств с возможностью отзыва;
- прямой P2P-перенос файлов между клиентами;
- публичные и приватные предложения файлов;
- потоковая передача, BLAKE2s-256, временные `.part` и атомарное завершение;
- предпросмотр изображений;
- `/send`, `/sendto`, `/files`, `/get` и автодополнение;
- drag-and-drop файлов в Windows;
- настраиваемый PTT;
- глобальный PTT даже когда игра находится в фокусе;
- отдельные звуки открытия и закрытия микрофона;
- Windows launcher и Qt launcher для Arch Linux;
- терминальный launcher на Arch сохранён;
- обновление через GitHub Releases без Tailscale/Funnel.

## PTT

Поддерживаются F1-F12, A-Z, 0-9, Space и CapsLock.

Windows получает глобальные события через Raw Input. На KDE Wayland/Arch используется read-only listener Linux input events, поэтому PTT продолжает видеть нажатие и отпускание, когда FakeDiscord свёрнут, например во время игры.

## Модифицированный Tincan

Core проекта развивается отдельно как настоящий GitHub fork upstream Tincan:

**https://github.com/KpOwOJluK/tincan-cli**

Там сохранена upstream-история и отдельно описаны изменения протокола, PTT, file transfer и private-server admission.

## Разработка с ChatGPT

Основные модификации FakeDiscord реализованы с помощью **ChatGPT от OpenAI** под руководством и с проверкой владельца репозитория.

ChatGPT использовался для C++/Rust-разработки, рефакторинга, отладки Windows и Arch, тестов, PTT, передачи файлов, launcher UI, сборочных сценариев и документации.

Подробности: [AI_ASSISTED_DEVELOPMENT.md](AI_ASSISTED_DEVELOPMENT.md).
