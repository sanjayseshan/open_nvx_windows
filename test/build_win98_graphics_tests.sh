#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT="$ROOT/test/win98"
mkdir -p "$OUT"

CC="${WIN98_CC:-i686-w64-mingw32-gcc}"
OBJDUMP="${WIN98_OBJDUMP:-i686-w64-mingw32-objdump}"
COMMON=(
    -O2 -static-libgcc -Wno-cast-function-type
    -D_WIN32_WINNT=0x0400 -DWINVER=0x0400
    -DNVX_WIN98_TEST_BUILD=1
    -Wl,--major-subsystem-version,4 -Wl,--minor-subsystem-version,0
)

"$CC" "${COMMON[@]}" -mwindows -DNVX_WIN98_GL_FRAMES=30 \
    "$ROOT/test/test_gpu_accel.c" -lopengl32 -lgdi32 -luser32 \
    -o "$OUT/antigravity.exe"
"$CC" -x c "${COMMON[@]}" -mwindows -I"$ROOT/src_driver" \
    "$ROOT/test/3d_tests/glxgears_main.cpp" \
    -lopengl32 -lgdi32 -luser32 -o "$OUT/wglgears.exe"
"$CC" "${COMMON[@]}" -mwindows "$ROOT/test/win98/runner.c" \
    -ladvapi32 -o "$OUT/runner.exe"
"$CC" "${COMMON[@]}" -mwindows "$ROOT/test/win98/legacy_ddraw.c" \
    -lddraw -ldxguid -lgdi32 -luser32 -lole32 -o "$OUT/legacy_ddraw.exe"
if [[ ! -s "$ROOT/dist_win98/nine/d3d9.dll" || ! -s "$ROOT/dist_win98/nine/mesa99.dll" ]]; then
    echo "Missing dist_win98/nine runtime; run test/build_win98_nine.sh first" >&2
    exit 1
fi
"$CC" "${COMMON[@]}" -mwindows "$ROOT/test/win98/nine_d3d9.c" \
    -L"$ROOT/dist_win98/nine" -ld3d9 -lgdi32 -luser32 \
    -o "$OUT/nine_d3d9.exe"

for binary in "$OUT/antigravity.exe" "$OUT/wglgears.exe" "$OUT/runner.exe" \
              "$OUT/legacy_ddraw.exe" "$OUT/nine_d3d9.exe"; do
    info="$("$OBJDUMP" -p "$binary")"
    if ! grep -q $'MajorSubsystemVersion\t4' <<<"$info" ||
       ! grep -q $'MinorSubsystemVersion\t0' <<<"$info"; then
        echo "Error: $binary is not marked as a Win9x subsystem 4.0 executable" >&2
        exit 1
    fi
    if grep -Eq 'AddVectoredExceptionHandler|GetEnvironmentVariableA|SetEnvironmentVariableA|GetTickCount64|InitializeCriticalSectionAndSpinCount' <<<"$info"; then
        echo "Error: $binary statically imports an API not safe for Win98" >&2
        exit 1
    fi
    echo "Built and import-checked: $binary"
done
