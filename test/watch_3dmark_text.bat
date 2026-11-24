@echo off
setlocal
cd /d "C:\Program Files\Futuremark\3DMark05"
:WATCH
ping -n 6 127.0.0.1 >nul
tasklist /fi "imagename eq 3DMark05.exe" 2>nul | find /i "3DMark05.exe" >nul
if errorlevel 1 exit /b 0
C:\test_accel\dump_3dmark_text32.exe C:\3dmark_text.log
findstr /i /c:"3DMark Score:" C:\3dmark_text.log >nul 2>&1
if errorlevel 1 goto WATCH
echo [3DMARK05_SCORE_TEXT_FOUND] > \\.\COM1
type C:\3dmark_text.log > \\.\COM1
C:\test_accel\dump_screen32.exe C:\3dmark_score.ppm
echo [3DMARK05_SCORE_SCREEN_CAPTURED] > \\.\COM1
exit /b 0
