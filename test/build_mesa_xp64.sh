#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
MESON="${MESON:-/tmp/nvx-meson-venv/bin/meson}"
if [ ! -x "$MESON" ] && command -v meson >/dev/null 2>&1; then
    MESON="$(command -v meson)"
fi
NINJA="${NINJA:-ninja}"
NVX_MESA_ARCH="${NVX_MESA_ARCH:-x64}"
NVX_MESA_TRANSPORT="${NVX_MESA_TRANSPORT:-xp}"
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
        if [[ "$NVX_MESA_TRANSPORT" == win98 ]]; then
            MESA_CROSS_FILE="$ROOT/test/mesa_win98.ini"
        else
            MESA_CROSS_FILE="$ROOT/test/mesa_xp32.ini"
        fi
        MESA_DLL_NAME=nvxogl32
        MESA_TARGET=i686-pc-windows-gnu
        MESA_OBJDUMP=i686-w64-mingw32-objdump
        ;;
    *) echo "NVX_MESA_ARCH must be x64 or x86" >&2; exit 1 ;;
esac
if [[ "$NVX_MESA_TRANSPORT" != xp && "$NVX_MESA_TRANSPORT" != win98 ]]; then
    echo "NVX_MESA_TRANSPORT must be xp or win98" >&2
    exit 1
fi
if [[ "$NVX_MESA_TRANSPORT" == win98 && "$NVX_MESA_ARCH" != x86 ]]; then
    echo "The Win98 DIOC transport is x86-only" >&2
    exit 1
fi
if [[ "$NVX_MESA_TRANSPORT" == win98 ]]; then
    BUILD_DIR="${NVX_MESA_BUILD_DIR:-${TMPDIR:-/tmp}/nvx-mesa-win98-dioc-build}"
else
    BUILD_DIR="${NVX_MESA_BUILD_DIR:-${TMPDIR:-/tmp}/nvx-mesa-xp${MESA_ARCH_SUFFIX}-build}"
fi
WIN98_PTHREAD_DIR=""
if [[ "$NVX_MESA_TRANSPORT" == win98 ]]; then
    PTHREAD_SOURCE="$ROOT/submodules/mesa9x/winpthreads"
    PTHREAD_REV="$(git -C "$PTHREAD_SOURCE" rev-parse HEAD)"
    WIN98_PTHREAD_PATCH="$ROOT/mesa_patches/win98/0002-winpthreads-win9x-cpu-count.patch"
    WIN98_CRT_PATCH="$ROOT/mesa_patches/win98/0004-winpthreads-win98-crt-compat.patch"
    WIN98_PTHREAD_PATCH_HASH="$(sha256sum "$WIN98_PTHREAD_PATCH" "$WIN98_CRT_PATCH" | sha256sum | cut -c1-12)"
    WIN98_PTHREAD_DIR="${NVX_WIN98_PTHREAD_DIR:-${TMPDIR:-/tmp}/nvx-win98-pthread-$PTHREAD_REV-$WIN98_PTHREAD_PATCH_HASH}"
    if [[ ! -s "$WIN98_PTHREAD_DIR/winpthreads/libpthread.a" ||
          ! -s "$WIN98_PTHREAD_DIR/winpthreads/crtfix.o" ]]; then
        mkdir -p "$WIN98_PTHREAD_DIR/winpthreads"
        git -C "$PTHREAD_SOURCE" archive HEAD |
            tar -x -C "$WIN98_PTHREAD_DIR/winpthreads"
        patch -d "$WIN98_PTHREAD_DIR/winpthreads" -p1 \
            < "$WIN98_PTHREAD_PATCH"
        patch -d "$WIN98_PTHREAD_DIR/winpthreads" -p1 \
            < "$WIN98_CRT_PATCH"
        cp "$ROOT/submodules/mesa9x/config.mk" \
           "$ROOT/submodules/mesa9x/pthread.mk" "$WIN98_PTHREAD_DIR/"
        make -C "$WIN98_PTHREAD_DIR/winpthreads"
    fi
fi
XP_CROSS_FILE="${BUILD_DIR}.ini"
DLL="$BUILD_DIR/src/gallium/targets/wgl/${MESA_DLL_NAME}.dll"
MESA_WGL_DLL="$BUILD_DIR/src/gallium/targets/libgl-gdi/opengl32.dll"
# Mesa's version option 5 disables its Vista-era futex path. The cross file
# carries the actual target API floor (XP for release, Win98 for DIOC smoke).
# Final Win98 imports are checked against the known Win9x-incompatible APIs.
MESA_MIN_WINDOWS_VERSION=5
if [[ "$NVX_MESA_TRANSPORT" == win98 ]]; then
    # Mesa's upstream option only allows Win7+; the NVX Win9x compatibility
    # patch lowers that gate to 4. The Win98 transport build uses the same
    # guarded XP/Win9x shims, but remains isolated from the XP package.
    MESA_MIN_WINDOWS_VERSION=4
fi
# Include Nouveau in the normal XP ICD package so WGL exercises the native
# Gallium screen factory. Softpipe remains available as a deliberate fallback
# while the NVX channel/submission boundary is being brought up.
MESA_GALLIUM_DRIVERS="${NVX_MESA_GALLIUM_DRIVERS:-nouveau,softpipe}"
MESA_VULKAN_DRIVERS="${NVX_MESA_VULKAN_DRIVERS:-}"
MESA_CLC="${NVX_MESA_CLC:-auto}"
MESON_VERSION_FILE="$BUILD_DIR/.nvx-meson-version"
MESON_VERSION="$($MESON --version)"
MESA_CROSS_HASH_FILE="$BUILD_DIR/.nvx-cross-file-hash"

mkdir -p "$BUILD_DIR"
python3 - "$MESA_CROSS_FILE" "$XP_CROSS_FILE" \
    "$ROOT/mesa_patches/xp_include" "${NVX_RUSTC:-}" \
    "${NVX_RUST_TARGET:-$MESA_TARGET}" "$NVX_MESA_TRANSPORT" \
    "$WIN98_PTHREAD_DIR" <<'PY'
from pathlib import Path
import sys

source, destination, include_dir, rustc, rust_target, transport, pthread_dir = sys.argv[1:]
text = Path(source).read_text()
for option in ("c_args", "cpp_args"):
    prefix = f"{option} = ["
    lines = text.splitlines()
    for index, line in enumerate(lines):
        if line.startswith(prefix):
            extra = f", '-I{include_dir}'"
            if transport == "win98":
                extra += ", '-DNVX_WIN98_DIOC=1', '-DWIN9X=1', '-D_WIN32_WINDOWS=0x0410', '-D_CRTIMP=', '-I" + pthread_dir + "/winpthreads/include'"
            lines[index] = line[:-1] + extra + "]"
            break
    else:
        raise SystemExit(f"missing {option} in Mesa cross file")
    text = "\n".join(lines) + "\n"
Path(destination).write_text(text)
if transport == "win98":
    text = Path(destination).read_text()
    # Match Mesa9x's MinGW Win32 threading model. The POSIX driver silently
    # appends libwinpthread, conflicting with pthread9x's allocator shims.
    text = text.replace("i686-w64-mingw32-gcc-posix", "i686-w64-mingw32-gcc-win32")
    text = text.replace("i686-w64-mingw32-g++-posix", "i686-w64-mingw32-g++")
    Path(destination).write_text(text)
    text = Path(destination).read_text()
    for option in ("c_link_args", "cpp_link_args"):
        prefix = f"{option} = ["
        lines = text.splitlines()
        for index, line in enumerate(lines):
            if line.startswith(prefix):
                lines[index] = line[:-1] + f", '-L{pthread_dir}/winpthreads']"
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
MESA_CROSS_HASH="$(sha256sum "$XP_CROSS_FILE" | awk '{print $1}')"

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
if [[ "$NVX_MESA_TRANSPORT" == win98 ]]; then
    WIN98_MESA_PATCH="$ROOT/mesa_patches/win98/0001-win98-mesa-api-compat.patch"
    WIN98_VERSION_PATCH="$ROOT/mesa_patches/win98/0003-mesa-win98-version-floor.patch"
    WIN98_TLS_PATCH="$ROOT/mesa_patches/win98/0005-wgl-tls-critical-section-init.patch"
    git -C "$ROOT/mesa" apply --check "$WIN98_VERSION_PATCH"
    git -C "$ROOT/mesa" apply "$WIN98_VERSION_PATCH"
    git -C "$ROOT/mesa" apply --check "$WIN98_MESA_PATCH"
    git -C "$ROOT/mesa" apply "$WIN98_MESA_PATCH"
    git -C "$ROOT/mesa" apply --check "$WIN98_TLS_PATCH"
    git -C "$ROOT/mesa" apply "$WIN98_TLS_PATCH"
    # Match Mesa9x's Win98 link: crtfix.o must be an explicit object on the
    # ICD DLL link, not a global cross-file link arg (which contaminates
    # Meson's compiler feature probes).
    python3 - "$ROOT/mesa/src/gallium/targets/wgl/meson.build" \
        "$WIN98_PTHREAD_DIR/winpthreads/crtfix.o" \
        "$WIN98_PTHREAD_DIR/winpthreads" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
text = p.read_text()
anchor = "gallium_wgl_link_args = []"
if text.count(anchor) != 1:
    raise SystemExit("could not uniquely find WGL link-args anchor")
text = text.replace(anchor,
                    "gallium_wgl_link_args = [" + repr(sys.argv[2]) + ", " +
                    repr("-L" + sys.argv[3]) + ", '-lpthread', " +
                    "'-Wl,--exclude-libs=pthread', " +
                    "'-Wl,--allow-multiple-definition']")
p.write_text(text)
PY
fi
if [[ -e "$ROOT/mesa/src/gallium/winsys/nouveau/drm/nvx_xf86drm.c" ]]; then
    echo "Refusing to overwrite existing Nouveau source: nvx_xf86drm.c" >&2
    exit 1
fi
cp "$ROOT/mesa_patches/nvx_xf86drm.c" \
   "$ROOT/mesa/src/gallium/winsys/nouveau/drm/nvx_xf86drm.c"
if [[ -f "$BUILD_DIR/meson-private/coredata.dat" &&
      -f "$MESON_VERSION_FILE" &&
      "$(cat "$MESON_VERSION_FILE")" == "$MESON_VERSION" &&
      -f "$MESA_CROSS_HASH_FILE" &&
      "$(cat "$MESA_CROSS_HASH_FILE")" == "$MESA_CROSS_HASH" ]]; then
    "$MESON" setup --reconfigure "$BUILD_DIR" "$ROOT/mesa" \
        --cross-file "$XP_CROSS_FILE" \
        --buildtype=release -Dstrip=true -Dplatforms=windows \
        -Dgallium-drivers="$MESA_GALLIUM_DRIVERS" -Dvulkan-drivers="$MESA_VULKAN_DRIVERS" -Dllvm=disabled \
        -Dglx=disabled -Degl=disabled -Dgbm=disabled -Dopengl=true \
        -Dmesa-clc="$MESA_CLC" \
        -Dgallium-wgl-dll-name="$MESA_DLL_NAME" -Ddefault_library=static \
        -Dmin-windows-version="$MESA_MIN_WINDOWS_VERSION"
elif [[ -f "$BUILD_DIR/meson-private/coredata.dat" ]]; then
    # Meson can retain stale cross-file flags and option metadata on
    # reconfigure. Wipe when the Meson executable or generated cross file
    # changes so architecture/transport flags and XP options are refreshed.
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
printf '%s\n' "$MESA_CROSS_HASH" > "$MESA_CROSS_HASH_FILE"
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

if [[ "$NVX_MESA_TRANSPORT" == win98 ]]; then
    # This is a compile/link smoke build for the DIOC transport selection.
    # Mesa's Windows SDK floor and imports still need Win98-specific auditing;
    # never place this artifact in the XP package or claim it is installable.
    unsupported=$("$MESA_OBJDUMP" -p "$DLL" | \
        grep -E 'GetConsoleWindow|GetModuleHandleExW|GetProcessAffinityMask|GetProcessTimes|GetThreadTimes|GlobalMemoryStatusEx|IsDebuggerPresent|SetThreadAffinityMask|GetRandomRgn|SwitchToThread|_aligned_(malloc|realloc|free)' || true)
    if [[ -n "$unsupported" ]]; then
        echo "Win98 smoke ICD still imports APIs unavailable on Win9x:" >&2
        printf '%s\n' "$unsupported" >&2
        exit 1
    fi
    echo "Built Win98-DIOC x86 Mesa transport smoke artifact: $DLL"
else
    install -m 0644 "$DLL" "$ROOT/dist_full/${MESA_DLL_NAME}.dll"
    if [[ "$NVX_MESA_ARCH" == x64 ]]; then
        install -m 0644 "$MESA_WGL_DLL" "$ROOT/dist_full/nvxmesa.dll"
    fi
    echo "Built XP $NVX_MESA_ARCH Mesa WGL ICD ($MESA_GALLIUM_DRIVERS): $ROOT/dist_full/${MESA_DLL_NAME}.dll"
    if [[ "$NVX_MESA_ARCH" == x64 ]]; then
        echo "Built direct Mesa WGL diagnostic frontend: $ROOT/dist_full/nvxmesa.dll"
    fi
fi
echo "Imported DLLs:"
printf '%s\n' "$imports"

# Record only packaged XP builds. The Win98-DIOC smoke artifact is intentionally
# isolated in /tmp and must not alter XP release manifests.
if [[ "$NVX_MESA_TRANSPORT" == xp ]]; then
    {
        git -C "$ROOT/mesa" rev-parse HEAD
        while IFS= read -r relative_patch; do
            [ -n "$relative_patch" ] || continue
            sha256sum "$ROOT/$relative_patch"
        done < "$ROOT/test/mesa25_patch_series.txt"
        sha256sum "$ROOT/dist_full/${MESA_DLL_NAME}.dll"
    } > "$ROOT/dist_full/mesa-build-manifest-${NVX_MESA_ARCH}.txt"
fi
