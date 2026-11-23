#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MESON="${MESON:-/tmp/nvx-meson-venv/bin/meson}"
if [ ! -x "$MESON" ] && command -v meson >/dev/null 2>&1; then
    MESON="$(command -v meson)"
fi
NINJA="${NINJA:-ninja}"
NVX_MESA_ARCH="${NVX_MESA_ARCH:-x64}"
case "$NVX_MESA_ARCH" in
    x64)
        MESA_ARCH_SUFFIX=64
        MESA_CROSS_FILE="$ROOT/test/mesa_xp64.ini"
        MESA_DLL_NAME=nvxogl64
        MESA_TARGET=x86_64-pc-windows-gnu
        MESA_OBJDUMP=x86_64-w64-mingw32-objdump
        ;;
    x86)
        MESA_ARCH_SUFFIX=32
        MESA_CROSS_FILE="$ROOT/test/mesa_xp32.ini"
        MESA_DLL_NAME=nvxogl32
        MESA_TARGET=i686-pc-windows-gnu
        MESA_OBJDUMP=i686-w64-mingw32-objdump
        ;;
    *) echo "NVX_MESA_ARCH must be x64 or x86" >&2; exit 1 ;;
esac
BUILD_DIR="${NVX_MESA_BUILD_DIR:-${TMPDIR:-/tmp}/nvx-mesa-xp${MESA_ARCH_SUFFIX}-build}"
XP_CROSS_FILE="${BUILD_DIR}.ini"
DLL="$BUILD_DIR/src/gallium/targets/wgl/${MESA_DLL_NAME}.dll"
MESA_WGL_DLL="$BUILD_DIR/src/gallium/targets/libgl-gdi/opengl32.dll"
# The external XP patch lowers Mesa's minimum-version option to 5 (Windows XP).
# The cross file sets _WIN32_WINNT/WINVER to 0x0502 for XP; final PE imports
# and subsystem versions are checked by audit_xp_imports.py.
MESA_MIN_WINDOWS_VERSION=5
# Include Nouveau in the normal XP ICD package so WGL exercises the native
# Gallium screen factory. Softpipe remains available as a deliberate fallback
# while the NVX channel/submission boundary is being brought up.
MESA_GALLIUM_DRIVERS="${NVX_MESA_GALLIUM_DRIVERS:-nouveau,softpipe}"
MESA_VULKAN_DRIVERS="${NVX_MESA_VULKAN_DRIVERS:-}"
MESA_CLC="${NVX_MESA_CLC:-auto}"
MESON_VERSION_FILE="$BUILD_DIR/.nvx-meson-version"
MESON_VERSION="$($MESON --version)"

mkdir -p "$BUILD_DIR"
python3 - "$MESA_CROSS_FILE" "$XP_CROSS_FILE" \
    "$ROOT/mesa_patches/xp_include" "${NVX_RUSTC:-}" \
    "${NVX_RUST_TARGET:-$MESA_TARGET}" <<'PY'
from pathlib import Path
import sys

source, destination, include_dir, rustc, rust_target = sys.argv[1:]
text = Path(source).read_text()
for option in ("c_args", "cpp_args"):
    prefix = f"{option} = ["
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if line.startswith(prefix):
            lines[index] = line[:-1] + f", '-I{include_dir}']"
            break
    else:
        raise SystemExit(f"missing {option} in Mesa cross file")
    text = "\n".join(lines) + "\n"
Path(destination).write_text(text)
if rustc:
    text = Path(destination).read_text()
    text = text.replace('[binaries]\n',
                        '[binaries]\nrust = ' +
                        repr([rustc, '--target', rust_target]) + '\n', 1)
    Path(destination).write_text(text)
PY

restore_mesa_sources() {
    # apply_mesa_xp_patches.sh requires a clean submodule before it starts.
    # Restore to that exact baseline instead of reversing every patch: reverse
    # checks can match untouched source and insert/remove overlapping lines.
    rm -f "$ROOT/mesa/src/gallium/winsys/nouveau/drm/nvx_xf86drm.c"
    git -C "$ROOT/mesa" restore --worktree -- .
    python3 - "$ROOT/mesa/src/c11/impl/nvx_win32_xp.h" <<'PY'
from pathlib import Path
import sys
Path(sys.argv[1]).unlink(missing_ok=True)
PY
}
trap restore_mesa_sources EXIT
"$ROOT/test/apply_mesa_xp_patches.sh"
if [[ -e "$ROOT/mesa/src/gallium/winsys/nouveau/drm/nvx_xf86drm.c" ]]; then
    echo "Refusing to overwrite existing Nouveau source: nvx_xf86drm.c" >&2
    exit 1
fi
cp "$ROOT/mesa_patches/nvx_xf86drm.c" \
   "$ROOT/mesa/src/gallium/winsys/nouveau/drm/nvx_xf86drm.c"
if [[ -f "$BUILD_DIR/meson-private/coredata.dat" &&
      -f "$MESON_VERSION_FILE" &&
      "$(cat "$MESON_VERSION_FILE")" == "$MESON_VERSION" ]]; then
    "$MESON" setup --reconfigure "$BUILD_DIR" "$ROOT/mesa" \
        --cross-file "$XP_CROSS_FILE" \
        --buildtype=release -Dstrip=true -Dplatforms=windows \
        -Dgallium-drivers="$MESA_GALLIUM_DRIVERS" -Dvulkan-drivers="$MESA_VULKAN_DRIVERS" -Dllvm=disabled \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=true \
        -Dmesa-clc="$MESA_CLC" \
        -Dgallium-wgl-dll-name="$MESA_DLL_NAME" -Ddefault_library=static \
        -Dmin-windows-version="$MESA_MIN_WINDOWS_VERSION"
elif [[ -f "$BUILD_DIR/meson-private/coredata.dat" ]]; then
    # Meson can regenerate coredata from an older executable while retaining
    # stale option metadata. Wipe once when the Meson version changes so XP's
    # patched min-windows-version range is read from the current source tree.
    "$MESON" setup --wipe "$BUILD_DIR" "$ROOT/mesa" \
        --cross-file "$XP_CROSS_FILE" \
        --buildtype=release -Dstrip=true -Dplatforms=windows \
        -Dgallium-drivers="$MESA_GALLIUM_DRIVERS" -Dvulkan-drivers="$MESA_VULKAN_DRIVERS" -Dllvm=disabled \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=true \
        -Dmesa-clc="$MESA_CLC" \
        -Dgallium-wgl-dll-name="$MESA_DLL_NAME" -Ddefault_library=static \
        -Dmin-windows-version="$MESA_MIN_WINDOWS_VERSION"
else
    "$MESON" setup "$BUILD_DIR" "$ROOT/mesa" \
        --cross-file "$XP_CROSS_FILE" \
        --buildtype=release -Dstrip=true -Dplatforms=windows \
        -Dgallium-drivers="$MESA_GALLIUM_DRIVERS" -Dvulkan-drivers="$MESA_VULKAN_DRIVERS" -Dllvm=disabled \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=true \
        -Dmesa-clc="$MESA_CLC" \
        -Dgallium-wgl-dll-name="$MESA_DLL_NAME" -Ddefault_library=static \
        -Dmin-windows-version="$MESA_MIN_WINDOWS_VERSION"
fi
printf '%s\n' "$MESON_VERSION" > "$MESON_VERSION_FILE"
"$NINJA" -C "$BUILD_DIR" -j"${JOBS:-4}"

if [[ ! -s "$DLL" ]]; then
    echo "Mesa WGL ICD was not produced: $DLL" >&2
    exit 1
fi
if [[ ! -s "$MESA_WGL_DLL" ]]; then
    echo "Mesa WGL frontend was not produced: $MESA_WGL_DLL" >&2
    exit 1
fi
imports=$($MESA_OBJDUMP -p "$DLL" | sed -n 's/^[[:space:]]*DLL Name: //p')
if printf '%s\n' "$imports" | grep -Eiq 'api-ms-win|winpthread|libz'; then
    echo "Mesa ICD has non-XP runtime imports:" >&2
    printf '%s\n' "$imports" >&2
    exit 1
fi

install -m 0644 "$DLL" "$ROOT/dist_full/${MESA_DLL_NAME}.dll"
if [[ "$NVX_MESA_ARCH" == x64 ]]; then
    install -m 0644 "$MESA_WGL_DLL" "$ROOT/dist_full/nvxmesa.dll"
fi
echo "Built XP $NVX_MESA_ARCH Mesa WGL ICD ($MESA_GALLIUM_DRIVERS): $ROOT/dist_full/${MESA_DLL_NAME}.dll"
if [[ "$NVX_MESA_ARCH" == x64 ]]; then
    echo "Built direct Mesa WGL diagnostic frontend: $ROOT/dist_full/nvxmesa.dll"
fi
echo "Imported DLLs:"
printf '%s\n' "$imports"

# Record the exact external patch inputs alongside the packaged binary.
{
    git -C "$ROOT/mesa" rev-parse HEAD
    while IFS= read -r relative_patch; do
        [ -n "$relative_patch" ] || continue
        sha256sum "$ROOT/$relative_patch"
    done < "$ROOT/test/mesa25_patch_series.txt"
    sha256sum "$ROOT/dist_full/${MESA_DLL_NAME}.dll"
} > "$ROOT/dist_full/mesa-build-manifest-${NVX_MESA_ARCH}.txt"
