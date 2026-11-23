#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MESA9X="$ROOT/submodules/mesa9x"
OUT="$ROOT/dist_full/x86/d3d9.dll"

command -v i686-w64-mingw32-objdump >/dev/null || {
    echo "Missing i686-w64-mingw32-objdump" >&2
    exit 1
}

MESA_VERSION="$(tr -d '\r\n' < "$ROOT/mesa/VERSION")"
if [[ "$MESA_VERSION" != "25.1.6" ]]; then
    echo "Expected top-level Mesa 25.1.6, found $MESA_VERSION" >&2
    exit 1
fi

echo "==> Building x86 Gallium Nine from mesa9x bundled Mesa 25.1.6"
"$ROOT/test/apply_mesa9x_25_patches.sh"
for source in mesa99.c nine_present.c nine_window.c nine_memory_helper.c; do
    install -m 0644 "$ROOT/mesa_patches/nine/$source" \
        "$MESA9X/win9x/nine/$source"
done
make -C "$MESA9X" MESA_VER=mesa-25.1.x mesa99.dll

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
{
    printf 'Mesa bundled version: %s\n' "$(tr -d '\r\n' < "$MESA9X/mesa-25.1.x/VERSION")"
    printf 'mesa9x revision: %s\n' "$(git -C "$MESA9X" rev-parse HEAD)"
    sha256sum "$ROOT/patches/mesa9x/0002-mesa-25.1.6-wgl-lock-compat.patch"
    sha256sum "$ROOT/patches/mesa9x/0003-nouveau-nine-pointsize-3dmark-compat.patch"
    sha256sum "$ROOT"/mesa_patches/nine/*.c
    sha256sum "$OUT"
} > "$ROOT/dist_full/nine-build-manifest.txt"
sha256sum "$OUT"
printf '%s\n' "$EXPORTS" | grep -F 'Direct3DCreate9'
