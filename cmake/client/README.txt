TerMino / Linux and Windows clients / 本地候选包

Start: ./Play.sh on Linux, Play.bat on Windows. Pass --help for options.
The terminal package includes tetr_ft7; Server starts the relay when included.
The OpenGL package includes its fonts and licenses. OpenGL 3.3 Core and a
working system GPU/window driver are required. All game flows use the keyboard.

Extract the entire archive into a writable folder. Launchers use this folder's
data, independent of the invoking working directory. Fresh packages contain
only empty .tetr/replay directories; ALL settings use built-in defaults.
Never delete an existing .tetr when upgrading. Back it up and replace program
and resource files only. Two clients cannot write the same runtime directory
at once; use separate extracted folders or tetr_gl --data-dir DIR for another
instance. The OS releases the client.lock on exit; do not delete a live lock.

Terminal and OpenGL use the same native platform profiles, maps, presets,
challenge history and recordings. Maps and presets must be moved together.
Graphical window settings are separate in .tetr/graphics.ini. See --help for
--window-size, --frame-limit and --vsync / --no-vsync.

BUILD.json identifies the source and compiler. PACKAGE.json lists SHA256 of
every file; the archive has its own .sha256 file. All four candidates must use
the same source snapshot. Candidate packages do not establish native IME,
device, DPI or GPU acceptance, and are not a GitHub release.

Linux dependencies depend on the build (see BUILD.json and PACKAGE.json).
OpenGL drivers remain system-owned. The terminal also needs a terminal and
readable keyboard input devices on Linux; graphics does not require evdev.
WSS requires system certificate trust. No personal profiles, keys, maps,
presets, scores, recordings or server room history are copied into packages.
