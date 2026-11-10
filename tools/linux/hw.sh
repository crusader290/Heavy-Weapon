#!/usr/bin/env bash
# Linux x64 build of the game, run inside a container (Podman, or Docker when Podman is not
# installed) so the host needs nothing else. Expects the game data in "Heavy Weapon Deluxe/"
# at the repo root.
#
#   tools/linux/hw.sh image                 build the container image (once)
#   tools/linux/hw.sh build                 compile into build-linux/
#   tools/linux/hw.sh run [game args]       play in a window (X11/XWayland, with sound)
#   tools/linux/hw.sh shot OUT.png [args]   headless: render a frame to OUT.png
#
# HW_PERF=1 tools/linux/hw.sh build         enable the perf.log profiler (src/core/Perf.h)
# HW_ENGINE=docker tools/linux/hw.sh ...    force the container engine
#
# Game args: --state title|map|play|armory  --level N  --frames N  --stretch  --scale N
# Example:   tools/linux/hw.sh shot shots/play.png --state play --level 0 --frames 240
set -euo pipefail

PERF_FLAG=OFF
[[ "${HW_PERF:-0}" == "1" || "${HW_PERF:-}" == "ON" ]] && PERF_FLAG=ON

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
IMAGE=heavyweapon-linux

ENGINE="${HW_ENGINE:-}"
if [[ -z "$ENGINE" ]]; then
    if command -v podman >/dev/null 2>&1; then ENGINE=podman; else ENGINE=docker; fi
fi

# Run as the invoking user so build output is not owned by root.
run_in() {
    local user_args=()
    if [[ "$ENGINE" == podman ]]; then
        user_args=(--userns=keep-id)
    else
        user_args=(--user "$(id -u):$(id -g)")
    fi
    "$ENGINE" run --rm "${user_args[@]}" --security-opt label=disable -e HOME=/tmp \
        -v "$REPO:/src" -w /src "$@"
}

cmd="${1:-}"
shift || true

case "$cmd" in
    image)
        "$ENGINE" build -t "$IMAGE" -f "$REPO/tools/linux/Containerfile" "$REPO/tools/linux"
        ;;
    build)
        run_in "$IMAGE" bash -c "cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=RelWithDebInfo -DHW_PERF=$PERF_FLAG >/dev/null && cmake --build build-linux -j\$(nproc)"
        ;;
    run)
        uid="$(id -u)"
        args=(-e DISPLAY="${DISPLAY:-:0}" -v /tmp/.X11-unix:/tmp/.X11-unix --device /dev/dri
              -e SDL_VIDEO_DRIVER=x11)
        if [[ -n "${XAUTHORITY:-}" && -f "$XAUTHORITY" ]]; then
            args+=(-e XAUTHORITY=/tmp/.xauth -v "$XAUTHORITY:/tmp/.xauth:ro")
        fi
        if [[ -S "/run/user/$uid/pulse/native" ]]; then
            args+=(-e SDL_AUDIO_DRIVER=pulseaudio -e PULSE_SERVER="unix:/run/user/$uid/pulse/native"
                   -v "/run/user/$uid/pulse:/run/user/$uid/pulse")
        else
            args+=(-e SDL_AUDIO_DRIVER=dummy)
        fi
        run_in "${args[@]}" "$IMAGE" ./build-linux/heavyweapon "$@"
        ;;
    shot)
        out="${1:?usage: hw.sh shot OUT.png [game args]}"
        shift
        mkdir -p "$(dirname "$REPO/$out")"
        run_in -e SDL_AUDIO_DRIVER=dummy -e SDL_VIDEO_DRIVER=x11 -e LIBGL_ALWAYS_SOFTWARE=1 "$IMAGE" \
            bash -c 'Xvfb :99 -screen 0 1280x960x24 -nolisten tcp >/dev/null 2>&1 & sleep 1;
                     DISPLAY=:99 ./build-linux/heavyweapon --scale 1 "$@"; status=$?; kill %1; exit $status' \
            _ --frames 120 "$@" --screenshot "$out"
        ;;
    *)
        sed -n '2,15p' "$0"
        exit 1
        ;;
esac
