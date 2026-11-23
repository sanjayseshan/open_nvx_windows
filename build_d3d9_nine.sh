#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MESA9X="$ROOT/submodules/mesa9x"
OUT="$ROOT/dist_full/x86/d3d9.dll"

command -v i686-w64-mingw32-objdump >/dev/null || {
    echo "Missing i686-w64-mingw32-objdump" >&2
    exit 1
}

echo "==> Building x86 Gallium Nine from mesa9x (Mesa 23.1.x Nine frontend)"
make -C "$MESA9X" mesa99.dll

NINE_DLL="$MESA9X/mesa99.dll"
[ -f "$NINE_DLL" ] || { echo "Nine build did not produce $NINE_DLL" >&2; exit 1; }

mkdir -p "$(dirname "$OUT")"
cp "$NINE_DLL" "$OUT"

if ! file "$OUT" | grep 'PE32 executable.*Intel 80386' >/dev/null; then
    echo "Nine output is not an x86 PE DLL: $OUT" >&2
    exit 1
fi
EXPORTS="$(i686-w64-mingw32-objdump -p "$OUT")"
if [[ "$EXPORTS" != *Direct3DCreate9* ]]; then
    echo "Nine output does not export Direct3DCreate9" >&2
    exit 1
fi
if [[ "$EXPORTS" != *Direct3DCreate9Ex* ]]; then
    echo "Nine output does not export Direct3DCreate9Ex" >&2
    exit 1
fi

echo "==> Staged Gallium Nine x86 D3D9: $OUT"
sha256sum "$OUT"
printf '%s\n' "$EXPORTS" | grep -F 'Direct3DCreate9'
