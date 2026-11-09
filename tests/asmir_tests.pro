QT += core gui qml quick network testlib

CONFIG += c++17 testcase
TARGET = asmir_tests
TEMPLATE = app

WHISPER = $$PWD/../third_party/whisper.cpp
WHISPER_BUILD = $$PWD/../build/whisper
INCLUDEPATH += $$PWD/../src $$WHISPER/include $$WHISPER/ggml/include
LIBS += -L$$WHISPER_BUILD/src -lwhisper \
        -L$$WHISPER_BUILD/ggml/src -lggml -lggml-cpu -lggml-base -lpthread -ldl
DEFINES += ASMIR_SAMPLES=\\\"$$WHISPER/samples\\\"

HEADERS += \
    ../src/audio.h \
    ../src/config.h \
    ../src/models.h \
    ../src/setup.h \
    ../src/client.h \
    ../src/dictation.h \
    ../src/recorder.h \
    ../src/theme.h \
    ../src/transcriber.h \
    ../src/translator.h

SOURCES += \
    asmir_tests.cpp \
    ../src/audio.cpp \
    ../src/config.cpp \
    ../src/models.cpp \
    ../src/setup.cpp \
    ../src/client.cpp \
    ../src/dictation.cpp \
    ../src/recorder.cpp \
    ../src/theme.cpp \
    ../src/transcriber.cpp \
    ../src/translator.cpp

RESOURCES += ../src/resources.qrc
