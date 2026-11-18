#ifndef NVX_GR_CONTEXT_H
#define NVX_GR_CONTEXT_H
#include <stdint.h>

/* Indices are NV0080_CTRL_FIFO_GET_ENGINE_CONTEXT_PROPERTIES_ENGINE_ID;
   buffer IDs are NV2080_CTRL_GPU_PROMOTE_CTX_BUFFER_ID. */
#define NVX_GR_REQUIREMENT_COUNT 25U
#define NVX_GR_MAX_BUFFERS 12U
typedef struct { uint32_t size, alignment; } NVX_GR_REQUIREMENT;
typedef struct { uint32_t id, alignment; uint64_t offset, size; } NVX_GR_BUFFER;
typedef struct { uint32_t count; uint64_t bytes; NVX_GR_BUFFER buffers[NVX_GR_MAX_BUFFERS]; } NVX_GR_LAYOUT;

/* Build a bounded, page-isolated layout; never map or initialize GPU memory. */
int NvxGrBuildLayout(const NVX_GR_REQUIREMENT *, uint32_t, int, uint64_t, NVX_GR_LAYOUT *);
/* Encode 4KB coherent system-memory leaves into caller-owned PTEs. */
int NvxGrBuildPtes(uint64_t *, uint32_t, uint64_t, uint64_t, int);

/* Primary bring-up window plus a separate 1 GiB system-memory VA region.
 * Leaf PTEs describe OS-owned pages; this reserves no physical VRAM. */
#define NVX_GART_GPU_VA_BASE UINT64_C(0x11a00000)
#define NVX_GART_PTES_PER_SLICE 512U
#define NVX_GART_PRIMARY_SLICE_COUNT 115U
#define NVX_GART_PRIMARY_PAGE_COUNT \
    (NVX_GART_PRIMARY_SLICE_COUNT * NVX_GART_PTES_PER_SLICE)
#define NVX_GART_EXTRA_GPU_VA_BASE UINT64_C(0x40000000)
#define NVX_GART_EXTRA_SLICE_COUNT 512U
#define NVX_GART_SLICE_BYTES UINT32_C(0x00200000)
#define NVX_GART_SLICE_COUNT \
    (NVX_GART_PRIMARY_SLICE_COUNT + NVX_GART_EXTRA_SLICE_COUNT)
#define NVX_GART_PAGE_BYTES 4096U
#define NVX_GART_MIN_VA_ALIGNMENT 0x10000U
#define NVX_GART_PAGE_COUNT (NVX_GART_SLICE_COUNT * NVX_GART_PTES_PER_SLICE)
#define NVX_GART_TOTAL_BYTES ((uint64_t)NVX_GART_SLICE_COUNT * NVX_GART_SLICE_BYTES)
#define NVX_GART_SEGMENT_BYTES \
    ((uint64_t)NVX_GART_PRIMARY_SLICE_COUNT * NVX_GART_SLICE_BYTES)
#define NVX_GART_EXTRA_BYTES     ((uint64_t)NVX_GART_EXTRA_SLICE_COUNT * NVX_GART_SLICE_BYTES)
#define NVX_GART_PD0_COUNT ((NVX_GART_EXTRA_SLICE_COUNT + 255U) / 256U)
#define NVX_GART_PD0_OFFSET (NVX_GART_SLICE_COUNT * 4096U)
#define NVX_GART_TABLE_BYTES (NVX_GART_PD0_OFFSET + NVX_GART_PD0_COUNT * 4096U)
#define NVX_GART_BITMAP_WORDS ((NVX_GART_PAGE_COUNT + 31U) / 32U)
static inline uint64_t NvxGartGpuVaForPage(uint32_t page)
{
    if (page >= NVX_GART_PAGE_COUNT)
        return 0;
    if (page < NVX_GART_PRIMARY_PAGE_COUNT)
        return NVX_GART_GPU_VA_BASE + (uint64_t)page * NVX_GART_PAGE_BYTES;
    return NVX_GART_EXTRA_GPU_VA_BASE +
        (uint64_t)(page - NVX_GART_PRIMARY_PAGE_COUNT) * NVX_GART_PAGE_BYTES;
}
static inline int NvxGartPageRangeValid(uint32_t first, uint32_t count)
{
    if (!count || first >= NVX_GART_PAGE_COUNT ||
        count > NVX_GART_PAGE_COUNT - first)
        return 0;
    return first < NVX_GART_PRIMARY_PAGE_COUNT ?
        count <= NVX_GART_PRIMARY_PAGE_COUNT - first : 1;
}
static inline int NvxGartPageForGpuVa(uint64_t gpuVa, uint32_t *page)
{
    uint64_t offset;
    if (gpuVa >= NVX_GART_GPU_VA_BASE &&
        gpuVa < NVX_GART_GPU_VA_BASE + NVX_GART_SEGMENT_BYTES) {
        offset = gpuVa - NVX_GART_GPU_VA_BASE;
        if (page) *page = (uint32_t)(offset / NVX_GART_PAGE_BYTES);
        return 1;
    }
    if (gpuVa >= NVX_GART_EXTRA_GPU_VA_BASE &&
        gpuVa < NVX_GART_EXTRA_GPU_VA_BASE + NVX_GART_EXTRA_BYTES) {
        offset = gpuVa - NVX_GART_EXTRA_GPU_VA_BASE;
        if (page) *page = NVX_GART_PRIMARY_PAGE_COUNT +
                          (uint32_t)(offset / NVX_GART_PAGE_BYTES);
        return 1;
    }
    return 0;
}
typedef struct NVX_GART_HEAP {
    uint32_t used[NVX_GART_BITMAP_WORDS];
} NVX_GART_HEAP;
int NvxGartReserve(NVX_GART_HEAP *, uint64_t, uint32_t, uint32_t *, uint32_t *, uint64_t *);
int NvxGartRelease(NVX_GART_HEAP *, uint32_t, uint32_t);
int NvxGartBuildPtes(uint64_t *, uint32_t, uint32_t, uint32_t,
                     uint64_t, uint64_t, int);
uint64_t NvxGartEncodePte(uint64_t, int);
#endif
