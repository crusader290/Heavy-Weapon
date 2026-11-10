# Handoff

State of the project as of **2026-11-10**, for whoever (human or agent) picks it up next.
`CLAUDE.md` has the build commands, verification workflow and code map.

## Where things stand

The game is a working native desktop port for **Windows x64** (MSVC) and **Linux x64**
(GCC), on **SDL3 3.4.20 / SDL3_image 3.4.8 / SDL3_mixer 3.2.4**. The PS Vita target it came
from has been removed completely: code paths, LiveArea assets, VitaSDK build and docs.

### Done in this round

1. **Windows x64 target.** MSVC + Ninja via `CMakePresets.json` (`windows-x64`) and
   `tools/windows/build.cmd`. SDL3 VC packages are downloaded at configure time and their
   DLLs are copied next to the exe. GUI-subsystem exe. Logs go to the parent console or to
   redirected stdout/stderr. An error box appears when startup fails.
2. **SDL2 → SDL3.** All platform code was ported. The audio system was rewritten for the
   SDL3_mixer 3.x API (`MIX_Mixer` / `MIX_Track` / `MIX_Audio`) as a 32-track pool that
   behaves like the old SDL2_mixer channels, including panning, loops and the old volume mix.
3. **Vita removed.** `__vita__` branches, `sce_sys/`, the VitaSDK CMake branch and the VPK
   script target are gone. PlayStation button names in the UI were replaced with
   keyboard and Xbox-style labels.
4. **Linux target.** Uses system SDL3 packages if they're recent enough, otherwise builds
   SDL3 from the release tarballs (`HW_BUNDLE_SDL`). There's a `linux-x64` preset, and the
   container helper moved to `tools/linux/` and works with Podman or Docker.
5. **Steering.** The tank now drives with **A/D** (arrows and the gamepad still work). The
   mouse only aims; it used to also drag the tank toward the cursor.
6. **Bugs fixed along the way:**
   - A freshly spawned tank stayed off-screen (x = -380) whenever input came from
     keys/gamepad instead of a mouse over the window. It now always drives in.
   - `Vfs` dropped the leading `/` of absolute paths, so with `--data /abs/path` on Linux
     every sound and the menu music failed to load.
   - On Windows, reattaching to the parent console swallowed redirected stdout/stderr.
7. **Data path options:** `--data <dir>`, the exe's own folder, and a configure-time
   `HW_DATA_DIR` default.

### Verified

| Check | Result |
| :--- | :--- |
| Windows x64 build (MSVC 19.44) | Builds clean (one old C4244 warning) |
| Windows run: title, help, mission 1 frames | Correct |
| Windows audio (disk driver capture) | Real PCM output (menu music + SFX) |
| A/D steering (cursor over window; with and without D held) | Mouse aims only; D drives right; tank drives in after spawn |
| Linux x64 build, Fedora 44 system SDL3 | Builds (one old unused-variable warning) |
| Linux run under Xvfb: title, mission 1 frames | Correct, matches Windows |
| Linux audio (disk driver capture) | Real PCM output |
| Linux bundled SDL3 fallback (Fedora 44 without SDL3 packages) | Auto-falls back, builds SDL3 statically, runs; frame matches, audio non-silent |

### Not verified

- Real gamepad hardware (mappings were renamed to SDL3's SOUTH/EAST/WEST/NORTH buttons).
- Real touch screens (finger coordinates now map from window-normalised SDL3 values).
- Holding **A** specifically (same code path as D, mirrored).
- Hearing the audio with your own ears: it was checked as non-silent PCM, and the
  relative loudness/panning was reasoned from code, not listened to.
- Interactive play on a Linux desktop (only headless Xvfb + software GL).
- The Debian/Ubuntu package list in the README for the bundled build: the Ubuntu 24.04
  test container never got past `apt-get update` (mirror unreachable from the dev box).
  The fallback itself was verified on Fedora.

## Open items / ideas

- **In-mission music:** `Music/AtomicTank.mo3` (MO3 = MP3/OGG-compressed tracker module)
  isn't decodable by libxmp/SDL3_mixer, so it's skipped and missions are silent. Options:
  convert to OGG offline (the user's own copy) and prefer `AtomicTank.ogg` if present,
  or add an MO3 decoder.
- **Heroes** menu entry plays a "denied" sound. It isn't implemented.
- **Warnings:** `GameEngine.cpp` C4244 (int→float argument around the mission map) and
  `TextureManager.cpp` unused variable `n`. Both predate this work.
- **HiDPI on Windows:** the window isn't DPI-aware, so Windows scales it on high-DPI
  screens. Could opt in with `SDL_HINT_WINDOWS_DPI_AWARENESS` if sharper output is wanted.
- **Packaging:** no installer, zip or AppImage target yet. `cmake --install` puts the
  exe + DLLs in a folder on Windows.
- **CI:** none. A GitHub Actions matrix (windows-latest + ubuntu with `HW_BUNDLE_SDL=ON`)
  would catch regressions. Running the game needs the user's assets, so CI could only build.
- **License:** the upstream Vita port (Chillaxhson) ships no license file. The repo is
  public; clarify with the original author if that matters.

## Practical notes

- Game data on the dev machine is at `H:\Games\Heavy Weapon\Heavy Weapon Deluxe`. The
  local Windows build bakes that in via `build.cmd "<path>"`. The pushed CMake defaults
  don't.
- If the Windows link fails with `LNK1168`, the game is still running (possibly the user
  playing it).
- The repo has a local git identity set to the GitHub noreply address. Keep using it.
