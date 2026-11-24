@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d C:\test_accel
mkdir C:\test_accel\startup_test.lock >nul 2>&1
if errorlevel 1 exit /b 0

rem The same image can be booted without the physical GPU for setup/control.
rem Install TightVNC normally on first boot so its service/resources are registered.
netsh firewall add portopening TCP 5900 "TightVNC" >nul 2>&1
if not exist "C:\Program Files\TightVNC\tvnserver.exe" (
    echo [XP32_VNC] installing TightVNC x86 service >> C:\test_accel.log
    msiexec /i C:\test_accel\tightvnc-setup-x86.msi /qn /norestart ADDLOCAL=Server SERVER_REGISTER_AS_SERVICE=1 SERVER_ADD_FIREWALL_EXCEPTION=0 SET_ACCEPTRFBCONNECTIONS=1 VALUE_OF_ACCEPTRFBCONNECTIONS=1 SET_ACCEPTHTTPCONNECTIONS=1 VALUE_OF_ACCEPTHTTPCONNECTIONS=0 SET_RFBPORT=1 VALUE_OF_RFBPORT=5900 SET_USEVNCAUTHENTICATION=1 VALUE_OF_USEVNCAUTHENTICATION=0 SET_USECONTROLAUTHENTICATION=1 VALUE_OF_USECONTROLAUTHENTICATION=0 SET_LOOPBACKONLY=1 VALUE_OF_LOOPBACKONLY=0 SET_ALLOWLOOPBACK=1 VALUE_OF_ALLOWLOOPBACK=0
    echo [XP32_VNC] MSI install rc=!ERRORLEVEL! >> C:\test_accel.log
)
if exist "C:\Program Files\TightVNC\tvnserver.exe" "C:\Program Files\TightVNC\tvnserver.exe" -start -silent
for /l %%i in (1,1,15) do (
    netstat -an | find ":5900" >nul 2>&1
    if not errorlevel 1 goto VNC_READY
    ping -n 2 127.0.0.1 >nul
)
echo [XP32_VNC_NOT_LISTENING] > \\.\COM1
goto VNC_DONE
:VNC_READY
echo [XP32_VNC_LISTENING] > \\.\COM1
:VNC_DONE
echo [XP32_VNC] TightVNC setup/start checked on TCP 5900 > \\.\COM1

if not exist C:\test_accel\run_3dmark.flag (
    echo [3DMARK05_READY_NOT_ARMED] > \\.\COM1
    exit /b 0
)

if exist C:\test_accel.log del C:\test_accel.log
if exist C:\3dmark05.log del C:\3dmark05.log
if exist C:\3dmark_text.log del C:\3dmark_text.log
if exist C:\3dmark_caps.log del C:\3dmark_caps.log
echo [3DMARK05] XP x86 automated benchmark run >> C:\test_accel.log
echo [3DMARK05_START] > \\.\COM1

test_gpu_accel.exe --2d-only >> C:\test_accel.log 2>&1
echo [XP32_2D_PREFLIGHT] rc=!ERRORLEVEL! >> C:\test_accel.log
test_gpu_accel.exe --opengl-only >> C:\test_accel.log 2>&1
echo [XP32_OPENGL_PREFLIGHT] rc=!ERRORLEVEL! >> C:\test_accel.log

cd /d "C:\Program Files\Futuremark\3DMark05"
if not exist 3DMark05.exe goto NOT_INSTALLED

C:\test_accel\test_3dmark_caps_eval.exe > C:\3dmark_caps.log 2>&1
type C:\3dmark_caps.log > \\.\COM1

rem Capture the result dialog's accessible controls while the benchmark runs.
start "" /min C:\test_accel\watch_3dmark_text.bat

echo [3DMARK05] launching x86 UI automation >> C:\test_accel.log
if exist C:\test_accel\run_3dmark_diag.flag (
    C:\test_accel\test_3dmark_launch_diag.exe >> C:\3dmark05.log 2>&1
) else (
    .\run_3dmark_full.exe >> C:\3dmark05.log 2>&1
)
set NVX_3DMARK_RC=!ERRORLEVEL!
echo [3DMARK05] helper exit code=!NVX_3DMARK_RC! >> C:\test_accel.log
echo [3DMARK05_HELPER_EXIT] code=!NVX_3DMARK_RC! > \\.\COM1

dump_3dmark_text32.exe C:\3dmark_text.log
dump_screen32.exe C:\3dmark_score.ppm
if exist C:\3dmark_text.log (
    echo [3DMARK05_TEXT_CAPTURE_START] > \\.\COM1
    type C:\3dmark_text.log > \\.\COM1
    echo [3DMARK05_TEXT_CAPTURE_END] > \\.\COM1
)
if exist C:\3dmark05.3dr echo [3DMARK05_RESULT_FILE] C:\3dmark05.3dr > \\.\COM1
echo [3DMARK05_RUN_COMPLETE] > \\.\COM1
echo [ALL_BENCHMARKS_FINISHED] Status: COMPLETE > \\.\COM1
goto DONE

:NOT_INSTALLED
echo [3DMARK05] NOT_INSTALLED at C:\Program Files\Futuremark\3DMark05 >> C:\test_accel.log
echo [3DMARK05_NOT_INSTALLED] > \\.\COM1

:DONE
exit /b 0
