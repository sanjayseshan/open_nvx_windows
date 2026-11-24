#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MESA9X="$ROOT/submodules/mesa9x"
PATCHES=(
    "$ROOT/patches/mesa9x/0002-mesa-25.1.6-wgl-lock-compat.patch"
    "$ROOT/patches/mesa9x/0003-nouveau-nine-pointsize-3dmark-compat.patch"
    "$ROOT/patches/mesa9x/0004-nine-window-fullscreen-robustness.patch"
    "$ROOT/patches/mesa9x/0005-nine-draw-range-trace.patch"
    "$ROOT/patches/mesa9x/0006-nine-draw-range-metadata.patch"
)

for PATCH in "${PATCHES[@]}"; do
    if git -C "$MESA9X" apply --check "$PATCH" 2>/dev/null; then
        git -C "$MESA9X" apply "$PATCH"
        echo "Applied $(basename "$PATCH")."
    elif git -C "$MESA9X" apply --reverse --check "$PATCH" 2>/dev/null; then
        echo "Already applied: $(basename "$PATCH")."
    else
        echo "Cannot apply or verify $(basename "$PATCH") against mesa9x." >&2
        exit 1
    fi
done

PATCH="$ROOT/patches/mesa9x/0007-xp-crt-failure-termination.patch"
if git -C "$MESA9X/winpthreads" apply --check "$PATCH" 2>/dev/null; then
    git -C "$MESA9X/winpthreads" apply "$PATCH"
elif ! git -C "$MESA9X/winpthreads" apply --reverse --check "$PATCH" 2>/dev/null; then
    echo "Cannot apply XP CRT compatibility patch" >&2
    exit 1
fi
