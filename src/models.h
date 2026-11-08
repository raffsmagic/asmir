#pragma once

#include <QList>
#include <QString>

// The speech models asmir knows how to fetch, from whisper.cpp's own
// repository on Hugging Face, each checked against the SHA-256 listed there.
struct Model {
    QString name;  // e.g. "base.en-q5_1"
    qint64 bytes;
    QString sha256;

    QString fileName() const { return "ggml-" + name + ".bin"; }
    QString url() const;
};

namespace Models {

const QList<Model> &known();
const Model *find(const QString &name);

// Downloads the model into the models directory, printing progress to stderr.
// Returns false with a message in error when it fails or the checksum differs.
bool download(const Model &model, QString *error);

}
