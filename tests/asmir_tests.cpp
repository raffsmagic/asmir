#include "audio.h"
#include "config.h"
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
        Config config;
        QCOMPARE(QFileInfo(config.modelPath()).fileName(), QString("ggml-base.en-q5_1.bin"));
        config.model = "ggml-small.en-q5_1.bin";
        QCOMPARE(QFileInfo(config.modelPath()).fileName(), QString("ggml-small.en-q5_1.bin"));
        config.model = "/opt/models/custom.bin";
        QCOMPARE(config.modelPath(), QString("/opt/models/custom.bin"));

        QVERIFY(Models::find("base.en-q5_1"));
        QVERIFY(Models::find("ggml-small.en-q5_1.bin"));
        QVERIFY(!Models::find("huge"));
        QCOMPARE(Models::find(Config::defaultModelName())->fileName(), QString("ggml-base.en-q5_1.bin"));
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
    }
};

QTEST_MAIN(AsmirTests)
#include "asmir_tests.moc"
