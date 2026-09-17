#!/usr/bin/env bash
# Runs ctest inside a headless Wayland compositor, so the tests that need one
# actually run instead of skipping.
#
# Everything about the picker that matters happens on a compositor: the
# layer-shell surface, the exclusive keyboard grab, being placed on an output.
# None of it is exercised by the offscreen platform the other tests use, which
# is why "verified by hand" was the honest description until now.
#
#   tests/run-under-compositor.sh --test-dir build -R AppSmokeTest
#
# Arguments are passed straight to ctest.
set -euo pipefail

command -v sway >/dev/null || { echo "sway is not installed; skipping compositor tests" >&2; exit 0; }

work=$(mktemp -d)
trap 'kill "${sway_pid:-}" 2>/dev/null || true; rm -rf "$work"' EXIT

# sway ships with cap_sys_nice, and a process without that capability in its
# bounding set cannot exec a file that demands it -- which is every container
# using Docker's default capability set, CI included. Copying the binary drops
# file capabilities, so the copy runs anywhere. Stripping the original would
# need root and would quietly alter the developer's own system.
cp "$(command -v sway)" "$work/sway"

export XDG_RUNTIME_DIR="$work/run"
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"

# Two outputs, because one is the case that is always right by accident.
export WLR_BACKENDS=headless
export WLR_LIBINPUT_NO_DEVICES=1
export WLR_RENDERER=pixman
export WLR_HEADLESS_OUTPUTS=${WLR_HEADLESS_OUTPUTS:-2}

echo 'exec "touch '"$work"'/ready"' > "$work/config"
"$work/sway" --config "$work/config" > "$work/sway.log" 2>&1 &
sway_pid=$!

for _ in $(seq 1 80); do
    [ -e "$work/ready" ] && break
    sleep 0.25
done
if [ ! -e "$work/ready" ]; then
    echo "the compositor did not come up:" >&2
    cat "$work/sway.log" >&2
    exit 1
fi

export WAYLAND_DISPLAY=$(basename "$(ls "$XDG_RUNTIME_DIR"/wayland-* | grep -v '\.lock$' | head -1)")
export SWAYSOCK=$(ls "$XDG_RUNTIME_DIR"/sway-ipc.*.sock | head -1)
# The tests that take over a screen look for this rather than for
# WAYLAND_DISPLAY, so running ctest on a real desktop skips them instead of
# grabbing the keyboard of whoever ran it.
export LOB_TEST_COMPOSITOR=1

echo "compositor up on $WAYLAND_DISPLAY with $WLR_HEADLESS_OUTPUTS outputs"

ctest "$@"
