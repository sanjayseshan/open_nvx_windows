# NVX experimental display driver for Windows 98 SE

## Current target and status

The reproducible build target is **one primary monitor**. Run `./src_win98/build.sh` from the repository root; it builds `nvx.drv` and `nvx.vxd`, generates a primary-only `nvx.inf`, and stages `GSPGA104.BIN` in `dist_win98/`. Set `WIN98_SINGLE_MONITOR=0` only to build the older secondary display path as well.

The package has booted on the RTX 3070 under Win98 SE through GSP initialization, display-driver startup, and `Control 39`. The DIB access callbacks use Open Watcom `__loadds` to load the driver's data segment. The current VFIO run also proves that Win98 GDI solid fills can reach the GPU through the display driver's BitBlt callback, the VxD PM service, and the shared miniport. The guest remains running for physical display inspection.

For the primary monitor, do not select 2560×1440 through the VBE path. The observed `mode 6014` was a synthetic primary VBE entry: Windows accepted it and the driver changed its pitch without programming a matching primary scanout, causing stripes. The primary driver now trusts the option ROM's enumerated modes after a successful VBE probe and does not advertise synthetic QHD. The separate `nvx2.drv` head-1 path has a native EVO scanout commit and can expose a 2560×1440 mode when the EDID reports QHD; the current default single-monitor package does not install that second driver. Keep these two paths distinct.

The VxD DIOC handler forwards the shared `IOCTL_NVX_*` ABI through `NvxShimDispatchMiniportIoctl()` to the same `HwStartIO` handler used by XP. The Win98 PM service calls that VxD handler, while the shared miniport validates requests and uses `NvxGspSubmitFill/Copy/Upload`; the Win98 layer does not build GPU packets. A private development VxD ID (`0x7d00`) currently connects the 16-bit driver to the PM service. Replace it with an assigned, collision-free ID before distributing a package.

`DibEngine=0` is required for this first accelerated path: with DIBENG enabled, Windows draws directly into its DIB shadow and bypasses this driver's `BitBlt`. The driver keeps its shadow coherent, then submits eligible 32bpp screen solid fills to the GPU. Other drawing operations still use CPU fallback. No Win98 WGL/OpenGL ICD or Direct3D client is connected, so application 3D acceleration is not available.

The QEMU input setup reads the host Logitech evdev mouse and forwards its events through the emulated PS/2 mouse. `test/update_win98_driver.sh` removes every `mouse*=` key and mouse/HID `device=` or `minivdd=` directive from `SYSTEM.INI`, then writes exactly `[boot] mouse.drv=mouse.drv` and `[386Enh] mouse=*vmouse`. PS/2 PnP registry records remain in `nvx_reg.reg`; they are not additional SYSTEM.INI mouse entries. QMP reports the active QEMU PS/2 mouse, but actual cursor movement still needs visual confirmation.

## Current architecture

```text
Win98 GDI solid fill
  -> 16-bit nvx.drv BitBlt -> VxD PM service
  -> NVX.VXD DIOC -> shared HwStartIO -> OpenRM/GSP -> GA104 CE
  -> scanout VRAM + synchronized driver shadow

Other BitBlt operations -> CPU fallback
Win98 OpenGL / Direct3D -> not connected yet
```

The VxD build reuses the XP miniport source and pinned OpenRM policy/container code. The GDI fill DIOC/PM route is runtime-tested; the separate VMM user-mapping and general client-memory interfaces remain unvalidated and must stay fail-closed.

## Win98 2D and 3D acceleration roadmap

The long-term goal is to reuse the existing NVX GPU submission and Mesa/Nouveau code as much as possible, while adapting the Win98 OS boundary. The XP XDDM miniport is not a binary shim target for Win98: XP's VideoPort/ExtEscape and Win98's VxD DIOC, 16-bit display-driver, and DirectDraw interfaces differ. The reusable part is the shared NVX request ABI and GPU implementation behind `HwStartIO`; a small Win98 adapter should translate Win98 requests into that ABI.

```text
Win98 GDI solid fill -> nvx.drv BitBlt -> VxD PM service
  -> NVX.VXD DIOC -> shared miniport HwStartIO -> OpenRM/GSP -> GA104

Other GDI operations -> CPU fallback
Win98 OpenGL / Direct3D -> not connected
```

### 2D: accelerate common GDI operations first

1. Replace development PM service ID `0x7d00` with an assigned collision-free ID before packaging for distribution.
2. Visually confirm the color-fill probe on the physical monitor. Add a pixel/readback check so correctness does not rely only on serial submission records.
3. Extend only measured, well-bounded 2D cases, starting with SRCCOPY when source and destination layout can be represented safely. Keep CPU fallback for unsupported ROPs, formats, overlap, and DIBENG paths.
4. Reduce synchronous overhead by batching rectangles through a VxD-owned validated queue; preserve fence/synchronization before CPU reads, mode changes, or presentation. Measure before expanding acceleration.
5. Reconcile `ddhal.c` capability advertisement with callbacks; do not claim DirectDraw hardware 3D merely because surfaces are marked `DDSCAPS_3DDEVICE`.
6. For application 3D, add a native Win98 Mesa/Nouveau winsys adapter. Keep kernel mappings fail-closed until per-process mapping or a validated shared-ring protocol exists.

### 3D: reuse Mesa/Nouveau, adapt the Win98 client boundary

1. Use the Mesa 9x work already present under `submodules/mesa9x/win9x/` as the reference for Win9x loader, WGL, memory, and presentation integration. First identify which frontend and Gallium pieces are built for the chosen Win98 configuration; XP's `nvx_winsys_win32.c`/`ExtEscape` path and XP DLLs are not automatically Win98-compatible.
2. Provide a native Win98 OpenGL ICD (and later a D3D9 frontend if desired) whose calls reach Gallium Nouveau/NVC0. Keep Mesa's API/state tracking and Nouveau command generation; implement only the OS-facing winsys adapter that maps BO allocation/map/unmap, client open/close, batch submission, fences, and present to the NVX VxD ABI.
3. Complete safe resource management before allowing general apps to submit: per-process client identity, BO handles and access validation, user mappings or shared-ring transfer, reset/close cleanup, and synchronization. The current VxD adapter's fail-closed user map means the existing XP-style mapped-buffer winsys cannot simply be enabled yet.
4. Bring up progressively: ICD load and renderer identification; clear/readback; one triangle; depth-tested indexed geometry; textures and buffer updates; swap/present; multiple contexts/processes. Use a software fallback only as an explicit diagnostic. Never report hardware rendering unless the renderer string and a GPU submission/fence/readback prove it.
5. Keep the first implementation narrow and upstream-shaped: reuse Mesa/Nouveau's NVC0 screen, resource and batch logic; keep Win98-specific code in the frontend/winsys and VxD shims. Avoid duplicating Gallium or inventing a second command encoder.

### What SoftGPU contributes

[SoftGPU](https://github.com/JHRobotics/softgpu) is a useful Windows 9x integration and testbed reference. Its project describes a bundle of separate components (VMDisp9x, Mesa9x, WineD3D, OpenGlide, VMHAL9x, and an optional QEMU 3D passthrough component), with render paths including software Mesa, VMware SVGA3D, and QEMU 3D passthrough. That is evidence that a 9x user-mode graphics stack can be assembled from OS-specific frontends and a backend; it is not a drop-in NVIDIA/Ampere driver or an XP XDDM shim. For this project, the strongest reuse is the existing Mesa9x frontend/winsys work and its Win9x compatibility patterns, while NVX.VXD supplies this card's transport and OpenRM/GSP stack.

### Separate 1440p display work

The primary VBE path is limited to modes reported by the option ROM; do not synthesize primary QHD by changing pitch. QHD on the secondary head is a different path: the optional `nvx2.drv` uses head-1 native scanout and should add 2560×1440 only when EDID confirms that mode. The current default single-monitor package omits `nvx2.drv`; validate the dual-head package separately before describing it as available in that build.

---

## Driver Components

| File | Description |
|---|---|
| `dist_win98/nvx.drv` | Primary 16-bit GDI display driver (Head 0, 1920×1080 @ 32bpp) |
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

---

## Installation Guide

### Primary monitor installation
1. Copy `nvx.inf`, `nvx.drv`, `nvx.vxd`, and `GSPGA104.BIN` from `dist_win98/` to a directory on Windows 98 (for example, `C:\DRIVERS\NVX`).
2. Right-click on **My Computer** -> **Properties** -> **Device Manager**.
3. Expand **Display adapters**, double-click **Standard PCI Graphics Adapter (VGA)**, and select the **Driver** tab.
4. Click **Update Driver...** -> **Specify the location of the driver** -> **Browse** to `C:\DRIVERS\NVX`.
5. Select **NVIDIA GeForce RTX 3070** (or corresponding model) and click **Next** to install.
6. When prompted, restart Windows 98.

The INF copies the GSP firmware into `C:\WINDOWS\SYSTEM`. On 2026-11-24, `test/run_vfio_win98_autoretry.sh` booted `win98_small.img` on the RTX 3070. Log `test_logs/win98-20261124-214215.log` records `DibEngine=0`, active 1920×1080×32 mode, successful PM API connection, and 32 validated `IOCTL_NVX_ACCEL_FILL_RECT` operations. The miniport logs each rectangle only after every row's `NvxGspSubmitFill` completed successfully. `C:\NVXFILL.LOG` was read from the guest and contains `START`/`PAINT` markers for the GDI fill probe. The VM is intentionally left running. This validates the GPU solid-fill route for GDI desktop drawing; physical color accuracy, broader 2D coverage, and application-level 3D remain unverified or unimplemented.

---

## Credits & Acknowledgments

- **NVIDIA Corporation** for Ampere GA104 GPU hardware, GSP-RM firmware, SEC2 bootloader, and published open-source kernel modules.
- **The Linux Nouveau Project** for invaluable documentation on NVIDIA Falcon microcode loading, RPC queue mechanics, and display engine registers.
- **Michal Necasek & Jmh (WINVBE Project)** for the 16-bit modular DIB Engine display driver base.
- **nullmoth ([nvidia-macos-driver](https://github.com/nullmoth/nvidia-macos-driver))** for community reverse-engineering references.
- **Sanjay Seshan** for physical GPU and multi-monitor testing, reverse engineering, hardware validation, and project leadership.
- **GPT & Gemini** models for architectural collaboration, code generation, and iterative debugging assistance.
