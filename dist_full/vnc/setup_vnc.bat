@echo off
setlocal
set "VNC_DIR=%SystemDrive%\TightVNC"
if not exist "%VNC_DIR%" mkdir "%VNC_DIR%"

copy /y "%~dp0\tvnserver.exe" "%VNC_DIR%\" >nul
copy /y "%~dp0\screenhooks*.dll" "%VNC_DIR%\" >nul
copy /y "%~dp0\tvnhookloader.exe" "%VNC_DIR%\" >nul
copy /y "%~dp0\tvnsas.dll" "%VNC_DIR%\" >nul

:: Stop existing service if running
"%VNC_DIR%\tvnserver.exe" -stop -silent >nul 2>&1
"%VNC_DIR%\tvnserver.exe" -remove -silent >nul 2>&1

:: Register service
"%VNC_DIR%\tvnserver.exe" -install -silent

:: Configure registry: no password required (AcceptWithoutAuthentication = 1) or set password
reg add "HKLM\SOFTWARE\TightVNC\Server" /v AcceptHttpConnections /t REG_DWORD /d 0 /f >nul 2>&1
reg add "HKLM\SOFTWARE\TightVNC\Server" /v AcceptRfbConnections /t REG_DWORD /d 1 /f >nul 2>&1
reg add "HKLM\SOFTWARE\TightVNC\Server" /v RfbPort /t REG_DWORD /d 5900 /f >nul 2>&1
reg add "HKLM\SOFTWARE\TightVNC\Server" /v DisconnectAction /t REG_DWORD /d 0 /f >nul 2>&1
reg add "HKLM\SOFTWARE\TightVNC\Server" /v AcceptWithoutAuthentication /t REG_DWORD /d 1 /f >nul 2>&1
reg add "HKLM\SOFTWARE\TightVNC\Server" /v UseControlAuthentication /t REG_DWORD /d 0 /f >nul 2>&1

:: Open Windows Firewall for port 5900
netsh firewall add portopening TCP 5900 "TightVNC" >nul 2>&1

:: Start service
"%VNC_DIR%\tvnserver.exe" -start -silent
echo [VNC] TightVNC service configured and started.
