NVX Windows 98 SE experimental driver — version 4.0.0.45
========================================================

EXPERIMENTAL PRE-RELEASE. Use only for testing on a recoverable system.

This package targets one primary display and has been tested on an NVIDIA
GeForce RTX 3070 (GA104). The Win98 GDI solid-fill path and bounded OpenGL /
Gallium Nine smoke tests have reached the GPU. The general 3D pipeline is
work in progress and slow. 3DMark03 has not passed: its scene has shown
flashing/black output, and resource allocation and display-mode restore
failures remain under investigation. Do not expect reliable 3D applications.

The package also uses a private development VxD service ID (0x7D00), which
may conflict with another VxD. This is a developer pre-release, not a general
distribution build.

Supported configuration
------------------------
- Windows 98 Second Edition
- One primary monitor
- GA104 devices listed in NVX.INF (RTX 3060 / 3060 Ti / 3070 variants)
- 32-bit color modes; QHD mode depends on the monitor's reported timing

Install
-------
1. Extract this ZIP to a DOS-accessible directory, for example C:\NVXPKG.
2. Boot to "Command prompt only", change to that directory, and run:
       INSTALL.BAT
   This copies the package to C:\DRIVERS\NVX. It does not change active
   system files or install the driver by itself.
3. Start Windows by running WIN.
4. In Device Manager, open Display adapters and update the display driver.
   Choose "Have Disk" / "Specify a location" and select:
       C:\DRIVERS\NVX\NVX.INF
5. Select the matching NVIDIA GA104 device and reboot when prompted.

The NINE subdirectory contains experimental D3D9 components for development
and testing. INSTALL.BAT stages them but does not register or replace system
Direct3D DLLs. Do not copy them into C:\WINDOWS\SYSTEM for normal use.

ATTACH_SEC.EXE is included as an untested legacy secondary-display utility.
The release target is single-monitor; do not run this utility expecting a
working secondary display.

Recovery
--------
Boot to "Command prompt only" and restore the previous display driver in
Device Manager or use the system's backup/recovery method. Keep a boot disk
available before changing display drivers.

This build is provided for evaluation. The 3D path is incomplete; performance,
stability, multi-monitor operation, and broad hardware compatibility are not
established.
