# NVX experimental display driver for Windows 98 SE — 4.0.0.45

This is an experimental pre-release. The 3D pipeline is work in progress and slow; 3DMark03 is not reliable.

## Current target and status

Icon rendering was visually confirmed correct on 2026-11-26 after restoring
standard GDI monochrome expansion, removing the icon color replay workaround,
and rebuilding Explorer's `ShellIconCache`. True-color GDI capabilities now
report unlimited colors and pens, matching the DIB Engine path. The deployment
script removes the disposable shell icon cache so Windows rebuilds it with the
new driver. The confirming run is `test_logs/win98-20261126-121305.log`:
2560×1440×32, `[2D_RESULT] PASS`, and one-shot 2D test PASS. This verifies
icons and the 2D test; it does not establish new Direct3D results.

The reproducible build target is **one primary monitor**. Run `./src_win98/build.sh` from the repository root; it builds `nvx.drv` and `nvx.vxd`, generates a primary-only `nvx.inf`, and stages `GSPGA104.BIN` in `dist_win98/`. Set `WIN98_SINGLE_MONITOR=0` only to build the older secondary display path as well.

The package has booted on the RTX 3070 under Win98 SE through GSP initialization, display-driver startup, and `Control 39`. The DIB access callbacks use Open Watcom `__loadds` to load the driver's data segment. Win98 GDI solid fills reach the GPU through the display driver's BitBlt callback, VxD PM service, and shared miniport. Mesa/Nouveau OpenGL cube/gears and a bounded Gallium Nine triangle have rendered in earlier runs. The current 3DMark03 route initializes Nine and submits NVC0 draws, but the visible scene is corrupted/black; it is not a passing application test.

Primary QHD now uses a native modeset instead of changing only the VBE pitch. On first load, WINVBE asks the VxD for the GSP-decoded primary EDID timing, advertises 2560×1440 only when that timing is present, and submits the selected mode through the shared miniport/EVO scanout path. The 2026-11-25 RTX 3070 boot log confirms `WINVBE loaded: 2560x1440x32`, native scanout activation, both active heads, and a matching 10,240-byte GDI pitch. GDI/OpenGL/legacy DDraw/Nine test stages pass at this mode. The physical monitor image still needs visual confirmation; the earlier pitch-only synthetic mode caused stripes and is no longer used.

The VxD DIOC handler forwards the shared `IOCTL_NVX_*` ABI through `NvxShimDispatchMiniportIoctl()` to the same `HwStartIO` handler used by XP. The Win98 PM service calls that VxD handler, while the shared miniport validates requests and uses `NvxGspSubmitFill/Copy/Upload`; the Win98 layer does not build GPU packets. A private development VxD ID (`0x7d00`) currently connects the 16-bit driver to the PM service. Replace it with an assigned, collision-free ID before distributing a package.

`DibEngine=0` is required for the accelerated GDI path: with DIBENG enabled, Windows draws directly into its DIB shadow and bypasses this driver's `BitBlt`. The driver keeps its shadow coherent, then submits eligible 32bpp screen solid fills to the GPU. Other drawing operations use CPU fallback. The x86 Mesa/Nouveau ICD and Gallium Nine D3D9 path connect through Win98 DIOC winsys shims to the VxD's shared miniport ABI. These paths have passed bounded OpenGL and Nine smoke tests, but the current 3DMark03 scene is corrupted/black and the GART backing allocator reports failures under load; general application support is not established.

The QEMU input setup reads the host Logitech evdev mouse and forwards its events through the emulated PS/2 mouse. `test/update_win98_driver.sh` removes every `mouse*=` key and mouse/HID `device=` or `minivdd=` directive from `SYSTEM.INI`, then writes exactly `[boot] mouse.drv=mouse.drv` and `[386Enh] mouse=*vmouse`. PS/2 PnP registry records remain in `nvx_reg.reg`; they are not additional SYSTEM.INI mouse entries. QMP reports the active QEMU PS/2 mouse, but actual cursor movement still needs visual confirmation.

## Current architecture

```text
Win98 GDI solid fill -> nvx.drv BitBlt -> VxD PM service
  -> NVX.VXD DIOC -> shared HwStartIO -> OpenRM/GSP -> GA104 CE
  -> scanout VRAM + synchronized driver shadow

Win98 OpenGL -> Win98 ICD loader -> Mesa Nouveau -> DIOC winsys -> NVX.VXD
  -> OpenRM/GSP -> NVC0; cube/gears rendered in prior runs

Win98 D3D9 -> Wine9x D9Switcher -> Mesa Gallium Nine -> DIOC winsys
  -> NVX.VXD -> OpenRM/GSP -> NVC0; bounded triangle passed, 3DMark03 is corrupted

Other GDI operations -> CPU fallback
```

The VxD build reuses the XP miniport source and pinned OpenRM policy/container code. The GDI fill and bounded OpenGL/Nine paths have reached hardware. The separate VMM user-mapping and general client-memory interfaces remain unvalidated and must stay fail-closed. The current 3DMark03 log shows successful `Direct3DCreate9` and NVC0 draw submissions, followed later by page-backed GART allocation failures with about 164 MiB and 1,898 buffers live. That is evidence of resource pressure, not proof that it explains the first flashing/black frame. Win98 pool metadata previously required contiguous physical pages; this shim now requests fixed, nonpageable but physically scatterable pages, matching the virtual-contiguous/nonpaged semantics needed by callers and reducing low-memory fragmentation. New `backing_stage` diagnostics distinguish metadata, VMM page allocation, and PTE-validation failures on the next run.

## Win98 2D and 3D acceleration roadmap

The long-term goal is to reuse the existing NVX GPU submission and Mesa/Nouveau code as much as possible, while adapting the Win98 OS boundary. The XP XDDM miniport is not a binary shim target for Win98: XP's VideoPort/ExtEscape and Win98's VxD DIOC, 16-bit display-driver, and DirectDraw interfaces differ. The reusable part is the shared NVX request ABI and GPU implementation behind `HwStartIO`; the Win98 DIOC adapter translates requests into that ABI. Mesa9x already supplies the Win9x WGL/ICD loader and screen/presentation conventions; use those as the OS-boundary reference and keep only the Nouveau transport/backend adapter NVX-specific.

```text
Win98 GDI solid fill -> nvx.drv BitBlt -> VxD PM service
  -> NVX.VXD -> shared miniport
  -> OpenRM/GSP -> GA104

Other GDI operations -> CPU fallback
Win98 OpenGL/Nouveau -> NV170 render; cube/gears tests pass
Win98 Direct3D Nine -> D3D9 triangle/Present test passes 60 frames
```

### 2D: accelerate common GDI operations first

1. Replace development PM service ID `0x7d00` with an assigned collision-free ID before packaging for distribution.
2. Visually confirm the color-fill probe on the physical monitor. Add a pixel/readback check so correctness does not rely only on serial submission records.
3. Extend only measured, well-bounded 2D cases, starting with SRCCOPY when source and destination layout can be represented safely. Keep CPU fallback for unsupported ROPs, formats, overlap, and DIBENG paths.
4. Reduce synchronous overhead by batching rectangles through a VxD-owned validated queue; preserve fence/synchronization before CPU reads, mode changes, or presentation. Measure before expanding acceleration.
5. Reconcile `ddhal.c` capability advertisement with callbacks; do not claim DirectDraw hardware 3D merely because surfaces are marked `DDSCAPS_3DDEVICE`.
6. Complete general application 3D support: validate BO lifetime/mapping, D3D resource allocation under churn, and frame presentation; keep kernel user mappings fail-closed until per-process mapping or a validated shared-ring protocol exists.

### 3D: reuse Mesa/Nouveau, adapt the Win98 client boundary

For the Direct3D 9 application boundary, Mesa 25.1.6 supplies Gallium Nine and the pinned `submodules/wine9x` project supplies a per-application D3D9 switcher that routes calls to `mesa99.dll`. The x86 bundle is built and staged in `dist_win98/nine/`. A bounded triangle test has submitted through NVC0 and completed; 3DMark03 currently produces a corrupted/black scene. Keep project-specific changes in root-owned patches, not in the Wine9x submodule.

1. Reuse `submodules/mesa9x/win9x/` for Win9x loader, WGL, and presentation conventions. The root-owned DIOC adapter connects Nouveau's existing winsys ABI to NVX.VXD; it does not replace Mesa9x's OS-level WGL boundary.
2. Build a genuine Win98-compatible OpenGL ICD (and later a D3D9 frontend if desired) whose calls reach Gallium Nouveau/NVC0. Keep Mesa's API/state tracking and Nouveau command generation; finish the OS-facing boundary for BO allocation, staged upload/readback, client lifetime, batch submission, fences, and present. The adapter intentionally leaves user mapping disabled.
3. Complete safe resource management before allowing general apps to submit: per-process client identity, BO handles and access validation, user mappings or shared-ring transfer, reset/close cleanup, and synchronization. The current VxD adapter's fail-closed user map means the existing XP-style mapped-buffer winsys cannot simply be enabled yet.
4. Bring up progressively: ICD load and renderer identification; clear/readback; one triangle; depth-tested indexed geometry; textures and buffer updates; swap/present; multiple contexts/processes. Use a software fallback only as an explicit diagnostic. Never report hardware rendering unless the renderer string and a GPU submission/fence/readback prove it.
5. Keep the first implementation narrow and upstream-shaped: reuse Mesa/Nouveau's NVC0 screen, resource and batch logic; keep Win98-specific code in the frontend/winsys and VxD shims. Avoid duplicating Gallium or inventing a second command encoder.

### What SoftGPU contributes

[SoftGPU](https://github.com/JHRobotics/softgpu) is a useful Windows 9x integration and testbed reference. Its project describes a bundle of separate components (VMDisp9x, Mesa9x, WineD3D, OpenGlide, VMHAL9x, and an optional QEMU 3D passthrough component), with render paths including software Mesa, VMware SVGA3D, and QEMU 3D passthrough. That is evidence that a 9x user-mode graphics stack can be assembled from OS-specific frontends and a backend; it is not a drop-in NVIDIA/Ampere driver or an XP XDDM shim. For this project, the strongest reuse is the existing Mesa9x frontend/winsys work and its Win9x compatibility patterns, while NVX.VXD supplies this card's transport and OpenRM/GSP stack.

### QHD mode status

The single-monitor package exposes the GSP-confirmed primary monitor timing through WINVBE and commits it with the native EVO path. The boot log proves the mode and pitch agree; inspect the actual monitor before considering QHD visually validated. A full Win98 GDI dual-view setup remains separate work: the current package is built with `WIN98_SINGLE_MONITOR=1`, even though the native scanout transaction can verify two active physical heads.

---

## Driver Components

| File | Description |
|---|---|
| `dist_win98/nvx.drv` | Primary 16-bit GDI display driver (Head 0; GSP-confirmed QHD mode supported) |
| `dist_win98/nvx.vxd` | 32-bit Ring 0 Virtual Device Driver with VideoPort shim and GA104 GSP miniport |
| `dist_win98/nvx.inf` | Windows 98 Plug-and-Play setup information file |
| `dist_win98/GSPGA104.BIN` | GSP firmware payload installed into `C:\WINDOWS\SYSTEM` |
| `src_win98/nvx_reg.reg` | Recommended registry configuration for modes, refresh rates, and settings |
| `src_win98/qipnp.reg` | Quick-install PCI PNP registry configuration helper |

---

## Build Instructions

### Prerequisites
On a Debian or Ubuntu x86-64 build host, install:
  ```sh
  sudo apt-get install build-essential gcc-mingw-w64-i686 binutils-mingw-w64 nasm python3
  ```
Install Open Watcom 2.0 and make sure `src_win98/owenv.sh` resolves it. NASM is used for the VxD assembly; JWasm is not required. Initialize the Mesa and Open GPU Kernel Modules submodules before building.

### Build the primary-monitor package

From the repository root:

```sh
./src_win98/build.sh
```

The default `WIN98_SINGLE_MONITOR=1` build outputs `nvx.drv`, `nvx.vxd`, `nvx.inf`, and `GSPGA104.BIN` in `dist_win98/`. To also build the older secondary `.DRV` path, set `WIN98_SINGLE_MONITOR=0`; that does not imply the dual-monitor path has been runtime validated.

The build also stages `install.bat`. It is DOS-compatible and only copies the
package into `C:\DRIVERS\NVX`; it deliberately does not edit `SYSTEM.INI` or
the registry. This keeps recovery simple and leaves PnP binding to the Win98
INF installer.

---

## Installation Guide

### Primary monitor installation
#### Stage package from real-mode DOS

1. Extract the `dist_win98` package to a DOS-accessible directory, such as `C:\NVXPKG`.
2. Boot with **Command prompt only**, change to that directory, and run `INSTALL.BAT`.
3. The script copies the package into `C:\DRIVERS\NVX`. It does not replace active system files or alter registry state.
4. Start the GUI with `WIN`.

#### Bind the display driver in Windows

1. Open **My Computer** -> **Properties** -> **Device Manager**.
2. Expand **Display adapters**, open the NVIDIA/standard VGA adapter, and select **Update Driver**.
3. Choose **Specify the location** / **Have Disk**, then select `C:\DRIVERS\NVX\NVX.INF`.
4. Select the matching NVIDIA GA104 device and complete setup.
5. Reboot when prompted.

The INF copies the GSP firmware into `C:\WINDOWS\SYSTEM`. Win98 has booted at 2560×1440×32 with native GSP/EVO scanout and matching 10,240-byte pitch. OpenGL cube/gears and a bounded Nine triangle smoke test have passed in earlier runs. The current 3DMark03 test is not passing: Nine initializes and submits draws, but the scene is corrupted/black and GART page-backed allocation failures occur under load. Treat OpenGL/Nine as experimental; general applications and performance are unproven. The VxD user-buffer mapping path remains fail-closed.

---

## Credits & Acknowledgments

- **NVIDIA Corporation** for Ampere GA104 GPU hardware, GSP-RM firmware, SEC2 bootloader, and published open-source kernel modules.
- **The Linux Nouveau Project** for invaluable documentation on NVIDIA Falcon microcode loading, RPC queue mechanics, and display engine registers.
- **Michal Necasek & Jmh (WINVBE Project)** for the 16-bit modular DIB Engine display driver base.
- **nullmoth ([nvidia-macos-driver](https://github.com/nullmoth/nvidia-macos-driver))** for community reverse-engineering references.
- **Sanjay Seshan** for physical GPU and multi-monitor testing, reverse engineering, hardware validation, and project leadership.
- **GPT & Gemini** models for architectural collaboration, code generation, and iterative debugging assistance.
