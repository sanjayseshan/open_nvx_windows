#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MESA9X="$ROOT/submodules/mesa9x"
WINE9X="$ROOT/submodules/wine9x"
PATCH="$ROOT/patches/wine9x/0001-pthread9x-fastfail-compat.patch"
OUT="$ROOT/dist_win98/nine"

for tool in i686-w64-mingw32-gcc i686-w64-mingw32-objdump nasm make patch; do
    command -v "$tool" >/dev/null || { echo "Missing build tool: $tool" >&2; exit 1; }
done
for dep in "$WINE9X/nocrt/nocrt.h" "$WINE9X/pthread9x/Makefile"; do
    [[ -f "$dep" ]] || {
        echo "Missing nested Wine9x dependency: $dep" >&2
        echo "Run: git submodule update --init --recursive submodules/wine9x" >&2
        exit 1
    }
done

echo "==> Build Mesa Gallium Nine frontend (x86)"
"$ROOT/build_d3d9_nine.sh"

CRT="$WINE9X/pthread9x/extra/crtfix.c"
if rg -q 'void __fastfail\(' "$CRT"; then
    (cd "$WINE9X" && patch --binary -p1 < "$PATCH")
fi
if rg -q 'void __fastfail\(' "$CRT"; then
    echo "Wine9x fast-fail compatibility patch did not apply" >&2
    exit 1
fi

if [[ ! -f "$WINE9X/config.mk" ]]; then
    cp "$WINE9X/config.mk-sample" "$WINE9X/config.mk"
fi

echo "==> Build Wine9x D3D9 switcher and WineD3D runtime"
make -B -C "$WINE9X/pthread9x" -j"${JOBS:-4}"
make -C "$WINE9X" -j"${JOBS:-4}"

mkdir -p "$OUT"
cp "$MESA9X/mesa99.dll" "$OUT/mesa99.dll"
cp "$WINE9X/d3d9_98.dll" "$OUT/d3d9.dll"
cp "$WINE9X/wined3d.dll" "$WINE9X/wined9.dll" "$OUT/"

for dll in "$OUT"/*.dll; do
    file "$dll" | grep -q 'PE32 executable.*Intel 80386' || {
        echo "Not an x86 PE DLL: $dll" >&2; exit 1;
    }
done
MESA_EXPORTS="$(i686-w64-mingw32-objdump -p "$OUT/mesa99.dll")"
SWITCHER_EXPORTS="$(i686-w64-mingw32-objdump -p "$OUT/d3d9.dll")"
[[ "$MESA_EXPORTS" == *Direct3DCreate9* ]]
[[ "$SWITCHER_EXPORTS" == *Direct3DCreate9* ]]

cat > "$OUT/README.txt" <<'EOF'
Optional Direct3D 9 runtime for Windows 98 SE

This is a build bundle, not yet validated on the Win98 guest.

  d3d9.dll     Wine9x D3D9 switcher for Win98
  mesa99.dll   Mesa Gallium Nine D3D9 frontend
  wined3d.dll  Wine9x shared D3D implementation
  wined9.dll   Wine9x D3D9 compatibility backend

For a controlled test, back up the existing Windows\\System\\d3d9.dll as
msd3d9.dll, install these DLLs in Windows\\System, and set the test program's
executable name under HKLM\\Software\\D9Switcher to the string "nineforce".
The Wine9x switcher README documents per-application routing. Restore the
backup to undo the test. Runtime compatibility and accelerated D3D9 are not
confirmed until a Win98 D3D9 application completes a test with GPU submissions.
EOF

{
    printf 'Mesa Nine: '
    git -C "$MESA9X" rev-parse HEAD
    printf 'Wine9x: '
    git -C "$WINE9X" rev-parse HEAD
    printf 'Wine9x no-CRT: '
    git -C "$WINE9X/nocrt" rev-parse HEAD
    printf 'Wine9x pthread9x: '
    git -C "$WINE9X/pthread9x" rev-parse HEAD
    sha256sum "$PATCH" "$OUT"/*.dll
} > "$OUT/build-manifest.txt"

echo "==> Win98 Nine bundle staged at $OUT"
ls -lh "$OUT"
