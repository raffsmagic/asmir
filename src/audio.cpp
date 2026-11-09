#include "audio.h"

#include <QRegularExpression>
#include <QtEndian>
#include <algorithm>
#include <cmath>

namespace Audio {

void appendFloats(const QByteArray &chunk, QByteArray &remainder, QVector<float> &out) {
    QByteArray bytes = remainder + chunk;
    const qsizetype count = bytes.size() / qsizetype(sizeof(float));
    out.reserve(out.size() + count);
    for (qsizetype i = 0; i < count; ++i)
        out.append(qFromLittleEndian<float>(bytes.constData() + i * sizeof(float)));
    remainder = bytes.mid(count * qsizetype(sizeof(float)));
}

float level(const float *samples, qsizetype count) {
    if (count <= 0)
        return 0.0f;
    double squares = 0.0;
    for (qsizetype i = 0; i < count; ++i)
        squares += double(samples[i]) * samples[i];
    const double rms = std::sqrt(squares / count);
    if (rms <= 0.0)
        return 0.0f;
    // -55 dB is a quiet room, -10 dB is speech close to the microphone.
    const double decibels = 20.0 * std::log10(rms);
    return float(std::clamp((decibels + 55.0) / 45.0, 0.0, 1.0));
}

float peak(const QVector<float> &samples) {
    float loudest = 0.0f;
    for (float sample : samples)
        loudest = std::max(loudest, std::abs(sample));
    return loudest;
}

bool looksRunaway(const QString &text, int seconds) {
    const qsizetype words = text.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts).size();
    return words > 5 * qMax(1, seconds) + 8;
}

QString cleanTranscript(const QString &text) {
    static const QRegularExpression tags(R"(\[[^\]]*\]|\([^)]*\)|\*[^*]*\*)");
    static const QRegularExpression spaces(R"(\s+)");
    QString cleaned = text;
    cleaned.replace(tags, " ");
    cleaned.replace(spaces, " ");
    return cleaned.trimmed();
}

}
