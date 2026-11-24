@echo off
setlocal EnableExtensions

set "SRC_DIR=%~dp0"
if "%SRC_DIR:~-1%"=="\" set "SRC_DIR=%SRC_DIR:~0,-1%"

echo ==========================================================
echo  NVIDIA RTX XDDM graphics installer - Windows XP x86
echo  Package: %SRC_DIR%
echo ==========================================================

if /i not "%PROCESSOR_ARCHITECTURE%"=="x86" goto :wrong_arch
if defined PROCESSOR_ARCHITEW6432 goto :wrong_arch

for %%F in (nvxmp.sys nvxdisp.dll nvx.inf gsp_ga104.bin nvxogl32.dll d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll) do (
    if not exist "%SRC_DIR%\%%F" (
        echo [ERROR] Missing package component: %%F
        echo Assemble the complete native XP x86 package as described in README.md.
        goto :fail
    )
)

set "SYS=%SystemRoot%\System32"
set "DRV=%SYS%\drivers"
set "BACKUP=%SystemDrive%\nvx_backup_x86"
set "STAGE=%SystemDrive%\nvx_driver_x86"

if not exist "%BACKUP%\drivers" mkdir "%BACKUP%\drivers"
if not exist "%BACKUP%\System32" mkdir "%BACKUP%\System32"
for %%F in (nvxmp.sys gsp_ga104.bin) do if exist "%DRV%\%%F" copy /y "%DRV%\%%F" "%BACKUP%\drivers\%%F" >nul
for %%F in (nvxdisp.dll nvxogl32.dll d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll) do if exist "%SYS%\%%F" copy /y "%SYS%\%%F" "%BACKUP%\System32\%%F" >nul
echo [1/4] Existing driver and DirectX files backed up to %BACKUP%.

for %%F in (nvxmp.sys gsp_ga104.bin) do copy /y "%SRC_DIR%\%%F" "%DRV%\%%F" >nul || goto :copy_fail
for %%F in (nvxdisp.dll nvxogl32.dll d3d9.dll d3d8.dll ddraw.dll wined3d.dll libwine.dll) do copy /y "%SRC_DIR%\%%F" "%SYS%\%%F" >nul || goto :copy_fail
copy /y "%SRC_DIR%\nvx.inf" "%SystemRoot%\inf\nvx.inf" >nul || goto :copy_fail
echo [2/4] Driver, OpenGL ICD, Gallium Nine D3D9, and WineD3D D3D8/DirectDraw files installed.

if not exist "%STAGE%" mkdir "%STAGE%"
xcopy /e /i /y "%SRC_DIR%\*" "%STAGE%\" >nul || goto :copy_fail

set "SVC=HKLM\SYSTEM\CurrentControlSet\Services\nvxmp"
reg add "%SVC%" /v Type /t REG_DWORD /d 1 /f >nul || goto :registry_fail
reg add "%SVC%" /v Start /t REG_DWORD /d 1 /f >nul || goto :registry_fail
reg add "%SVC%" /v ErrorControl /t REG_DWORD /d 0 /f >nul || goto :registry_fail
reg add "%SVC%" /v ImagePath /t REG_EXPAND_SZ /d "system32\DRIVERS\nvxmp.sys" /f >nul || goto :registry_fail
reg add "%SVC%" /v Group /t REG_SZ /d "Video" /f >nul || goto :registry_fail
reg add "%SVC%" /v MultiFunctionSupported /t REG_DWORD /d 1 /f >nul || goto :registry_fail
reg add "%SVC%\Device0" /v InstalledDisplayDrivers /t REG_MULTI_SZ /d "nvxdisp" /f >nul || goto :registry_fail
reg add "%SVC%\Device0" /v VgaCompatible /t REG_DWORD /d 0 /f >nul || goto :registry_fail
reg add "%SVC%\Device0" /v DefaultSettings.BitsPerPel /t REG_DWORD /d 32 /f >nul || goto :registry_fail
reg add "%SVC%\Device0" /v DefaultSettings.XResolution /t REG_DWORD /d 1920 /f >nul || goto :registry_fail
reg add "%SVC%\Device0" /v DefaultSettings.YResolution /t REG_DWORD /d 1080 /f >nul || goto :registry_fail

set "OGL=HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\OpenGLDrivers\nvxdisp"
reg add "%OGL%" /v Dll /t REG_SZ /d "nvxogl32.dll" /f >nul || goto :registry_fail
reg add "%OGL%" /v DriverVersion /t REG_DWORD /d 65536 /f >nul || goto :registry_fail
reg add "%OGL%" /v Flags /t REG_DWORD /d 1 /f >nul || goto :registry_fail
reg add "%OGL%" /v Version /t REG_DWORD /d 2 /f >nul || goto :registry_fail

echo [3/4] Registering the display miniport and OpenGL ICD...
rundll32.exe setupapi,InstallHinfSection nvx_Inst 132 "%STAGE%\nvx.inf"
if errorlevel 1 echo [WARNING] INF installation returned an error; use Device Manager Have Disk with %STAGE%\nvx.inf.

if not exist "%SystemDrive%\test_accel" mkdir "%SystemDrive%\test_accel"
for %%F in (test_gpu_accel32.exe wglgears32.exe test_3dmark_launch_diag.exe) do if exist "%SRC_DIR%\%%F" copy /y "%SRC_DIR%\%%F" "%SystemDrive%\test_accel\%%F" >nul
echo [4/4] Installation files and optional test programs staged.
echo.
echo Reboot Windows XP to load the driver.
echo If the NVIDIA adapter is not active after reboot, open Device Manager,
echo choose Update Driver ^> Have Disk, and select %STAGE%\nvx.inf.
echo Backups are in %BACKUP%.
pause
exit /b 0

:wrong_arch
echo [ERROR] This installer is for native 32-bit Windows XP only. It does not install under WoW64.
goto :fail

:copy_fail
echo [ERROR] A file copy failed. Run this installer as Administrator and check that files are not in use.
goto :fail

:registry_fail
echo [ERROR] Registry setup failed. Run this installer as Administrator.

:fail
pause
exit /b 1
