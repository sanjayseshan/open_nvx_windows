#!/usr/bin/env bash
set -euo pipefail

NVX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WIN98_IMG="${1:-${NVX_IMG:-$NVX_ROOT/win98_small.img}}"
PKG_DIR="$NVX_ROOT/dist_win98"
NINE_DIR="$PKG_DIR/nine"
TMP_SYS_INI="$(mktemp /tmp/nvx-win98-system.XXXXXX)"
TMP_MSDOS_SYS="$(mktemp /tmp/nvx-win98-msdos.XXXXXX)"
TMP_WIN_INI="$(mktemp /tmp/nvx-win98-win.XXXXXX)"
trap 'rm -f "$TMP_SYS_INI" "$TMP_MSDOS_SYS" "$TMP_WIN_INI"' EXIT

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
INSTALL_NINE="${NVX_WIN98_INSTALL_NINE:-1}"
NINE_UPLOAD_COMMANDS=""
if [[ "$INSTALL_NINE" == "1" ]]; then
    for f in d3d9.dll mesa99.dll wined3d.dll wined9.dll; do
        if [[ ! -s "$NINE_DIR/$f" ]]; then
            echo "Error: missing Win98 D3D runtime component $NINE_DIR/$f" >&2
            echo "Run test/build_win98_nine.sh to build the Nine/Wine9x runtime." >&2
            exit 1
        fi
    done
    NINE_UPLOAD_COMMANDS=$(cat <<EOF
upload "$NINE_DIR/d3d9.dll" /WINDOWS/SYSTEM/d3d9.dll
upload "$NINE_DIR/mesa99.dll" /WINDOWS/SYSTEM/mesa99.dll
upload "$NINE_DIR/wined3d.dll" /WINDOWS/SYSTEM/wined3d.dll
upload "$NINE_DIR/wined9.dll" /WINDOWS/SYSTEM/wined9.dll
EOF
)
fi
if [ -f "$PKG_DIR/nvxogl32.dll" ]; then
    echo "==> Experimental NVX Mesa ICD is present in the package."
elif [ "${NVX_WIN98_USE_MESA_ICD:-0}" = "1" ]; then
    echo "Error: NVX_WIN98_USE_MESA_ICD=1 but dist_win98/nvxogl32.dll is missing." >&2
    exit 1
fi
if [ "${NVX_WIN98_USE_MESA_ICD:-0}" = "1" ] &&
   ! grep -Fq 'NVX,,nvxogl32.dll' "$PKG_DIR/nvx.inf"; then
    echo "Error: NVX ICD DLL is present but nvx.inf has no NVX OpenGLDrivers registration." >&2
    echo "Rebuild with NVX_WIN98_MESA_ICD=/path/to/nvxogl32.dll ./src_win98/build.sh" >&2
    exit 1
fi
for qemu_pid in $(ps -C qemu-system-x86_64 -o pid= 2>/dev/null || true); do
    qemu_cmdline=$(tr '\0' ' ' <"/proc/$qemu_pid/cmdline" 2>/dev/null || true)
    case "$qemu_cmdline" in
        *"$WIN98_IMG"*)
            echo "Error: $WIN98_IMG appears to be open in QEMU; refusing to modify it." >&2
            exit 1
            ;;
    esac
done

if [[ "$INSTALL_NINE" == "1" ]]; then
    backup_guest_file() {
        local source="$1" backup="$2" source_exists backup_exists
        source_exists=$(guestfish --ro -a "$WIN98_IMG" -m /dev/sda1 exists "$source" | tail -n 1 | tr -d '\r')
        backup_exists=$(guestfish --ro -a "$WIN98_IMG" -m /dev/sda1 exists "$backup" | tail -n 1 | tr -d '\r')
        if [[ "$source_exists" == "true" && "$backup_exists" != "true" ]]; then
            guestfish --rw -a "$WIN98_IMG" -m /dev/sda1 <<EOF
mkdir-p /DRIVERS/NVX/originals
cp "$source" "$backup"
sync
EOF
            echo "==> Preserved existing $source at $backup"
        fi
    }
    backup_guest_file /WINDOWS/SYSTEM/d3d9.dll /DRIVERS/NVX/originals/d3d9.dll
    backup_guest_file /WINDOWS/SYSTEM/wined3d.dll /DRIVERS/NVX/originals/wined3d.dll

    # D9Switcher calls the native/system backend "msd3d9.dll" so it can
    # coexist with its own replacement at d3d9.dll. Preserve the original
    # Direct3D runtime under that name when installing the switcher. Do not
    # overwrite an existing runtime supplied by a later DirectX installation.
    system_d3d9_exists=$(guestfish --ro -a "$WIN98_IMG" -m /dev/sda1 \
        exists /WINDOWS/SYSTEM/msd3d9.dll | tail -n 1 | tr -d '\r')
    original_d3d9_exists=$(guestfish --ro -a "$WIN98_IMG" -m /dev/sda1 \
        exists /DRIVERS/NVX/originals/d3d9.dll | tail -n 1 | tr -d '\r')
    if [[ "$system_d3d9_exists" != "true" && "$original_d3d9_exists" == "true" ]]; then
        guestfish --rw -a "$WIN98_IMG" -m /dev/sda1 <<'EOF'
cp /DRIVERS/NVX/originals/d3d9.dll /WINDOWS/SYSTEM/msd3d9.dll
sync
EOF
        echo "==> Installed preserved system D3D9 as /WINDOWS/SYSTEM/msd3d9.dll"
    elif [[ "$system_d3d9_exists" != "true" ]]; then
        echo "Warning: no preserved native D3D9 runtime found; D9Switcher system routing will fail." >&2
    fi
fi

echo "==> Installing single-monitor Win98 package into $WIN98_IMG"
guestfish --ro -a "$WIN98_IMG" -m /dev/sda1 download /WINDOWS/SYSTEM.INI "$TMP_SYS_INI"
python3 - "$TMP_SYS_INI" <<'PY'
import pathlib
import os
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
ensure("WINVBE", "DibEngine", os.environ.get("NVX_WIN98_DIB_ENGINE", "0"))
ensure("WINVBE", "ShadowBuffer", os.environ.get("NVX_WIN98_SHADOW_BUFFER", "0"))
ensure("WINVBE", "GpuFill", os.environ.get("NVX_WIN98_GPU_FILL", "0"))
ensure("WINVBE", "GpuCopy", os.environ.get("NVX_WIN98_GPU_COPY", "0"))
if os.environ.get("NVX_WIN98_USE_MESA_ICD", "0") == "1":
    ensure("WINVBE", "OpenGL", "2")
ensure("WINVBE", "Debug", "1")
path.write_bytes(("\r\n".join(out) + "\r\n").encode())
PY

guestfish --ro -a "$WIN98_IMG" -m /dev/sda1 download /MSDOS.SYS "$TMP_MSDOS_SYS"
python3 - "$TMP_MSDOS_SYS" "${NVX_WIN98_BOOTLOG:-0}" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
enable_bootlog = sys.argv[2] == "1"
lines = path.read_text(errors="replace").splitlines()
values = {"bootmenu": "1", "bootmenudefault": "2", "bootmenudelay": "1"} if enable_bootlog else {}
found = set()
out = []
in_options = False
for line in lines:
    stripped = line.strip()
    if stripped.startswith("[") and stripped.endswith("]"):
        in_options = stripped[1:-1].strip().lower() == "options"
    key = stripped.split("=", 1)[0].strip().lower() if "=" in stripped else ""
    # SearchPath is not a supported MSDOS.SYS [Options] key. Some old test
    # images contain SearchPath=0 and Windows 98 reports it as an invalid
    # setting on every boot; remove it while preserving the other boot keys.
    if in_options and key == "searchpath":
        continue
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

guestfish --ro -a "$WIN98_IMG" -m /dev/sda1 download /WINDOWS/WIN.INI "$TMP_WIN_INI"
python3 - "$TMP_WIN_INI" <<'PY'
import pathlib
import sys

path = pathlib.Path(sys.argv[1])
lines = path.read_text(errors="replace").splitlines()
out = []
for line in lines:
    key, sep, value = line.partition("=")
    if sep and key.strip().lower() in {"run", "load"} and "attach_sec" in value.lower():
        continue
    out.append(line)
path.write_bytes(("\r\n".join(out) + "\r\n").encode())
PY

BOOTLOG_UPLOAD="upload \"$TMP_MSDOS_SYS\" /MSDOS.SYS"

# Explorer persists converted icon image lists across boots. Rebuild these
# after changing the display driver so earlier conversion defects do not
# survive an otherwise corrected deployment.
guestfish --rw -a "$WIN98_IMG" -m /dev/sda1 <<EOF
mkdir-p /WINDOWS/INF
mkdir-p /WINDOWS/SYSTEM
mkdir-p /WINDOWS/SYSTEM/VMM32
mkdir-p "/WINDOWS/ALL USERS/Start Menu/Programs/Startup"
mkdir-p /DRIVERS/NVX
mkdir-p /test_accel/win98
rm-f /test_accel/win98/complete.txt
rm-f /WINDOWS/ShellIconCache
rm-f /WINDOWS/ATTACH_SEC.EXE
rm-f /WINDOWS/attach_sec.exe
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
$NINE_UPLOAD_COMMANDS
upload "$NVX_ROOT/src_win98/nvx_reg.reg" /NVX_REG.REG
upload "$NVX_ROOT/src_win98/slog.com" /SLOG.COM
upload "$NVX_ROOT/src_win98/slog.com" /WINDOWS/SLOG.COM
upload "$TMP_SYS_INI" /WINDOWS/SYSTEM.INI
upload "$TMP_WIN_INI" /WINDOWS/WIN.INI
$BOOTLOG_UPLOAD
sync
EOF

if [ -f "$PKG_DIR/nvxogl32.dll" ]; then
    guestfish --rw -a "$WIN98_IMG" -m /dev/sda1 upload \
        "$PKG_DIR/nvxogl32.dll" /WINDOWS/SYSTEM/nvxogl32.dll
fi
rm -f "$TMP_SYS_INI" "$TMP_MSDOS_SYS" "$TMP_WIN_INI"
trap - EXIT
echo "==> Single-monitor package deployed. WINVBE2, the stale ATTACH_SEC autostart, and its executable are removed."
if [[ "$INSTALL_NINE" == "1" ]]; then
    echo "==> Installed the Wine9x D3D9 switcher and Mesa Nine runtime in WINDOWS/SYSTEM. Original d3d9.dll and wined3d.dll are preserved under DRIVERS/NVX/originals."
fi
if [ -f "$PKG_DIR/nvxogl32.dll" ]; then
    echo "==> The Mesa ICD is copied to WINDOWS/SYSTEM."
    if [ "${NVX_WIN98_USE_MESA_ICD:-0}" = "1" ]; then
        echo "==> SYSTEM.INI selects [WINVBE] OpenGL=2. The test runner writes the NVX OpenGLDrivers value before launching tests; a normal Device Manager INF install writes it during installation."
    else
        echo "==> Existing [WINVBE] OpenGL selection was preserved; use NVX_WIN98_USE_MESA_ICD=1 to select/register it for a test run."
    fi
fi
