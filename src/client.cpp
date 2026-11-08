#include "client.h"

#include "dictation.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QProcess>
#include <QThread>

namespace Client {

namespace {

bool connectTo(QLocalSocket &socket, int timeout) {
    socket.connectToServer(Dictation::socketPath());
    return socket.waitForConnected(timeout);
}

bool launchDaemon(QString *error) {
    QProcess daemon;
    daemon.setProgram(QCoreApplication::applicationFilePath());
    daemon.setArguments({"serve"});
    if (qEnvironmentVariableIsEmpty("ASMIR_DEBUG")) {
        daemon.setStandardOutputFile(QProcess::nullDevice());
        daemon.setStandardErrorFile(QProcess::nullDevice());
    }
    if (!daemon.startDetached()) {
        *error = QString("Couldn't start the asmir daemon: %1").arg(daemon.errorString());
        return false;
    }
    return true;
}

}

QJsonObject send(const QString &command, QString *error) {
    QLocalSocket socket;
    if (!connectTo(socket, 200)) {
        if (command != "start" && command != "toggle")
            return {{"ok", true}, {"state", "stopped"}};
        if (!launchDaemon(error))
            return {};
        // The daemon listens within a few hundred milliseconds; it loads the
        // model in the background, so recording can begin before that's done.
        QElapsedTimer waited;
        waited.start();
        bool connected = false;
        while (waited.elapsed() < 5000 && !(connected = connectTo(socket, 100)))
            QThread::msleep(30);
        if (!connected) {
            *error = "The asmir daemon didn't start. Run `asmir serve` to see why.";
            return {};
        }
    }

    socket.write(command.toUtf8() + "\n");
    socket.flush();
    QByteArray reply;
    QElapsedTimer waited;
    waited.start();
    while (!reply.contains('\n') && waited.elapsed() < 3000) {
        if (socket.waitForReadyRead(100))
            reply += socket.readAll();
        else if (socket.state() != QLocalSocket::ConnectedState)
            break;
    }
    reply += socket.readAll();
    const QJsonObject object = QJsonDocument::fromJson(reply.trimmed()).object();
    if (object.isEmpty())
        *error = "The asmir daemon didn't answer.";
    return object;
}

}
