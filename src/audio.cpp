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

Span speech(const QVector<float> &samples) {
    // Loudness in 20 ms frames.
    constexpr qsizetype Frame = Rate / 50;
    const qsizetype frames = samples.size() / Frame;
    if (frames == 0)
        return {};
    QVector<float> loudness(frames);
    for (qsizetype f = 0; f < frames; ++f) {
        double squares = 0.0;
        for (qsizetype i = f * Frame; i < (f + 1) * Frame; ++i)
            squares += double(samples[i]) * samples[i];
        loudness[f] = float(std::sqrt(squares / Frame));
    }

    // The room's own level is what the quietest tenth of the recording
    // reaches, and speech is clearly above it (10 dB). Between -54 dB, which
    // even a quiet voice clears, and -40 dB, which is always a voice: a
    // recording with no pause in it would otherwise measure its "room" from
    // the speech and throw all of it away.
    QVector<float> sorted = loudness;
    std::nth_element(sorted.begin(), sorted.begin() + frames / 10, sorted.end());
    const float threshold = std::clamp(sorted[frames / 10] * 3.2f, 0.002f, 0.01f);

    qsizetype first = -1, last = -1, voiced = 0;
    for (qsizetype f = 0; f < frames; ++f) {
        if (loudness[f] < threshold)
            continue;
        if (first < 0)
            first = f;
        last = f;
        ++voiced;
    }
    // The shortest word is about a tenth of a second of sound; a click or a
    // bump of the desk is a frame or two.
    if (voiced < 5)
        return {};

    // whisper hears the start and end of words better with some quiet around
    // them.
    constexpr qsizetype Margin = Rate * 4 / 10;
    return {std::max<qsizetype>(0, first * Frame - Margin), std::min(samples.size(), (last + 1) * Frame + Margin)};
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
