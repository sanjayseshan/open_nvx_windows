#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
XP32_IMAGE="${NVX_XP32_IMAGE:-$ROOT_DIR/winxp_x32.qcow2}"
XP64_IMAGE="${NVX_XP64_IMAGE:-$ROOT_DIR/winxp_x64.qcow2}"
STAGE="${NVX_3DMARK_STAGE:-/tmp/nvx-xp32-3dmark-vnc-stage}"
TIGHTVNC_MSI="${NVX_TIGHTVNC_MSI:-/tmp/tightvnc-2.8.90-x86.msi}"
TIGHTVNC_URL="https://www.tightvnc.com/download/2.8.90/tightvnc-2.8.90-gpl-setup-32bit.msi"
TIGHTVNC_SHA256="669bcf5796c2655fb1a8b556cad8decf25d3008a650ef495b1ed3882aa918211"

for tool in qemu-img guestfish 7z i686-w64-mingw32-gcc hivexregedit sha256sum; do
    command -v "$tool" >/dev/null || { echo "Missing required tool: $tool" >&2; exit 1; }
done
qemu-img info "$XP32_IMAGE" >/dev/null
qemu-img info "$XP64_IMAGE" >/dev/null

if [ ! -f "$TIGHTVNC_MSI" ]; then
    curl -fL --retry 2 --output "$TIGHTVNC_MSI" "$TIGHTVNC_URL"
fi
echo "$TIGHTVNC_SHA256  $TIGHTVNC_MSI" | sha256sum -c -

guestfish --ro -a "$XP64_IMAGE" -i copy-out \
    '/Program Files (x86)/Futuremark/3DMark05' "$STAGE"

i686-w64-mingw32-gcc -O2 -Wall -Wextra -static-libgcc \
    "$ROOT_DIR/test/dump_3dmark_text.c" -luser32 \
    -Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,1 \
    -o "$STAGE/dump_3dmark_text32.exe"
i686-w64-mingw32-gcc -O2 -Wall -Wextra -static-libgcc \
    "$ROOT_DIR/test/dump_screen.c" -lgdi32 -luser32 \
    -Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,1 \
    -o "$STAGE/dump_screen32.exe"
i686-w64-mingw32-gcc -O2 -Wall -Wextra -static-libgcc \
    -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 \
    "$ROOT_DIR/test/run_3dmark_full.c" -luser32 \
    -Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,1 \
    -o "$STAGE/run_3dmark_full32.exe"
i686-w64-mingw32-gcc -O2 -Wall -Wextra -static-libgcc \
    -D_WIN32_WINNT=0x0501 -DWINVER=0x0501 \
    "$ROOT_DIR/test/test_3dmark_launch_diag.c" -lpsapi \
    -Wl,--major-subsystem-version,5 -Wl,--minor-subsystem-version,1 \
    -o "$STAGE/test_3dmark_launch_diag32.exe"

file "$ROOT_DIR/dist_full/x86/d3d9.dll" | grep -q 'PE32 executable.*Intel 80386'
file "$STAGE/run_3dmark_full32.exe" | grep -q 'PE32 executable.*Intel 80386'
file "$STAGE/test_3dmark_launch_diag32.exe" | grep -q 'PE32 executable.*Intel 80386'

TMP_SOFT="$(mktemp /tmp/nvx-xp32-software.XXXXXX)"
cat > "$TMP_SOFT.reg" <<'REG'
Windows Registry Editor Version 5.00

[\TightVNC]

[\TightVNC\Server]
"AcceptHttpConnections"=dword:00000000
"AcceptRfbConnections"=dword:00000001
"RfbPort"=dword:0000170c
"DisconnectAction"=dword:00000000
"AcceptWithoutAuthentication"=dword:00000001
"UseVncAuthentication"=dword:00000000
"UseControlAuthentication"=dword:00000000
REG

guestfish --rw -a "$XP32_IMAGE" -i <<GUEST
rm-rf "/Program Files/Futuremark/3DMark05"
mkdir-p "/Program Files/Futuremark"
copy-in "$STAGE/3DMark05" "/Program Files/Futuremark"
upload "$ROOT_DIR/dist_full/x86/d3d9.dll" "/Program Files/Futuremark/3DMark05/d3d9.dll"
upload "$STAGE/run_3dmark_full32.exe" "/Program Files/Futuremark/3DMark05/run_3dmark_full.exe"
upload "$TIGHTVNC_MSI" /test_accel/tightvnc-setup-x86.msi
upload "$ROOT_DIR/test/run_test_xp32_3dmark.bat" /test_accel/run_test.bat
upload "$ROOT_DIR/test/run_test_xp32_3dmark.bat" "/Documents and Settings/All Users/Start Menu/Programs/Startup/run_accel_test.bat"
upload "$ROOT_DIR/test/watch_3dmark_text.bat" /test_accel/watch_3dmark_text.bat
upload "$STAGE/run_3dmark_full32.exe" /test_accel/run_3dmark_full.exe
upload "$STAGE/test_3dmark_launch_diag32.exe" /test_accel/test_3dmark_launch_diag.exe
upload "$ROOT_DIR/test/test_3dmark_caps_eval.exe" /test_accel/test_3dmark_caps_eval.exe
upload "$STAGE/dump_3dmark_text32.exe" /test_accel/dump_3dmark_text32.exe
upload "$STAGE/dump_screen32.exe" /test_accel/dump_screen32.exe
upload "$ROOT_DIR/test/3d_tests/3DMark05_v130_1901.exe" /test_accel/3DMark05_v130_1901.exe
download /WINDOWS/system32/config/software "$TMP_SOFT"
GUEST

hivexregedit --merge "$TMP_SOFT" "$TMP_SOFT.reg"
guestfish --rw -a "$XP32_IMAGE" -i <<GUEST
upload "$TMP_SOFT" /WINDOWS/system32/config/software
rm_f /test_accel/run_3dmark.flag
rm_f /test_accel/run_3dmark_diag.flag
rm-rf /test_accel/startup_test.lock
GUEST

echo "Prepared XP x86 image with the installed 3DMark05 folder and TightVNC 2.8.90 x86 MSI."
echo "3DMark is not armed yet. Set /test_accel/run_3dmark.flag to run it at next boot."
echo "Guest VNC: TCP 5900 (QEMU host forwarding is local-only on host port 5911)."
