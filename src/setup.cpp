#include "setup.h"

#include "client.h"
#include "config.h"
#include "models.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <cstdio>

namespace Setup {

namespace {

void say(const QString &text) {
    std::printf("%s\n", qPrintable(text));
    std::fflush(stdout);
}

void fail(const QString &text) {
    std::fprintf(stderr, "%s\n", qPrintable(text));
}

// Writes a file bundled in the binary to target, unless it already holds the
// same bytes. Returns false only when writing fails.
bool install(const QString &resource, const QString &target, bool executable) {
    QFile source(resource);
    if (!source.open(QIODevice::ReadOnly))
        return false;
    const QByteArray contents = source.readAll();

    QFile existing(target);
    if (existing.open(QIODevice::ReadOnly) && existing.readAll() == contents)
        return true;

    QDir().mkpath(QFileInfo(target).absolutePath());
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
        return false;
    if (executable)
        QFile::setPermissions(target, QFile::permissions(target) | QFileDevice::ExeOwner | QFileDevice::ExeGroup
                                          | QFileDevice::ExeOther);
    return true;
}

const char *MenuKey = "setup.default.dictation.asmir";

int runQuietly(const QString &program, const QStringList &arguments) {
    QProcess process;
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process.setStandardOutputFile(QProcess::nullDevice());
    process.start(program, arguments);
    if (!process.waitForFinished(30000))
        return -1;
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -1;
}

}

QString withMenuEntry(const QString &existing) {
    if (existing.contains(MenuKey))
        return existing;
    // Picking it from the menu runs `asmir setup` where its output shows, the
    // way Omarchy runs its own dictation installers.
    const QString entry = QString(R"(  "%1": {"icon":"󰍬","label":"asmir","checked":"[[ \"$(omarchy-default-dictation)\" == \"asmir\" ]]","action":"omarchy-launch-floating-terminal-with-presentation asmir setup"},)")
                              .arg(MenuKey);
    if (existing.trimmed().isEmpty())
        return "{\n" + entry + "\n}\n";

    // Insert right after the opening brace. The trailing comma is fine there:
    // Omarchy's parser allows trailing commas, and any entries follow it.
    QStringList lines = existing.split('\n');
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i).trimmed() == "{") {
            lines.insert(i + 1, entry);
            return lines.join('\n');
        }
    }
    return {};
}

int run() {
    if (QStandardPaths::findExecutable("omarchy-dictation-use").isEmpty()) {
        fail("asmir setup is for Omarchy, and Omarchy's dictation commands aren't on the PATH.\n"
             "Elsewhere, bind keys to `asmir start` and `asmir stop` yourself.");
        return 1;
    }

    const Config config = Config::load();
    if (!QFileInfo::exists(config.modelPath())) {
        const QString name = config.model.isEmpty() ? Config::defaultModelName() : config.model;
        const Model *model = Models::find(name);
        if (!model) {
            fail(QString("The model in %1 (%2) isn't one asmir can download. Fetch it yourself, or remove the line.")
                     .arg(Config::path(), name));
            return 1;
        }
        QString error;
        if (!Models::download(*model, &error)) {
            fail(error);
            return 1;
        }
        say(QString("Downloaded the %1 speech model.").arg(model->name));
    }

    // Omarchy runs omarchy-dictation-<backend>. A package puts asmir's in
    // /usr/bin; a build from source gets it here.
    if (QStandardPaths::findExecutable("omarchy-dictation-asmir").isEmpty()) {
        const QString adapter = QDir::homePath() + "/.local/bin/omarchy-dictation-asmir";
        if (!install(":/omarchy/omarchy-dictation-asmir", adapter, true)) {
            fail(QString("Couldn't write %1.").arg(adapter));
            return 1;
        }
        say(QString("Installed %1.").arg(adapter));
    }

    // Omarchy loads this only while asmir is the dictation backend.
    const QString hook = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/asmir/shortcuts.lua";
    if (!install(":/omarchy/shortcuts.lua", hook, false)) {
        fail(QString("Couldn't write %1.").arg(hook));
        return 1;
    }

    // List asmir in Omarchy's own Setup > Defaults > Dictation menu.
    const QString menu = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                         + "/omarchy/extensions/omarchy-menu.jsonc";
    QFile current(menu);
    const QString before = current.open(QIODevice::ReadOnly) ? QString::fromUtf8(current.readAll()) : QString();
    current.close();
    const QString after = withMenuEntry(before);
    if (after.isEmpty()) {
        say(QString("Left %1 alone: add asmir under Setup > Defaults > Dictation by hand if you like.").arg(menu));
    } else if (after != before) {
        QDir().mkpath(QFileInfo(menu).absolutePath());
        QSaveFile file(menu);
        if (!file.open(QIODevice::WriteOnly) || file.write(after.toUtf8()) < 0 || !file.commit()) {
            fail(QString("Couldn't write %1.").arg(menu));
            return 1;
        }
        say("Added asmir to Setup > Defaults > Dictation.");
    }

    if (runQuietly("omarchy-dictation-use", {"asmir"}) != 0) {
        fail("Couldn't select asmir as the dictation backend.");
        return 1;
    }
    say("asmir is now the dictation backend.");

    // The dictation keys are bound when Hyprland reads its config, and the
    // login hook only runs at login, so reload and start the daemon now.
    if (!qEnvironmentVariableIsEmpty("HYPRLAND_INSTANCE_SIGNATURE")) {
        runQuietly("hyprctl", {"reload"});
        QString ignored;
        if (Client::send("status", &ignored).value("state").toString() == "stopped") {
            const QString self = QCoreApplication::applicationFilePath();
            const QString uwsm = QStandardPaths::findExecutable("uwsm-app");
            QProcess daemon;
            daemon.setProgram(uwsm.isEmpty() ? self : uwsm);
            daemon.setArguments(uwsm.isEmpty() ? QStringList{"serve"} : QStringList{"--", self, "serve"});
            daemon.setStandardOutputFile(QProcess::nullDevice());
            daemon.setStandardErrorFile(QProcess::nullDevice());
            daemon.startDetached();
        }
    }

    say("Hold Right Alt and talk, or press Alt + Space to start and stop.\n"
        "To change it later, pick another backend under Setup > Defaults > Dictation.");
    return 0;
}

}
