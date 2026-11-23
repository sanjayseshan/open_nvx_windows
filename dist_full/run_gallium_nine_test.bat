@echo off
echo Running Gallium Nine direct D3D9 deployment and test...
copy /y "%~dp0x86\d3d9.dll" "C:\test_accel\x86\d3d9.dll" >nul
copy /y "%~dp0x86\d3d9.dll" "C:\WINDOWS\SysWOW64\d3d9.dll" >nul
if exist "C:\Program Files (x86)\Futuremark\3DMark05" (
    copy /y "%~dp0x86\d3d9.dll" "C:\Program Files (x86)\Futuremark\3DMark05\d3d9.dll" >nul
)

cd /d "C:\Program Files (x86)\Futuremark\3DMark05"
if exist 3DMark05.exe (
    echo Starting 3DMark05 with Gallium Nine...
    start /wait 3DMark05.exe -nosound -batch
) else (
    echo 3DMark05 not found in Program Files.
)
