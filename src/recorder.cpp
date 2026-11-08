#include "recorder.h"

#include "audio.h"

#include <QProcess>
#include <QStandardPaths>

namespace {

// One waveform bar per this many samples: 33 ms, about 30 bars a second.
constexpr qsizetype LevelSlice = Audio::Rate / 30;

}

Recorder::Recorder(QObject *parent) : QObject(parent) {}

Recorder::~Recorder() {
    stop();
}

QStringList Recorder::arguments() {
    // Raw 16 kHz mono floats on stdout, exactly what whisper takes. A short
    // latency keeps the waveform in step with the voice, and the node name is
    // what volume mixers show while asmir listens.
    return {"--raw", "--rate", "16000", "--channels", "1", "--format", "f32", "--latency", "20ms",
            "-P", "{ node.name = \"asmir\" media.name = \"Dictation\" }", "-"};
}

bool Recorder::start(QString *error) {
    if (m_process)
        return true;

    const QString program = QStandardPaths::findExecutable("pw-record");
    if (program.isEmpty()) {
        if (error)
            *error = tr("Recording needs PipeWire.");
        return false;
    }

    m_remainder.clear();
    m_samples.clear();
    m_samples.reserve(Audio::Rate * 30);
    m_levelFrom = 0;

    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setStandardErrorFile(QProcess::nullDevice());
    connect(m_process, &QProcess::readyReadStandardOutput, this, &Recorder::read);
    m_process->start(program, arguments());
    if (!m_process->waitForStarted(2000)) {
        if (error)
            *error = tr("Couldn't start recording.");
        delete m_process;
        m_process = nullptr;
        return false;
    }
    return true;
}

QVector<float> Recorder::stop() {
    if (!m_process)
        return {};
    // pw-record writes what it still holds when asked to stop, so read after.
    m_process->terminate();
    if (!m_process->waitForFinished(500))
        m_process->kill();
    read();
    m_process->disconnect(this);
    m_process->deleteLater();
    m_process = nullptr;
    QVector<float> samples;
    samples.swap(m_samples);
    return samples;
}

void Recorder::read() {
    if (!m_process)
        return;
    const QByteArray chunk = m_process->readAllStandardOutput();
    if (chunk.isEmpty())
        return;
    Audio::appendFloats(chunk, m_remainder, m_samples);
    while (m_samples.size() - m_levelFrom >= LevelSlice) {
        emit level(Audio::level(m_samples.constData() + m_levelFrom, LevelSlice));
        m_levelFrom += LevelSlice;
    }
}
