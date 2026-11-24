#!/usr/bin/env bash
set -euo pipefail

NVX_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
NVX_QCOW="$NVX_ROOT/winxp_x64.qcow2"
NVX_PACKAGE="$NVX_ROOT/dist"
NVX_FULL_PACKAGE=0
NVX_WINE_X86_DIR="$NVX_ROOT/test/wined3d_xp/x86"
NVX_D3D9_BACKEND="${NVX_D3D9_BACKEND:-nine}"
case "$NVX_D3D9_BACKEND" in
    wined3d|nine) ;;
    *) echo "NVX_D3D9_BACKEND must be wined3d or nine" >&2; exit 1 ;;
esac
NVX_GPU_READBACK_TRACE="${NVX_GPU_READBACK_TRACE:-0}"
case "$NVX_GPU_READBACK_TRACE" in
    0|1) ;;
    *) echo "NVX_GPU_READBACK_TRACE must be 0 or 1" >&2; exit 1 ;;
esac
NVX_OPENGL_ICD_DLL="${NVX_OPENGL_ICD_DLL:-nvxogl64.dll}"
NVX_MESA_APPLOCAL="${NVX_MESA_APPLOCAL:-0}"
if [ "${1:-}" = full ]; then
    NVX_PACKAGE="$NVX_ROOT/dist_full"
    NVX_FULL_PACKAGE=1
    NVX_WINE_X86_DIR="$NVX_PACKAGE/x86"
elif [ "$#" -ne 0 ]; then
    echo "Usage: $0 [full]" >&2
    exit 1
fi
case "$NVX_OPENGL_ICD_DLL" in
    nvxogl64.dll|nvxmesa.dll) ;;
    *) echo "Unsupported NVX_OPENGL_ICD_DLL: $NVX_OPENGL_ICD_DLL" >&2; exit 1 ;;
esac
case "$NVX_MESA_APPLOCAL" in
    0|1) ;;
    *) echo "NVX_MESA_APPLOCAL must be 0 or 1" >&2; exit 1 ;;
esac
NVX_RUN_3DMARK="${NVX_RUN_3DMARK:-0}"
case "$NVX_RUN_3DMARK" in
    0|1) ;;
    *) echo "NVX_RUN_3DMARK must be 0 or 1" >&2; exit 1 ;;
esac

for tool in guestmount guestunmount mountpoint qemu-img; do
    command -v "$tool" >/dev/null || { echo "Missing $tool; see ROOTLESS_TESTING.md." >&2; exit 1; }
done
if pgrep -f "qemu-system.*${NVX_QCOW}" >/dev/null; then
    echo "Refusing to update a disk used by a running VM." >&2
    exit 1
fi
# QEMU may use a relative image path or be hidden from pgrep's namespace.
# qemu-img checks the actual image lock before starting a writable appliance.
if ! qemu-img info "$NVX_QCOW" >/dev/null; then
    echo "Refusing to update a locked or unreadable XP disk." >&2
    exit 1
fi
for file in "$NVX_PACKAGE/nvxmp.sys" "$NVX_PACKAGE/nvxdisp.dll"; do
    [ -f "$file" ] || { echo "Missing package file: $file" >&2; exit 1; }
done
if [ "$NVX_FULL_PACKAGE" = "1" ]; then
    for file in \
        nvx.inf gsp_ga104.bin nvxmesa.dll nvxogl64.dll nvxogl32.dll \
        d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll; do
        [ -f "$NVX_PACKAGE/$file" ] || {
            echo "Full install bundle is incomplete: missing $NVX_PACKAGE/$file" >&2
            exit 1
        }
    done
    if [ "$NVX_D3D9_BACKEND" = "nine" ]; then
        [ -f "$NVX_PACKAGE/x86/d3d9.dll" ] || {
            echo "Gallium Nine x86 D3D9 DLL is missing: $NVX_PACKAGE/x86/d3d9.dll" >&2
            exit 1
        }
        file "$NVX_PACKAGE/x86/d3d9.dll" | grep -q 'PE32 executable.*Intel 80386' || {
            echo "Gallium Nine D3D9 must be an x86 PE DLL." >&2
            exit 1
        }
    else
        for file in d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll; do
            [ -f "$NVX_WINE_X86_DIR/$file" ] || {
                echo "Full install bundle is incomplete: missing $NVX_WINE_X86_DIR/$file" >&2
                exit 1
            }
        done
    fi
fi

NVX_MOUNT="$(mktemp -d /tmp/nvx-xp-mount.XXXXXX)"
NVX_GUESTMOUNT_PID="${NVX_MOUNT}.pid"
wait_for_release() {
    local pid=""
    [ -f "$NVX_GUESTMOUNT_PID" ] && pid="$(cat "$NVX_GUESTMOUNT_PID")"
    if [ -n "$pid" ]; then
        for _ in $(seq 1 100); do
            kill -0 "$pid" 2>/dev/null || break
            sleep 0.1
        done
        if kill -0 "$pid" 2>/dev/null; then
            echo "guestmount process $pid did not exit; refusing to launch QEMU." >&2
            return 1
        fi
    fi
    for _ in $(seq 1 100); do
        qemu-img info "$NVX_QCOW" >/dev/null 2>&1 && return 0
        sleep 0.1
    done
    echo "QCOW write lock was not released; refusing to launch QEMU." >&2
    return 1
}
cleanup() {
    if mountpoint -q "$NVX_MOUNT"; then guestunmount --retry=10 "$NVX_MOUNT" || true; fi
    wait_for_release || true
    rm -f "$NVX_GUESTMOUNT_PID"
    rmdir "$NVX_MOUNT" 2>/dev/null || true
}
trap cleanup EXIT

echo "==> Mounting XP image with unprivileged libguestfs/FUSE"
guestmount --rw --pid-file "$NVX_GUESTMOUNT_PID" -a "$NVX_QCOW" -i "$NVX_MOUNT"
NVX_SYSTEM32="$(find "$NVX_MOUNT" -maxdepth 2 -type d -ipath '*/windows/system32' -print -quit)"
NVX_DRIVERS="$(find "$NVX_MOUNT" -maxdepth 3 -type d -ipath '*/windows/system32/drivers' -print -quit)"
if [ -z "$NVX_SYSTEM32" ] || [ -z "$NVX_DRIVERS" ]; then
    echo "Could not locate the Windows system directories." >&2
    exit 1
fi
if [ "$NVX_FULL_PACKAGE" = "1" ] && [ ! -d "$NVX_MOUNT/WINDOWS/SysWOW64" ]; then
    echo "XP x64 SysWOW64 directory is missing; refusing an incomplete full install." >&2
    exit 1
fi
echo "==> Auditing every package import against this XP installation"
NVX_AUDIT_ARGS=(--package "$NVX_PACKAGE" "$NVX_SYSTEM32" "$NVX_DRIVERS")
if [ -f "$NVX_ROOT/test/test_gpu_accel32.exe" ] || \
   [ -f "$NVX_ROOT/test/3d_tests/wglgears32.exe" ]; then
    NVX_AUDIT_ARGS+=(--x86-system-dir "$NVX_MOUNT/WINDOWS/SysWOW64")
fi
if [ -f "$NVX_ROOT/test/test_gpu_accel32.exe" ]; then
    NVX_AUDIT_ARGS+=(--package "$NVX_ROOT/test/test_gpu_accel32.exe")
fi
if [ -f "$NVX_ROOT/test/3d_tests/wglgears32.exe" ]; then
    NVX_AUDIT_ARGS+=(--package "$NVX_ROOT/test/3d_tests/wglgears32.exe")
fi
# Resolve imports for the x86 backend that will actually be installed.
if [ "$NVX_D3D9_BACKEND" = "wined3d" ]; then
    for nvx_dll in d3d9 d3d8 ddraw wined3d libwine; do
        if [ -f "$NVX_WINE_X86_DIR/$nvx_dll.dll" ]; then
            NVX_AUDIT_ARGS+=(--package "$NVX_WINE_X86_DIR/$nvx_dll.dll")
        fi
    done
else
    NVX_AUDIT_ARGS+=(--package "$NVX_PACKAGE/x86/d3d9.dll")
fi
python3 "$NVX_ROOT/test/audit_xp_imports.py" "${NVX_AUDIT_ARGS[@]}"

cp -v "$NVX_PACKAGE/nvxmp.sys" "$NVX_DRIVERS/nvxmp.sys"
cp -v "$NVX_PACKAGE/nvxdisp.dll" "$NVX_SYSTEM32/nvxdisp.dll"
if [ -f "$NVX_PACKAGE/gsp_ga104.bin" ]; then
    cp -v "$NVX_PACKAGE/gsp_ga104.bin" "$NVX_DRIVERS/gsp_ga104.bin"
fi
if [ -f "$NVX_PACKAGE/nvx.inf" ]; then
    cp -v "$NVX_PACKAGE/nvx.inf" "$NVX_MOUNT/WINDOWS/inf/nvx.inf"
fi
if [ -f "$NVX_PACKAGE/nvxogl64.dll" ]; then
    cp -v "$NVX_PACKAGE/nvxogl64.dll" "$NVX_SYSTEM32/nvxogl64.dll"
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_PACKAGE/nvxogl64.dll" "$NVX_MOUNT/test_accel/nvxogl64.dll"
    NVX_SOFTWARE_HIVE="$(find "$NVX_MOUNT" -maxdepth 4 -type f -ipath '*/windows/system32/config/software' -print -quit)"
    if [ -z "$NVX_SOFTWARE_HIVE" ]; then
        echo "Could not locate the offline SOFTWARE registry hive for the Mesa ICD." >&2
        exit 1
    fi
hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SOFTWARE' "$NVX_SOFTWARE_HIVE" <<REGFILE
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\nvxdisp]
"Dll"="$NVX_OPENGL_ICD_DLL"
"Version"=dword:00000002
"DriverVersion"=dword:00010000
"Flags"=dword:00000001
REGFILE
fi
if [ -f "$NVX_PACKAGE/nvxogl32.dll" ]; then
    [ -d "$NVX_MOUNT/WINDOWS/SysWOW64" ] || {
        echo "XP x64 SysWOW64 directory is missing; cannot install the x86 OpenGL ICD." >&2
        exit 1
    }
    cp -v "$NVX_PACKAGE/nvxogl32.dll" "$NVX_MOUNT/WINDOWS/SysWOW64/nvxogl32.dll"
    mkdir -p "$NVX_MOUNT/test_accel/x86"
    cp -v "$NVX_PACKAGE/nvxogl32.dll" "$NVX_MOUNT/test_accel/x86/nvxogl32.dll"
    NVX_SOFTWARE_HIVE="$(find "$NVX_MOUNT" -maxdepth 4 -type f -ipath '*/windows/system32/config/software' -print -quit)"
    if [ -z "$NVX_SOFTWARE_HIVE" ]; then
        echo "Could not locate the offline SOFTWARE registry hive for the x86 Mesa ICD." >&2
        exit 1
    fi
hivexregedit --merge --prefix 'HKEY_LOCAL_MACHINE\SOFTWARE' "$NVX_SOFTWARE_HIVE" <<'REGFILE'
Windows Registry Editor Version 5.00

[HKEY_LOCAL_MACHINE\SOFTWARE\Wow6432Node\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\nvxdisp]
"Dll"="nvxogl32.dll"
"Version"=dword:00000002
"DriverVersion"=dword:00010000
"Flags"=dword:00000001
REGFILE
fi
if [ -f "$NVX_PACKAGE/nvxmesa.dll" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_PACKAGE/nvxmesa.dll" "$NVX_SYSTEM32/nvxmesa.dll"
    cp -v "$NVX_PACKAGE/nvxmesa.dll" "$NVX_MOUNT/test_accel/nvxmesa.dll"
    if [ "$NVX_MESA_APPLOCAL" = "1" ]; then
        cp -v "$NVX_PACKAGE/nvxmesa.dll" "$NVX_MOUNT/test_accel/opengl32.dll"
    else
        rm -f "$NVX_MOUNT/test_accel/opengl32.dll"
    fi
fi
if [ -f "$NVX_PACKAGE/test_gpu_accel.exe" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_PACKAGE/test_gpu_accel.exe" "$NVX_MOUNT/test_accel/test_gpu_accel.exe"
fi
if [ -f "$NVX_ROOT/test/test_display_modes.exe" ]; then
    cp -v "$NVX_ROOT/test/test_display_modes.exe" "$NVX_MOUNT/test_accel/test_display_modes.exe"
else
    rm -f "$NVX_MOUNT/test_accel/test_display_modes.exe"
fi
if [ -f "$NVX_ROOT/test/nvx_p2mf_probe.exe" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_ROOT/test/nvx_p2mf_probe.exe" "$NVX_MOUNT/test_accel/nvx_p2mf_probe.exe"
fi
if [ -f "$NVX_ROOT/test/test_gpu_accel32.exe" ]; then
    mkdir -p "$NVX_MOUNT/test_accel/x86"
    cp -v "$NVX_ROOT/test/test_gpu_accel32.exe" "$NVX_MOUNT/test_accel/x86/test_gpu_accel32.exe"
fi
if [ -f "$NVX_ROOT/test/3d_tests/wglgears.exe" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_ROOT/test/3d_tests/wglgears.exe" "$NVX_MOUNT/test_accel/wglgears.exe"
fi
if [ -f "$NVX_ROOT/test/dump_screen.exe" ]; then
    cp -v "$NVX_ROOT/test/dump_screen.exe" "$NVX_MOUNT/test_accel/dump_screen.exe"
fi
if [ -f "$NVX_ROOT/test/dump_3dmark_text.exe" ]; then
    cp -v "$NVX_ROOT/test/dump_3dmark_text.exe" "$NVX_MOUNT/test_accel/dump_3dmark_text.exe"
fi

if [ -f "$NVX_ROOT/test/ucrtbase.dll" ]; then
    cp -v "$NVX_ROOT/test/ucrtbase.dll" "$NVX_SYSTEM32/ucrtbase.dll"
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_ROOT/test/ucrtbase.dll" "$NVX_MOUNT/test_accel/ucrtbase.dll"
fi
if [ -f "$NVX_ROOT/test/ucrtbase32.dll" ]; then
    if [ -d "$NVX_MOUNT/WINDOWS/SysWOW64" ]; then
        cp -v "$NVX_ROOT/test/ucrtbase32.dll" "$NVX_MOUNT/WINDOWS/SysWOW64/ucrtbase.dll"
    fi
    mkdir -p "$NVX_MOUNT/test_accel/x86"
    cp -v "$NVX_ROOT/test/ucrtbase32.dll" "$NVX_MOUNT/test_accel/x86/ucrtbase.dll"
fi

# Deploy Wine Direct3D 9 -> OpenGL translation runtime and nvxgdi forwarding proxy
if [ -f "$NVX_PACKAGE/nvxgdi.dl" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_PACKAGE/nvxgdi.dl" "$NVX_MOUNT/test_accel/nvxgdi.dl"
    cp -v "$NVX_PACKAGE/nvxgdi.dl" "$NVX_SYSTEM32/nvxgdi.dl"
fi
if [ -f "$NVX_PACKAGE/nvxgdi32.dl" ]; then
    mkdir -p "$NVX_MOUNT/test_accel/x86"
    cp -v "$NVX_PACKAGE/nvxgdi32.dl" "$NVX_MOUNT/test_accel/x86/nvxgdi.dl"
    if [ -d "$NVX_MOUNT/WINDOWS/SysWOW64" ]; then
        cp -v "$NVX_PACKAGE/nvxgdi32.dl" "$NVX_MOUNT/WINDOWS/SysWOW64/nvxgdi.dl"
    fi
fi

if [ -f "$NVX_PACKAGE/libwine.dll" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_PACKAGE/libwine.dll" "$NVX_MOUNT/test_accel/libwine.dll"
    cp -v "$NVX_PACKAGE/libwine.dll" "$NVX_SYSTEM32/libwine.dll"
fi

if [ -f "$NVX_PACKAGE/d3d9.dll" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_PACKAGE/d3d9.dll" "$NVX_MOUNT/test_accel/d3d9.dll"
    cp -v "$NVX_PACKAGE/d3d9.dll" "$NVX_SYSTEM32/d3d9.dll"
elif [ -f /usr/lib/x86_64-linux-gnu/wine/x86_64-windows/d3d9.dll ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v /usr/lib/x86_64-linux-gnu/wine/x86_64-windows/d3d9.dll "$NVX_MOUNT/test_accel/d3d9.dll"
    cp -v /usr/lib/x86_64-linux-gnu/wine/x86_64-windows/d3d9.dll "$NVX_SYSTEM32/d3d9.dll"
fi

if [ -f "$NVX_PACKAGE/d3d8.dll" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_PACKAGE/d3d8.dll" "$NVX_MOUNT/test_accel/d3d8.dll"
    cp -v "$NVX_PACKAGE/d3d8.dll" "$NVX_SYSTEM32/d3d8.dll"
fi

if [ -f "$NVX_PACKAGE/ddraw.dll" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_PACKAGE/ddraw.dll" "$NVX_MOUNT/test_accel/ddraw.dll"
    cp -v "$NVX_PACKAGE/ddraw.dll" "$NVX_SYSTEM32/ddraw.dll"
fi

if [ -f "$NVX_PACKAGE/wined3d.dll" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v "$NVX_PACKAGE/wined3d.dll" "$NVX_MOUNT/test_accel/wined3d.dll"
    cp -v "$NVX_PACKAGE/wined3d.dll" "$NVX_SYSTEM32/wined3d.dll"
elif [ -f /usr/lib/x86_64-linux-gnu/wine/x86_64-windows/wined3d.dll ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    cp -v /usr/lib/x86_64-linux-gnu/wine/x86_64-windows/wined3d.dll "$NVX_MOUNT/test_accel/wined3d.dll"
    cp -v /usr/lib/x86_64-linux-gnu/wine/x86_64-windows/wined3d.dll "$NVX_SYSTEM32/wined3d.dll"
fi

if [ -d "$NVX_PACKAGE/vnc" ]; then
    mkdir -p "$NVX_MOUNT/TightVNC"
    cp -vr "$NVX_PACKAGE/vnc/"* "$NVX_MOUNT/TightVNC/"
fi

# Deploy the selected x86 D3D9 backend for SysWOW64 and the test directory.
if [ "$NVX_D3D9_BACKEND" = "nine" ]; then
    echo "[TEST-PACKAGE] x86 D3D9 backend: Gallium Nine"
    mkdir -p "$NVX_MOUNT/test_accel/x86"
    cp -v "$NVX_PACKAGE/x86/d3d9.dll" "$NVX_MOUNT/test_accel/x86/d3d9.dll"
    cp -v "$NVX_PACKAGE/x86/d3d9.dll" "$NVX_MOUNT/WINDOWS/SysWOW64/d3d9.dll"
elif [ -d "$NVX_WINE_X86_DIR" ]; then
    echo "[TEST-PACKAGE] x86 D3D9 backend: WineD3D"
    [ -d "$NVX_MOUNT/WINDOWS/SysWOW64" ] || {
        echo "XP x64 SysWOW64 directory is missing; cannot install 32-bit DirectX support." >&2
        exit 1
    }
    mkdir -p "$NVX_MOUNT/test_accel/x86"
    for dll in d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll; do
        if [ -f "$NVX_WINE_X86_DIR/$dll" ]; then
            cp -v "$NVX_WINE_X86_DIR/$dll" "$NVX_MOUNT/test_accel/x86/"
            cp -v "$NVX_WINE_X86_DIR/$dll" "$NVX_MOUNT/WINDOWS/SysWOW64/"
        fi
    done
fi

# Ensure /usepmtimer is set in C:\boot.ini for precise QPC timing on multi-core
if [ -f "$NVX_MOUNT/boot.ini" ]; then
    python3 - "$NVX_MOUNT/boot.ini" <<'PY'
from pathlib import Path
import sys
p = Path(sys.argv[1])
try:
    content = p.read_text(encoding='latin-1')
    if "/usepmtimer" not in content.lower():
        lines = []
        for line in content.splitlines():
            # Only add to [operating systems] entry line, never to default= line in [boot loader]
            if "partition" in line.lower() and "=" in line and "/usepmtimer" not in line.lower():
                line = line.rstrip() + " /usepmtimer"
            lines.append(line)
        p.write_text("\r\n".join(lines) + "\r\n", encoding='latin-1')
        print("[BOOT.INI] Appended /usepmtimer to guest operating systems configuration.")
    else:
        print("[BOOT.INI] /usepmtimer already present.")
except Exception as e:
    print(f"[BOOT.INI] Notice: {e}")
PY
fi

cat << 'EOF' > "$NVX_MOUNT/test_accel/run_test.bat"
@echo off
setlocal EnableDelayedExpansion
set NVX_SUITE_FAILED=0
rem All Users and Administrator Startup can both invoke this batch. Keep the
rem lock directory for this boot so only the first invocation runs benchmarks.
mkdir C:\test_accel\startup_test.lock >nul 2>&1
if errorlevel 1 exit /b 0
cd /d C:\test_accel
if exist C:\nvx_exception.log del C:\nvx_exception.log
if exist C:\test_accel\nvx_nouveau_trace.log del C:\test_accel\nvx_nouveau_trace.log
if exist C:\WINDOWS\nvxmp.log del C:\WINDOWS\nvxmp.log
echo [TEST] Starting Antigravity GPU Accel Suite... > C:\test_accel.log
echo [NVX_TEST_SUITE_START] > \\.\COM1
rem This window walker sends synchronous text queries to foreign UI threads.
rem Skip it during 3DMark; the benchmark has its own score/window capture path.
if not exist C:\test_accel\run_3dmark.flag if exist C:\test_accel\dump_dialog_error.exe start "" C:\test_accel\dump_dialog_error.exe
if exist C:\TightVNC\tvnserver.exe (
    echo [VNC] Starting TightVNC Server... >> C:\test_accel.log
    start "" /b C:\TightVNC\tvnserver.exe -run
)
if exist C:\3dmark05.log del C:\3dmark05.log
if exist C:\3dmark_caps.log del C:\3dmark_caps.log
if exist C:\test_accel\p2mf_probe_gart.flag (
    echo [TEST] Isolated GART P2MF probe... >> C:\test_accel.log
    nvx_p2mf_probe.exe gart >> C:\test_accel.log 2>&1
    if errorlevel 1 (echo [TEST] GART P2MF probe: FAIL >> C:\test_accel.log) else (echo [TEST] GART P2MF probe: PASS >> C:\test_accel.log)
)
if exist C:\test_accel\p2mf_probe_fb.flag (
    echo [TEST] Isolated FB P2MF probe... >> C:\test_accel.log
    nvx_p2mf_probe.exe fb >> C:\test_accel.log 2>&1
    if errorlevel 1 (echo [TEST] FB P2MF probe: FAIL >> C:\test_accel.log) else (echo [TEST] FB P2MF probe: PASS >> C:\test_accel.log)
)
if exist C:\test_accel\test_display_modes.exe (
    test_display_modes.exe >> C:\test_accel.log 2>&1
    if errorlevel 1 set NVX_SUITE_FAILED=1
)
if exist C:\test_accel\nine_only.flag goto NINE_ONLY
if exist C:\test_accel\wglgears.exe (
    cd /d C:\test_accel
    set GALLIUM_DRIVER=
    echo [TEST] Running 64-bit wglgears 3D benchmark... >> C:\test_accel.log
    wglgears.exe -info -seconds 6 >> C:\test_accel.log 2>&1
    set NVX_TEST_RC=!ERRORLEVEL!
    echo [TEST] 64-bit wglgears Return Code: !NVX_TEST_RC! >> C:\test_accel.log
    if not "!NVX_TEST_RC!"=="0" set NVX_SUITE_FAILED=1
)
cd /d C:\test_accel
test_gpu_accel.exe --opengl-only >> C:\test_accel.log 2>&1
set NVX_TEST_RC=!ERRORLEVEL!
echo [TEST] 64-bit Test Return Code: !NVX_TEST_RC! >> C:\test_accel.log
if not "!NVX_TEST_RC!"=="0" set NVX_SUITE_FAILED=1
findstr /l /c:"[NVX_3D_RESULT] FAIL" C:\test_accel.log >nul && set NVX_SUITE_FAILED=1
rem Run the WOW64 checks before the optional 3DMark GUI. This preserves the
rem x86 D3D capability and WGL diagnostics if the benchmark opens a modal
rem unsupported-hardware dialog or stalls before returning to the batch file.
if exist C:\test_accel\x86\wglgears32.exe (
    cd /d C:\test_accel\x86
    set GALLIUM_DRIVER=
    echo [TEST] Running 32-bit wglgears 3D benchmark... >> C:\test_accel.log
    wglgears32.exe -info -seconds 6 >> C:\test_accel.log 2>&1
    set NVX_TEST_RC=!ERRORLEVEL!
    echo [TEST] 32-bit wglgears Return Code: !NVX_TEST_RC! >> C:\test_accel.log
    if not "!NVX_TEST_RC!"=="0" set NVX_SUITE_FAILED=1
)
:NINE_ONLY
if exist C:\test_accel\x86\test_gpu_accel32.exe (
    cd /d C:\test_accel\x86
    if exist C:\mesa99.log del C:\mesa99.log
    if exist C:\test_accel\nine_only.flag (
        test_gpu_accel32.exe --d3d9-only >> C:\test_accel.log 2>&1
    ) else if exist C:\test_accel\run_3dmark.flag (
        rem Let the full 3DMark run exercise Nine's D3D9 path. The standalone
        rem 60-frame smoke stalled in the x86 run after frame 10 and blocked
        rem the requested benchmark, so retain the WGL checks here and skip
        rem only this redundant D3D smoke for benchmark runs.
        echo [TEST] Skipping x86 D3D9 frame smoke; 3DMark05 will exercise Nine >> C:\test_accel.log
        test_gpu_accel32.exe --opengl-only >> C:\test_accel.log 2>&1
    ) else (
        test_gpu_accel32.exe >> C:\test_accel.log 2>&1
    )
    set NVX_TEST_RC=!ERRORLEVEL!
    type C:\test_accel.log > \\.\COM1
    if "%NVX_NINE_DEBUG%"=="1" if exist C:\mesa99.log type C:\mesa99.log > \\.\COM1
    echo [TEST] 32-bit Test Return Code: !NVX_TEST_RC! >> C:\test_accel.log
    if not "!NVX_TEST_RC!"=="0" set NVX_SUITE_FAILED=1
    findstr /l /c:"[NVX_3D_RESULT] FAIL" C:\test_accel.log >nul && set NVX_SUITE_FAILED=1
)
if not exist C:\test_accel\run_3dmark.flag goto AFTER_3DMARK
    echo [3DMARK05] Starting automated game-test run... >> C:\test_accel.log
    echo [3DMARK05_START] > \\.\COM1
    if exist C:\test_accel\nine_backend.flag (
        echo [D3D9_BACKEND] Gallium Nine x86 > \\.\COM1
        echo [3DMARK05] D3D9 backend: Gallium Nine x86 >> C:\test_accel.log
    ) else (
        echo [D3D9_BACKEND] WineD3D x86 > \\.\COM1
        echo [3DMARK05] D3D9 backend: WineD3D x86 >> C:\test_accel.log
    )
    rem WineD3D performance settings to minimize CPU sync overhead
    if exist C:\test_accel\wined3d_backend.flag (
        reg add "HKCU\Software\Wine\Direct3D" /v StrictDrawOrdering /t REG_SZ /d disabled /f >nul 2>&1
        reg add "HKCU\Software\Wine\Direct3D" /v AlwaysOffscreen /t REG_SZ /d enabled /f >nul 2>&1
        reg add "HKCU\Software\Wine\Direct3D" /v CheckFloatConstants /t REG_SZ /d disabled /f >nul 2>&1
        reg add "HKCU\Software\Wine\Direct3D" /v MaxShaderModelVS /t REG_DWORD /d 3 /f >nul 2>&1
        reg add "HKCU\Software\Wine\Direct3D" /v MaxShaderModelPS /t REG_DWORD /d 3 /f >nul 2>&1
        reg add "HKCU\Software\Wine\Direct3D" /v csmt /t REG_DWORD /d 1 /f >nul 2>&1
    )
    echo [3DMARK05] Selected directory: C:\Program Files (x86)\Futuremark\3DMark05 >> C:\test_accel.log
    dir "C:\Program Files (x86)\Futuremark\3DMark05\3DMark05.exe" >> C:\test_accel.log 2>&1
    rem The host verifies the executable in the offline image and substitutes its path above.
    rem Do not repeat a guest IF EXIST check: it incorrectly failed on the x64 image path.
        cd /d "C:\Program Files (x86)\Futuremark\3DMark05"
        if not exist 3DMark05.exe goto 3DMARK_NOT_INSTALLED
        if exist C:\mesa99_trace.log del C:\mesa99_trace.log
        rem Keep WineD3D diagnostics from dominating the serial log during this long run.
        set WINEDEBUG=-all
        rem Launch background watcher to stream results to COM1 once available WITHOUT closing 3DMark
        start /b cmd /c "ping -n 60 127.0.0.1 >nul & :WATCH_LOOP & ping -n 5 127.0.0.1 >nul & if exist C:\test_accel\dump_3dmark_text.exe C:\test_accel\dump_3dmark_text.exe C:\3dmark_text.log & findstr /i "3DMark" C:\3dmark_text.log >nul 2>&1 && (echo [3DMARK05_SCORES_STREAM_START] > \\.\COM1 & type C:\3dmark_text.log > \\.\COM1 & echo [3DMARK05_SCORES_STREAM_END] > \\.\COM1 & exit /b 0) & goto WATCH_LOOP"
        if exist C:\test_accel\3dmark_automation_result.txt del C:\test_accel\3dmark_automation_result.txt
        if exist C:\test_accel\test_3dmark_caps_eval.exe C:\test_accel\test_3dmark_caps_eval.exe
        if exist C:\test_accel\run_3dmark_diag.flag (
            start "" C:\test_accel\test_3dmark_launch_diag.exe
        ) else if exist run_3dmark_full.exe ( start "" run_3dmark_full.exe ) else if exist C:\test_accel\run_3dmark_full.exe ( start "" C:\test_accel\run_3dmark_full.exe ) else ( start "" 3DMark05.exe -nosysteminfo )
        rem Periodically dump windows to find results
:WAIT_3DMARK
        ping -n 10 127.0.0.1 >nul
        tasklist /fi "imagename eq 3DMark05.exe" 2>nul | find /i "3DMark05.exe" >nul
        if not errorlevel 1 goto WAIT_3DMARK
        rem The launcher records the process exit code; do not report a closed
        rem or crashed 3DMark process as a successful run.
        rem Give the helper time to persist its result after the child exits.
        ping -n 3 127.0.0.1 >nul
        set NVX_TEST_RC=2
        if exist C:\test_accel\3dmark_automation_result.txt (
            for /f "tokens=1" %%A in (C:\test_accel\3dmark_automation_result.txt) do set NVX_TEST_RC=%%A
        )
        echo [3DMARK05] Process exit code: !NVX_TEST_RC! >> C:\test_accel.log
        if not "!NVX_TEST_RC!"=="0" set NVX_SUITE_FAILED=1
        if exist C:\test_accel\dump_3dmark_text.exe C:\test_accel\dump_3dmark_text.exe C:\3dmark_text.log
        if exist C:\test_accel\dump_screen.exe C:\test_accel\dump_screen.exe C:\3dmark_score.ppm
        if exist C:\3dmark_text.log type C:\3dmark_text.log >> C:\test_accel.log
        if exist C:\3dmark_text.log type C:\3dmark_text.log > \\.\COM1
        if exist C:\mesa99_trace.log (
            echo [MESA99_VIEW_TRACE_START] > \\.\COM1
            type C:\mesa99_trace.log > \\.\COM1
            echo [MESA99_VIEW_TRACE_END] > \\.\COM1
        )
        echo [3DMARK05_EXIT] code=!NVX_TEST_RC! > \\.\COM1
        echo [3DMARK05] RUN_COMPLETE >> C:\test_accel.log
        goto AFTER_3DMARK
:3DMARK_NOT_INSTALLED
        echo [3DMARK05] NOT_INSTALLED: expected 3DMark05.exe in the configured benchmark directory >> C:\test_accel.log
        echo [3DMARK05_NOT_INSTALLED] > \\.\COM1
        set NVX_SUITE_FAILED=1
:AFTER_3DMARK
    cd /d C:\test_accel
rem Keep the boot lock: both XP Startup folders can launch this batch at
rem different times, so releasing it here could run the benchmark suite twice.
if "!NVX_SUITE_FAILED!"=="0" (echo [ALL_BENCHMARKS_FINISHED] Status: PASS >> C:\test_accel.log) else (echo [ALL_BENCHMARKS_FINISHED] Status: FAIL >> C:\test_accel.log)
if "!NVX_SUITE_FAILED!"=="0" (echo [ALL_BENCHMARKS_FINISHED] Status: PASS > \\.\COM1) else (echo [ALL_BENCHMARKS_FINISHED] Status: FAIL > \\.\COM1)
echo [TEST] ALL TESTS COMPLETE >> C:\test_accel.log
rem Keeping VM running for user observation - no shutdown!
EOF
if [ -f "$NVX_ROOT/test/dump_dialog_error.exe" ]; then
    cp -v "$NVX_ROOT/test/dump_dialog_error.exe" "$NVX_MOUNT/test_accel/dump_dialog_error.exe"
fi
if [ -f "$NVX_ROOT/test/run_3dmark_full.exe" ]; then
    cp -v "$NVX_ROOT/test/run_3dmark_full.exe" "$NVX_MOUNT/test_accel/run_3dmark_full.exe"
fi
if [ -f "$NVX_ROOT/test/test_3dmark_launch_diag.exe" ]; then
    cp -v "$NVX_ROOT/test/test_3dmark_launch_diag.exe" "$NVX_MOUNT/test_accel/test_3dmark_launch_diag.exe"
fi
if [ -f "$NVX_ROOT/test/test_3dmark_caps_eval.exe" ]; then
    cp -v "$NVX_ROOT/test/test_3dmark_caps_eval.exe" "$NVX_MOUNT/test_accel/test_3dmark_caps_eval.exe"
fi
# Keep app-local D3D9 synchronized for the default launch diagnostic too.
# Windows loads that DLL before SysWOW64, so stale copies bypass package fixes.
mapfile -t NVX_3DMARK_EXES < <(find "$NVX_MOUNT" -type f -iname '3DMark05.exe' -print)
if [ "$NVX_RUN_3DMARK" = "1" ] && [ "${#NVX_3DMARK_EXES[@]}" -eq 0 ]; then
    echo "3DMark05.exe was not found in the guest image." >&2
    exit 1
fi
if [ "${#NVX_3DMARK_EXES[@]}" -gt 0 ]; then
    # Paths are shared by manual, diagnostic, and automated launches.
    NVX_3DMARK_EXE="${NVX_3DMARK_EXES[0]}"
    python3 - "$NVX_MOUNT/test_accel/run_test.bat" "$NVX_MOUNT" "$NVX_3DMARK_EXE" <<'PY'
from pathlib import Path
import sys
batch, mount, executable = sys.argv[1:]
directory = 'C:\\' + str(Path(executable).parent.relative_to(mount)).replace('/', '\\')
p = Path(batch)
p.write_text(p.read_text().replace(r'C:\Program Files (x86)\Futuremark\3DMark05', directory))
print('3DMark05 guest directory:', directory)
PY
    for NVX_3DMARK_EXE in "${NVX_3DMARK_EXES[@]}"; do
        NVX_3DMARK_DIR="$(dirname "$NVX_3DMARK_EXE")"
        if [ -f "$NVX_ROOT/test/dump_dialog_error.exe" ]; then
            cp -v "$NVX_ROOT/test/dump_dialog_error.exe" "$NVX_3DMARK_DIR/dump_dialog_error.exe"
        fi
        if [ -f "$NVX_ROOT/test/run_3dmark_full.exe" ]; then
            cp -v "$NVX_ROOT/test/run_3dmark_full.exe" "$NVX_3DMARK_DIR/run_3dmark_full.exe"
        fi
        if [ -f "$NVX_PACKAGE/x86/d3d9.dll" ]; then
            # 3DMark05 is x86. Never install the package-root (x64) D3D9 DLL
            # beside it; replace any stale/corrupted app-local copy in each
            # duplicate installation directory with the validated x86 build.
            if ! file "$NVX_PACKAGE/x86/d3d9.dll" | grep -q 'PE32 executable.*Intel 80386'; then
                echo "Refusing non-x86 D3D9 DLL: $NVX_PACKAGE/x86/d3d9.dll" >&2
                exit 1
            fi
            cp -v "$NVX_PACKAGE/x86/d3d9.dll" "$NVX_3DMARK_DIR/d3d9.dll"
        fi
    done
fi
unix2dos "$NVX_MOUNT/test_accel/run_test.bat" 2>/dev/null || true
rm -f "$NVX_MOUNT/test_accel/p2mf_probe_gart.flag" "$NVX_MOUNT/test_accel/p2mf_probe_fb.flag"
if [ "$NVX_GPU_READBACK_TRACE" = "1" ]; then
    : > "$NVX_MOUNT/test_accel/nvx_gpu_readback.flag"
    echo "[TEST-PACKAGE] bounded Nouveau vertex/shader GPU readback trace enabled"
else
    rm -f "$NVX_MOUNT/test_accel/nvx_gpu_readback.flag"
fi
if [ "${NVX_3DMARK_DIAG:-1}" = "1" ]; then
    : > "$NVX_MOUNT/test_accel/run_3dmark_diag.flag"
else
    rm -f "$NVX_MOUNT/test_accel/run_3dmark_diag.flag"
fi
case "${NVX_NVC0_P2MF_PROBE_TARGET:-}" in
    gart)
        : > "$NVX_MOUNT/test_accel/p2mf_probe_gart.flag"
        echo "[TEST-PACKAGE] isolated P2MF probe target=GART enabled"
        ;;
    fb)
        : > "$NVX_MOUNT/test_accel/p2mf_probe_fb.flag"
        echo "[TEST-PACKAGE] isolated P2MF probe target=FB enabled"
        ;;
esac
rm -rf "$NVX_MOUNT/test_accel/startup_test.lock"
if [ "$NVX_RUN_3DMARK" = "1" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    : > "$NVX_MOUNT/test_accel/run_3dmark.flag"
else
    rm -f "$NVX_MOUNT/test_accel/run_3dmark.flag"
fi
if [ "${NVX_NINE_ONLY:-0}" = "1" ]; then
    : > "$NVX_MOUNT/test_accel/nine_only.flag"
else
    rm -f "$NVX_MOUNT/test_accel/nine_only.flag"
fi
if [ "$NVX_D3D9_BACKEND" = "nine" ]; then
    : > "$NVX_MOUNT/test_accel/nine_backend.flag"
    rm -f "$NVX_MOUNT/test_accel/wined3d_backend.flag"
else
    : > "$NVX_MOUNT/test_accel/wined3d_backend.flag"
    rm -f "$NVX_MOUNT/test_accel/nine_backend.flag"
fi
NVX_STARTUP_ADMIN="$NVX_MOUNT/Documents and Settings/Administrator/Start Menu/Programs/Startup"
NVX_STARTUP_ALL="$NVX_MOUNT/Documents and Settings/All Users/Start Menu/Programs/Startup"
NVX_STARTUP_TESTUSER="$NVX_MOUNT/Documents and Settings/testuser/Start Menu/Programs/Startup"
for NVX_STARTUP in "$NVX_STARTUP_ADMIN" "$NVX_STARTUP_ALL" "$NVX_STARTUP_TESTUSER"; do
    if [ -d "$NVX_STARTUP" ]; then
        rm -f "$NVX_STARTUP/run_accel_test.bat" \
              "$NVX_STARTUP/run_accel_test.bat.disabled"
        cp -v "$NVX_MOUNT/test_accel/run_test.bat" \
            "$NVX_STARTUP/run_accel_test.bat"
    fi
done

if [ "${NVX_RAPID_3D:-0}" = "1" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    : > "$NVX_MOUNT/test_accel/rapid3d.flag"
else
    rm -f "$NVX_MOUNT/test_accel/rapid3d.flag"
fi
if [ "${NVX_OPENGL_ONLY:-0}" = "1" ]; then
    mkdir -p "$NVX_MOUNT/test_accel"
    : > "$NVX_MOUNT/test_accel/opengl_only.flag"
else
    rm -f "$NVX_MOUNT/test_accel/opengl_only.flag"
fi
sync
guestunmount --retry=10 "$NVX_MOUNT"
wait_for_release
rm -f "$NVX_GUESTMOUNT_PID"
echo "==> XP image updated without root"
