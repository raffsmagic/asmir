-- Loaded by Omarchy while asmir is the dictation backend
-- (default/hypr/dictation-backend.lua). Starting the daemon at login keeps the
-- speech model loaded, so the first Right Alt press records at once.
o.launch_on_start("asmir serve")
