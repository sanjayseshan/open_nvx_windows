#!/bin/sh
# Reproducible one-monitor Win98 SE build for the display driver and VxD.
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT_DIR=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)
WIN98_SINGLE_MONITOR=${WIN98_SINGLE_MONITOR:-1}

if [ ! -f "$ROOT_DIR/dist_full/gsp_ga104.bin" ]; then
    echo "Missing dist_full/gsp_ga104.bin; required GSP firmware input." >&2
    exit 1
fi

WIN98_SINGLE_MONITOR="$WIN98_SINGLE_MONITOR" "$SCRIPT_DIR/winvbe_drv/build.sh"
WIN98_SINGLE_MONITOR="$WIN98_SINGLE_MONITOR" "$SCRIPT_DIR/build_vxd.sh"

cp "$SCRIPT_DIR/nvx_win98.inf" "$ROOT_DIR/dist_win98/nvx.inf"
cp "$ROOT_DIR/dist_full/gsp_ga104.bin" "$ROOT_DIR/dist_win98/GSPGA104.BIN"

echo "Win98 SE build package is in dist_win98/"
ls -lh "$ROOT_DIR/dist_win98/nvx.drv" \
       "$ROOT_DIR/dist_win98/nvx.vxd" \
       "$ROOT_DIR/dist_win98/nvx.inf" \
       "$ROOT_DIR/dist_win98/GSPGA104.BIN"
if [ "$WIN98_SINGLE_MONITOR" != "1" ]; then
    ls -lh "$ROOT_DIR/dist_win98/nvx2.drv"
fi
