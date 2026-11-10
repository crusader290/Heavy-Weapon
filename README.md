# Heavy Weapon

A source port of PopCap's arcade tank shooter **Heavy Weapon Deluxe** that runs natively on
**Windows x64**, **PlayStation Vita**, and **Linux**, written in C++17 on SDL2 and OpenGL.

All platforms share the same game code. The game renders at its original 640×480 into an
offscreen target, then scales that frame to whatever window or screen it runs on.

| Title | Mission map |
| :---: | :---: |
| ![Title screen](screenshots/img_2.png) | ![Mission map](screenshots/img_1.png) |
| ![Combat](screenshots/img_3.png) | ![Enemy squadron](screenshots/img_4.png) |

> **No game assets are included.** You need your own copy of *Heavy Weapon Deluxe*
> (for example from Steam). The port reads the original game's data files.

---

## Features

- **Campaign:** all 19 missions with the original bosses, waves, enemy craft and the full
  Armory upgrade tree, loaded straight from the game's own XML (`levels.xml`, `waves.xml`,
  `craft.xml`, `bosses.xml`).
- **Survival mode:** endless waves from `survival0-9.xml` that step up every minute, with
  one tank, more frequent supply drops, and a saved best time.
- **Original look:** 4-plane parallax, PopCap's split `.jpg` + `_.png` alpha masks, additive
  effects and particles, shown at 4:3 (default) or stretched.
- **Controls for each platform:** keyboard and mouse on PC, twin-stick on any gamepad, and
  sticks plus touch on Vita.
- **Audio:** sound effects and menu music through SDL2_mixer, with a music toggle.
- **No load hitches:** every sprite and sound is preloaded at startup, behind a progress bar.

---

## Game data

Copy these folders from your *Heavy Weapon Deluxe* install:

```
data/      bosses.xml, craft.xml, credits.xml, levels.xml, waves.xml, survival0-9.xml
Fonts/
Images/
Music/     AtomicTank.mo3, LoveTheme.ogg
Sounds/
```

File names are matched case-insensitively, so the data works as shipped on every platform.

---

## Windows x64

### Requirements

- **Visual Studio 2022** or the **Build Tools for Visual Studio 2022**, with the
  *Desktop development with C++* workload, which includes MSVC, CMake and Ninja.
- An internet connection on the first build. SDL2 2.32.10, SDL2_image 2.8.12 and
  SDL2_mixer 2.8.2 are downloaded from the official
  [libsdl-org](https://github.com/libsdl-org) releases.

### Build

```bat
tools\windows\build.cmd
```

This finds Visual Studio, sets up the x64 compiler, and builds `build-windows\heavyweapon.exe`.
It also copies the SDL DLLs next to the exe.

To build in a default data folder so the exe can be run from anywhere:

```bat
tools\windows\build.cmd "C:\Games\Heavy Weapon Deluxe"
```

To build by hand from an *x64 Native Tools Command Prompt*:

```bat
cmake --preset windows-x64
cmake --build --preset windows-x64
```

### Run

```bat
build-windows\heavyweapon.exe --data "C:\Games\Heavy Weapon Deluxe"
```

The game looks for data in this order and uses the first match:

1. `--data <dir>`
2. `Heavy Weapon Deluxe\`, then `.\`, in the working directory
3. `Heavy Weapon Deluxe\`, then the exe's own folder
4. The folder passed to `build.cmd` (CMake `HW_DATA_DIR`)

The exe is a windowed app. When it is started from a terminal, its log output goes to that
terminal. If it can't start, for example because no data was found, it shows an error box.
`settings.ini` is saved in the working directory.

To ship a standalone copy, put `heavyweapon.exe`, every `.dll` from `build-windows\` and your
game data folders in one directory.

---

## PlayStation Vita

### Requirements

- A Vita or PS TV on custom firmware (HENkaku / Ensō) with **VitaShell**.
- `ur0:data/libshacccg.suprx`, the runtime shader compiler used by VitaGL. If you don't
  have it yet, run [ShaRKBR33D](https://github.com/Rinnegatamante/ShaRKBR33D/releases) to
  extract it from your own firmware.
- kubridge and repatch are **not** needed, because this is a native build.

### Install

1. Install `HeavyWeaponDeluxe.vpk` with VitaShell.
2. Copy the game data folders to `ux0:data/heavyweapon/`.
3. Launch **Heavy Weapon Deluxe** from LiveArea.

### Build

Needs [VitaSDK](https://vitasdk.org) with vitaGL, SDL2, SDL2_image, SDL2_mixer and tinyxml2:

```bash
cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=$VITASDK/share/vita.toolchain.cmake
cmake --build build          # -> build/HeavyWeaponDeluxe.vpk
```

Or with Podman alone, which runs the official VitaSDK container:

```bash
tools/desktop/hw.sh vpk
```

---

## Linux

The Linux build is mostly used for development, quick iteration and automated screenshots.
It needs SDL2, SDL2_image, SDL2_mixer and OpenGL development packages:

```bash
cmake -S . -B build-desktop -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build-desktop
./build-desktop/heavyweapon --data "/path/to/Heavy Weapon Deluxe"
```

`tools/desktop/hw.sh` wraps the same build in a Fedora container:

| Command | Does |
| :--- | :--- |
| `tools/desktop/hw.sh image` | Builds the container image (once) |
| `tools/desktop/hw.sh build` | Compiles into `build-desktop/` |
| `tools/desktop/hw.sh run` | Plays in a window, with sound |
| `tools/desktop/hw.sh shot out.png --state play --level 2` | Renders a frame headlessly |

---

## Controls

| Action | Keyboard / mouse | Gamepad | Vita |
| :--- | :--- | :--- | :--- |
| Drive | A / D, ← / → | Left stick, D-pad | Left stick, D-pad |
| Aim | Mouse | Right stick | Right stick, touch |
| Fire | Left click, Ctrl, J | A, R1, right stick pushed far | ✕, R1, right stick pushed far |
| Nuke | Right click, X, N | Y, L1 | △, L1 |
| Pause | P, Esc | Start | Start |
| Music on / off | Tab | Back | Select |
| Menu confirm / back | Enter, Space / Esc, Backspace | A / B | ✕ / ○, tap |

---

## Command-line options

| Option | Effect |
| :--- | :--- |
| `--data <dir>` | Game data directory, searched first |
| `--scale <n>` | Initial window size as a multiple of 640×480 (default 2) |
| `--stretch` | Fill the window instead of keeping 4:3 |
| `--state <title\|map\|play\|survival\|armory\|options\|help>` | Screen to start on |
| `--level <n>` | Mission index (0-based) for `map` / `play` |
| `--frames <n>` | Quit after *n* frames at a fixed 60 Hz |
| `--screenshot <file.png>` | Save the last 640×480 frame (with `--frames`) |
| `--autofire`, `--god`, `--armory <n>`, `--progress <n>` | Testing aids |

Example: `heavyweapon --state play --level 4 --frames 600 --screenshot level5.png`

---

## Project layout

```
src/core/       game loop, state machine, menus, optional profiler (HW_PERF)
src/game/       board, player tank, enemies, bosses, weapons, supplies, effects
src/render/     GL renderer, textures and alpha masks, bitmap fonts, parallax
src/platform/   case-insensitive file lookup, audio, input
src/loader/     XML data loading
include/        headers
3rdparty/       tinyxml2
tools/windows/  Windows build script
tools/desktop/  Linux / Vita container scripts
sce_sys/        Vita LiveArea assets
```

CMake picks the platform automatically. It uses Windows with MSVC when built on Windows,
the Vita when `VITASDK` is set, and Linux otherwise.

---

## Known limitations

- `AtomicTank.mo3` (the in-mission music) can't be decoded yet, so missions play
  without music. Sound effects and menu music work.
- Decorative background animations (igloos, snowmen, ...) are off by default. See
  `kDecorativeAnims` in `include/Constants.h`.

---

## Credits & legal

- Based on the PS Vita port by
  [Chillaxhson](https://github.com/Chillaxhson/heavy-weapon-deluxe-vita). The Windows x64
  target was added in this repository.
- *Heavy Weapon Deluxe* is © 2005 PopCap Games, Inc. / Electronic Arts. This project is
  not affiliated with or endorsed by them.
- This repository contains no copyrighted game assets, music, or binaries. It is a fan
  project for preservation and personal use.
