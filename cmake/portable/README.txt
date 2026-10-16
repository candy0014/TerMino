TerMino portable / 终端方块便携包

Windows: double-click Play.bat; Server.bat starts the UDP relay on port 3000.
Linux: run ./Play.sh in a terminal; ./Server.sh starts the UDP relay.
Online server address: IPv4/hostname[:port] uses UDP (default port 3000).
For a WebSocket gateway, enter the full ws:// or wss:// URL, including /ws.
Default online server: wss://termino.candy0014.icu/ws.
Both players need a version with WebSocket support for this URL.
Play accepts game arguments, e.g. Play.bat --play / ./Play.sh --play.

Extract the whole package into a writable folder. Move/copy the whole folder
to another computer with the same operating system and CPU architecture.
The launchers always use this folder's settings, maps and presets.
No compiler or CMake is needed on the destination computer.
Windows uses static C/C++ runtimes. Linux is fully static by default;
packages made with --dynamic require a compatible system libc (see BUILD.txt).
WSS validates certificates: Linux needs its system CA bundle; Windows uses
the system trust store. Fully static Linux packages include c-ares for both
WSS and UDP DNS, reading system DNS/hosts configuration without loading the
target system's glibc NSS shared modules. Other builds use system resolution.
Linux still needs a compatible kernel and readable /dev/input/event* keyboard
devices, just as a normal build does. Use a terminal with a Chinese font.

.tetr/player1_windows.ini, player2_windows.ini: Windows player settings
.tetr/player1_linux.ini, player2_linux.ini: Linux player settings
.tetr/player1.ini, player2.ini: legacy settings, imported if necessary
.tetr/active_player: selected single-player profile
.tetr/maps.txt: complete custom map library
.tetr/presets.txt: complete preset library, including customized built-ins
keybinding*.txt: keyboard bindings (Windows and Linux use different key codes)
.tetr/replay/: personal recordings, including the recent replay cache
licenses/: third-party license notices

Missing configuration files are optional: the game uses built-in defaults.
Every new package resets all player settings, including
handling, appearance, online identity and key bindings. Players 1 and 2 use
the same default physical keys on both Linux and Windows; native key codes
are platform-specific. Personal maps, presets, challenge scores and recordings
are also excluded from fresh public packages. See BUILD.txt for the reset flag.
Settings are saved in this package's .tetr directory. Keep it when upgrading.
Maps and presets must be migrated together to preserve map references.
Keep .tetr/replay when upgrading; personal recordings are not included in new packages.
FT7 and development/test tools are not included.
