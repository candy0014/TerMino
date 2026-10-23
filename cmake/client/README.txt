TerMino / Linux and Windows clients / 本地候选包

Terminal: ./Play.sh on Linux, Play.bat on Windows.
Graphics: ./PlayGL.sh on Linux, PlayGL.bat on Windows (combined packages).
Server: ./Server.sh on Linux, Server.bat on Windows. Pass --help for options.
Combined packages include terminal, OpenGL, tetr_ft7 and server. Separate
frontend packages remain available; their Play launcher starts that frontend.
The graphical client includes its fonts and licenses. OpenGL 3.3 Core and a
working system GPU/window driver are required. All game flows use the keyboard.

Extract the entire archive into a writable folder. Launchers use this folder's
data, independent of the invoking working directory. Fresh packages contain
an empty .tetr/replay directory plus the approved initial library of 24 maps
and 4 presets, including 下棋模式. Player settings use built-in defaults.
Never delete an existing .tetr when upgrading. Back it up and replace program
and resource files only. Start the graphical client repeatedly for independent
windows sharing scores and recordings. The first window saves configuration;
additional windows use temporary settings and do not overwrite profiles or
keybindings. Maps, presets and challenge records are shared with locked saves.
After the settings owner exits, a newly opened window may save configuration;
existing temporary windows keep their temporary settings. Terminal clients keep
exclusive directory access. Use tetr_gl --data-dir DIR for fully separate data.
The OS releases locks on exit; do not delete a live lock.

Terminal and OpenGL use the same native platform profiles, maps, presets,
challenge history and recordings. Maps and presets must be moved together.
Player settings, platform bindings and graphical window settings share
.tetr/config.json. Existing INI profiles are imported automatically;
--migrate-config imports settings without starting a game. See --help for
--window-size, --frame-limit and --vsync / --no-vsync.

BUILD.json identifies the source and compiler. PACKAGE.json lists SHA256 of
every file; the archive has its own .sha256 file. All platform packages must use the same source snapshot. Candidate packages do not establish native IME,
device, DPI or GPU acceptance, and are not a GitHub release.

Linux dependencies depend on the build (see BUILD.json and PACKAGE.json).
OpenGL drivers remain system-owned. The terminal also needs a terminal and
readable keyboard input devices on Linux; graphics does not require evdev.
WSS requires system certificate trust. Only the explicitly approved initial
map/preset libraries are bundled. No personal profiles, keys, scores,
recordings or server room history are copied into packages.
