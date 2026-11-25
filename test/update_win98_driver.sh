#!/usr/bin/env bash
set -euo pipefail

NVX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIN98_IMG="${1:-${NVX_IMG:-$NVX_ROOT/win98_small.img}}"
PKG_DIR="$NVX_ROOT/dist_win98"
TMP_SYS_INI="$(mktemp /tmp/nvx-win98-system.XXXXXX)"
TMP_MSDOS_SYS="$(mktemp /tmp/nvx-win98-msdos.XXXXXX)"
trap 'rm -f "$TMP_SYS_INI" "$TMP_MSDOS_SYS"' EXIT

if [ ! -f "$WIN98_IMG" ]; then
    echo "Error: Win98 image not found: $WIN98_IMG" >&2
    exit 1
fi
for f in nvx.inf nvx.drv nvx.vxd GSPGA104.BIN; do
    if [ ! -f "$PKG_DIR/$f" ]; then
        echo "Error: missing single-monitor package file $PKG_DIR/$f" >&2
        exit 1
    fi
done
for qemu_pid in $(ps -C qemu-system-x86_64 -o pid= 2>/dev/null || true); do
    qemu_cmdline=$(tr '\0' ' ' <"/proc/$qemu_pid/cmdline" 2>/dev/null || true)
    case "$qemu_cmdline" in
        *"$WIN98_IMG"*)
            echo "Error: $WIN98_IMG appears to be open in QEMU; refusing to modify it." >&2
            exit 1
            ;;
    esac
done

echo "==> Installing single-monitor Win98 package into $WIN98_IMG"
guestfish --ro -a "$WIN98_IMG" -m /dev/sda1 download /WINDOWS/SYSTEM.INI "$TMP_SYS_INI"
python3 - "$TMP_SYS_INI" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
lines = path.read_text(errors="replace").splitlines()
out = []
section = ""
skip_section = False
for line in lines:
    stripped = line.strip()
    if stripped.startswith("[") and stripped.endswith("]"):
        section = stripped[1:-1].strip().lower()
        skip_section = section == "winvbe2"
    if skip_section:
        continue
    key, _, value = stripped.partition("=")
    key = key.strip().lower()
    value = value.strip().lower()
    # This image uses QEMU's emulated i8042/PS/2 mouse. Canonicalize the
    # SYSTEM.INI mouse configuration below to exactly the two supported
    # entries; remove stale USB HID/VxD and alternate driver settings.
    if key.startswith("mouse"):
        continue
    if key in {"device", "minivdd"} and ("mouse" in value or "hid" in value):
        continue
    if section == "boot" and stripped.lower().startswith("display.drv="):
        line = "display.drv=nvx.drv"
    if section == "386enh" and stripped.lower() in {
        "device=nvx.vxd", "minivdd=nvx.vxd", "display=*vdd,*vflatd",
    }:
        continue
    out.append(line)

def ensure(section_name, key, value):
    start = None
    end = len(out)
    for i, line in enumerate(out):
        s = line.strip()
        if s.startswith("[") and s.endswith("]"):
            name = s[1:-1].strip().lower()
            if name == section_name.lower():
                start = i
                for j in range(i + 1, len(out)):
                    sj = out[j].strip()
                    if sj.startswith("[") and sj.endswith("]"):
                        end = j
                        break
                break
    if start is None:
        out.extend([f"[{section_name}]", f"{key}={value}"])
        return
    for i in range(start + 1, end):
        if out[i].strip().lower().startswith(key.lower() + "="):
            if key.lower() == "device":
                if out[i].split("=", 1)[1].strip().lower() == value.lower():
                    return
            else:
                out[i] = f"{key}={value}"
                return
    out.insert(end, f"{key}={value}")

ensure("boot", "display.drv", "nvx.drv")
ensure("boot", "mouse.drv", "mouse.drv")
ensure("386Enh", "device", "nvx.vxd")
ensure("386Enh", "minivdd", "nvx.vxd")
ensure("386Enh", "display", "*vdd,*vflatd")
ensure("386Enh", "mouse", "*vmouse")
ensure("WINVBE", "DibEngine", "0")
ensure("WINVBE", "Debug", "1")
path.write_bytes(("\r\n".join(out) + "\r\n").encode())
PY

if [ "${NVX_WIN98_BOOTLOG:-0}" = "1" ]; then
    guestfish --ro -a "$WIN98_IMG" -m /dev/sda1 download /MSDOS.SYS "$TMP_MSDOS_SYS"
    python3 - "$TMP_MSDOS_SYS" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
lines = path.read_text(errors="replace").splitlines()
values = {"bootmenu": "1", "bootmenudefault": "2", "bootmenudelay": "1"}
found = set()
out = []
in_options = False
for line in lines:
    stripped = line.strip()
    if stripped.startswith("[") and stripped.endswith("]"):
        in_options = stripped[1:-1].strip().lower() == "options"
    key = stripped.split("=", 1)[0].strip().lower() if "=" in stripped else ""
    if in_options and key in values:
        out.append(f"{key}={values[key]}")
        found.add(key)
    else:
        out.append(line)
for key, value in values.items():
    if key not in found:
        out.append(f"{key}={value}")
path.write_bytes(("\r\n".join(out) + "\r\n").encode())
PY
fi

BOOTLOG_UPLOAD=""
if [ "${NVX_WIN98_BOOTLOG:-0}" = "1" ]; then
    BOOTLOG_UPLOAD="upload \"$TMP_MSDOS_SYS\" /MSDOS.SYS"
fi

guestfish --rw -a "$WIN98_IMG" -m /dev/sda1 <<EOF
mkdir-p /WINDOWS/INF
mkdir-p /WINDOWS/SYSTEM
mkdir-p /WINDOWS/SYSTEM/VMM32
mkdir-p /DRIVERS/NVX
upload "$PKG_DIR/nvx.inf" /WINDOWS/INF/nvx.inf
upload "$PKG_DIR/nvx.inf" /DRIVERS/NVX/nvx.inf
upload "$PKG_DIR/nvx.drv" /WINDOWS/SYSTEM/nvx.drv
upload "$PKG_DIR/nvx.drv" /WINDOWS/nvx.drv
upload "$PKG_DIR/nvx.drv" /DRIVERS/NVX/nvx.drv
upload "$PKG_DIR/nvx.vxd" /WINDOWS/SYSTEM/nvx.vxd
upload "$PKG_DIR/nvx.vxd" /WINDOWS/SYSTEM/VMM32/nvx.vxd
upload "$PKG_DIR/nvx.vxd" /DRIVERS/NVX/nvx.vxd
upload "$PKG_DIR/GSPGA104.BIN" /WINDOWS/SYSTEM/GSPGA104.BIN
upload "$PKG_DIR/GSPGA104.BIN" /GSPGA104.BIN
upload "$NVX_ROOT/src_win98/nvx_reg.reg" /NVX_REG.REG
upload "$NVX_ROOT/src_win98/slog.com" /SLOG.COM
upload "$NVX_ROOT/src_win98/slog.com" /WINDOWS/SLOG.COM
upload "$TMP_SYS_INI" /WINDOWS/SYSTEM.INI
$BOOTLOG_UPLOAD
sync
EOF

rm -f "$TMP_SYS_INI" "$TMP_MSDOS_SYS"
trap - EXIT
echo "==> Single-monitor package deployed. WINVBE2 and its second-head registry entry are disabled on next boot."
