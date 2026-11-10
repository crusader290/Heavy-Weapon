# CLAUDE.md

Guidance for Claude (and other agents) working in this repository. See `HANDOFF.md` for
the current state of the work and open items.

## What this is

A native C++17 source port of PopCap's *Heavy Weapon Deluxe* for **Windows x64** and
**Linux x64**, on **SDL3 + SDL3_image + SDL3_mixer** and fixed-function (legacy) OpenGL.
It started as a PS Vita port. All Vita code, assets and build targets have been removed;
don't reintroduce them.

The game logic reimplements the original executable. Many comments cite addresses or
functions from it (for example `FUN_00401150`, `VA 0x529808`). Keep behaviour faithful to
the original unless asked otherwise, and keep those references when editing.

## Hard rules

- **Never commit game assets.** The data (`data/`, `Fonts/`, `Images/`, `Music/`,
  `Sounds/`) is the user's own copy. It lives outside the repo or in the ignored
  `Heavy Weapon Deluxe/` folder. Also never commit `*.exe`, `*.dll`, build folders or
  `settings.ini`.
- Line endings are LF, except `*.cmd`/`*.bat`, which are CRLF (`.gitattributes`).
- Only 64-bit builds are supported (CMake fails otherwise).

## Build

Windows (MSVC from VS 2022 / Build Tools; SDL3 VC packages are downloaded by CMake):

```bat
tools\windows\build.cmd                         :: -> build-windows\heavyweapon.exe
tools\windows\build.cmd "C:\path\to\data"       :: also bakes in HW_DATA_DIR
```

`build.cmd` sets up the x64 compiler itself, so plain `cmake` only works from an
*x64 Native Tools* prompt. If the link fails with `LNK1168`, `heavyweapon.exe` is still
running. It may be the user playing, so ask before killing it.

Linux (system SDL3 packages if new enough, otherwise SDL3 is built from source;
`-DHW_BUNDLE_SDL=ON` forces that):

```bash
cmake --preset linux-x64 && cmake --build --preset linux-x64   # -> build-linux/heavyweapon
tools/linux/hw.sh image|build|run|shot ...                       # Fedora container (podman/docker)
```

From a Windows host, build Linux with Docker directly:

```powershell
docker build -t heavyweapon-linux -f tools\linux\Containerfile tools\linux
docker run --rm -v "${PWD}:/src" -w /src heavyweapon-linux bash -c "cmake --preset linux-x64 && cmake --build --preset linux-x64 -j"
```

CMake options: `HW_DATA_DIR` (default data dir), `HW_PERF` (perf.log profiler),
`HW_BUNDLE_SDL` (Linux), `HW_SDL3_VERSION` / `HW_SDL3_IMAGE_VERSION` / `HW_SDL3_MIXER_VERSION`.

## Verifying changes

There are no unit tests. Verify by running the game headlessly and looking at the frames:

```
heavyweapon --data <dir> --state play --level 0 --frames 400 --autofire --scale 1 --screenshot out.png
```

- `--frames N` runs a fixed 60 Hz timestep and quits, so runs are reproducible.
- `--state title|map|play|survival|armory|options|help` jumps to a screen.
- **Audio check:** set `SDL_AUDIO_DRIVER=disk`. SDL writes the mixed output to
  `sdlaudio.raw` (16-bit PCM) in the working directory. Non-zero samples mean sound works.
  Use `SDL_AUDIO_DRIVER=dummy` for silent runs.
- **Linux headless:** run under `Xvfb` with `SDL_VIDEO_DRIVER=x11 LIBGL_ALWAYS_SOFTWARE=1`
  (see `tools/linux/hw.sh shot`).
- **The mouse affects runs.** The mouse cursor aims the turret, so if the real cursor sits
  over the window, frames can differ from run to run. Park the cursor or keep that in mind.
- On Windows the exe is a GUI-subsystem app. Its logs reach a parent console, or
  stdout/stderr when they are redirected (`Start-Process -RedirectStandardError`).

## Code map

| Path | What |
| :--- | :--- |
| `src/main.cpp` | SDL3 init, window + GL context, CLI options, Windows console attach |
| `src/core/GameEngine.cpp` | State machine, menus, loading screen, `ApplyBoardInput()` (input → tank) |
| `src/game/Board.cpp` | Gameplay: tank movement/firing, spawns, collisions, scoring |
| `src/game/*.cpp` | Crafts, Hazards, Supplies, Weapons, Bosses, Gfx (effects) |
| `src/platform/InputManager.cpp` | SDL3 events, keyboard/mouse/gamepad/touch → `InputState` |
| `src/platform/AudioSystem.cpp` | SDL3_mixer: 32-track pool emulating SDL2_mixer channels, music track |
| `src/platform/Vfs.cpp` | Case-insensitive data lookup over search paths (memoised) |
| `src/render/` | GL renderer (640×480 FBO scaled to window), textures + PopCap alpha masks, fonts, parallax |
| `src/loader/XmlLoader.cpp` | Parses the game's XML (levels, waves, craft, bosses) via tinyxml2 |
| `include/Constants.h` | Screen size, timestep, feature switches (`kDecorativeAnims`) |

## Things that are easy to get wrong

- **Controls:** the tank drives with A/D or the arrow keys (`moveAxisX`), and the mouse
  only aims (`Board::SetDriveOverride(true, axis)`). On a touch screen the finger still
  drives and aims, like the original mouse handling. `Board.cpp` drives a freshly spawned
  tank on-screen from x = -380 even while the drive override is active. Keep that, or the
  tank never appears.
- **Audio volume:** the SDL2 build applied the SFX volume twice (channel × chunk).
  `SfxGain()` deliberately keeps that mix. Volumes stay on the 0..128 scale.
- **SDL3 API differences** that already bit once: `SDL_GetBasePath()` returns memory owned
  by SDL (don't free it), `SDL_GetKeyboardState()` returns `const bool*`, `SDL_GetTicks()`
  is 64-bit, and most functions return `bool` for success rather than `< 0`.
- **Vfs:** resolved paths are fed back into `Exists()`. Absolute Linux paths
  (`/data/...`) must keep their leading slash (see `asGiven` in `ResolveMemo`).
- **Bundled SDL3 (Linux):** SDL3_image is configured to decode PNG with stb, not libpng.
  With static builds, Fedora's zlib CMake package points at a missing `libz.a` and breaks
  configure. Keep `SDLIMAGE_PNG_LIBPNG OFF`.
- `AtomicTank.mo3` can't be decoded and is skipped on purpose.
- The in-game help screen (`GameEngine::RenderHelp`) mirrors the bindings in
  `InputManager.cpp`. Update both together, and the README controls table too.

## Repo

- Remote: `https://github.com/crusader290/Heavy-Weapon`, branch `main`.
- Commit as the repo-local identity that is already configured (GitHub noreply address).
