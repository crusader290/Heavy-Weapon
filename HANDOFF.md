# Handoff

State of the project as of **2026-11-10**, for whoever (human or agent) picks it up next.
`CLAUDE.md` has the build commands, verification workflow and code map.

## Where things stand

The game is a working native desktop port for **Windows x64** (MSVC) and **Linux x64**
(GCC), on **SDL3 3.4.20 / SDL3_image 3.4.8 / SDL3_mixer 3.2.4** and **libopenmpt 0.8.10**.
The full soundtrack and the ambient scenery animations work. The PS Vita target it came
from has been removed completely: code paths, LiveArea assets, VitaSDK build and docs.

### Latest round: mod support

Lua 5.4.9 is vendored in `3rdparty/lua`, built from only the core and pure libraries. Mods
are folders in `mods/` with a `mod.lua`; the guide is `docs/MODDING.md`.

- **Pickups:** `hw.pickup`, with a sprite, sound, message, drop chance, tier, per-run cap,
  modes and `on_pickup`. They mix into the helicopter's power-up pick by weight; with no
  mods or tweaks the pick is the original uniform one.
- **Abilities:** `hw.ability`, with keys, gamepad buttons or triggers, charges, cooldown
  and `on_use`. Shown on the HUD.
- **Custom projectiles:** `hw.projectile`, with gravity, homing, pierce, glow, animated
  strips and `on_hit`.
- **Tweaks:** `hw.tweak_powerup` changes caps, unlock rates, drop chance and messages of
  the original six.
- **Events:** `level_start`, `tick`, `enemy_killed`, `tank_destroyed`, `pickup`;
  `hw.after` timers.
- **Game API:** tank, lives, score, upgrades, firing the built-in weapons, megalaser,
  invulnerability, enemies and damage, explosions, drops, messages and sounds.
- **Asset overrides:** any file in a mod mirrors and replaces the game's. Image lookup now
  prefers the highest-priority folder in any format and keeps masks from that folder
  (`Vfs::Rank`).
- **Safety:** no `io`/`os`/`require`/`load`, an instruction budget, and errors logged
  with tracebacks to `mods.log`. The title screen lists mods and flags errors.
  `--no-mods` turns it all off.
- **No example mods** ship in the repo (two were built for testing and later removed at
  the user's request). Later API additions (pickup sound ids, projectile `bounces` /
  `restitution` / `collide` / `on_expire` and returned ids, `hw.projectile_pos`, `hw.aim`,
  `hw.hazards` / `hw.steer_hazard` / `hw.destroy_hazard`) work but are **not yet in
  docs/MODDING.md**.

Verified on Windows and Linux with test mods:
- syntax error, endless loop, a skipped `_` folder, and a runtime error are all caught and
  logged
- the sandbox has no io, os, require or load
- forced drops are collected and apply their effects
- key presses fire abilities, spending charges, getting kills and updating the HUD
- an override replaced the HUD map pointer

Not tried: real gamepad buttons and triggers for abilities, or a bigger third-party-style
mod.

### Previous round: music and scenery animations

Both were mapped from the original exe (see "Reading the original executable" in
`CLAUDE.md`), not guessed.

1. **MO3 music.** libopenmpt decodes `AtomicTank.mo3`: on Windows, the official dev package
   is downloaded like SDL3; on Linux, the system package is used, with a static from-source
   fallback (`HW_BUNDLE_OPENMPT`). The exe's music table gives six songs. 0 is
   `LoveTheme.ogg`; 1-5 are MO3 sections starting at orders 0/42/59/73/79, and each loops
   on its own. Cues, as in the original:

   | When | Call | Song |
   | :-- | :-- | :-- |
   | Startup (title) | PlayMusic, once | 0 LoveTheme |
   | Title (after LoveTheme), map, armory | `if (!IsPlaying(3)) { FadeOutAll; FadeIn(3) }` | 3 menus |
   | Mission start | FadeOutAll; FadeIn(1) | 1 missions |
   | Boss warning starts | FadeOut(1) | |
   | Boss warning ends (5 x 127 ticks) | PlayMusic(2) | 2 boss |
   | Level complete | PlayMusic(4); FadeOut(2) | 4 debriefing |
   | Last tank destroyed | FadeOutAll; FadeIn(5) | 5 game over |

   Fades: in 5 s, out 2.5 s, matching the original's per-update steps of 0.002 / 0.004.
2. **Boss warning** (0x40e800), which the port lacked: siren cycles, the "danger" voice, and
   the red screen pulse, then the boss music. Nukes are refused while it runs, as in the
   original.
3. **Ambient animations** (`kDecorativeAnims` now on), reimplemented from 0x419eb0 (spawn),
   0x418f50 (update), 0x410c20 (draw) and 0x41a1d8 (nuke):
   - one live anim per plane, spawned at random, with `rare` ones 20x rarer
   - background anims are pinned to their spot on the 960-pixel background image
   - looping and ping-pong animation, with per-frame `<Delay>` speeds
   - nukes switch on-screen anims to their nuked frame
4. **Region fix.** `Anims.xml` is per region, but the port indexed it by mission, so missions
   10-19 showed the wrong scenery or none. Survival now uses its own backdrop
   (`survival_*`) and its own anims (posters, satellite dishes); before, it borrowed the
   mission's backdrop.

5. **PlayStation button names kept as an option.** `include/ButtonLabels.h` holds the
   Xbox set (default) and the original PS Vita wording (CROSS / CIRCLE / TRIANGLE / SELECT).
   Choose with `buttons=xbox|playstation|auto` in `settings.ini` or `--buttons`; `auto`
   uses PlayStation names while a PS3/PS4/PS5 pad is connected. Checked: help and options
   screens render the PS text, and an unknown value logs a warning. `auto` hasn't been
   tried with a real PlayStation pad.

### Earlier round

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
| MO3 cues (Windows; Linux with system and with bundled libopenmpt) | Logged song order correct: menu 3; mission 1, then boss 2 after the warning |
| Music output (disk driver capture) | Non-silent; the fade-in ramps up over the first seconds |
| Boss warning red pulse | Visible at the start of each siren cycle |
| Ambient anims (Frigistan, Petrovakia, Dictastroika, survival) | Spawn/scroll/despawn logged; igloo, cow, oil cans, waste barrel, poster and dish drawn in place |

### Not verified

- Real gamepad hardware (mappings were renamed to SDL3's SOUTH/EAST/WEST/NORTH buttons).
- Real touch screens (finger coordinates now map from window-normalised SDL3 values).
- Holding **A** specifically (same code path as D, mirrored).
- Hearing the audio with your own ears: it was checked as non-silent PCM, and the
  relative loudness/panning was reasoned from code, not listened to.
- Interactive play on a Linux desktop (only headless Xvfb + software GL).
- **Listening to the music**: cue order and non-silence were checked, not how it sounds.
  The −5 dB module gain was set from measured levels (it keeps peaks near full scale and
  loudness level with LoveTheme). The debriefing and game-over cues were checked in code
  only, not in a run.
- The nuked anim frames (logic only, no screenshot taken after a nuke).
- The Debian/Ubuntu package list in the README for the bundled build: the Ubuntu 24.04
  test container never got past `apt-get update` (mirror unreachable from the dev box).
  The fallback itself was verified on Fedora.

## Open items / ideas

- **Boss warning visuals:** only the red pulse is drawn. The original (0x40e920) also
  draws six animated hazard stripes, a per-mission boss picture (table at 0x52aef8) and
  typewriter briefing text ("ENCOUNTERED: ..." strings near 0x52b05c).
- **Intro / story screens:** `intro.swf`, `middle.swf` and `final.swf` aren't implemented.
  The original also plays song 5 under the story cutscenes.
- **Detail setting:** the original skips background-plane anims when `app+0xb28` is
  non-zero (likely a low-detail option); the port always draws them.
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
