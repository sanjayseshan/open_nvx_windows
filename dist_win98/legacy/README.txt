Experimental Windows 98 DirectDraw / legacy Direct3D test bundle

ddraw.dll          Wine9x Win98 DDraw switcher (install as Windows\\System\\ddraw.dll)
winedd.dll         Wine9x DirectDraw / Direct3D 6-7 implementation
wined3d.dll        Wine9x shared renderer; uses the system OpenGL ICD
legacy_ddraw.exe   DDraw + D3D3 HAL device and triangle-submission smoke test
nine_d3d9.exe      Gallium Nine D3D9 HAL device, clear, triangle, present test

The test runner writes API results to COM1. A passing D3D3 triangle submission
proves that the Wine9x DDraw/Direct3D path reaches its renderer; GPU activity
must also be confirmed from NVX submission records in the host serial log.
Back up the existing system DDRAW.DLL as DDSYS.DLL before installing the
switcher. The switcher routes OpenGL's DDraw calls to the original system DLL
and application DDraw calls to WineD3D.
