#include "audio.h"
#include "config.h"
#include "dictation.h"
#include "models.h"
#include "setup.h"
#include "theme.h"
#include "transcriber.h"
#include "translator.h"

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtEndian>
#include <QtTest>
#include <cmath>

namespace {

QByteArray floats(std::initializer_list<float> values) {
    QByteArray bytes;
    for (float value : values) {
        char raw[sizeof(float)];
        qToLittleEndian(value, raw);
        bytes.append(raw, sizeof(float));
    }
    return bytes;
}

// whisper.cpp's sample is 16-bit, 16 kHz mono PCM: skip to the data chunk.
QVector<float> readPcmWav(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    const qsizetype data = bytes.indexOf("data");
    if (data < 0)
        return {};
    QVector<float> samples;
    for (qsizetype i = data + 8; i + 1 < bytes.size(); i += 2)
        samples.append(qFromLittleEndian<qint16>(bytes.constData() + i) / 32768.0f);
    return samples;
}

// Faint, steady room noise, the same every run.
QVector<float> roomNoise(qsizetype count) {
    QVector<float> samples(count);
    quint32 state = 1;
    for (float &sample : samples) {
        state = state * 1103515245u + 12345u;
        sample = (int((state >> 16) % 2000) - 1000) / 1000.0f * 0.004f;
    }
    return samples;
}

// A key click: a few milliseconds of something loud.
void addClick(QVector<float> &samples, qsizetype at) {
    for (qsizetype i = at; i < at + 200 && i < samples.size(); ++i)
        samples[i] = i % 2 ? 0.3f : -0.3f;
}

}

class AsmirTests : public QObject {
    Q_OBJECT

private slots:
    void floatsSplitAcrossReadsAreKept() {
        // pw-record's reads don't line up with samples; a sample cut in two
        // must come out whole once its second half arrives.
        const QByteArray all = floats({0.25f, -0.5f, 1.0f});
        QByteArray remainder;
        QVector<float> out;
        Audio::appendFloats(all.left(6), remainder, out);
        QCOMPARE(out, QVector<float>({0.25f}));
        QCOMPARE(remainder.size(), 2);
        Audio::appendFloats(all.mid(6), remainder, out);
        QCOMPARE(out, QVector<float>({0.25f, -0.5f, 1.0f}));
        QVERIFY(remainder.isEmpty());
    }

    void levelFollowsLoudness() {
        const QVector<float> silence(533, 0.0f);
        QCOMPARE(Audio::level(silence.constData(), silence.size()), 0.0f);

        QVector<float> room(533), speech(533);
        for (int i = 0; i < 533; ++i) {
            room[i] = 0.0005f * std::sin(i * 0.3f);
            speech[i] = 0.2f * std::sin(i * 0.3f);
        }
        const float quiet = Audio::level(room.constData(), room.size());
        const float loud = Audio::level(speech.constData(), speech.size());
        QVERIFY(quiet < 0.1f);
        QVERIFY(loud > 0.6f);
        QVERIFY(loud <= 1.0f);
    }

    void runawayTranscriptsAreSpotted() {
        QVERIFY(!Audio::looksRunaway("And so my fellow Americans ask not what your country can do for you.", 4));
        QVERIFY(!Audio::looksRunaway("Yes.", 0));
        QVERIFY(Audio::looksRunaway(QString("the the the ").repeated(20), 3));
    }

    void speechIsFoundBetweenTheQuiet() {
        // Two seconds of room, a second of voice, two more of room.
        QVector<float> samples = roomNoise(5 * Audio::Rate);
        for (qsizetype i = 2 * Audio::Rate; i < 3 * Audio::Rate; ++i)
            samples[i] += 0.1f * std::sin(i * 0.05f);
        const Audio::Span span = Audio::speech(samples);
        QVERIFY(!span.isEmpty());
        // The voice and its margin, and none of the rest.
        QVERIFY(span.from <= 2 * Audio::Rate - Audio::Rate / 4);
        QVERIFY(span.from >= Audio::Rate);
        QVERIFY(span.to >= 3 * Audio::Rate + Audio::Rate / 4);
        QVERIFY(span.to <= 4 * Audio::Rate);

        // Speech from the very first sample keeps the start.
        QVector<float> talking(Audio::Rate);
        for (qsizetype i = 0; i < talking.size(); ++i)
            talking[i] = 0.1f * std::sin(i * 0.05f);
        QCOMPARE(Audio::speech(talking).from, qsizetype(0));
        QCOMPARE(Audio::speech(talking).to, talking.size());
    }

    void quietAndClicksAreNotSpeech() {
        QVERIFY(Audio::speech({}).isEmpty());
        QVERIFY(Audio::speech(QVector<float>(3 * Audio::Rate, 0.0f)).isEmpty());
        QVector<float> room = roomNoise(3 * Audio::Rate);
        QVERIFY(Audio::speech(room).isEmpty());
        addClick(room, Audio::Rate / 2);
        addClick(room, 2 * Audio::Rate);
        QVERIFY(Audio::speech(room).isEmpty());
    }

    void transcriptLosesNonSpeechTags() {
        QCOMPARE(Audio::cleanTranscript(" [BLANK_AUDIO]"), QString());
        QCOMPARE(Audio::cleanTranscript(" Hello (typing) there.  *music* "), QString("Hello there."));
        QCOMPARE(Audio::cleanTranscript(" Two\n lines"), QString("Two lines"));
    }

    void configReadsKeysAndIgnoresComments() {
        const Config config = Config::parse(
            "# asmir settings\n"
            "model = small.en-q5_1   # more accurate\n"
            "prompt = \"Omarchy, Hyprland, Neovim.\"\n"
            "threads = 3\n"
            "trailing_space = false\n"
            "nonsense line\n");
        QCOMPARE(config.model, QString("small.en-q5_1"));
        QCOMPARE(config.prompt, QString("Omarchy, Hyprland, Neovim."));
        QCOMPARE(config.threadCount(), 3);
        QVERIFY(!config.trailingSpace);
        QCOMPARE(config.language, QString("en"));
    }

    void modelNamesResolveToFiles() {
        // An empty models directory, so the downloads on this machine don't count.
        QTemporaryDir data;
        const QByteArray savedData = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", data.path().toLocal8Bit());
        auto restore = qScopeGuard([&] {
            if (savedData.isEmpty())
                qunsetenv("XDG_DATA_HOME");
            else
                qputenv("XDG_DATA_HOME", savedData);
        });

        Config config;
        QCOMPARE(QFileInfo(config.modelPath()).fileName(), QString("ggml-tiny.en-q5_1.bin"));
        config.model = "ggml-small.en-q5_1.bin";
        QCOMPARE(QFileInfo(config.modelPath()).fileName(), QString("ggml-small.en-q5_1.bin"));
        config.model = "/opt/models/custom.bin";
        QCOMPARE(config.modelPath(), QString("/opt/models/custom.bin"));

        QVERIFY(Models::find("base.en-q5_1"));
        QVERIFY(Models::find("ggml-small.en-q5_1.bin"));
        QVERIFY(!Models::find("huge"));
        QCOMPARE(Models::find(Config::defaultModelName())->fileName(), QString("ggml-tiny.en-q5_1.bin"));
    }

    void keepsTheOldDefaultUntilTheNewOneIsDownloaded() {
        QTemporaryDir data;
        const QByteArray savedData = qgetenv("XDG_DATA_HOME");
        qputenv("XDG_DATA_HOME", data.path().toLocal8Bit());
        auto restore = qScopeGuard([&] {
            if (savedData.isEmpty())
                qunsetenv("XDG_DATA_HOME");
            else
                qputenv("XDG_DATA_HOME", savedData);
        });
        QDir().mkpath(Config::modelsDirectory());
        auto touch = [](const QString &name) {
            QFile file(Config::modelsDirectory() + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
        };

        // Upgraded from 0.1.1 with only base downloaded: keep using it.
        touch("ggml-base.en-q5_1.bin");
        QCOMPARE(QFileInfo(Config().modelPath()).fileName(), QString("ggml-base.en-q5_1.bin"));
        // Once tiny is there, it wins.
        touch("ggml-tiny.en-q5_1.bin");
        QCOMPARE(QFileInfo(Config().modelPath()).fileName(), QString("ggml-tiny.en-q5_1.bin"));
        // A model set in the config is never swapped.
        Config chosen;
        chosen.model = "small.en-q5_1";
        QCOMPARE(QFileInfo(chosen.modelPath()).fileName(), QString("ggml-small.en-q5_1.bin"));
    }

    void menuEntryJoinsTheUsersExtensions() {
        // The sample file Omarchy ships: comments around an empty object.
        const QString sample = "{\n  // Extend the menu.\n  // \"personal\": {}\n}\n";
        const QString added = Setup::withMenuEntry(sample);
        QVERIFY(added.startsWith("{\n  \"setup.default.dictation.asmir\""));
        QVERIFY(added.contains("// Extend the menu."));
        // Running setup again changes nothing.
        QCOMPARE(Setup::withMenuEntry(added), added);

        // Someone's own entries stay, with asmir ahead of them.
        const QString own = "{\n  \"personal\": {\"label\": \"Mine\"}\n}\n";
        const QString merged = Setup::withMenuEntry(own);
        QVERIFY(merged.indexOf("dictation.asmir") < merged.indexOf("personal"));
        QVERIFY(merged.contains("\"personal\": {\"label\": \"Mine\"}"));

        // No file yet, and a file that isn't laid out as expected.
        QVERIFY(Setup::withMenuEntry({}).contains("dictation.asmir"));
        QVERIFY(Setup::withMenuEntry("{ \"x\": 1 }").isEmpty());
    }

    void themeReadsOmarchyColors() {
        QTemporaryDir directory;
        QDir().mkpath(directory.path() + "/theme");
        QFile colors(directory.path() + "/theme/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly));
        colors.write("accent = \"#8bc9eb\"\nbackground = \"#16242d\"\n");
        colors.close();

        Theme theme(directory.path());
        QCOMPARE(theme.accent(), QString("#8bc9eb"));
        QCOMPARE(theme.background(), QString("#16242d"));
        // A theme without the key keeps the default.
        QCOMPARE(theme.foreground(), QString("#EAEAEA"));
    }

    void everyLanguageTranslatesEveryMessage() {
        // A message added in English must reach every Omarchy language.
        QFile english(":/i18n/en.json");
        QVERIFY(english.open(QIODevice::ReadOnly));
        const QJsonObject source = QJsonDocument::fromJson(english.readAll()).object();
        QVERIFY(!source.isEmpty());
        const QStringList catalogs = QDir(":/i18n").entryList({"*.json"});
        QCOMPARE(catalogs.size(), 50);
        for (const QString &name : catalogs) {
            QFile file(":/i18n/" + name);
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QJsonObject catalog = QJsonDocument::fromJson(file.readAll()).object();
            for (auto it = source.begin(); it != source.end(); ++it)
                QVERIFY2(!catalog.value(it.key()).toString().isEmpty(),
                         qPrintable(QString("%1 lacks \"%2\"").arg(name, it.key())));
        }
    }

    void followsTheOmarchyLanguage() {
        QTemporaryDir directory;
        QFile language(directory.path() + "/language");
        QVERIFY(language.open(QIODevice::WriteOnly));
        language.write("de\n");
        language.close();
        Translator translator(language.fileName());
        QCOMPARE(translator.language(), QString("de"));
        QCOMPARE(translator.translate("Transcriber", "Speech recognition failed."),
                 QString("Die Spracherkennung ist fehlgeschlagen."));
    }

    void daemonAnswersEveryCommand() {
        Dictation dictation;
        // Nothing is recording, so cancel and stop leave it idle.
        for (const QString command : {"cancel", "stop", "status"}) {
            const QJsonObject reply = dictation.handle(command);
            QVERIFY2(reply.value("ok").toBool(), qPrintable(command));
            QCOMPARE(reply.value("state").toString(), QString("idle"));
        }
        QVERIFY(!dictation.handle("dance").value("ok").toBool());
    }

    void missingModelSaysHowToGetIt() {
        Transcriber transcriber;
        QString error;
        QVERIFY(!transcriber.loadNow("/nonexistent/ggml-none.bin", 2, &error));
        QVERIFY(error.contains("asmir setup"));
    }

    void transcribesTheSample() {
        const QString model = Config().modelPath();
        if (!QFileInfo::exists(model))
            QSKIP("The default model isn't downloaded (asmir model download).");
        const QVector<float> audio = readPcmWav(QString(ASMIR_SAMPLES) + "/jfk.wav");
        QVERIFY(audio.size() > 16000 * 10);

        Transcriber transcriber;
        QString error;
        QVERIFY2(transcriber.loadNow(model, Config().threadCount(), &error), qPrintable(error));
        const QString text = transcriber.transcribeNow(audio, "en", {}, &error).toLower();
        QVERIFY2(error.isEmpty(), qPrintable(error));
        QVERIFY2(text.contains("not what your country can do for you"), qPrintable(text));

        // With quiet before and room noise after, whisper still gets it all.
        QVector<float> padded(3 * Audio::Rate, 0.0f);
        padded += audio;
        padded += roomNoise(4 * Audio::Rate);
        const QString paddedText = transcriber.transcribeNow(padded, "en", {}, &error).toLower();
        QVERIFY2(paddedText.contains("ask not what your country can do for you"), qPrintable(paddedText));
        QVERIFY2(paddedText.contains("what you can do for your country"), qPrintable(paddedText));

        // Room noise and a key click are nothing, not made-up words.
        QVector<float> nothing = roomNoise(8 * Audio::Rate);
        addClick(nothing, Audio::Rate / 2);
        QCOMPARE(transcriber.transcribeNow(nothing, "en", {}, &error), QString());
        QVERIFY2(error.isEmpty(), qPrintable(error));
    }
};

QTEST_MAIN(AsmirTests)
#include "asmir_tests.moc"
