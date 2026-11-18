@echo off
setlocal EnableExtensions EnableDelayedExpansion

:: Resolve the directory where this .bat and dist_full files live
set "SRC_DIR=%~dp0"
if "%SRC_DIR:~-1%"=="\" set "SRC_DIR=%SRC_DIR:~0,-1%"

echo ==========================================================
echo  Installing Antigravity RTX 3070 XDDM + OpenGL + DirectX (WinXP x64)
echo  Source Folder: %SRC_DIR%
echo ==========================================================
echo.

:: Require the complete x64 and WOW64 graphics runtime before changing the guest.
for %%F in (gsp_ga104.bin nvx.inf nvxdisp.dll nvxmesa.dll nvxmp.sys nvxogl32.dll nvxogl64.dll d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll) do (
    if not exist "%SRC_DIR%\%%F" (
        echo [ERROR] Missing required x64 package file: %%F
        echo Build and stage the driver, Mesa ICD, and WineD3D outputs first.
        pause
        exit /b 1
    )
)
for %%F in (d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll) do (
    if not exist "%SRC_DIR%\x86\%%F" (
        echo [ERROR] Missing required x86 WineD3D file: x86\%%F
        echo Build and stage both WineD3D architectures first.
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
if not exist "%SYSWOW64%" (
    echo [ERROR] This installer targets Windows XP x64; SysWOW64 was not found.
    pause
    exit /b 1
)

:: 1. Back up every replaced binary, keeping x86 and x64 files separate.
set "BACKUP_DIR=%SystemDrive%\nvx_backup"
if not exist "%BACKUP_DIR%\drivers" mkdir "%BACKUP_DIR%\drivers"
if not exist "%BACKUP_DIR%\System32" mkdir "%BACKUP_DIR%\System32"
if not exist "%BACKUP_DIR%\SysWOW64" mkdir "%BACKUP_DIR%\SysWOW64"
for %%F in (nvxmp.sys gsp_ga104.bin) do if exist "%DRV_DIR%\%%F" copy /y "%DRV_DIR%\%%F" "%BACKUP_DIR%\drivers\%%F" >nul
for %%F in (nvxdisp.dll nvxmesa.dll nvxogl64.dll d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll ucrtbase.dll nvxgdi.dl) do if exist "%SYS32%\%%F" copy /y "%SYS32%\%%F" "%BACKUP_DIR%\System32\%%F" >nul
for %%F in (nvxogl32.dll d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll ucrtbase.dll nvxgdi.dl) do if exist "%SYSWOW64%\%%F" copy /y "%SYSWOW64%\%%F" "%BACKUP_DIR%\SysWOW64\%%F" >nul
echo [1/5] Existing driver and graphics files backed up to %BACKUP_DIR%

:: 2. Install miniport, display DDI, firmware, OpenGL ICDs, and both WineD3D runtimes.
echo [2/5] Installing x64 and x86 driver, OpenGL, and DirectX components...
for %%F in (nvxmp.sys gsp_ga104.bin) do copy /y "%SRC_DIR%\%%F" "%DRV_DIR%\%%F" >nul || goto :copy_fail
for %%F in (nvxdisp.dll nvxmesa.dll nvxogl64.dll d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll) do copy /y "%SRC_DIR%\%%F" "%SYS32%\%%F" >nul || goto :copy_fail
for %%F in (nvxogl32.dll d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll) do (
    if "%%F"=="nvxogl32.dll" (
        copy /y "%SRC_DIR%\%%F" "%SYSWOW64%\%%F" >nul || goto :copy_fail
    ) else (
        copy /y "%SRC_DIR%\x86\%%F" "%SYSWOW64%\%%F" >nul || goto :copy_fail
    )
)
copy /y "%SRC_DIR%\nvx.inf" "%SystemRoot%\inf\nvx.inf" >nul || goto :copy_fail
if exist "%SRC_DIR%\nvxgdi.dl" copy /y "%SRC_DIR%\nvxgdi.dl" "%SYS32%\nvxgdi.dl" >nul || goto :copy_fail
if exist "%SRC_DIR%\x86\nvxgdi.dl" copy /y "%SRC_DIR%\x86\nvxgdi.dl" "%SYSWOW64%\nvxgdi.dl" >nul || goto :copy_fail
if exist "%SRC_DIR%\ucrtbase.dll" copy /y "%SRC_DIR%\ucrtbase.dll" "%SYS32%\ucrtbase.dll" >nul || goto :copy_fail
if exist "%SRC_DIR%\x86\ucrtbase.dll" copy /y "%SRC_DIR%\x86\ucrtbase.dll" "%SYSWOW64%\ucrtbase.dll" >nul || goto :copy_fail

:: Stage the complete package and tests for Device Manager and local diagnostics.
if not exist "%SystemDrive%\nvx_driver" mkdir "%SystemDrive%\nvx_driver"
xcopy /e /i /y "%SRC_DIR%\*" "%SystemDrive%\nvx_driver\" >nul || goto :copy_fail
if not exist "%SystemDrive%\test_accel\x86" mkdir "%SystemDrive%\test_accel\x86"
copy /y "%SRC_DIR%\nvxmesa.dll" "%SystemDrive%\test_accel\nvxmesa.dll" >nul || goto :copy_fail
if exist "%SRC_DIR%\nvxgdi.dl" copy /y "%SRC_DIR%\nvxgdi.dl" "%SystemDrive%\test_accel\nvxgdi.dl" >nul || goto :copy_fail
for %%F in (test_gpu_accel.exe wglgears.exe ucrtbase.dll) do if exist "%SRC_DIR%\%%F" copy /y "%SRC_DIR%\%%F" "%SystemDrive%\test_accel\%%F" >nul
for %%F in (d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll) do copy /y "%SRC_DIR%\%%F" "%SystemDrive%\test_accel\%%F" >nul || goto :copy_fail
if exist "%SRC_DIR%\x86\nvxgdi.dl" copy /y "%SRC_DIR%\x86\nvxgdi.dl" "%SystemDrive%\test_accel\x86\nvxgdi.dl" >nul || goto :copy_fail
for %%F in (test_gpu_accel32.exe wglgears32.exe ucrtbase.dll) do if exist "%SRC_DIR%\x86\%%F" copy /y "%SRC_DIR%\x86\%%F" "%SystemDrive%\test_accel\x86\%%F" >nul
for %%F in (d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll) do copy /y "%SRC_DIR%\x86\%%F" "%SystemDrive%\test_accel\x86\%%F" >nul || goto :copy_fail

:: 3. Register nvxmp kernel service & display settings in Registry
echo [3/5] Registering nvxmp service and x64/WOW64 OpenGL ICDs...
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
echo  3. Direct3D 9/8/DirectDraw use the installed WineD3D runtimes.
echo  4. After reboot, run C:\test_accel\test_gpu_accel.exe and
echo     check C:\WINDOWS\nvxmp.log and C:\WINDOWS\nvxdisp.log.
echo ==========================================================
pause
exit /b 0

:copy_fail
echo [ERROR] Failed to copy driver files. Make sure you are running as Administrator
echo         and the files are not currently locked in use.
pause
exit /b 1
