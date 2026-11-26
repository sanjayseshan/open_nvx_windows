#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
IMG="${1:-${NVX_IMG:-$ROOT/win98_small.img}}"
PKG="$ROOT/dist_win98/legacy"
SYS="/WINDOWS/SYSTEM"

for file in ddraw.dll winedd.dll wined3d.dll legacy_ddraw.exe; do
    [[ -s "$PKG/$file" ]] || {
        echo "Missing $PKG/$file; run test/build_win98_legacy_ddraw.sh first" >&2
        exit 1
    }
done
[[ -f "$IMG" ]] || { echo "Missing Win98 image: $IMG" >&2; exit 1; }
for pid in $(ps -C qemu-system-x86_64 -o pid= 2>/dev/null || true); do
    cmdline="$(tr '\0' ' ' <"/proc/$pid/cmdline" 2>/dev/null || true)"
    case "$cmdline" in *"$IMG"*)
        echo "QEMU has $IMG open (PID $pid); refusing to modify the image" >&2
        exit 1
    esac
done

guest_exists() {
    local result
    result="$(guestfish --ro -a "$IMG" -m /dev/sda1 exists "$1")"
    [[ "$result" == true ]]
}

mkdir -p "$ROOT/test_logs/win98_backups"
STAMP="$(date +%Y%m%d-%H%M%S)"
ORIGINAL="$ROOT/test_logs/win98_backups/ddraw-original-$STAMP.dll"
DDSYS="$ROOT/test_logs/win98_backups/ddsys-$STAMP.dll"

if guest_exists "$SYS/DDSYS.DLL"; then
    echo "Existing $SYS/DDSYS.DLL found; preserving and reusing it."
    guestfish --ro -a "$IMG" -m /dev/sda1 download "$SYS/DDSYS.DLL" "$DDSYS"
else
    echo "Backing up the original system DDRAW.DLL before installing the switcher."
    guestfish --ro -a "$IMG" -m /dev/sda1 download "$SYS/DDRAW.DLL" "$ORIGINAL"
    cp "$ORIGINAL" "$DDSYS"
    python3 - "$DDSYS" <<'PY'
import pathlib, sys
p = pathlib.Path(sys.argv[1])
b = p.read_bytes()
old, new = b"DDRAW.DLL", b"DDSYS.DLL"
old16, new16 = old.decode().encode("utf-16le"), new.decode().encode("utf-16le")
count = b.upper().count(old)
if not count:
    raise SystemExit("Original DDRAW.DLL has no internal DDRAW.DLL reference; refusing unsafe switcher install")
b = b.replace(old, new).replace(old.lower(), new.lower())
b = b.replace(old16, new16)
p.write_bytes(b)
print(f"Prepared DDSYS.DLL, renamed {count} ASCII DDRAW.DLL reference(s)")
PY
fi

guestfish --rw -a "$IMG" -m /dev/sda1 <<EOF
upload "$DDSYS" $SYS/DDSYS.DLL
upload "$PKG/ddraw.dll" $SYS/DDRAW.DLL
upload "$PKG/winedd.dll" $SYS/WINEDD.DLL
upload "$PKG/wined3d.dll" $SYS/WINED3D.DLL
mkdir-p /test_accel/win98
upload "$PKG/legacy_ddraw.exe" /test_accel/win98/legacy_ddraw.exe
sync
EOF

echo "Installed Wine9x legacy DirectDraw runtime and smoke test in $IMG."
echo "Original guest DDRAW.DLL backup: $ORIGINAL"
echo "DDSYS.DLL currently installed from: $DDSYS"
echo "Run test/run_vfio_win98_autoretry.sh; startup runner invokes legacy_ddraw.exe."
