#include "dictation.h"

#include "audio.h"
#include "transcriber.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcess>
#include <QStandardPaths>
#include <unistd.h>

namespace {

// A tap shorter than this is a mis-press, not speech.
constexpr qsizetype ShortestRecording = Audio::Rate / 4;
// Below this the microphone heard nothing at all (muted or unplugged).
constexpr float SilentPeak = 0.002f;
// A recording left running (Alt + Space pressed and forgotten) stops itself
// and pastes, rather than listening and filling memory all day.
constexpr int LongestRecordingMs = 5 * 60 * 1000;

}

Dictation::Dictation(QObject *parent) : QObject(parent), m_config(Config::load()) {
    for (int i = 0; i < Bars; ++i)
        m_levels.append(0.0);

    m_messageTimer.setSingleShot(true);
    m_messageTimer.setInterval(3500);
    connect(&m_messageTimer, &QTimer::timeout, this, [this] {
        m_message.clear();
        emit messageChanged();
        updateIdleState();
    });

    // Editors save in bursts (a temporary file, a rename, a write), so wait
    // for the dust to settle before reading the config again.
    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(200);
    connect(&m_reloadTimer, &QTimer::timeout, this, &Dictation::reloadConfig);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadTimer, qOverload<>(&QTimer::start));

    m_limitTimer.setSingleShot(true);
    m_limitTimer.setInterval(LongestRecordingMs);
    connect(&m_limitTimer, &QTimer::timeout, this, &Dictation::stop);

    connect(&m_recorder, &Recorder::level, this, &Dictation::pushLevel);
    connect(&m_recorder, &Recorder::heard, this, [this] {
        m_waking = false;
        emit wakingChanged();
    });

    m_transcriber = new Transcriber;
    m_transcriber->moveToThread(&m_worker);
    connect(&m_worker, &QThread::finished, m_transcriber, &QObject::deleteLater);
    connect(this, &Dictation::requestLoad, m_transcriber, &Transcriber::load);
    connect(this, &Dictation::requestTranscription, m_transcriber, &Transcriber::transcribe);
    connect(m_transcriber, &Transcriber::loaded, this, [this](bool ok, const QString &error) {
        // A later config change already asked for another model.
        if (--m_loading > 0)
            return;
        m_modelFailed = !ok;
        m_modelError = error;
        if (!ok)
            showMessage(error);
    });
    connect(m_transcriber, &Transcriber::finished, this, &Dictation::transcribed);
    connect(m_transcriber, &Transcriber::failed, this, &Dictation::transcriptionFailed);
    m_worker.start();
}

Dictation::~Dictation() {
    m_worker.quit();
    m_worker.wait();
    if (m_server)
        m_server->close();
}

QString Dictation::socketPath() {
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        runtime = QDir::tempPath() + QString("/asmir-%1").arg(getuid());
    QDir().mkpath(runtime);
    return runtime + "/asmir.sock";
}

bool Dictation::listen(QString *error) {
    const QString path = socketPath();

    // A live daemon answers; a socket file left by one that crashed doesn't.
    QLocalSocket probe;
    probe.connectToServer(path);
    if (probe.waitForConnected(200)) {
        if (error)
            *error = "asmir is already running.";
        return false;
    }
    QLocalServer::removeServer(path);

    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_server->listen(path)) {
        if (error)
            *error = m_server->errorString();
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            connect(socket, &QLocalSocket::readyRead, socket, [this, socket] {
                if (!socket->canReadLine())
                    return;
                const QString command = QString::fromUtf8(socket->readLine()).trimmed();
                socket->write(QJsonDocument(handle(command)).toJson(QJsonDocument::Compact) + "\n");
                socket->flush();
                socket->disconnectFromServer();
            });
        }
    });

    loadModel();
    watch();
    return true;
}

void Dictation::loadModel() {
    m_requestedModel = m_config.modelPath();
    m_requestedThreads = m_config.threadCount();
    ++m_loading;
    emit requestLoad(m_requestedModel, m_requestedThreads);
}

void Dictation::watch() {
    // The config file may not exist yet, and editors and downloads replace
    // files rather than change them, which drops them from the watcher: watch
    // both folders, and the file whenever it's there.
    const QString config = Config::path();
    const QStringList folders{QFileInfo(config).absolutePath(), Config::modelsDirectory()};
    for (const QString &folder : folders) {
        QDir().mkpath(folder);
        if (!m_watcher.directories().contains(folder))
            m_watcher.addPath(folder);
    }
    if (QFileInfo::exists(config) && !m_watcher.files().contains(config))
        m_watcher.addPath(config);
}

void Dictation::reloadConfig() {
    watch();
    m_config = Config::load();
    // Language, prompt and spacing apply to the next dictation as they are.
    // A new model or thread count needs loading; so does a model that was
    // missing and has just been downloaded.
    const bool changed = m_config.modelPath() != m_requestedModel || m_config.threadCount() != m_requestedThreads;
    const bool arrived = m_modelFailed && QFileInfo::exists(m_config.modelPath());
    if (changed || arrived)
        loadModel();
}

QJsonObject Dictation::handle(const QString &command) {
    if (command == "start")
        start();
    else if (command == "stop")
        stop();
    else if (command == "toggle")
        toggle();
    else if (command == "cancel")
        cancel();
    else if (command == "quit")
        QMetaObject::invokeMethod(qApp, &QCoreApplication::quit, Qt::QueuedConnection);
    else if (command != "status")
        return {{"ok", false}, {"error", QString("unknown command: %1").arg(command)}};

    QJsonObject reply{{"ok", true}, {"state", m_state}, {"model", m_config.modelPath()}};
    if (m_state == "recording")
        reply.insert("waking", m_waking);
    if (m_modelFailed)
        reply.insert("error", m_modelError);
    return reply;
}

void Dictation::start() {
    if (m_recorder.recording())
        return;
    if (m_modelFailed) {
        showMessage(m_modelError);
        return;
    }
    QString error;
    if (!m_recorder.start(&error)) {
        showMessage(error);
        return;
    }
    for (auto &bar : m_levels)
        bar = 0.0;
    emit levelsChanged();
    m_waking = true;
    emit wakingChanged();
    m_limitTimer.start();
    setState("recording");
}

void Dictation::stop() {
    if (!m_recorder.recording())
        return;
    m_limitTimer.stop();
    const QVector<float> audio = m_recorder.stop();
    if (m_waking) {
        m_waking = false;
        emit wakingChanged();
    }
    if (!qEnvironmentVariableIsEmpty("ASMIR_DEBUG"))
        qInfo("recorded %.2f s, peak %.4f", audio.size() / double(Audio::Rate), Audio::peak(audio));
    if (audio.size() < ShortestRecording || Audio::peak(audio) < SilentPeak) {
        updateIdleState();
        return;
    }
    ++m_pending;
    setState("transcribing");
    emit requestTranscription(m_nextId++, audio, m_config.language, m_config.prompt);
}

void Dictation::cancel() {
    if (!m_recorder.recording())
        return;
    m_limitTimer.stop();
    m_recorder.stop();
    if (m_waking) {
        m_waking = false;
        emit wakingChanged();
    }
    updateIdleState();
}

void Dictation::toggle() {
    if (m_recorder.recording())
        stop();
    else
        start();
}

void Dictation::transcribed(quint64, const QString &text, int milliseconds) {
    --m_pending;
    if (!qEnvironmentVariableIsEmpty("ASMIR_DEBUG"))
        qInfo("transcribed in %d ms: %s", milliseconds, qPrintable(text));
    if (!text.isEmpty())
        paste(m_config.trailingSpace ? text + ' ' : text);
    updateIdleState();
}

void Dictation::transcriptionFailed(quint64, const QString &error) {
    --m_pending;
    showMessage(error);
}

void Dictation::paste(const QString &text) {
    // For trying the daemon without typing into whatever has focus.
    if (!qEnvironmentVariableIsEmpty("ASMIR_DRY_RUN")) {
        qInfo("would paste: %s", qPrintable(text));
        return;
    }
    // Omarchy's paste copies to the clipboard and sends the paste key that
    // suits the focused window: Ctrl+Shift+V in terminals, Ctrl+V elsewhere.
    // The text arrives in one piece, whatever the keyboard layout.
    const QString omarchyPaste = QStandardPaths::findExecutable("omarchy-clipboard-paste-text");
    if (!omarchyPaste.isEmpty()) {
        QProcess::startDetached(omarchyPaste, {"--universal", text});
        return;
    }
    // Without Omarchy, leave it on the clipboard and say so.
    QProcess copy;
    copy.start("wl-copy", {});
    if (copy.waitForStarted(1000)) {
        copy.write(text.toUtf8());
        copy.closeWriteChannel();
        copy.waitForFinished(2000);
        showMessage(tr("Copied to the clipboard."));
    } else {
        showMessage(tr("Couldn't paste the text."));
    }
}

void Dictation::setState(const QString &state) {
    if (state == m_state)
        return;
    m_state = state;
    emit stateChanged();
}

void Dictation::updateIdleState() {
    if (m_recorder.recording())
        setState("recording");
    else if (m_pending > 0)
        setState("transcribing");
    else if (!m_message.isEmpty())
        setState("message");
    else
        setState("idle");
}

void Dictation::showMessage(const QString &message) {
    m_message = message;
    emit messageChanged();
    m_messageTimer.start();
    updateIdleState();
}

void Dictation::pushLevel(float value) {
    m_levels.removeFirst();
    m_levels.append(value);
    emit levelsChanged();
}
