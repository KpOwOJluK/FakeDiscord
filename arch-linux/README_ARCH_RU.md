# FakeDiscord для Arch Linux — fd14

Arch-сборка использует тот же Rust-core, что и Windows: tincan 0.3.2-fd14.

## Что изменилось в fd14

Старой схемы `room + password` больше нет. В клиенте и launcher нет общего пароля или жёстко заданных данных конкретного сервера.

Используется:
- постоянная криптографическая identity координатора;
- постоянная identity каждого клиентского устройства;
- одноразовый invite для первой регистрации;
- persistent allowlist на стороне координатора;
- повторный вход зарегистрированного устройства без invite;
- отзыв доступа по метке или префиксу PeerId.

Состояние хранится отдельно от бинарника:
- coordinator: `~/.config/fakediscord/server/`
- client identity/pairing: `~/.config/fakediscord/client/`

Файлы identity на Linux создаются с правами 0600.

## Сборка

```bash
cd arch-linux
./build_arch.sh
```

Скрипт выполняет `cargo check`, E2E-тест передачи файлов и release-сборку.

## Установка

```bash
cd arch-linux
./install_arch.sh
```

Устанавливаются:
- графический Qt launcher: `~/.local/bin/fakediscord-gui`
- терминальный launcher: `~/.local/bin/fakediscord`
- core: `~/.local/lib/fakediscord/tincan`
- desktop entry: `~/.local/share/applications/fakediscord.desktop`

Ярлык KDE запускает графический launcher. Команда `fakediscord` по-прежнему открывает текстовое меню в терминале.

PTT на KDE Wayland работает глобально через read-only Linux input listener и продолжает принимать press/release, когда окно свёрнуто или фокус находится в игре. При открытии и закрытии микрофона проигрываются короткие разные звуковые сигналы.

## Первый запуск

На машине координатора выбери в launcher:

`3) Запустить приватный координатор`

Invite можно выпустить прямо внутри комнаты:

```text
/invite Alice-laptop
```

или из отдельного терминала на той же машине:

```bash
tincan invite "Alice laptop"
```

Друг на своём устройстве выбирает:

`2) Подключиться по одноразовому приглашению`

После первого успешного входа invite удаляется, PeerId устройства сохраняется в allowlist, а клиент запоминает координатор. Далее используется:

`1) Подключиться (сохранённая привязка)`

## Управление доступом

На coordinator:
- `/auth` — показать разрешённые устройства;
- `/revoke <метка|peer-prefix>` — отозвать устройство;
- F1 — быстрый одноразовый invite с меткой `guest`.

Те же операции доступны отдельными командами:

```bash
tincan peers
tincan revoke "Alice laptop"
tincan invite "Bob PC"
```

Клиент может удалить только локально сохранённую привязку:

```bash
tincan reset-pairing
```

Это не удаляет его PeerId из server allowlist. Для полного отзыва нужен `revoke` на coordinator.

## Файлы

В комнате:
- `/send <путь>`
- `/sendto <участник> <путь>`
- `/files`
- `/get <id>`

Передача файлов и голос остаются P2P; coordinator обслуживает control-plane и авторизацию.
