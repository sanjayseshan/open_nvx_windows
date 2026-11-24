# NVIDIA Modern GPU Display Drivers for Windows 2000/XP (x86 & x64) & Windows 98SE/ME

This project develops display drivers for modern NVIDIA GPUs on legacy Windows. The XP miniport uses code and policy adapted from NVIDIA’s Open GPU Kernel Modules (`open-gpu-kernel-modules`) and boots NVIDIA GSP-RM firmware. Hardware validation is primarily on an RTX 3070 (GA104); related GPU families are not yet validated. Full 2D acceleration is present. OpenGL hardware rendering works through Mesa/Nouveau, and the x86 Gallium Nine D3D9 path has rendered 3DMark05 in the XP x64 guest. Performance and broader application compatibility remain under investigation.

### Operating System Support
* **Windows XP Professional x64 Edition (NT 5.2):** Main development and hardware-validation target; tested with an RTX 3070, including VFIO passthrough.
* **Windows XP Professional x86 (NT 5.1):** Separate 32-bit build; tested in a VFIO guest. Feature coverage differs from x64.
* **Windows 98 Second Edition (SE, 4.10):** Experimental VxD/DIB path tested in a VFIO guest.
* **Windows ME / Windows 2000 / Vista / Windows 7:** Not validated. Compatibility should not be assumed.

### Hardware Validation & Target GPUs
* **Host Platform:** AMD AM5 (Ryzen 9000 series, IOMMU / VFIO passthrough)
* **Tested GPU:** NVIDIA GeForce RTX 3070 (Ampere GA104)
* **Tested Displays:** Dual-monitor setup (1x 2560×1440 @ 32bpp True Color + 1x 1920×1080 @ 32bpp True Color)
* **Potential GPU families for this firmware generation (only GA104 tested):**
  * GTX 16 series (Turing TU10x, untested)
  * RTX 20 series (Turing TU10x, untested)
  * RTX 30 series (Ampere GA10x, tested GA104)
  * RTX 40 series (Ada Lovelace AD10x, untested)

---

## Architectural Comparison

| Feature | Windows XP (x86) | Windows XP (x64) | Windows 98 SE |
| :--- | :--- | :--- | :--- |
| **Driver Architecture** | Windows 2000/XP Display Driver Model (XDDM 32-bit) | Windows XP Display Driver Model (XDDM 64-bit) | Hybrid Ring 0 VxD + 16-bit GDI Display Driver |
| **Kernel Component** | `nvxmp.sys` (32-bit video miniport driver, built as `nvxmp32.sys`) | `nvxmp.sys` (64-bit video miniport driver) | `nvx.vxd` (32-bit LE Virtual Device Driver) |
| **Display Component** | `nvxdisp.dll` (32-bit GDI display driver, built as `nvxdisp32.dll`) | `nvxdisp.dll` (64-bit GDI display driver) | `nvx.drv` (Head 0) & `nvx2.drv` (Head 1) (16-bit NE) |
| **2D Acceleration Engine** | Full Hardware Acceleration (Ampere Copy Engine) | Full Hardware Acceleration (Ampere Copy Engine) | Partial Acceleration (DIB Engine RAM shadow buffer + burst VRAM flush) |
| **3D Acceleration Engine** | Native x86 guest path not yet validated | Mesa WGL/Nouveau for OpenGL; x86 Gallium Nine for D3D9; WineD3D for other D3D paths | Software rasterization |
| **Hardware Bootstrap** | Authentic GSP-RM firmware booted via Falcon/SEC2 | Authentic GSP-RM firmware booted via Falcon/SEC2 | Authentic GSP-RM firmware booted via Falcon/SEC2 |
| **Multi-Monitor Method** | Windows XP XDDM multi-head scanout surfaces / DualView | Windows XP XDDM multi-head scanout surfaces / DualView | Dual display drivers (`nvx.drv` + `nvx2.drv`) & desktop extension via `attach_sec.exe` |
| **Tested Resolutions** | 1920×1080 + 2560×1440 @ 32bpp True Color | 2560×1440 + 2560×1440 @ 32bpp True Color | 1920×1080 + 2560×1440 @ 32bpp True Color |

---
## Architecture

The diagrams show the implemented data flow and current boundaries. XP x64 is the primary target. OpenGL and x86 D3D9 can use Mesa/Nouveau hardware rendering; the x64 D3D9 and D3D8/DirectDraw compatibility paths use WineD3D. Hardware validation is focused on an RTX 3070, and successful rendering does not imply broad API coverage or mature performance.

### Windows XP x64

```mermaid
flowchart TB
  subgraph User[User mode]
    APP[OpenGL application]
    D3D9[32-bit D3D9 application]
    D3DOther[x64 D3D9 or D3D8 / DirectDraw application]
    OGL[XP OpenGL loader / WGL]
    ICD[Mesa WGL ICD<br/>nvxogl64.dll]
    NINE[Gallium Nine<br/>x86 d3d9.dll]
    WD[WineD3D<br/>D3D9 / D3D8 / DirectDraw to OpenGL]
    GDI[GDI / XDDM display calls]
    APP --> OGL --> ICD
    D3D9 --> NINE
    D3DOther --> WD --> OGL
    GDI --> DDI
  end
  subgraph Kernel[Windows XP x64 kernel]
    DDI[nvxdisp.dll<br/>XDDM display DDI]
    MP[nvxmp.sys<br/>XDDM video miniport]
    DDI -->|DDI / escape requests| MP
    ICD -->|NVX winsys ABI / escapes| MP
    NINE -->|Gallium Nouveau winsys / escapes| MP
  end
  subgraph GPU[RTX GPU]
    RM[GSP-RM firmware<br/>RPC-managed resources]
    CE[Copy Engine / display scanout]
    GR[3D engine<br/>channel and submission path]
    RM --> CE
    RM --> GR
  end
  MP -->|DMA, RM RPC, channel submission| RM
  NINE -->|Gallium NVC0 command stream| GR
```

The miniport owns device setup, memory mappings, RM/GSP communication, display modes, and the user/kernel submission ABI. `nvxdisp.dll` provides the XP XDDM display interface. Mesa's WGL ICD and Nouveau winsys use that ABI for OpenGL. For 32-bit D3D9, the selected Gallium Nine `d3d9.dll` submits through Gallium/Nouveau to the miniport; it does not route D3D9 through WineD3D. WineD3D remains the D3D9 compatibility path for x64 applications and supplies the D3D8 and DirectDraw paths. Build and install the intended x86 D3D9 backend explicitly because both backends provide a `d3d9.dll`.

### 32-bit applications and Windows XP x86

```mermaid
flowchart TB
  subgraph XP64[Windows XP x64]
    A32[32-bit application under WOW64] --> ICD32[nvxogl32.dll<br/>OpenGL]
    A32 --> NINE32[Gallium Nine x86 d3d9.dll<br/>D3D9 when selected]
    A32 --> WD32[WineD3D x86<br/>D3D9 / D3D8 / DirectDraw]
    WD32 --> ICD32
    ICD32 --> DDI32[nvxdisp32.dll<br/>32-bit display interface]
    NINE32 --> MP64[nvxmp.sys<br/>64-bit miniport]
    DDI32 --> MP64
    A64[64-bit application] --> ICD64[nvxogl64.dll<br/>OpenGL]
    A64 --> WD64[WineD3D x64<br/>D3D9 / D3D8 / DirectDraw]
    WD64 --> ICD64
    ICD64 --> DDI64[nvxdisp.dll<br/>64-bit display interface]
    DDI64 --> MP64
  end
  subgraph XP32[Native Windows XP x86 build]
    A32N[32-bit application] --> ICD32N[nvxogl32.dll]
    A32N --> NINE32N[Gallium Nine x86 d3d9.dll]
    ICD32N --> DDI32N[nvxdisp.dll<br/>32-bit display interface]
    NINE32N --> MP32[nvxmp.sys<br/>32-bit miniport]
    DDI32N --> MP32
  end
  MP64 --> HW[RTX GPU / GSP-RM]
  MP32 --> HW
```

On XP x64, x86 and x64 user-mode DLLs belong in separate system directories and OpenGL registry views; both use the x64 kernel miniport. The x86 build is a separate native-OS target; do not infer that native x86 rendering has been validated from the WOW64 result. Build outputs use `nvxmp32.sys` and `nvxdisp32.dll` names before staging as `nvxmp.sys` and `nvxdisp.dll` for a native x86 guest. Gallium Nine currently supplies only the x86 D3D9 frontend.

### Windows 98 SE (experimental)

```mermaid
flowchart TB
  APP[Win32 applications / GDI] --> DIB[DIB Engine display drivers<br/>nvx.drv and nvx2.drv]
  DIB --> VXD[nvx.vxd<br/>VMM ring-0 device]
  VXD --> SHIM[VideoPort compatibility shim]
  SHIM --> CORE[Shared GPU initialization<br/>and display services]
  CORE --> RM[GSP-RM firmware]
  RM --> GPU[RTX GPU]
  UTIL[attach_sec.exe] -. secondary-head setup .-> APP
```

This path uses the DIB engine and a RAM shadow/flush strategy for desktop drawing; it is not the XP OpenGL ICD path. See [src_win98/README.md](src_win98/README.md) for design and setup details.

---

## Build Instructions

### Debian 12 host prerequisites

These commands assume a Debian 12 (Bookworm) amd64 host and a checkout with its source submodules initialized. Install the base tools and both MinGW targets for XP x64/x86 driver builds:

```sh
sudo apt update
sudo apt install build-essential git python3 \
  gcc-mingw-w64-x86-64 g++-mingw-w64-x86-64 \
  gcc-mingw-w64-i686 g++-mingw-w64-i686 \
  binutils-mingw-w64 genisoimage perl
git submodule update --init --recursive
```

The XP miniport embeds `dist_full/gsp_ga104.bin`; this firmware file must already be present. The miniport build does not build the OpenGL ICD or DirectX translation layer; build both before packaging graphics support.

For the Mesa WGL ICD, install the additional cross-build tools:

```sh
sudo apt install gcc-mingw-w64-x86-64-posix g++-mingw-w64-x86-64-posix \
  gcc-mingw-w64-i686-posix g++-mingw-w64-i686-posix \
  ninja-build python3-venv python3-pip python3-ply pkg-config bison flex \
  libelf-dev libzstd-dev libexpat1-dev zlib1g-dev
python3 -m venv /tmp/nvx-meson-venv
/tmp/nvx-meson-venv/bin/pip install 'meson>=1.3' mako pyyaml
```

VFIO/QEMU integration tests and offline guest-image updates additionally need host tools such as `qemu-system-x86`, `qemu-utils`, `libguestfs-tools`, `guestmount`, `libhivex-bin` (for `hivexregedit`), and `genisoimage`. They are not required for cross-compilation. VFIO also requires host IOMMU/VFIO configuration and access to the target GPU; see [test/README.md](test/README.md).

The Windows 98 16-bit display driver additionally needs Open Watcom C/C++ and JWasm; see [src_win98/README.md](src_win98/README.md).

### Build Windows XP x64 driver binaries

From the repository root:

```sh
# Build the x64 miniport with the GSP/RM path enabled.
make -C src_driver full

# Build both display-driver architectures; use target nvxdisp.dll for x64 only.
make -C src_disp all
```

Outputs are `src_driver/nvxmp.sys`, `src_disp/nvxdisp.dll`, and `src_disp/nvxdisp32.dll`. Host-side checks are separate from compilation:

```sh
make -C src_driver check
make -C src_disp check
```

To stage the x64 miniport and display DLL in the install bundle after building:

```sh
install -D -m 0644 src_driver/nvxmp.sys dist_full/nvxmp.sys
install -D -m 0644 src_disp/nvxdisp.dll dist_full/nvxdisp.dll
```

Keep the matching `dist_full/nvx.inf`, `dist_full/gsp_ga104.bin`, and `dist_full/install.bat` alongside them. The miniport embeds the firmware at build time; the firmware file is also included in the prepared install bundle.

The default miniport target is not the full hardware configuration; use `full` for the GSP build. To build the XP-compatible Mesa WGL ICD, the script applies temporary patches to the Mesa submodule, builds in `/tmp`, restores the submodule source, and copies finished DLLs to `dist_full/`:

```sh
NVX_MESA_ARCH=x64 MESON=/tmp/nvx-meson-venv/bin/meson ./test/build_mesa_xp64.sh
NVX_MESA_ARCH=x86 MESON=/tmp/nvx-meson-venv/bin/meson ./test/build_mesa_xp64.sh
```

The x64 build produces `dist_full/nvxogl64.dll` and `dist_full/nvxmesa.dll`; x86 produces `dist_full/nvxogl32.dll`. The ICD build includes Nouveau and Softpipe. A DLL's presence alone does not establish that an application selected Nouveau or completed its workload on the GPU.

### Build the DirectX-to-OpenGL layer (required for DirectX)

The project uses its pinned Wine source submodule to build XP-compatible WineD3D DLLs for both x64 and x86 applications. These provide Direct3D 9, Direct3D 8, and DirectDraw entry points and route rendering to the Mesa OpenGL ICD. Build them after installing the Debian cross-compilers above:

```sh
./test/build_wined3d_xp.sh
```

The script checks the pinned Wine revision and builds the XP-supported Direct3D 9, Direct3D 8, and DirectDraw DLLs for both architectures into `test/wined3d_xp/x64/` and `test/wined3d_xp/x86/`. Stage every runtime DLL for the full installer:

```sh
for dll in d3d9 d3d8 ddraw wined3d libwine; do
  install -D -m 0644 "test/wined3d_xp/x64/$dll.dll" "dist_full/$dll.dll"
  install -D -m 0644 "test/wined3d_xp/x86/$dll.dll" "dist_full/x86/$dll.dll"
done
```

For a full package, keep the x86 DLLs under `dist_full/x86/`; the offline updater and batch installer both use that directory for `SysWOW64`. The x64 DLLs belong at the package root and go to `Windows\System32`. Do not mix architectures. `test_full_driver.sh` builds WineD3D by default before its integration run; set `NVX_BUILD_WINED3D=0` only when deliberately reusing an already-built WineD3D package.

### Build the Gallium Nine Direct3D 9 frontend

Gallium Nine is built as an x86 `d3d9.dll` from Mesa 25.1.6 bundled in `submodules/mesa9x/mesa-25.1.x`. XP-specific changes are kept in the root repository under `patches/mesa9x/` and `mesa_patches/nine/`; the patch script applies the Mesa9x compatibility changes before building. Build Nine after WineD3D when Nine should be the selected x86 D3D9 implementation, because both produce `dist_full/x86/d3d9.dll`:

```sh
./build_d3d9_nine.sh
```

The script checks the Mesa 25.1.6 pin, applies the external Mesa9x compatibility patches and root-owned Nine shims, builds `mesa99.dll`, checks the x86 PE format and `Direct3DCreate9` exports, then stages it as `dist_full/x86/d3d9.dll`. The x86 Nine path has rendered the 3DMark05 game tests in the XP x64 guest; the latest user-reported score was 3,249. Performance remains well below the target, and CSMT is gated off when Nouveau does not advertise persistent coherent mappings. Nine does not replace x64 WineD3D or the x86 D3D8/DirectDraw DLLs. The full test runner selects Nine by default and avoids rebuilding WineD3D over its x86 `d3d9.dll`; set `NVX_D3D9_BACKEND=wined3d` to select WineD3D instead.

### Build Windows XP x86 driver binaries

```sh
make -C src_driver full32
make -C src_disp nvxdisp32.dll

# Stage with the filenames expected inside a 32-bit XP guest.
install -D src_driver/nvxmp32.sys dist32/nvxmp.sys
install -D src_disp/nvxdisp32.dll dist32/nvxdisp.dll
```

Include `dist32/nvx.inf` in the package. The x86 Mesa ICD can be built with `NVX_MESA_ARCH=x86 ./test/build_mesa_xp64.sh`; for XP x64, it is installed in `SysWOW64` for 32-bit applications. For a native x86 guest, use the 32-bit driver and matching ICD only.

### Build Windows 98 SE components

```sh
source src_win98/owenv.sh
(cd src_win98/winvbe_drv && ./build.sh)
(cd src_win98 && ./build_vxd.sh)
i686-w64-mingw32-gcc -O2 -s -o dist_win98/attach_sec.exe \
  src_win98/attach_sec.c -luser32
```

The output files are placed in `dist_win98/`.

## Installation & Usage

### Windows XP x64

`dist_full/` is the full integration bundle. For OpenGL and DirectX support it must contain `nvxmp.sys`, `nvxdisp.dll`, `gsp_ga104.bin`, `nvxmesa.dll`, `nvxogl64.dll`, `nvxogl32.dll`, the five x64 WineD3D DLLs (`d3d9.dll`, `d3d8.dll`, `ddraw.dll`, `wined3d.dll`, `libwine.dll`) at its root, the matching five x86 DLLs under `x86/`, and `nvx.inf`. Test programs can also be included. Native x64 DLLs go in `System32`; x86 DLLs for WOW64 applications go in `SysWOW64`.

For the repository's prepared XP x64 guest image, shut the VM down and run the rootless offline updater. It selects Gallium Nine for x86 D3D9 by default; use `NVX_D3D9_BACKEND=wined3d` to install WineD3D for x86 D3D9 instead:

```sh
./test/update_disk_driver_user.sh full
```

This updates `winxp_x64.qcow2`, installs the miniport, display DDI, firmware and INF, both Mesa ICDs, and x64 WineD3D runtime files, updates OpenGL registry entries, and stages available test binaries. It also installs the selected x86 D3D9 DLL (Nine by default) plus the x86 WineD3D D3D8/DirectDraw support files. Build both Mesa ICDs, Nine if selected, and the required WineD3D DLLs first; the updater rejects an incomplete package. It requires working libguestfs/guestmount support. For a mounted physical XP partition, the mounted-volume updater only rebuilds and copies the x64 miniport and display DLL; it does not install the OpenGL or DirectX runtimes:

```sh
XP_MOUNT=/mnt/xp ./test/update_xp_driver.sh
```

The current `dist_full/install.bat` installs the WineD3D D3D9 backend in both architectures. To install Nine for x86 D3D9, use the offline updater with its default `NVX_D3D9_BACKEND=nine` selection or manually copy `dist_full/x86/d3d9.dll` into `SysWOW64` after running the installer. The batch installer checks the miniport, display DDI, firmware, Mesa frontends, both OpenGL ICDs, and WineD3D runtime files; it backs up replaced system DLLs, installs runtimes into `System32` and `SysWOW64`, stages tests, registers both OpenGL ICDs, and runs the INF/PnP setup. Reboot after it completes. For a driver-only installation, use Device Manager -> **Display Adapters** -> **Update Driver** -> **Have Disk...** and select `nvx.inf`; that path alone does not install OpenGL or DirectX support. Back up the guest before installing because the package replaces system D3D runtime DLLs.

After reboot, verify the renderer using the included test tools and logs. `GDI Generic` means the application fell back to software OpenGL. Successful installation does not prove that all OpenGL or Direct3D workloads are supported; 3D performance/correctness remains under active validation.

### Windows XP x86

Build or stage `dist32/nvxmp.sys` and `dist32/nvxdisp.dll` as described above and include `dist32/nvx.inf`. In XP x86, open Device Manager -> **Display Adapters** -> **Update Driver** -> **Have Disk...**, select the INF, and reboot. An x64 ICD cannot be loaded by an x86 process; install the matching `nvxogl32.dll` if using OpenGL. To deploy the driver and test binaries to the repository's offline `winxp_x32.qcow2` image, shut down the VM and run:

```sh
./test/install_xp32_driver_and_tests.sh
```

### Windows 98 SE

Use `nvx.drv`, `nvx2.drv`, `nvx.vxd`, `nvx.inf`, and `attach_sec.exe` from `dist_win98/`. Follow [src_win98/README.md](src_win98/README.md) for setup. The secondary-head utility is `attach_sec.exe`. This experimental DIB/VxD path is independent of the XP Mesa ICD.

---

## Repository Structure

```
├── README.md           # Project overview and build instructions
├── dist_full/           # XP x64 integration bundle: driver, firmware, ICDs, tests, and WineD3D DLLs
├── dist32/              # Windows XP x86 release package (nvxmp.sys, nvxdisp.dll, nvx.inf)
├── dist_win98/          # Windows 98 SE release package (nvx.drv, nvx2.drv, nvx.vxd, nvx.inf, attach_sec.exe)
├── src_driver/          # Unified GA104 miniport engine, GSP/RPC/MMU/Display code, and OpenRM HAL shim
├── src_disp/            # Windows XP GDI display driver (both 64-bit and 32-bit builds)
├── src_win98/           # Windows 98 SE components:
│   ├── winvbe_drv/      # 16-bit DIB Engine display drivers (nvx.drv, nvx2.drv)
│   ├── shim/            # Ring 0 NT VideoPort to VMM translation layer
│   ├── attach_sec.c     # Win32 secondary monitor attachment utility
│   └── build_vxd.sh     # VxD compilation and packaging script
├── mesa_patches/        # XP compatibility shims and Nouveau/Softpipe winsys integration (Mesa submodule stays clean)
├── patches/mesa9x/      # External, reproducible compatibility patch series for Mesa9x
├── mesa/                # Pinned Mesa 25.1.6 source used for the WGL ICD
├── submodules/mesa9x/   # Mesa9x build integration with bundled Mesa 25.1.6 and Nine
├── wine/                # Pinned Wine source used to build XP WineD3D DLLs
├── wine_patches/        # External Wine compatibility patches used by the build script
├── test/                # Automated test infrastructure, regression runners, and QEMU/VFIO scripts
├── open-gpu-kernel-modules/ # NVIDIA OpenRM upstream repository (submodule)
├── notes/               # Project status, phase evaluation, hardware notes, and firmware provenance
│   ├── handoff.txt       # Current phase status and engineering roadmap
│   ├── SUMMARY.txt       # Cumulative hardware test history
│   ├── PHASE2C_MESA_EVALUATION.md
│   ├── nvx_nouveau_port_map.md
│   └── FIRMWARE_PROVENANCE.md
```

---

## Credits & Acknowledgments

- **NVIDIA Corporation** for Ampere GA104 GPU hardware, GSP-RM and SEC2 microcode, and the [NVIDIA Open GPU Kernel Modules](https://github.com/NVIDIA/open-gpu-kernel-modules).
- **The Linux Nouveau DRM Team** for extensive documentation and public reference implementations of GSP initialization and sequencer logic.
- **Michal Necasek & Jmh (WINVBE Project)** for the open-source 16-bit DIB Engine Windows 98 display driver framework.
- **nullmoth ([nvidia-macos-driver](https://github.com/nullmoth/nvidia-macos-driver))** for community reverse-engineering notes on modern NVIDIA GPU architectures.
- **Sanjay Seshan** for physical GPU and multi-monitor validation, reverse engineering, hardware testing, and project creation.
- **GPT and Gemini** artificial intelligence models for paired programming, kernel shim engineering, and continuous debugging assistance.
- **ReactOS** for XDDM implementation reference.

---

## License & Firmware Notice

MIT license. NVIDIA GSP-RM and SEC2 firmware components remain the intellectual property of NVIDIA Corporation and are subject to NVIDIA's licensing terms. Review [notes/FIRMWARE_PROVENANCE.md](notes/FIRMWARE_PROVENANCE.md) before redistributing.
