#pragma once

// `asmir setup`: makes asmir Omarchy's dictation backend in one step, the way
// `omazed setup` does for Zed. It fetches the default model when it's missing,
// puts the omarchy-dictation-asmir adapter on the PATH when nothing provides
// it yet, installs the login hook that starts the daemon, picks asmir under
// Setup > Defaults > Dictation, and reloads Hyprland so the keys follow.
#include <QString>

namespace Setup {

int run();

// The user's Omarchy menu extension with asmir listed under
// Setup > Defaults > Dictation, beside the backends Omarchy ships. Returns the
// text unchanged when asmir is already there, and an empty string when the
// file doesn't look like one asmir can safely add to.
QString withMenuEntry(const QString &existing);

}
