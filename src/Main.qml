import QtQuick
import QtQuick.Window

// The overlay: a small strip at the bottom of the screen that only exists
// while dictating. It is a layer-shell surface (set up in main.cpp) that takes
// no keyboard focus and no clicks, so the paste lands in the window you were
// typing in.
Window {
    id: overlay

    readonly property bool showingMessage: dictation.state === "message"
    readonly property int barWidth: 3
    readonly property int barGap: 3
    readonly property int padding: 14
    readonly property int barsWidth: dictation.levels.length * (barWidth + barGap) - barGap

    width: showingMessage ? Math.min(560, messageText.implicitWidth + padding * 2) : barsWidth + padding * 2
    height: 40
    visible: dictation.state !== "idle"
    color: "transparent"
    flags: Qt.FramelessWindowHint | Qt.WindowTransparentForInput
    title: "asmir"

    // Drives the waiting wave while whisper works.
    property real phase: 0
    NumberAnimation on phase {
        from: 0
        to: 1
        duration: 1100
        loops: Animation.Infinite
        running: dictation.state === "transcribing" || dictation.waking
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.alpha(theme.background, 0.94)
        border.color: Qt.alpha(theme.accent, 0.7)
        border.width: 2
        radius: theme.radius

        Row {
            anchors.centerIn: parent
            spacing: overlay.barGap
            visible: !overlay.showingMessage

            Repeater {
                model: dictation.levels.length

                Rectangle {
                    required property int index
                    readonly property real live: dictation.levels[index]
                    readonly property real wave: 0.5 + 0.5 * Math.sin((overlay.phase + index / 16) * 2 * Math.PI)
                    // While the microphone wakes up a faint glint runs along
                    // flat bars: talking now would be lost. The bars come
                    // alive with the first real sound.
                    readonly property real glint: Math.max(0, 1 - Math.abs(index / dictation.levels.length - overlay.phase) * 8)

                    width: overlay.barWidth
                    height: 3 + (dictation.state === "transcribing" ? wave * 9 : dictation.waking ? glint * 3 : live * 24)
                    anchors.verticalCenter: parent.verticalCenter
                    radius: Math.min(1.5, theme.radius)
                    color: theme.accent
                    opacity: dictation.state === "transcribing" ? 0.45 + wave * 0.4
                           : dictation.waking ? 0.2 + glint * 0.5
                           : 0.35 + live * 0.65

                    Behavior on height {
                        enabled: dictation.state === "recording"
                        NumberAnimation { duration: 70 }
                    }
                }
            }
        }

        Text {
            id: messageText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, 560 - overlay.padding * 2)
            visible: overlay.showingMessage
            text: dictation.message
            color: theme.foreground
            elide: Text.ElideRight
            // "monospace" is the font picked under Omarchy's Style > Font.
            font.family: "monospace"
            font.pixelSize: 13
        }
    }
}
