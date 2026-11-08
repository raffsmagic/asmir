#pragma once

#include <QObject>
#include <QString>
#include <QVector>

struct whisper_context;

// Runs whisper.cpp. It lives on its own thread, keeps the model loaded between
// dictations so each one starts decoding at once, and handles requests one at
// a time in the order they arrive.
class Transcriber : public QObject {
    Q_OBJECT

public:
    explicit Transcriber(QObject *parent = nullptr);
    ~Transcriber() override;

    // Synchronous, for the command line and tests.
    bool loadNow(const QString &modelPath, int threads, QString *error = nullptr);
    QString transcribeNow(const QVector<float> &audio, const QString &language, const QString &prompt,
                          QString *error = nullptr);

public slots:
    void load(const QString &modelPath, int threads);
    void transcribe(quint64 id, const QVector<float> &audio, const QString &language, const QString &prompt);

signals:
    void loaded(bool ok, const QString &error);
    void finished(quint64 id, const QString &text, int milliseconds);
    void failed(quint64 id, const QString &error);

private:
    whisper_context *m_context = nullptr;
    int m_threads = 4;
    QString m_modelPath;
};
