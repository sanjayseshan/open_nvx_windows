#!/usr/bin/env bash
set -euo pipefail
NVX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NVX_WINE_COMMIT=a786dca935c3e9b3d85853db4b61700e590a1c8b
NVX_WINE_BUILD="${NVX_WINE_BUILD:-/tmp/nvx-wined3d-xp}"
NVX_WINE_JOBS="${JOBS:-8}"
NVX_D3D9_CAPS_DIAGNOSTICS="${NVX_D3D9_CAPS_DIAGNOSTICS:-0}"
if [ "$(git -C "$NVX_ROOT/wine" rev-parse HEAD)" != "$NVX_WINE_COMMIT" ]; then
    echo "Expected Wine 1.7.55 at $NVX_WINE_COMMIT" >&2
    exit 1
fi
mkdir -p "$NVX_WINE_BUILD/source" "$NVX_WINE_BUILD/host-tools"
# Keep the submodule pristine; all generated files and patched sources live in /tmp.
git -C "$NVX_ROOT/wine" archive "$NVX_WINE_COMMIT" | tar -xf - -C "$NVX_WINE_BUILD/source"
git -C "$NVX_WINE_BUILD/source" apply --recount "$NVX_ROOT/wine_patches/0001-native-wgl-pixel-format-query.patch"
git -C "$NVX_WINE_BUILD/source" apply --recount "$NVX_ROOT/wine_patches/0002-log-3dmark-d3d9-caps.patch"
git -C "$NVX_WINE_BUILD/source" apply --recount "$NVX_ROOT/wine_patches/0004-xp-dynamic-d3dkmt.patch"
git -C "$NVX_WINE_BUILD/source" apply --recount "$NVX_ROOT/wine_patches/0005-csmt-nonblocking-discard-map-and-queue-size.patch"
NVX_WINE_DIAG_CFLAGS=
if [ "${NVX_D3D9_CAPS_DIAGNOSTICS:-0}" = "1" ]; then
    git -C "$NVX_WINE_BUILD/source" apply --recount "$NVX_ROOT/wine_patches/0003-log-3dmark-d3d9-failed-probes.patch"
    NVX_WINE_DIAG_CFLAGS='-DNVX_D3D9_CAPS_DIAGNOSTICS'
fi
(
    cd "$NVX_WINE_BUILD/host-tools"
    ../source/configure --enable-win64 --without-x --without-freetype
    make -j"$NVX_WINE_JOBS" tools/winebuild tools/widl tools/wrc tools/winegcc tools/makedep tools
)
for nvx_arch in x64 x86; do
    nvx_target=i686-w64-mingw32
    nvx_options=()
    nvx_opt_flags="-O3 -fomit-frame-pointer -fcommon -DUSE_WIN32_OPENGL -D_WIN32_WINNT=0x0502 -DWINVER=0x0502"
    if [ "$nvx_arch" = x64 ]; then
        nvx_target=x86_64-w64-mingw32
        nvx_options=(--enable-win64)
        nvx_opt_flags="$nvx_opt_flags -march=x86-64"
    else
        nvx_opt_flags="$nvx_opt_flags -march=pentium4 -msse2 -mfpmath=sse"
    fi
    mkdir -p "$NVX_WINE_BUILD/$nvx_arch"
    (
        cd "$NVX_WINE_BUILD/$nvx_arch"
        # Wine 1.7 predates GCC's -fno-common default.
        ../source/configure --host="$nvx_target" "${nvx_options[@]}" \
            --with-wine-tools="$NVX_WINE_BUILD/host-tools" --without-x --without-freetype \
            CFLAGS="$nvx_opt_flags $NVX_WINE_DIAG_CFLAGS"
        make -j"$NVX_WINE_JOBS" dlls/wined3d dlls/d3d9 dlls/d3d8 dlls/ddraw
    )
    mkdir -p "$NVX_ROOT/test/wined3d_xp/$nvx_arch"
    for nvx_dll in wined3d d3d9 d3d8 ddraw; do
        cp "$NVX_WINE_BUILD/$nvx_arch/dlls/$nvx_dll/$nvx_dll.dll" \
            "$NVX_ROOT/test/wined3d_xp/$nvx_arch/$nvx_dll.dll"
    done
    cp "$NVX_WINE_BUILD/$nvx_arch/libs/wine/libwine.dll" \
        "$NVX_ROOT/test/wined3d_xp/$nvx_arch/libwine.dll"
    sha256sum "$NVX_ROOT/test/wined3d_xp/$nvx_arch/"{wined3d,d3d9,d3d8,ddraw,libwine}.dll
done
