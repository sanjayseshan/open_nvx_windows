#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WINE9X="$ROOT/submodules/wine9x"
OUT="$ROOT/dist_win98/legacy"

"$ROOT/test/build_win98_nine.sh"
"$ROOT/test/build_win98_graphics_tests.sh"
make -C "$WINE9X" -j"${JOBS:-4}"
mkdir -p "$OUT"
cp "$WINE9X/ddraw_98.dll" "$OUT/ddraw.dll"
cp "$WINE9X/winedd.dll" "$WINE9X/wined3d.dll" "$OUT/"
cp "$ROOT/test/win98/legacy_ddraw.exe" "$OUT/"
cp "$ROOT/test/win98/nine_d3d9.exe" "$OUT/"

for file in "$OUT"/*.dll "$OUT"/*.exe; do
    file "$file" | grep -q 'PE32 executable.*Intel 80386' || {
        echo "Not an x86 PE binary: $file" >&2; exit 1;
    }
done

cat > "$OUT/README.txt" <<'EOF'
Experimental Windows 98 DirectDraw / legacy Direct3D test bundle

ddraw.dll          Wine9x Win98 DDraw switcher (install as Windows\\System\\ddraw.dll)
winedd.dll         Wine9x DirectDraw / Direct3D 6-7 implementation
wined3d.dll        Wine9x shared renderer; uses the system OpenGL ICD
legacy_ddraw.exe   DDraw + D3D3 HAL triangle-submission and visible-blit test
nine_d3d9.exe      Gallium Nine D3D9 60-frame clear/draw/present triangle test

The test runner writes API results to COM1. A passing D3D3 triangle submission
proves that the Wine9x DDraw/Direct3D path reaches its renderer; GPU activity
must also be confirmed from NVX submission records in the host serial log.
Back up the existing system DDRAW.DLL as DDSYS.DLL before installing the
switcher. The switcher routes OpenGL's DDraw calls to the original system DLL
and application DDraw calls to WineD3D.
EOF

{
    printf 'Wine9x: '
    git -C "$WINE9X" rev-parse HEAD
    printf 'Wine9x no-CRT: '
    git -C "$WINE9X/nocrt" rev-parse HEAD
    printf 'Wine9x pthread9x: '
    git -C "$WINE9X/pthread9x" rev-parse HEAD
    sha256sum "$OUT"/*.dll "$OUT"/*.exe
} > "$OUT/build-manifest.txt"
echo "Win98 legacy DDraw test bundle staged: $OUT"
ls -lh "$OUT"
