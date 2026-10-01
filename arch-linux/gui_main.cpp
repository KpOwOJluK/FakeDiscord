#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QMessageBox>
#include <QProcess>
#include <QPushButton>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

struct LauncherSettings {
    QString language = "ru";
    bool notifications = true;
    bool pttEnabled = false;
    QString pttKey = "F4";
    int maxFileGiB = 8;
    QString serverName = "Private";
    QString channels = "general,gaming,music";
};

QString configDir() {
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + "/.config";
    const QString dir = base + "/fakediscord";
    QDir().mkpath(dir);
    return dir;
}

QString settingsPath() { return configDir() + "/launcher-settings.conf"; }
QString nickPath() { return configDir() + "/nick"; }

QString findTincan() {
    const QString override = qEnvironmentVariable("FAKEDISCORD_TINCAN");
    if (!override.isEmpty() && QFileInfo::exists(override))
        return override;

    const QString installed = QDir::homePath() + "/.local/lib/fakediscord/tincan";
    if (QFileInfo::exists(installed))
        return installed;
    return QStandardPaths::findExecutable("tincan");
}

LauncherSettings loadSettings() {
    LauncherSettings settings;
    QFile file(settingsPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return settings;

    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        const qsizetype split = line.indexOf('=');
        if (split <= 0)
            continue;
        const QString key = line.left(split);
        const QString value = line.mid(split + 1);
        if (key == "language" && (value == "ru" || value == "en"))
            settings.language = value;
        else if (key == "notifications")
            settings.notifications = value != "0";
        else if (key == "ptt_enabled")
            settings.pttEnabled = value == "1";
        else if (key == "ptt_key" && !value.isEmpty())
            settings.pttKey = value.toUpper();
        else if (key == "max_file_gib") {
            bool ok = false;
            const int limit = value.toInt(&ok);
            if (ok && limit >= 1 && limit <= 16)
                settings.maxFileGiB = limit;
        } else if (key == "server_name" && !value.isEmpty())
            settings.serverName = value;
        else if (key == "channels" && !value.isEmpty())
            settings.channels = value;
    }
    return settings;
}

void saveSettings(const LauncherSettings& settings) {
    QFile file(settingsPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return;
    QTextStream out(&file);
    out << "language=" << settings.language << '\n';
    out << "notifications=" << (settings.notifications ? 1 : 0) << '\n';
    out << "ptt_enabled=" << (settings.pttEnabled ? 1 : 0) << '\n';
    out << "ptt_key=" << settings.pttKey << '\n';
    out << "max_file_gib=" << settings.maxFileGiB << '\n';
    out << "server_name=" << settings.serverName << '\n';
    out << "channels=" << settings.channels << '\n';
}

QString loadNick() {
    QFile file(nickPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readLine()).trimmed();
}

void saveNick(const QString& nick) {
    QFile file(nickPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate))
        return;
    file.write(nick.toUtf8());
    file.write("\n");
}

QString coreVersion(const QString& tincan) {
    if (tincan.isEmpty())
        return "core not found";
    QProcess process;
    process.start(tincan, {"--version"});
    if (!process.waitForFinished(2500))
        return "core unavailable";
    const QString output = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    return output.isEmpty() ? "core unavailable" : output;
}

class LauncherWindow final : public QMainWindow {
public:
    LauncherWindow()
        : settings_(loadSettings()), nick_(loadNick()), tincan_(findTincan()) {
        setMinimumSize(680, 720);
        resize(760, 800);
        setWindowTitle("FakeDiscord");
        setStyleSheet(R"(
            QMainWindow, QDialog { background:#111114; color:#f2f2f5; }
            QLabel { color:#f2f2f5; }
            QLabel#muted { color:#a7a7b0; }
            QPushButton {
                background:#2d2d34; color:#f2f2f5; border:1px solid #41414b;
                border-radius:8px; padding:11px 14px; text-align:left;
                font-size:14px;
            }
            QPushButton:hover { background:#383842; }
            QPushButton:pressed { background:#232329; }
            QLineEdit, QComboBox, QSpinBox, QTextEdit {
                background:#1d1d22; color:#f2f2f5; border:1px solid #454550;
                border-radius:6px; padding:7px;
            }
            QCheckBox { color:#f2f2f5; spacing:8px; }
        )");
        ensureNickname();
        rebuild();
    }

private:
    QString trText(const QString& ru, const QString& en) const {
        return settings_.language == "en" ? en : ru;
    }

    void ensureNickname() {
        if (!nick_.trimmed().isEmpty())
            return;
        bool ok = false;
        const QString value = QInputDialog::getText(
            this, "FakeDiscord", "Ник / Nick:", QLineEdit::Normal, {}, &ok).trimmed();
        nick_ = ok && !value.isEmpty() ? value : "Player";
        saveNick(nick_);
    }

    QStringList sessionSettings() const {
        QStringList args{"--max-file-gib", QString::number(settings_.maxFileGiB)};
        if (!settings_.notifications)
            args << "--no-notifications";
        if (settings_.pttEnabled)
            args << "--ptt" << "--ptt-key" << settings_.pttKey;
        return args;
    }

    QPushButton* addButton(QVBoxLayout* layout, const QString& text) {
        auto* button = new QPushButton(text);
        button->setMinimumHeight(44);
        layout->addWidget(button);
        return button;
    }

    bool launchInTerminal(const QString& program, const QStringList& args, const QString& title) {
        const QString kitty = QStandardPaths::findExecutable("kitty");
        if (!kitty.isEmpty()) {
            QStringList termArgs{"--title", title, program};
            termArgs.append(args);
            return QProcess::startDetached(kitty, termArgs);
        }

        const QString konsole = QStandardPaths::findExecutable("konsole");
        if (!konsole.isEmpty()) {
            QStringList termArgs{"-p", "tabtitle=" + title, "-e", program};
            termArgs.append(args);
            return QProcess::startDetached(konsole, termArgs);
        }

        const QString xterm = QStandardPaths::findExecutable("xterm");
        if (!xterm.isEmpty()) {
            QStringList termArgs{"-T", title, "-e", program};
            termArgs.append(args);
            return QProcess::startDetached(xterm, termArgs);
        }
        return false;
    }

    void startSession(QStringList args) {
        if (tincan_.isEmpty()) {
            QMessageBox::critical(this, "FakeDiscord", trText(
                "Core tincan не найден.", "Tincan core was not found."));
            return;
        }
        args.append(sessionSettings());
        if (!launchInTerminal(tincan_, args, "FakeDiscord")) {
            QMessageBox::critical(this, "FakeDiscord", trText(
                "Не найден поддерживаемый терминал.", "No supported terminal was found."));
        }
    }

    QString runCore(const QStringList& args, int timeoutMs = 10000) {
        if (tincan_.isEmpty())
            return trText("Core tincan не найден.", "Tincan core was not found.");
        QProcess process;
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start(tincan_, args);
        if (!process.waitForStarted(2500))
            return trText("Не удалось запустить core.", "Could not start the core.");
        if (!process.waitForFinished(timeoutMs)) {
            process.kill();
            process.waitForFinished();
            return trText("Команда не завершилась вовремя.", "The command timed out.");
        }
        return QString::fromUtf8(process.readAll()).trimmed();
    }

    void showOutput(const QString& title, const QString& output) {
        QDialog dialog(this);
        dialog.setWindowTitle(title);
        dialog.resize(720, 480);
        auto* layout = new QVBoxLayout(&dialog);
        auto* text = new QTextEdit;
        text->setReadOnly(true);
        text->setPlainText(output);
        layout->addWidget(text);

        auto* row = new QHBoxLayout;
        auto* copy = new QPushButton(trText("Копировать", "Copy"));
        auto* close = new QPushButton(trText("Закрыть", "Close"));
        row->addStretch();
        row->addWidget(copy);
        row->addWidget(close);
        layout->addLayout(row);
        connect(copy, &QPushButton::clicked, [&]() {
            QApplication::clipboard()->setText(text->toPlainText());
        });
        connect(close, &QPushButton::clicked, &dialog, &QDialog::accept);
        dialog.exec();
    }

    void showSettings() {
        QDialog dialog(this);
        dialog.setWindowTitle(trText("Настройки FakeDiscord", "FakeDiscord settings"));
        dialog.resize(520, 430);
        auto* outer = new QVBoxLayout(&dialog);
        auto* form = new QFormLayout;

        auto* language = new QComboBox;
        language->addItem("Русский", "ru");
        language->addItem("English", "en");
        language->setCurrentIndex(settings_.language == "en" ? 1 : 0);

        auto* notifications = new QCheckBox;
        notifications->setChecked(settings_.notifications);
        auto* ptt = new QCheckBox;
        ptt->setChecked(settings_.pttEnabled);

        auto* pttKey = new QComboBox;
        QStringList keys;
        for (int i = 1; i <= 12; ++i)
            keys << QString("F%1").arg(i);
        for (QChar c = 'A'; c <= 'Z'; c = QChar(c.unicode() + 1))
            keys << QString(c);
        for (QChar c = '0'; c <= '9'; c = QChar(c.unicode() + 1))
            keys << QString(c);
        keys << "SPACE" << "CAPSLOCK";
        pttKey->addItems(keys);
        pttKey->setCurrentText(settings_.pttKey);

        auto* maxFile = new QSpinBox;
        maxFile->setRange(1, 16);
        maxFile->setSuffix(" GiB");
        maxFile->setValue(settings_.maxFileGiB);

        auto* serverName = new QLineEdit(settings_.serverName);
        auto* channels = new QLineEdit(settings_.channels);

        form->addRow(trText("Язык", "Language"), language);
        form->addRow(trText("Уведомления", "Notifications"), notifications);
        form->addRow("Push-to-talk", ptt);
        form->addRow(trText("Клавиша PTT", "PTT key"), pttKey);
        form->addRow(trText("Макс. размер файла", "Max file size"), maxFile);
        form->addRow(trText("Имя приватного сервера", "Private server name"), serverName);
        form->addRow(trText("Каналы", "Channels"), channels);
        outer->addLayout(form);
        outer->addStretch();

        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        buttons->button(QDialogButtonBox::Save)->setText(trText("Сохранить", "Save"));
        buttons->button(QDialogButtonBox::Cancel)->setText(trText("Отмена", "Cancel"));
        outer->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        if (dialog.exec() != QDialog::Accepted)
            return;

        settings_.language = language->currentData().toString();
        settings_.notifications = notifications->isChecked();
        settings_.pttEnabled = ptt->isChecked();
        settings_.pttKey = pttKey->currentText();
        settings_.maxFileGiB = maxFile->value();
        settings_.serverName = serverName->text().trimmed().isEmpty()
            ? "Private" : serverName->text().trimmed();
        settings_.channels = channels->text().trimmed().isEmpty()
            ? "general,gaming,music" : channels->text().trimmed();
        saveSettings(settings_);
        rebuild();
    }

    void changeNickname() {
        bool ok = false;
        const QString value = QInputDialog::getText(
            this,
            trText("Смена ника", "Change nickname"),
            trText("Новый ник:", "New nickname:"),
            QLineEdit::Normal,
            nick_,
            &ok).trimmed();
        if (ok && !value.isEmpty()) {
            nick_ = value;
            saveNick(nick_);
            rebuild();
        }
    }

    void rebuild() {
        auto* page = new QWidget;
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(32, 28, 32, 28);
        layout->setSpacing(10);

        auto* title = new QLabel("FakeDiscord");
        QFont titleFont = title->font();
        titleFont.setPointSize(24);
        titleFont.setBold(true);
        title->setFont(titleFont);
        layout->addWidget(title);

        auto* subtitle = new QLabel(
            trText("Ник: ", "Nick: ") + nick_ + "   •   " + coreVersion(tincan_));
        subtitle->setObjectName("muted");
        layout->addWidget(subtitle);
        layout->addSpacing(14);

        auto* startServer = addButton(layout, trText(
            "Запустить приватный сервер", "Start private server"));
        auto* connectSaved = addButton(layout, trText(
            "Подключиться", "Connect"));
        auto* connectInvite = addButton(layout, trText(
            "Подключиться по одноразовому приглашению", "Connect with one-time invite"));
        auto* invite = addButton(layout, trText(
            "Создать одноразовое приглашение", "Create one-time invite"));
        auto* authorized = addButton(layout, trText(
            "Разрешённые устройства", "Authorized devices"));
        auto* revoke = addButton(layout, trText(
            "Отозвать устройство", "Revoke device"));
        auto* devices = addButton(layout, trText(
            "Аудиоустройства", "Audio devices"));
        auto* nickname = addButton(layout, trText(
            "Сменить ник", "Change nickname"));
        auto* settings = addButton(layout, trText(
            "Настройки", "Settings"));
        auto* update = addButton(layout, trText(
            "Обновить с GitHub", "Update from GitHub"));
        auto* reset = addButton(layout, trText(
            "Сбросить привязку к серверу", "Reset saved server pairing"));
        auto* terminal = addButton(layout, trText(
            "Открыть терминальный лаунчер", "Open terminal launcher"));

        layout->addStretch();
        auto* quit = addButton(layout, trText("Выход", "Exit"));
        quit->setStyleSheet("text-align:center;");

        connect(startServer, &QPushButton::clicked, this, [this]() {
            startSession({"host", "--name", nick_, "--server-name",
                          settings_.serverName, "--channels", settings_.channels});
        });
        connect(connectSaved, &QPushButton::clicked, this, [this]() {
            startSession({"join", "--name", nick_});
        });
        connect(connectInvite, &QPushButton::clicked, this, [this]() {
            bool ok = false;
            const QString code = QInputDialog::getText(
                this,
                trText("Одноразовое приглашение", "One-time invite"),
                trText("Код приглашения:", "Invite code:"),
                QLineEdit::Normal, {}, &ok).trimmed();
            if (ok && !code.isEmpty())
                startSession({"join", code, "--name", nick_});
        });
        connect(invite, &QPushButton::clicked, this, [this]() {
            bool ok = false;
            const QString label = QInputDialog::getText(
                this, trText("Приглашение", "Invite"),
                trText("Метка устройства/друга:", "Device/friend label:"),
                QLineEdit::Normal, {}, &ok).trimmed();
            if (ok && !label.isEmpty())
                showOutput(trText("Приглашение", "Invite"), runCore({"invite", label}));
        });
        connect(authorized, &QPushButton::clicked, this, [this]() {
            showOutput(trText("Разрешённые устройства", "Authorized devices"),
                       runCore({"peers"}));
        });
        connect(revoke, &QPushButton::clicked, this, [this]() {
            bool ok = false;
            const QString selector = QInputDialog::getText(
                this, trText("Отзыв устройства", "Revoke device"),
                trText("Метка или начало PeerId:", "Label or PeerId prefix:"),
                QLineEdit::Normal, {}, &ok).trimmed();
            if (ok && !selector.isEmpty())
                showOutput(trText("Отзыв устройства", "Revoke device"),
                           runCore({"revoke", selector}));
        });
        connect(devices, &QPushButton::clicked, this, [this]() {
            showOutput(trText("Аудиоустройства", "Audio devices"),
                       runCore({"devices", "--all"}));
        });
        connect(nickname, &QPushButton::clicked, this, [this]() { changeNickname(); });
        connect(settings, &QPushButton::clicked, this, [this]() { showSettings(); });
        connect(update, &QPushButton::clicked, this, [this]() {
            const QUrl releases("https://github.com/KpOwOJluK/FakeDiscord/releases/latest");
            if (!QDesktopServices::openUrl(releases)) {
                QMessageBox::warning(
                    this,
                    "FakeDiscord",
                    trText("Не удалось открыть GitHub Releases.",
                           "Could not open GitHub Releases."));
            }
        });
        connect(reset, &QPushButton::clicked, this, [this]() {
            const auto answer = QMessageBox::question(
                this, "FakeDiscord",
                trText("Сбросить сохранённую привязку к серверу?",
                       "Reset the saved server pairing?"));
            if (answer == QMessageBox::Yes)
                showOutput("FakeDiscord", runCore({"reset-pairing"}));
        });
        connect(terminal, &QPushButton::clicked, this, [this]() {
            const QString launcher = QDir::homePath() + "/.local/bin/fakediscord";
            if (!launchInTerminal(launcher, {}, "FakeDiscord Terminal"))
                QMessageBox::warning(this, "FakeDiscord",
                    trText("Не удалось открыть терминал.", "Could not open a terminal."));
        });
        connect(quit, &QPushButton::clicked, this, &QWidget::close);

        setCentralWidget(page);
    }

    LauncherSettings settings_;
    QString nick_;
    QString tincan_;
};

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("FakeDiscord");
    app.setOrganizationName("FakeDiscord");

    LauncherWindow window;
    window.show();
    return app.exec();
}
