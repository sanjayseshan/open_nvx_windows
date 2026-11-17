@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: Resolve the directory where this .bat and dist_full files live
set "SRC_DIR=%~dp0"
if "%SRC_DIR:~-1%"=="\" set "SRC_DIR=%SRC_DIR:~0,-1%"

echo ==========================================================
echo  Installing Antigravity RTX 3070 XDDM Driver (WinXP)
echo  Source Folder: %SRC_DIR%
echo ==========================================================
echo.

:: Verify all required files from dist_full are in the same folder
for %%F in (gsp_ga104.bin nvx.inf nvxdisp.dll nvxmesa.dll nvxmp.sys nvxogl32.dll nvxogl64.dll) do (
    if not exist "%SRC_DIR%\%%F" (
        echo [ERROR] Missing required file: "%SRC_DIR%\%%F"
        echo Make sure install.bat is inside the dist_full folder.
        pause
        exit /b 1
    )
)

:: Handle WOW64 redirection if invoked from 32-bit cmd.exe on XP x64
set "SYS32=%SystemRoot%\System32"
set "SYSWOW64=%SystemRoot%\SysWOW64"
set "REG_VIEW="
if exist "%SystemRoot%\Sysnative\cmd.exe" (
    set "SYS32=%SystemRoot%\Sysnative"
    set "REG_VIEW=/reg:64"
)
set "DRV_DIR=%SYS32%\drivers"

:: 1. Backup existing driver files
set "BACKUP_DIR=%SystemDrive%\nvx_backup"
if not exist "%BACKUP_DIR%" mkdir "%BACKUP_DIR%"
for %%F in ("%DRV_DIR%\nvxmp.sys" "%DRV_DIR%\gsp_ga104.bin" "%SYS32%\nvxdisp.dll" "%SYS32%\nvxmesa.dll" "%SYS32%\nvxogl64.dll" "%SYSWOW64%\nvxogl32.dll") do (
    if exist %%F copy /y %%F "%BACKUP_DIR%\" >nul
)
echo [1/5] Existing files backed up to %BACKUP_DIR%

:: 2. Copy kernel miniport, GSP firmware, display DLL, Mesa, and OpenGL ICD
echo [2/5] Copying driver, OpenGL, and WineD3D binaries...
copy /y "%SRC_DIR%\nvxmp.sys"     "%DRV_DIR%\nvxmp.sys"     >nul || goto :copy_fail
copy /y "%SRC_DIR%\gsp_ga104.bin" "%DRV_DIR%\gsp_ga104.bin" >nul || goto :copy_fail
copy /y "%SRC_DIR%\nvxdisp.dll"   "%SYS32%\nvxdisp.dll"     >nul || goto :copy_fail
copy /y "%SRC_DIR%\nvxmesa.dll"   "%SYS32%\nvxmesa.dll"     >nul || goto :copy_fail
copy /y "%SRC_DIR%\nvxogl64.dll"  "%SYS32%\nvxogl64.dll"    >nul || goto :copy_fail
if exist "%SystemRoot%\SysWOW64" (
    copy /y "%SRC_DIR%\nvxogl32.dll" "%SYSWOW64%\nvxogl32.dll" >nul || goto :copy_fail
)
copy /y "%SRC_DIR%\nvx.inf"       "%SystemRoot%\inf\nvx.inf" >nul || goto :copy_fail
if exist "%SRC_DIR%\nvxgdi.dl"    copy /y "%SRC_DIR%\nvxgdi.dl"    "%SYS32%\nvxgdi.dl"    >nul
if exist "%SRC_DIR%\ucrtbase.dll" copy /y "%SRC_DIR%\ucrtbase.dll" "%SYS32%\ucrtbase.dll" >nul
if exist "%SRC_DIR%\libwine.dll"  copy /y "%SRC_DIR%\libwine.dll"  "%SYS32%\libwine.dll"  >nul
if exist "%SRC_DIR%\d3d9.dll"     copy /y "%SRC_DIR%\d3d9.dll"     "%SYS32%\d3d9.dll"     >nul
if exist "%SRC_DIR%\d3d8.dll"     copy /y "%SRC_DIR%\d3d8.dll"     "%SYS32%\d3d8.dll"     >nul
if exist "%SRC_DIR%\ddraw.dll"    copy /y "%SRC_DIR%\ddraw.dll"    "%SYS32%\ddraw.dll"    >nul
if exist "%SRC_DIR%\wined3d.dll"  copy /y "%SRC_DIR%\wined3d.dll"  "%SYS32%\wined3d.dll"  >nul

:: Stage a clean copy in C:\nvx_driver for Device Manager / PnP Wizard
if not exist "%SystemDrive%\nvx_driver" mkdir "%SystemDrive%\nvx_driver"
copy /y "%SRC_DIR%\*.*" "%SystemDrive%\nvx_driver\" >nul

:: Stage test suite in C:\test_accel if present
if not exist "%SystemDrive%\test_accel" mkdir "%SystemDrive%\test_accel"
if exist "%SRC_DIR%\test_gpu_accel.exe" copy /y "%SRC_DIR%\test_gpu_accel.exe" "%SystemDrive%\test_accel\test_gpu_accel.exe" >nul
if exist "%SRC_DIR%\wglgears.exe"       copy /y "%SRC_DIR%\wglgears.exe"       "%SystemDrive%\test_accel\wglgears.exe"       >nul
if exist "%SRC_DIR%\nvxgdi.dl"          copy /y "%SRC_DIR%\nvxgdi.dl"          "%SystemDrive%\test_accel\nvxgdi.dl"          >nul
if exist "%SRC_DIR%\ucrtbase.dll"       copy /y "%SRC_DIR%\ucrtbase.dll"       "%SystemDrive%\test_accel\ucrtbase.dll"       >nul
if exist "%SRC_DIR%\libwine.dll"        copy /y "%SRC_DIR%\libwine.dll"        "%SystemDrive%\test_accel\libwine.dll"        >nul
if exist "%SRC_DIR%\d3d9.dll"           copy /y "%SRC_DIR%\d3d9.dll"           "%SystemDrive%\test_accel\d3d9.dll"           >nul
if exist "%SRC_DIR%\d3d8.dll"           copy /y "%SRC_DIR%\d3d8.dll"           "%SystemDrive%\test_accel\d3d8.dll"           >nul
if exist "%SRC_DIR%\ddraw.dll"          copy /y "%SRC_DIR%\ddraw.dll"          "%SystemDrive%\test_accel\ddraw.dll"          >nul
if exist "%SRC_DIR%\wined3d.dll"        copy /y "%SRC_DIR%\wined3d.dll"        "%SystemDrive%\test_accel\wined3d.dll"        >nul

:: 3. Register nvxmp kernel service & display settings in Registry
echo [3/5] Registering nvxmp service and OpenGL ICD in Registry...
set "SVC_KEY=HKLM\SYSTEM\CurrentControlSet\Services\nvxmp"

reg add "%SVC_KEY%" /v Type /t REG_DWORD /d 1 /f %REG_VIEW% >nul
reg add "%SVC_KEY%" /v Start /t REG_DWORD /d 1 /f %REG_VIEW% >nul
reg add "%SVC_KEY%" /v ErrorControl /t REG_DWORD /d 0 /f %REG_VIEW% >nul
reg add "%SVC_KEY%" /v Tag /t REG_DWORD /d 2 /f %REG_VIEW% >nul
reg add "%SVC_KEY%" /v ImagePath /t REG_EXPAND_SZ /d "system32\DRIVERS\nvxmp.sys" /f %REG_VIEW% >nul
reg add "%SVC_KEY%" /v Group /t REG_SZ /d "Video" /f %REG_VIEW% >nul
reg add "%SVC_KEY%" /v MultiFunctionSupported /t REG_DWORD /d 1 /f %REG_VIEW% >nul

reg add "%SVC_KEY%\Device0" /v InstalledDisplayDrivers /t REG_MULTI_SZ /d "nvxdisp" /f %REG_VIEW% >nul
reg add "%SVC_KEY%\Device0" /v VgaCompatible /t REG_DWORD /d 0 /f %REG_VIEW% >nul
reg add "%SVC_KEY%\Device0" /v DefaultSettings.BitsPerPel /t REG_DWORD /d 32 /f %REG_VIEW% >nul
reg add "%SVC_KEY%\Device0" /v DefaultSettings.XResolution /t REG_DWORD /d 1920 /f %REG_VIEW% >nul
reg add "%SVC_KEY%\Device0" /v DefaultSettings.YResolution /t REG_DWORD /d 1080 /f %REG_VIEW% >nul

:: Register OpenGL ICD (nvxdisp -> nvxogl64.dll)
set "OGL_KEY=HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\nvxdisp"
reg add "%OGL_KEY%" /v Dll /t REG_SZ /d "nvxogl64.dll" /f %REG_VIEW% >nul
reg add "%OGL_KEY%" /v DriverVersion /t REG_DWORD /d 65536 /f %REG_VIEW% >nul
reg add "%OGL_KEY%" /v Flags /t REG_DWORD /d 1 /f %REG_VIEW% >nul
reg add "%OGL_KEY%" /v Version /t REG_DWORD /d 2 /f %REG_VIEW% >nul
set "OGL_WOW_KEY=HKLM\SOFTWARE\Wow6432Node\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\nvxdisp"
reg add "%OGL_WOW_KEY%" /v Dll /t REG_SZ /d "nvxogl32.dll" /f %REG_VIEW% >nul
reg add "%OGL_WOW_KEY%" /v DriverVersion /t REG_DWORD /d 65536 /f %REG_VIEW% >nul
reg add "%OGL_WOW_KEY%" /v Flags /t REG_DWORD /d 1 /f %REG_VIEW% >nul
reg add "%OGL_WOW_KEY%" /v Version /t REG_DWORD /d 2 /f %REG_VIEW% >nul

:: 4. Run INF DefaultInstall & trigger Plug-and-Play device rescan
echo [4/5] Running INF installation and PnP hardware scan...
rundll32.exe setupapi,InstallHinfSection DefaultInstall 132 %SystemDrive%\nvx_driver\nvx.inf

:: If devcon.exe is available on the system, update PCI\VEN_10DE&DEV_2488 directly
where devcon.exe >nul 2>&1
if %ERRORLEVEL%==0 (
    echo       Found devcon.exe - updating PCI\VEN_10DE^&DEV_2488...
    devcon.exe update "%SystemDrive%\nvx_driver\nvx.inf" "PCI\VEN_10DE&DEV_2488"
) else (
    :: Trigger Windows XP PnP bus rescan
    rundll32.exe syssetup,SetupInfObjectInstallAction DefaultInstall 128 %SystemDrive%\nvx_driver\nvx.inf >nul 2>&1
)

:: 5. Done
echo [5/5] Installation complete!
echo.
echo ==========================================================
echo  Next Steps:
echo  1. Open Device Manager (devmgmt.msc). If your RTX 3070 is
echo     still listed under "Video Controller (VGA Compatible)" or
echo     "Standard VGA Graphics Adapter", right-click it -^>
echo     "Update Driver..." and point it to: C:\nvx_driver
echo  2. Reboot Windows XP.
echo  3. After reboot, run C:\test_accel\test_gpu_accel.exe and
echo     check C:\WINDOWS\nvxmp.log and C:\WINDOWS\nvxdisp.log.
echo ==========================================================
pause
exit /b 0

:copy_fail
echo [ERROR] Failed to copy driver files. Make sure you are running as Administrator
echo         and the files are not currently locked in use.
pause
exit /b 1
