# Heavy Weapon

A native source port of PopCap's arcade tank shooter **Heavy Weapon Deluxe** for
**Windows x64** and **Linux x64**, written in C++17 on **SDL3** and OpenGL.

The game renders at its original 640×480 into an offscreen target, then scales that frame
to the window, either at 4:3 with black bars or stretched to fill. The window opens at the
largest half-step multiple of 640×480 that fits 90% of the desktop above the taskbar
(1366×768 → 1×, 1920×1080 → 1.5×, 2560×1440 → 2.5×, 3840×2160 → 3.5×; less with
Windows display scaling),
resizes at 4:3, and F11 switches to fullscreen.

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
  effects and particles. Each region's ambient scenery from `Anims.xml` (yetis, igloos, cows,
  oil rigs, propaganda posters, ...) wanders in as in the original, and gets nuked with
  everything else. Survival mode has its own backdrop.
- **Controls:** drive with A/D and aim with the mouse, or play twin-stick on any gamepad,
  or use a touch screen.
- **Music:** the full soundtrack. `AtomicTank.mo3` is played with libopenmpt and holds five
  sections, cued like the original: menus, missions, a boss theme after the siren warning,
  the debriefing and game over. `LoveTheme.ogg` plays once at startup. Songs cross-fade,
  and there is a music toggle.
- **Intro and story movies:** `intro.swf` at startup and the cutscenes after missions 9
  and 19 (`middle.swf`, `final.swf`) play through the native Flash runtime of the
  [Tanks recomp](https://github.com/crusader290/Tanks). Their ActionScript is translated
  to C++ at build time from your own copy of the game. Enter, Esc or a click skips them.
- **Sound effects** through SDL3_mixer, with the original's stereo panning.
- **No load hitches:** every sprite and sound is preloaded at startup, behind a progress bar.
- **Mods:** Lua scripts can add pickups, abilities and projectiles, tweak the original
  power-ups, and replace any game file. See [Mods](#mods).

---

## Game data

Copy these folders from your *Heavy Weapon Deluxe* install, or point the game at the
install folder with `--data`:

```
data/      bosses.xml, craft.xml, credits.xml, levels.xml, waves.xml, survival0-9.xml
Fonts/
Images/
Music/     AtomicTank.mo3, LoveTheme.ogg
Sounds/
```

File names are matched case-insensitively, so the data works unchanged on Linux.

The game looks for data in this order and uses the first match:

1. `--data <dir>`
2. `Heavy Weapon Deluxe/`, then `./`, in the working directory
3. `Heavy Weapon Deluxe/`, then the executable's own folder
4. The folder baked in at configure time with `-DHW_DATA_DIR=<dir>`

`settings.ini` (music on/off, best survival time) is saved in the working directory.

---

## Windows x64

### Requirements

- **Visual Studio 2022** or the **Build Tools for Visual Studio 2022**, with the
  *Desktop development with C++* workload, which includes MSVC, CMake and Ninja.
- **Python 3** (on `PATH`), to translate the movies' ActionScript at build time.
- An internet connection on the first build. SDL3 3.4.20, SDL3_image 3.4.8 and
  SDL3_mixer 3.2.4 are downloaded from the official [libsdl-org](https://github.com/libsdl-org)
  release packages, and libopenmpt 0.8.10 from [lib.openmpt.org](https://lib.openmpt.org),
  into the build folder.

### Build

```bat
tools\windows\build.cmd
```

This finds Visual Studio, sets up the x64 compiler, and builds `build-windows\heavyweapon.exe`.
It also copies the SDL3 and libopenmpt DLLs next to the exe.

The intro and story movies need the game's `Images/intro.swf`, `middle.swf` and
`final.swf` at build time (on Linux too). CMake looks in `<HW_DATA_DIR>/Images`, then in
the ignored `Heavy Weapon Deluxe/Images` folder of the repo; or set
`-DHW_FLASH_SWF_DIR=<folder>`. Without them the movies still play, but without their
timeline scripts.

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

The exe is a windowed app. When it is started from a terminal, its log output goes to that
terminal. If it can't start, for example because no data was found, it shows an error box.
To ship a standalone copy, put `heavyweapon.exe` and every `.dll` from `build-windows\` in
one folder, with the game data folders next to them.

---

## Linux x64

### Requirements

A C++17 compiler (C++20 for the Flash runtime), CMake 3.24 or newer, Python 3 and
OpenGL development files. For SDL3 and
libopenmpt you can use either option:

- **Distribution packages**, if they're new enough (SDL3 ≥ 3.2, SDL3_image ≥ 3.2,
  SDL3_mixer ≥ 3.0, libopenmpt ≥ 0.6). On Fedora 44:
  ```bash
  sudo dnf install gcc-c++ cmake SDL3-devel SDL3_image-devel SDL3_mixer-devel libopenmpt-devel mesa-libGL-devel
  ```
- **Bundled.** If those packages are missing (for example on Ubuntu 24.04), CMake
  downloads the release sources and builds them as static libraries. Force this with
  `-DHW_BUNDLE_SDL=ON` and/or `-DHW_BUNDLE_OPENMPT=ON`. libopenmpt needs nothing extra;
  SDL3 needs its usual X11/Wayland/audio development packages. On Debian or Ubuntu:
  ```bash
  sudo apt install build-essential cmake ninja-build pkg-config libgl-dev libegl-dev \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev \
      libxtst-dev libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev \
      libpulse-dev libasound2-dev
  ```

### Build and run

```bash
cmake --preset linux-x64
cmake --build --preset linux-x64
./build-linux/heavyweapon --data "/path/to/Heavy Weapon Deluxe"
```

### Container helper

`tools/linux/hw.sh` builds and runs the game inside a Fedora container, so the host only
needs Podman or Docker. It expects the game data in `Heavy Weapon Deluxe/` at the repo root.

| Command | Does |
| :--- | :--- |
| `tools/linux/hw.sh image` | Builds the container image (once) |
| `tools/linux/hw.sh build` | Compiles into `build-linux/` |
| `tools/linux/hw.sh run` | Plays in a window, with sound |
| `tools/linux/hw.sh shot out.png --state play --level 2` | Renders a frame headlessly |

---

## Controls

| Action | Keyboard / mouse | Gamepad | Touch screen |
| :--- | :--- | :--- | :--- |
| Drive left / right | **A / D** (or ← / →) | Left stick, D-pad | Hold where the tank should go |
| Aim | Mouse | Right stick | Finger |
| Fire | Left click, Ctrl, J | A, RB, right stick pushed far | Finger down |
| Nuke | Right click, X, N | Y, LB | — |
| Pause | P, Esc | Start | — |
| Music on / off | Tab | Back | — |
| Fullscreen on / off | F11 | — | — |
| Menu confirm / back | Enter, Space / Esc, Backspace | A / B | Tap |

On-screen prompts name the gamepad buttons Xbox-style by default. To use PlayStation names
(Cross, Circle, Triangle, Select), the wording of the original PS Vita port, set
`buttons=playstation` in `settings.ini` or pass `--buttons playstation`. `buttons=auto`
switches to PlayStation names while a PlayStation controller is connected. The buttons
themselves work the same either way.

---

## Mods

Put mod folders in a `mods` folder next to your game data; each needs a `mod.lua`.
Mods can:
- add pickups that the supply helicopter drops (or that your own rules drop)
- add abilities on a key or gamepad button
- fire custom projectiles
- tweak the original power-ups
- replace any sprite, sound, music or data file by shipping a file with the same path

Scripts run sandboxed (no file or system access). Errors go to `mods.log`, and the title
screen lists the loaded mods.

Guide and API reference: [docs/MODDING.md](docs/MODDING.md).

---

## Command-line options

| Option | Effect |
| :--- | :--- |
| `--data <dir>` | Game data directory, searched first |
| `--buttons <xbox\|playstation\|auto>` | Gamepad button names in prompts for this run (overrides `settings.ini`) |
| `--no-mods` | Don't load anything from the `mods` folder |
| `--scale <n>` | Window size as a multiple of 640×480, e.g. `1.5` (default: fit the desktop) |
| `--size <W>x<H>` | Window size in pixels |
| `--stretch` | Fill the window instead of keeping 4:3 |
| `--state <title\|map\|play\|survival\|armory\|options\|help\|intro\|middle\|final>` | Screen or movie to start on (default: the intro) |
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
src/render/     GL renderer, textures and alpha masks, bitmap fonts, parallax, Flash movies
src/platform/   case-insensitive file lookup, audio (SDL3_mixer + libopenmpt), input
src/mods/       mod loader, Lua sandbox and the hw.* API
src/loader/     XML data loading
include/        headers
3rdparty/       tinyxml2, Lua 5.4, flashrt (Flash runtime + translator, PlutoVG)
docs/           MODDING.md (mod guide and API reference)
tools/windows/  Windows build script
tools/linux/    Linux container build / run / screenshot script
```

---

## Known limitations

- The boss warning only pulses the screen red with its siren. The original also drew hazard
  stripes, a boss picture and a typed-out briefing.

---

## Credits & legal

- Based on the PS Vita port by
  [Chillaxhson](https://github.com/Chillaxhson/heavy-weapon-deluxe-vita). This repository
  turned it into a Windows/Linux desktop port on SDL3.
- Music playback by [libopenmpt](https://lib.openmpt.org/libopenmpt/) (BSD-3-Clause).
- Mod scripting by [Lua](https://www.lua.org) 5.4 (MIT), vendored in `3rdparty/lua`.
- Flash movie playback by the runtime and translator of the
  [Tanks recomp](https://github.com/crusader290/Tanks), forked from
  [IsaacRecomp](https://github.com/whylurking/IsaacRecomp) (MIT), with
  [PlutoVG](https://github.com/sammycage/plutovg) (MIT), vendored in `3rdparty/flashrt`.
- *Heavy Weapon Deluxe* is © 2005 PopCap Games, Inc. / Electronic Arts. This project is
  not affiliated with or endorsed by them.
- This repository contains no copyrighted game assets, music, or binaries. It is a fan
  project for preservation and personal use.
