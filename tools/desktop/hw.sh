#!/usr/bin/env bash
# Desktop (Linux) build of the port, run inside a Podman container so the host needs
# nothing but Podman. Expects the game assets in "Heavy Weapon Deluxe/" at the repo root.
#
#   tools/desktop/hw.sh image                 build the container image (once)
#   tools/desktop/hw.sh build                 compile into build-desktop/
#   tools/desktop/hw.sh run [game args]       play in a window (X11/XWayland, with sound)
#   tools/desktop/hw.sh shot OUT.png [args]   headless: render a frame to OUT.png
#   tools/desktop/hw.sh vpk                   build the Vita VPK into build/
#
# HW_PERF=1 tools/desktop/hw.sh build|vpk    enable the perf.log profiler (src/core/Perf.h)
#
# Game args: --state title|map|play|armory  --level N  --frames N  --stretch  --scale N
# Example:   tools/desktop/hw.sh shot shots/play.png --state play --level 0 --frames 240
set -euo pipefail

PERF_FLAG=OFF
[[ "${HW_PERF:-0}" == "1" || "${HW_PERF:-}" == "ON" ]] && PERF_FLAG=ON

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE=hwd-desktop

# Our image runs as the invoking user (keep-id). The VitaSDK image runs as root, which
# rootless Podman already maps to the invoking user, so it must not use keep-id.
run_in() {
    podman run --rm --userns=keep-id --security-opt label=disable -e HOME=/tmp \
        -v "$REPO:/src" -w /src "$@"
}

run_as_root() {
    podman run --rm --security-opt label=disable -e HOME=/tmp -v "$REPO:/src" -w /src "$@"
}

cmd="${1:-}"
shift || true

case "$cmd" in
    image)
        podman build -t "$IMAGE" -f "$REPO/tools/desktop/Containerfile" "$REPO/tools/desktop"
        ;;
    build)
        run_in "$IMAGE" bash -c "cmake -S . -B build-desktop -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHW_PERF=$PERF_FLAG >/dev/null && cmake --build build-desktop -j\$(nproc)"
        ;;
    run)
        uid="$(id -u)"
        args=(-e DISPLAY="${DISPLAY:-:0}" -v /tmp/.X11-unix:/tmp/.X11-unix --device /dev/dri
              -e SDL_VIDEODRIVER=x11)
        if [[ -n "${XAUTHORITY:-}" && -f "$XAUTHORITY" ]]; then
            args+=(-e XAUTHORITY=/tmp/.xauth -v "$XAUTHORITY:/tmp/.xauth:ro")
        fi
        if [[ -S "/run/user/$uid/pulse/native" ]]; then
            args+=(-e SDL_AUDIODRIVER=pulseaudio -e PULSE_SERVER="unix:/run/user/$uid/pulse/native"
                   -v "/run/user/$uid/pulse:/run/user/$uid/pulse")
        else
            args+=(-e SDL_AUDIODRIVER=dummy)
        fi
        run_in "${args[@]}" "$IMAGE" ./build-desktop/heavyweapon "$@"
        ;;
    shot)
        out="${1:?usage: hw.sh shot OUT.png [game args]}"
        shift
        mkdir -p "$(dirname "$REPO/$out")"
        run_in -e SDL_AUDIODRIVER=dummy -e SDL_VIDEODRIVER=x11 -e LIBGL_ALWAYS_SOFTWARE=1 "$IMAGE" \
            bash -c 'Xvfb :99 -screen 0 1280x960x24 -nolisten tcp >/dev/null 2>&1 & sleep 1;
                     DISPLAY=:99 ./build-desktop/heavyweapon --scale 1 "$@"; status=$?; kill %1; exit $status' \
            _ --frames 120 "$@" --screenshot "$out"
        ;;
    vpk)
        run_as_root docker.io/vitasdk/vitasdk:latest bash -c "cmake -S . -B build -DCMAKE_TOOLCHAIN_FILE=\$VITASDK/share/vita.toolchain.cmake -DHW_PERF=$PERF_FLAG >/dev/null && cmake --build build -j\$(nproc)"
        ;;
    *)
        sed -n '2,17p' "$0"
        exit 1
        ;;
esac
