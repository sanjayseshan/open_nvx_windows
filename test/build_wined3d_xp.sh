#!/usr/bin/env bash
set -euo pipefail
NVX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NVX_WINE_COMMIT=a786dca935c3e9b3d85853db4b61700e590a1c8b
NVX_WINE_BUILD="${NVX_WINE_BUILD:-/tmp/nvx-wined3d-xp}"
NVX_WINE_JOBS="${JOBS:-8}"
if [ "$(git -C "$NVX_ROOT/wine" rev-parse HEAD)" != "$NVX_WINE_COMMIT" ]; then
    echo "Expected Wine 1.7.55 at $NVX_WINE_COMMIT" >&2
    exit 1
fi
mkdir -p "$NVX_WINE_BUILD/source" "$NVX_WINE_BUILD/host-tools"
# Keep the submodule pristine; all generated files and patched sources live in /tmp.
git -C "$NVX_ROOT/wine" archive "$NVX_WINE_COMMIT" | tar -xf - -C "$NVX_WINE_BUILD/source"
patch -d "$NVX_WINE_BUILD/source" -p1 < "$NVX_ROOT/wine_patches/0001-native-wgl-pixel-format-query.patch"
(
    cd "$NVX_WINE_BUILD/host-tools"
    ../source/configure --enable-win64 --without-x --without-freetype
    make -j"$NVX_WINE_JOBS" tools/winebuild tools/widl tools/wrc tools/winegcc tools/makedep tools/make_xftmpl
)
for nvx_arch in x64 x86; do
    nvx_target=i686-w64-mingw32
    nvx_options=()
    if [ "$nvx_arch" = x64 ]; then
        nvx_target=x86_64-w64-mingw32
        nvx_options=(--enable-win64)
    fi
    mkdir -p "$NVX_WINE_BUILD/$nvx_arch"
    (
        cd "$NVX_WINE_BUILD/$nvx_arch"
        # Wine 1.7 predates GCC's -fno-common default.
        ../source/configure --host="$nvx_target" "${nvx_options[@]}" \
            --with-wine-tools="$NVX_WINE_BUILD/host-tools" --without-x --without-freetype \
            CFLAGS='-O2 -fcommon -DUSE_WIN32_OPENGL -D_WIN32_WINNT=0x0502 -DWINVER=0x0502'
        make -j"$NVX_WINE_JOBS" dlls/wined3d
    )
    mkdir -p "$NVX_ROOT/test/wined3d_xp/$nvx_arch"
    cp "$NVX_WINE_BUILD/$nvx_arch/dlls/wined3d/wined3d.dll" \
        "$NVX_ROOT/test/wined3d_xp/$nvx_arch/wined3d.dll"
    sha256sum "$NVX_ROOT/test/wined3d_xp/$nvx_arch/wined3d.dll"
done
