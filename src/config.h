#pragma once

#include <QString>

// Settings from ~/.config/asmir/config, one `key = value` per line, with `#`
// starting a comment. Every key is optional; a missing file means defaults.
struct Config {
    QString model;              // a file path, or a name in the models directory
    QString language = "en";    // "auto" lets a multilingual model guess
    QString prompt;             // vocabulary and style hints for whisper
    int threads = 0;            // 0 picks a good number for this machine (up to 8)
    bool trailingSpace = true;  // so back-to-back dictations don't run together

    static QString path();
    static QString modelsDirectory();
    static QString defaultModelName();
    static Config load(const QString &file = path());
    static Config parse(const QString &text);

    // The model file to load: model as given when it is a path, otherwise the
    // file of that name (with or without ggml- and .bin) in the models directory.
    QString modelPath() const;
    int threadCount() const;
};
