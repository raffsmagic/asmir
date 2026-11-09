#include "models.h"

#include "config.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QSaveFile>
#include <cstdio>

QString Model::url() const {
    return "https://huggingface.co/ggerganov/whisper.cpp/resolve/main/" + fileName();
}

namespace Models {

const QList<Model> &known() {
    // q5_1 is the quantization asmir is built around: a third of the size of
    // the full model with nearly the same accuracy. tiny.en is the default,
    // because snappy matters most in dictation; base.en and small.en are
    // slower and more accurate.
    static const QList<Model> models{
        {"tiny.en-q5_1", 32166155, "c77c5766f1cef09b6b7d47f21b546cbddd4157886b3b5d6d4f709e91e66c7c2b"},
        {"base.en-q5_1", 59721011, "4baf70dd0d7c4247ba2b81fafd9c01005ac77c2f9ef064e00dcf195d0e2fdd2f"},
        {"base.en-q8_0", 81781811, "a4d4a0768075e13cfd7e19df3ae2dbc4a68d37d36a7dad45e8410c9a34f8c87e"},
        {"small.en-q5_1", 190098681, "bfdff4894dcb76bbf647d56263ea2a96645423f1669176f4844a1bf8e478ad30"},
        {"small-q5_1", 190085487, "ae85e4a935d7a567bd102fe55afc16bb595bdb618e11b2fc7591bc08120411bb"},
    };
    return models;
}

const Model *find(const QString &name) {
    QString bare = name;
    if (bare.startsWith("ggml-"))
        bare = bare.mid(5);
    if (bare.endsWith(".bin"))
        bare.chop(4);
    for (const auto &model : known())
        if (model.name == bare)
            return &model;
    return nullptr;
}

bool download(const Model &model, QString *error) {
    const QString directory = Config::modelsDirectory();
    QDir().mkpath(directory);
    const QString target = directory + "/" + model.fileName();

    // QSaveFile writes beside the target and only renames over it on commit,
    // so a failed or tampered download never replaces a good model.
    QSaveFile file(target);
    if (!file.open(QIODevice::WriteOnly)) {
        *error = QString("Can't write %1: %2").arg(target, file.errorString());
        return false;
    }

    QNetworkAccessManager network;
    QNetworkRequest request{QUrl(model.url())};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    QNetworkReply *reply = network.get(request);

    QCryptographicHash hash(QCryptographicHash::Sha256);
    qint64 received = 0;
    int lastPercent = -1;
    QObject::connect(reply, &QNetworkReply::readyRead, [&] {
        const QByteArray chunk = reply->readAll();
        hash.addData(chunk);
        file.write(chunk);
        received += chunk.size();
        const int percent = int(received * 100 / std::max<qint64>(1, model.bytes));
        if (percent != lastPercent) {
            lastPercent = percent;
            std::fprintf(stderr, "\rDownloading %s: %d%%", qPrintable(model.name), std::min(percent, 100));
        }
    });
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    std::fprintf(stderr, "\n");

    const QByteArray rest = reply->readAll();
    hash.addData(rest);
    file.write(rest);
    if (reply->error() != QNetworkReply::NoError) {
        *error = QString("Download failed: %1").arg(reply->errorString());
        file.cancelWriting();
        reply->deleteLater();
        return false;
    }
    reply->deleteLater();

    if (QString::fromLatin1(hash.result().toHex()) != model.sha256) {
        *error = "The download doesn't match the model's checksum, so it wasn't kept.";
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        *error = QString("Can't save %1: %2").arg(target, file.errorString());
        return false;
    }
    return true;
}

}
