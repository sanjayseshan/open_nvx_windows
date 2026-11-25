#!/bin/bash
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

OBJ_DIR="$SCRIPT_DIR/obj_vxd"
mkdir -p "$OBJ_DIR"
mkdir -p "$ROOT_DIR/dist_win98"

CC="${CC:-i686-w64-mingw32-gcc}"
LD="${LD:-i686-w64-mingw32-ld}"
OBJCOPY="${OBJCOPY:-i686-w64-mingw32-objcopy}"
LIBGCC="${LIBGCC:-$($CC -print-libgcc-file-name)}"

OPEN_GPU_DIR="$ROOT_DIR/open-gpu-kernel-modules"
WIN98_SINGLE_MONITOR="${WIN98_SINGLE_MONITOR:-1}"
CONTAINER_DIR="$OPEN_GPU_DIR/src/nvidia/src/libraries/containers"
CFLAGS="-m32 -O2 -ffunction-sections -fdata-sections -Wall -Wextra -Wno-unused-parameter -Wno-pointer-to-int-cast -fno-pie -fno-stack-protector -falign-functions=32 -falign-labels=16 -falign-loops=16 -falign-jumps=16 -D__VXDKERNEL__ -DNVX_WIN98=1 -DNVX_WIN98_SINGLE_MONITOR=$WIN98_SINGLE_MONITOR -DNVX_ENABLE_EXPERIMENTAL_GSP=1 -I$ROOT_DIR/src_driver -I$ROOT_DIR/src_driver/include -I$ROOT_DIR/src_driver/openrm_xp -I$OPEN_GPU_DIR/src/common/sdk/nvidia/inc -I$OPEN_GPU_DIR/src/common/inc -I$OPEN_GPU_DIR/src/nvidia/arch/nvalloc/common/inc -I$OPEN_GPU_DIR/src/common/shared/msgq/inc -I$OPEN_GPU_DIR/kernel-open/common/inc -I$OPEN_GPU_DIR/src/nvidia/inc -I$OPEN_GPU_DIR/src/nvidia/inc/libraries -I$SCRIPT_DIR/shim -nostdlib -fno-builtin"

echo "==> 1. Assembling VxD entry and VMM wrappers..."
nasm -f win32 "$SCRIPT_DIR/vxd_entry.asm" -o "$OBJ_DIR/vxd_entry.o"
nasm -f win32 "$SCRIPT_DIR/shim/vmm_wrappers.asm" -o "$OBJ_DIR/vmm_wrappers.o"

echo "==> Generating OpenRM policy adapters from the pinned submodule..."
python3 "$ROOT_DIR/src_driver/tools/import_openrm_policy.py" \
    "$OPEN_GPU_DIR" "$ROOT_DIR/src_driver/nvx_openrm_policy_generated.c"

echo "==> 2. Compiling VideoPort VxD shim..."
$CC $CFLAGS -c "$SCRIPT_DIR/shim/videoprt_shim.c" -o "$OBJ_DIR/videoprt_shim.o"

echo "==> 3. Compiling core driver modules..."
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_fw.c" -o "$OBJ_DIR/nvx_fw.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_rpc.c" -o "$OBJ_DIR/nvx_rpc.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_seq.c" -o "$OBJ_DIR/nvx_seq.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_mmu.c" -o "$OBJ_DIR/nvx_mmu.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_vbios.c" -o "$OBJ_DIR/nvx_vbios.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_gr_context.c" -o "$OBJ_DIR/nvx_gr_context.o"
$CC $CFLAGS -c "$SCRIPT_DIR/shim/nvx_mdl_win98.c" -o "$OBJ_DIR/nvx_mdl_win98.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_openrm_shim.c" -o "$OBJ_DIR/nvx_openrm_shim.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_openrm_policy.c" -o "$OBJ_DIR/nvx_openrm_policy.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_openrm_policy_generated.c" -o "$OBJ_DIR/nvx_openrm_policy_generated.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_openrm_containers.c" -o "$OBJ_DIR/nvx_openrm_containers.o"
$CC $CFLAGS -c "$CONTAINER_DIR/eheap/eheap_old.c" -o "$OBJ_DIR/eheap_old.o"
$CC $CFLAGS -c "$CONTAINER_DIR/btree/btree.c" -o "$OBJ_DIR/btree.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvx_gsp.c" -o "$OBJ_DIR/nvx_gsp.o"
$CC $CFLAGS -c "$ROOT_DIR/src_driver/nvxmp.c" -o "$OBJ_DIR/nvxmp.o"
$CC $CFLAGS -c "$OPEN_GPU_DIR/src/common/shared/msgq/msgq.c" -o "$OBJ_DIR/msgq.o"

cat << 'EOF' > "$OBJ_DIR/vxd0.ld"
SECTIONS
{
    . = 0x00000000;
    .all : {
        KEEP(*(.text.entry))
        . = ALIGN(64);
        *videoprt_shim.o(.text*)
        . = ALIGN(64);
        *nvxmp.o(.text*)
        . = ALIGN(64);
        *nvx_openrm_shim.o(.text*)
        *nvx_openrm_policy.o(.text*)
        *nvx_openrm_policy_generated.o(.text*)
        *nvx_openrm_containers.o(.text*)
        . = ALIGN(64);
        *nvx_gsp.o(.text*)
        *nvx_gr_context.o(.text*)
        *nvx_mdl_win98.o(.text*)
        . = ALIGN(64);
        *nvx_fw.o(.text*)
        . = ALIGN(64);
        *nvx_rpc.o(.text*)
        . = ALIGN(64);
        *msgq.o(.text*)
        *eheap_old.o(.text*)
        *btree.o(.text*)
        . = ALIGN(64);
        *(.text*)
        . = ALIGN(4096);
        *(.rdata*)
        *(.rodata*)
        . = ALIGN(4096);
        *(.data*)
        . = ALIGN(4096);
        *(.bss*)
        *(COMMON)
    }
    /DISCARD/ : {
        *(.eh_frame*)
        *(.reloc*)
        *(.comment*)
        *(.note*)
    }
}
EOF

cat << 'EOF' > "$OBJ_DIR/vxd1.ld"
SECTIONS
{
    . = 0x00010000;
    .all : {
        KEEP(*(.text.entry))
        . = ALIGN(64);
        *videoprt_shim.o(.text*)
        . = ALIGN(64);
        *nvxmp.o(.text*)
        . = ALIGN(64);
        *nvx_openrm_shim.o(.text*)
        *nvx_openrm_policy.o(.text*)
        *nvx_openrm_policy_generated.o(.text*)
        *nvx_openrm_containers.o(.text*)
        . = ALIGN(64);
        *nvx_gsp.o(.text*)
        *nvx_gr_context.o(.text*)
        *nvx_mdl_win98.o(.text*)
        . = ALIGN(64);
        *nvx_fw.o(.text*)
        . = ALIGN(64);
        *nvx_rpc.o(.text*)
        . = ALIGN(64);
        *msgq.o(.text*)
        *eheap_old.o(.text*)
        *btree.o(.text*)
        . = ALIGN(64);
        *(.text*)
        . = ALIGN(4096);
        *(.rdata*)
        *(.rodata*)
        . = ALIGN(4096);
        *(.data*)
        . = ALIGN(4096);
        *(.bss*)
        *(COMMON)
    }
    /DISCARD/ : {
        *(.eh_frame*)
        *(.reloc*)
        *(.comment*)
        *(.note*)
    }
}
EOF

OBJECTS="$OBJ_DIR/vxd_entry.o $OBJ_DIR/vmm_wrappers.o $OBJ_DIR/videoprt_shim.o $OBJ_DIR/nvxmp.o $OBJ_DIR/nvx_gsp.o $OBJ_DIR/nvx_gr_context.o $OBJ_DIR/nvx_mdl_win98.o $OBJ_DIR/nvx_fw.o $OBJ_DIR/nvx_rpc.o $OBJ_DIR/nvx_seq.o $OBJ_DIR/nvx_mmu.o $OBJ_DIR/nvx_vbios.o $OBJ_DIR/nvx_openrm_shim.o $OBJ_DIR/nvx_openrm_policy.o $OBJ_DIR/nvx_openrm_policy_generated.o $OBJ_DIR/nvx_openrm_containers.o $OBJ_DIR/eheap_old.o $OBJ_DIR/btree.o $OBJ_DIR/msgq.o $LIBGCC"

echo "==> 5. Linking base 0 and base 0x10000 images..."
$LD -m i386pe --gc-sections --image-base 0x00000000 -T "$OBJ_DIR/vxd0.ld" $OBJECTS -o "$OBJ_DIR/a0.pe"
$LD -m i386pe --gc-sections --image-base 0x00010000 -T "$OBJ_DIR/vxd1.ld" $OBJECTS -o "$OBJ_DIR/a1.pe"

$OBJCOPY -O binary "$OBJ_DIR/a0.pe" "$OBJ_DIR/a0.bin"
$OBJCOPY -O binary "$OBJ_DIR/a1.pe" "$OBJ_DIR/a1.bin"

echo "==> 6. Generating final 32-bit LE VxD (nvx.vxd)..."
python3 "$SCRIPT_DIR/tools/mkvxd32.py" NVX "$OBJ_DIR/a0.bin" "$OBJ_DIR/a1.bin" "$ROOT_DIR/dist_win98/nvx.vxd"

echo "==> 7. Preparing optional secondary utility and assembling slog.com..."
if [ "$WIN98_SINGLE_MONITOR" != "1" ]; then
    $CC -O2 -s -mwindows -o "$ROOT_DIR/dist_win98/attach_sec.exe" "$SCRIPT_DIR/attach_sec.c" -luser32
else
    echo "==> Single-monitor target: skipping attach_sec.exe"
fi
nasm -f bin "$SCRIPT_DIR/serial_log.asm" -o "$SCRIPT_DIR/slog.com"

ls -lh "$ROOT_DIR/dist_win98/nvx.vxd" "$SCRIPT_DIR/slog.com"
if [ "$WIN98_SINGLE_MONITOR" != "1" ]; then
    ls -lh "$ROOT_DIR/dist_win98/attach_sec.exe"
fi
echo "==> VxD build complete!"
