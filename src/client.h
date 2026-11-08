#pragma once

#include <QJsonObject>
#include <QString>

namespace Client {

// Sends one command to the running daemon and returns its reply. start and
// toggle launch the daemon first when it isn't running; stop and status
// answer for it ({"state": "stopped"}) without launching anything.
QJsonObject send(const QString &command, QString *error);

}
