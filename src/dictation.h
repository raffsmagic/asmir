#pragma once

#include "config.h"
#include "recorder.h"

#include <QJsonObject>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>

class QLocalServer;
class Transcriber;

// The daemon behind `asmir serve`: takes start, stop and toggle over a local
// socket, records while asked, hands the audio to whisper, and pastes the text
// into whatever has focus. The overlay binds to its properties.
class Dictation : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY stateChanged)
    Q_PROPERTY(QVariantList levels READ levels NOTIFY levelsChanged)
    Q_PROPERTY(QString message READ message NOTIFY messageChanged)

public:
    // Waveform bars in the overlay.
    static constexpr int Bars = 32;

    explicit Dictation(QObject *parent = nullptr);
    ~Dictation() override;

    // Listens on the socket and starts loading the model. False when another
    // asmir already listens.
    bool listen(QString *error = nullptr);

    QString state() const { return m_state; }
    QVariantList levels() const { return m_levels; }
    QString message() const { return m_message; }

    static QString socketPath();

    // Answers one command line, as sent by `asmir <command>`.
    QJsonObject handle(const QString &command);

public slots:
    void start();
    void stop();
    void toggle();

signals:
    void stateChanged();
    void levelsChanged();
    void messageChanged();
    void requestLoad(const QString &modelPath, int threads);
    void requestTranscription(quint64 id, const QVector<float> &audio, const QString &language, const QString &prompt);

private:
    void setState(const QString &state);
    void showMessage(const QString &message);
    void pushLevel(float value);
    void transcribed(quint64 id, const QString &text, int milliseconds);
    void transcriptionFailed(quint64 id, const QString &error);
    void paste(const QString &text);
    void updateIdleState();

    Config m_config;
    Recorder m_recorder;
    QThread m_worker;
    Transcriber *m_transcriber = nullptr;
    QLocalServer *m_server = nullptr;
    QString m_state = "idle";
    QVariantList m_levels;
    QString m_message;
    QTimer m_messageTimer;
    quint64 m_nextId = 1;
    int m_pending = 0;
    bool m_modelFailed = false;
    QString m_modelError;
};
