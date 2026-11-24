#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"
NVX_IMAGE="${NVX_XP32_IMAGE:-$ROOT_DIR/winxp_x32.qcow2}"
NVX_VNC_DISPLAY="${NVX_VNC_DISPLAY:-12}"
NVX_VNC_PORT=$((5900 + NVX_VNC_DISPLAY))
NVX_LOG="${NVX_SERIAL_LOG:-$ROOT_DIR/test_logs/xp32-novfio-$(date +%Y%m%d-%H%M%S).log}"
NVX_HMP_SOCKET="${NVX_HMP_SOCKET:-/tmp/nvx-xp32-hmp-$$.sock}"

qemu-img info "$NVX_IMAGE" >/dev/null
if ss -ltn | awk '{print $4}' | grep -Eq "(^|:)${NVX_VNC_PORT}$"; then
    echo "Error: TCP ${NVX_VNC_PORT} is already in use." >&2
    exit 1
fi

mkdir -p "$(dirname "$NVX_LOG")"
echo "Starting non-VFIO XP x86 desktop. This mode is for setup/control only."
echo "QEMU VNC display: 127.0.0.1:${NVX_VNC_PORT}"
echo "Guest TightVNC forwarding: 127.0.0.1:5911 -> guest :5900"
echo "Serial log: $NVX_LOG"
echo "HMP monitor: $NVX_HMP_SOCKET"

exec qemu-system-x86_64 \
    -enable-kvm \
    -m 3072 \
    -cpu host \
    -smp 4 \
    -vga cirrus \
    -netdev user,id=net0,hostfwd=tcp:127.0.0.1:5911-:5900 \
    -device rtl8139,netdev=net0 \
    -drive "file=$NVX_IMAGE,format=qcow2,if=ide" \
    -display "vnc=127.0.0.1:${NVX_VNC_DISPLAY}" \
    -serial "file:$NVX_LOG" \
    -no-reboot \
    -monitor "unix:$NVX_HMP_SOCKET,server,nowait" \
    -monitor stdio
