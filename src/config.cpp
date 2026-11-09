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
    return "tiny.en-q5_1";
}

namespace {

// The default before 0.1.2. An install that downloaded it and never set a
// model keeps using it until the new default is downloaded.
const char *PreviousDefaultModel = "base.en-q5_1";

QString fileInModelsDirectory(QString name) {
    if (!name.startsWith("ggml-"))
        name.prepend("ggml-");
    if (!name.endsWith(".bin"))
        name.append(".bin");
    return Config::modelsDirectory() + "/" + name;
}

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
    if (model.isEmpty()) {
        const QString current = fileInModelsDirectory(defaultModelName());
        const QString previous = fileInModelsDirectory(PreviousDefaultModel);
        return !QFile::exists(current) && QFile::exists(previous) ? previous : current;
    }
    if (model.contains('/'))
        return QDir::cleanPath(model.startsWith("~/") ? QDir::homePath() + model.mid(1) : model);
    return fileInModelsDirectory(model);
}

int Config::threadCount() const {
    if (threads > 0)
        return threads;
    // Half the hardware threads, up to eight: past that, more threads barely
    // shorten a dictation while the rest of the desktop slows down.
    return std::clamp(QThread::idealThreadCount() / 2, 1, 8);
}
