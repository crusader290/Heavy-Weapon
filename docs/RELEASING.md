# Releasing

Releases are published on GitHub as `vX.Y.Z` with two zips, built from the tagged commit.
Each zip holds the game, `README.txt`, `LICENSES/` and `docs/MODDING.md`. Never game data.

Both builds need the game's `intro.swf`, `middle.swf` and `final.swf` at configure time
(`-DHW_FLASH_SWF_DIR=<data>/Images`), so the movies' scripts are translated and compiled
in. Check that configure prints "Flash movie ...: translating" three times.

## 1. Windows x64

From the repo root, in a shell with the MSVC x64 environment (`vcvars64.bat`; see
`tools/windows/build.cmd`):

```bat
cmake --preset windows-x64 -B build-release-windows -DCMAKE_BUILD_TYPE=Release -DHW_DATA_DIR= ^
    "-DHW_FLASH_SWF_DIR=<data>/Images"
cmake --build build-release-windows
```

`-DHW_DATA_DIR=` matters: a developer build may bake a local data path into the exe.
libopenmpt's DLLs need the Visual C++ runtime, so the packager ships
`vcruntime140.dll`, `vcruntime140_1.dll` and `msvcp140.dll` from the Build Tools'
`VC\Redist\MSVC\<version>\x64\Microsoft.VC143.CRT`. The zip then runs on a clean
Windows 10/11.

## 2. Linux x64

Built on Debian 12 (glibc 2.36) so the binary runs on most current distros. SDL3 and
libopenmpt are built in statically; the binary only needs libGL and glibc:

```bash
docker run --rm -v "$PWD:/src:ro" -v "$PWD/build-release-linux:/out" -v "<data>/Images:/swf:ro" debian:12 bash -c '
  apt-get update && apt-get install -y --no-install-recommends build-essential cmake \
    ninja-build pkg-config ca-certificates python3 libgl-dev libegl-dev libx11-dev libxext-dev \
    libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev libxtst-dev \
    libwayland-dev wayland-protocols libxkbcommon-dev libdecor-0-dev libpulse-dev \
    libasound2-dev libpipewire-0.3-dev libdbus-1-dev libudev-dev &&
  cmake -S /src -B /tmp/b -G Ninja -DCMAKE_BUILD_TYPE=Release -DHW_BUNDLE_SDL=ON \
    -DHW_BUNDLE_OPENMPT=ON -DHW_FLASH_SWF_DIR=/swf "-DCMAKE_EXE_LINKER_FLAGS=-static-libstdc++ -static-libgcc" &&
  cmake --build /tmp/b && cp /tmp/b/heavyweapon /out/ && strip /out/heavyweapon'
```

Check with `ldd build-release-linux/heavyweapon` (only libGL, libm and libc) and
`objdump -T … | grep -o 'GLIBC_[0-9.]*' | sort -V | tail -1` (2.36).

## 3. Package

```bash
python tools/release/package.py windows --version X.Y.Z --build build-release-windows \
    --vcredist "<VS>/VC/Redist/MSVC/<version>/x64/Microsoft.VC143.CRT"
python tools/release/package.py linux --version X.Y.Z --build build-release-linux \
    --deps build-release-windows/_deps --openmpt-src <libopenmpt-X+release source folder>
```

The Linux zip needs license files from a Windows build's `_deps` (SDL, libopenmpt) and
from the libopenmpt source tarball (its minimp3, miniz and stb_vorbis decoders). Zips
land in `dist/`.

## 4. Smoke test, then publish

Extract each zip somewhere fresh and run a mission with `--data <game data>`, checking
that the intro plays, then the title, and that a mission renders and plays music. Without `--data` (and no data next to it) it must show
the "game data not found" error instead of a black window. Run the Linux one on a
different distro than Debian (e.g. the Fedora container from `tools/linux`). Then:

```bash
git tag -a vX.Y.Z -m "Heavy Weapon X.Y.Z" && git push origin vX.Y.Z
gh release create vX.Y.Z dist/HeavyWeapon-X.Y.Z-*.zip --title "Heavy Weapon X.Y.Z" --notes-file <notes>
```
