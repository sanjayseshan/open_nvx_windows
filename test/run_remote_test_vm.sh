#!/bin/bash
# Launch Windows XP test VM with standard VGA + GTK display window + RDP port forward

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

qemu-system-x86_64 \
    -enable-kvm \
    -cpu host \
    -smp 2 \
    -m 2048 \
    -vga std \
    -display gtk \
    -drive file=winxp_x64.qcow2,format=qcow2,media=disk,if=ide,index=0 \
    -drive file=nvx_driver_full.iso,media=cdrom,if=ide,index=1 \
    -boot c \
    -netdev user,id=net0,hostfwd=tcp:0.0.0.0:3389-:3389 \
    -device rtl8139,netdev=net0 \
    -serial stdio \
    -monitor unix:/tmp/test-vm-monitor.sock,server,nowait
