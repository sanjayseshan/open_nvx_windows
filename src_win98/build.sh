#!/bin/sh
# Reproducible one-monitor Win98 SE build for the display driver and VxD.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WIN98_SINGLE_MONITOR=${WIN98_SINGLE_MONITOR:-1}
NVX_WIN98_MESA_ICD=${NVX_WIN98_MESA_ICD:-}

if [ ! -f "$ROOT_DIR/dist_full/gsp_ga104.bin" ]; then
    echo "Missing dist_full/gsp_ga104.bin; required GSP firmware input." >&2
    exit 1
fi

WIN98_SINGLE_MONITOR="$WIN98_SINGLE_MONITOR" "$SCRIPT_DIR/winvbe_drv/build.sh"
WIN98_SINGLE_MONITOR="$WIN98_SINGLE_MONITOR" "$SCRIPT_DIR/build_vxd.sh"
cp "$SCRIPT_DIR/nvx_win98.inf" "$ROOT_DIR/dist_win98/nvx.inf"
cp "$SCRIPT_DIR/install.bat" "$ROOT_DIR/dist_win98/install.bat"
cp "$SCRIPT_DIR/RELEASE_README.txt" "$ROOT_DIR/dist_win98/README.txt"
if [ -f "$ROOT_DIR/dist_win98/attach_sec.exe" ]; then
    echo "NOTE: existing attach_sec.exe is legacy and untested; the package remains single-monitor."
fi
cp "$ROOT_DIR/dist_full/gsp_ga104.bin" "$ROOT_DIR/dist_win98/GSPGA104.BIN"

if [ -n "$NVX_WIN98_MESA_ICD" ]; then
    if [ ! -s "$NVX_WIN98_MESA_ICD" ]; then
        echo "NVX_WIN98_MESA_ICD does not name a non-empty Mesa ICD: $NVX_WIN98_MESA_ICD" >&2
        exit 1
    fi
    install -m 0644 "$NVX_WIN98_MESA_ICD" "$ROOT_DIR/dist_win98/nvxogl32.dll"
    # This experimental ICD is opt-in. Add its file and OpenGLDrivers mapping
    # only when the caller supplies a Win98-targeted Mesa build artifact.
    python3 - "$ROOT_DIR/dist_win98/nvx.inf" <<'PY'
from pathlib import Path
import sys

path = Path(sys.argv[1])
text = path.read_text()
def replace_once(old, new):
    global text
    if text.count(old) != 1:
        raise SystemExit(f"expected one INF anchor, found {text.count(old)}: {old!r}")
    text = text.replace(old, new, 1)

replace_once('nvx.drv = 1\n', 'nvx.drv = 1\nnvxogl32.dll = 1\n')
replace_once('CopyFiles = Driver.Copy, Firmware.Copy',
             'CopyFiles = Driver.Copy, Firmware.Copy, MesaICD.Copy')
replace_once('AddReg    = Driver.AddReg',
             'AddReg    = Driver.AddReg, MesaICD.AddReg')
replace_once('[Firmware.Copy]\n',
             '[MesaICD.Copy]\nnvxogl32.dll\n\n[Firmware.Copy]\n')
text += ('\n[MesaICD.AddReg]\n'
         'HKLM,"Software\\Microsoft\\Windows\\CurrentVersion\\OpenGLDrivers",NVX,,nvxogl32.dll\n')
path.write_text(text)
PY
else
    # Never leave an old experimental ICD beside a package whose INF does not
    # install or register it.
    rm -f "$ROOT_DIR/dist_win98/nvxogl32.dll"
fi

echo "Win98 SE build package is in dist_win98/"
ls -lh "$ROOT_DIR/dist_win98/nvx.drv" \
       "$ROOT_DIR/dist_win98/nvx.vxd" \
       "$ROOT_DIR/dist_win98/nvx.inf" \
       "$ROOT_DIR/dist_win98/GSPGA104.BIN"
if [ -f "$ROOT_DIR/dist_win98/nvxogl32.dll" ]; then
    ls -lh "$ROOT_DIR/dist_win98/nvxogl32.dll"
    echo "Experimental Mesa ICD is opt-in; set [WINVBE] OpenGL=2 after installing its INF."
fi
if [ "$WIN98_SINGLE_MONITOR" != "1" ]; then
    ls -lh "$ROOT_DIR/dist_win98/nvx2.drv"
fi
