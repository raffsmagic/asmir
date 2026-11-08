#include "config.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>

QString Config::path() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + "/asmir/config";
}

QString Config::modelsDirectory() {
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/asmir/models";
}

QString Config::defaultModelName() {
    return "base.en-q5_1";
}

Config Config::load(const QString &file) {
    QFile input(file);
    if (!input.open(QIODevice::ReadOnly))
        return {};
    return parse(QString::fromUtf8(input.readAll()));
}

Config Config::parse(const QString &text) {
    Config config;
    for (QString line : text.split('\n')) {
        const int comment = line.indexOf('#');
        if (comment >= 0)
            line.truncate(comment);
        const int equals = line.indexOf('=');
        if (equals < 0)
            continue;
        const QString key = line.left(equals).trimmed().toLower();
        QString value = line.mid(equals + 1).trimmed();
        if (value.size() >= 2 && (value.startsWith('"') && value.endsWith('"')))
            value = value.mid(1, value.size() - 2);

        if (key == "model")
            config.model = value;
        else if (key == "language")
            config.language = value.isEmpty() ? "en" : value;
        else if (key == "prompt")
            config.prompt = value;
        else if (key == "threads")
            config.threads = std::max(0, value.toInt());
        else if (key == "trailing_space")
            config.trailingSpace = !(value == "false" || value == "no" || value == "0");
    }
    return config;
}

QString Config::modelPath() const {
    const QString name = model.isEmpty() ? defaultModelName() : model;
    if (name.contains('/'))
        return QDir::cleanPath(name.startsWith("~/") ? QDir::homePath() + name.mid(1) : name);
    QString file = name;
    if (!file.startsWith("ggml-"))
        file.prepend("ggml-");
    if (!file.endsWith(".bin"))
        file.append(".bin");
    return modelsDirectory() + "/" + file;
}

int Config::threadCount() const {
    if (threads > 0)
        return threads;
    // Half the hardware threads, up to eight: past that, more threads barely
    // shorten a dictation while the rest of the desktop slows down.
    return std::clamp(QThread::idealThreadCount() / 2, 1, 8);
}
