#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMG="${1:-${NVX_IMG:-$ROOT/win98_small.img}}"
TEST_DIR="$ROOT/test/win98"

for binary in antigravity.exe wglgears.exe runner.exe legacy_ddraw.exe nine_d3d9.exe; do
    if [[ ! -s "$TEST_DIR/$binary" ]]; then
        echo "Missing $TEST_DIR/$binary; run test/build_win98_graphics_tests.sh first" >&2
        exit 1
    fi
done
if [[ ! -f "$IMG" ]]; then
    echo "Missing Win98 image: $IMG" >&2
    exit 1
fi
for pid in $(ps -C qemu-system-x86_64 -o pid= 2>/dev/null || true); do
    cmdline="$(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null || true)"
    case "$cmdline" in *"$IMG"*)
        echo "QEMU has $IMG open (PID $pid); refusing to modify the image" >&2
        exit 1
    esac
done

ONE_SHOT_COMMAND=$'rm-f "/test_accel/win98/2d-only-once.txt"\nrm-f "/test_accel/win98/3dmark03-once.txt"\nrm-f /test_accel/win98/debug-3dmark03-once.txt'
if [[ "${NVX_WIN98_3DMARK03_ONCE:-0}" == "1" ]]; then
    ONE_SHOT_COMMAND=$'rm-f "/test_accel/win98/2d-only-once.txt"\nwrite /test_accel/win98/3dmark03-once.txt "1"'
    if [[ "${NVX_WIN98_DEBUG_3DMARK03:-0}" == "1" ]]; then
        ONE_SHOT_COMMAND+=$'\nwrite /test_accel/win98/debug-3dmark03-once.txt "1"'
    else
        ONE_SHOT_COMMAND+=$'\nrm-f /test_accel/win98/debug-3dmark03-once.txt'
    fi
elif [[ "${NVX_WIN98_2D_ONLY:-0}" == "1" ]]; then
    ONE_SHOT_COMMAND=$'rm-f "/test_accel/win98/3dmark03-once.txt"\nwrite /test_accel/win98/2d-only-once.txt "1"'
fi

guestfish --rw -a "$IMG" -m /dev/sda1 <<EOF
mkdir-p /test_accel/win98
mkdir-p "/WINDOWS/ALL USERS/Start Menu/Programs/Startup"
upload "$TEST_DIR/antigravity.exe" /test_accel/win98/antigravity.exe
upload "$TEST_DIR/wglgears.exe" /test_accel/win98/wglgears.exe
upload "$TEST_DIR/runner.exe" /test_accel/win98/runner.exe
upload "$TEST_DIR/legacy_ddraw.exe" /test_accel/win98/legacy_ddraw.exe
upload "$TEST_DIR/nine_d3d9.exe" /test_accel/win98/nine_d3d9.exe
upload "$ROOT/dist_win98/nine/d3d9.dll" /test_accel/win98/d3d9.dll
upload "$ROOT/dist_win98/nine/mesa99.dll" /test_accel/win98/mesa99.dll
upload "$ROOT/dist_win98/nine/wined3d.dll" /test_accel/win98/wined3d.dll
upload "$ROOT/dist_win98/nine/wined9.dll" /test_accel/win98/wined9.dll
rm-f "/test_accel/win98/complete.txt"
$ONE_SHOT_COMMAND
rm-f "/WINDOWS/START MENU/PROGRAMS/STARTUP/NVXTEST.EXE"
upload "$TEST_DIR/runner.exe" "/WINDOWS/ALL USERS/Start Menu/Programs/Startup/NVXTEST.EXE"
sync
EOF
if [[ "${NVX_WIN98_2D_ONLY:-0}" == "1" ]]; then
    echo "Installed a one-shot 2D-only boot test in $IMG. It removes its marker and skips all 3D tests."
elif [[ "${NVX_WIN98_3DMARK03_ONCE:-0}" == "1" ]]; then
    if [[ "${NVX_WIN98_DEBUG_3DMARK03:-0}" == "1" ]]; then
        echo "Installed a one-shot 3DMark03 launch with Win32 debug-event capture. Exceptions, code addresses, instructions and module maps will be reported over COM1."
    else
        echo "Installed a one-shot 3DMark03 launch in $IMG. The runner leaves it open and reports D9Switcher diagnostics over COM1."
    fi
else
    echo "Installed Win98 Antigravity 2D and WGL 3D tests in $IMG. A single-instance runner is in All Users Startup and reports over COM1."
fi
