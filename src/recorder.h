#pragma once

#include <QByteArray>
#include <QObject>
#include <QVector>

class QProcess;

// Records the default microphone as 16 kHz mono floats for whisper, and
// reports how loud each slice is so the overlay can draw the waveform.
//
// It records through PipeWire's pw-record rather than Qt Multimedia: a new
// pw-record stream always follows the microphone that's the default right
// now (a headset plugged in after login included), and asmir stays small.
class Recorder : public QObject {
    Q_OBJECT

public:
    explicit Recorder(QObject *parent = nullptr);
    ~Recorder() override;

    // False, with a message in error, when recording can't start.
    bool start(QString *error = nullptr);
    QVector<float> stop();
    bool recording() const { return m_process != nullptr; }

    // The pw-record command line, for tests and `asmir serve` diagnostics.
    static QStringList arguments();

signals:
    // About 30 times a second while recording, from 0 to 1.
    void level(float value);
    // Once per recording, when the first real sound arrives. A microphone that
    // PipeWire suspended after a few idle seconds delivers silence, or nothing,
    // for a moment while it wakes; words spoken before this are lost.
    void heard();

private:
    void read();

    QProcess *m_process = nullptr;
    QByteArray m_remainder;
    QVector<float> m_samples;
    qsizetype m_levelFrom = 0;
    bool m_heard = false;
};
