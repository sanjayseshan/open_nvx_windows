/* Win98 equivalent of the XP WDM MDL-backed GART allocator.
 *
 * XP's MmAllocatePagesForMdl returns locked, potentially discontiguous
 * physical pages plus a PFN array and a system mapping.  VMM _PageAllocate
 * provides the corresponding fixed system-page allocation on Win9x;
 * _CopyPageTable supplies the actual PFNs.  Do not route this through
 * VideoPortAllocateCommonBuffer: a common buffer requires a contiguous DMA
 * address, which is needlessly restrictive for the GART path. */
#include <stdint.h>
#include "videoprt_shim.h"
#include "nvx_mdl.h"
#include "nvx_gsp.h"

extern NVX_DEVICE_EXTENSION *g_pPrimaryDevExt;
extern void * __attribute__((stdcall)) _PageAllocate(
    uint32_t nPages, uint32_t pType, uint32_t VM, uint32_t AlignMask,
    uint32_t minPhys, uint32_t maxPhys, uint32_t *PhysAddr,
    uint32_t flags);
extern int __attribute__((stdcall)) _PageFree(void *hMem, uint32_t flags);
extern uint32_t __attribute__((stdcall)) _CopyPageTable(
    uint32_t LinPgNum, uint32_t nPages, void *Buffer, uint32_t Flags);

typedef struct NVX_WIN98_GART_BACKING {
    uint32_t signature;
    uint32_t bytes;
    uint32_t pages;
    void *virtualAddress;
    ULONG_PTR *pfnArray;
} NVX_WIN98_GART_BACKING;

#define NVX_WIN98_GART_SIGNATURE 0x394d564eU /* "NVM9" */
#define NVX_WIN98_PAGE_SIZE      4096U
#define NVX_WIN98_PAGE_MASK      0xfffff000U
#define NVX_WIN98_PTE_PRESENT    0x00000001U
#define NVX_WIN98_VMM_PG_SYS     1U
#define NVX_WIN98_PAGE_FLAGS     0x0000000bU /* ZERO_INIT | USE_ALIGN | FIXED */

int NvxMdlAllocateGartBacking(unsigned long bytes, void **mdlOut,
                              void **systemVirtualOut, void **pfnArrayOut)
{
    NVX_WIN98_GART_BACKING *backing;
    ULONG_PTR *pfns;
    uint32_t *ptes;
    uint32_t pageCount, i, physHint = 0;
    void *virtualAddress;
    uint64_t pfnBytes;

    if (mdlOut) *mdlOut = NULL;
    if (systemVirtualOut) *systemVirtualOut = NULL;
    if (pfnArrayOut) *pfnArrayOut = NULL;
    if (!bytes || (bytes & (NVX_WIN98_PAGE_SIZE - 1U)) || !mdlOut ||
        !systemVirtualOut || !pfnArrayOut || bytes > 0xffffffffUL)
        return 0;

    pageCount = (uint32_t)(bytes / NVX_WIN98_PAGE_SIZE);
    pfnBytes = (uint64_t)pageCount * sizeof(*pfns);
    if (!pageCount || pfnBytes > 0xffffffffU)
        return 0;

    /* Keep the metadata in nonpaged memory before reserving the backing
     * pages, so every failure path can release the VMM block cleanly. */
    backing = (NVX_WIN98_GART_BACKING *)VideoPortAllocatePool(
        g_pPrimaryDevExt, VpNonPagedPool, sizeof(*backing),
        NVX_WIN98_GART_SIGNATURE);
    if (!backing)
        return 0;
    pfns = (ULONG_PTR *)VideoPortAllocatePool(g_pPrimaryDevExt,
        VpNonPagedPool, (SIZE_T)pfnBytes, NVX_WIN98_GART_SIGNATURE);
    if (!pfns) {
        VideoPortFreePool(g_pPrimaryDevExt, backing);
        return 0;
    }
    ptes = (uint32_t *)VideoPortAllocatePool(g_pPrimaryDevExt,
        VpNonPagedPool, (SIZE_T)pfnBytes, NVX_WIN98_GART_SIGNATURE);
    if (!ptes) {
        VideoPortFreePool(g_pPrimaryDevExt, pfns);
        VideoPortFreePool(g_pPrimaryDevExt, backing);
        return 0;
    }

    /* PAGEFIXED pins the pages. PAGECONTIG is intentionally clear: the
     * nouveau GART maps each PFN independently, just like the XP MDL path. */
    virtualAddress = _PageAllocate(pageCount, NVX_WIN98_VMM_PG_SYS, 0,
        0, 0, 0x000fffffU, &physHint, NVX_WIN98_PAGE_FLAGS);
    if (!virtualAddress) {
        /* Older VMMs may reject PAGEUSEALIGN with an unconstrained range.
         * PAGEFIXED|PAGEZEROINIT is still sufficient for a locked block. */
        virtualAddress = _PageAllocate(pageCount, NVX_WIN98_VMM_PG_SYS, 0,
            0, 0, 0x000fffffU, &physHint, 0x00000009U);
    }
    if (!virtualAddress) {
        VideoPortFreePool(g_pPrimaryDevExt, ptes);
        VideoPortFreePool(g_pPrimaryDevExt, pfns);
        VideoPortFreePool(g_pPrimaryDevExt, backing);
        return 0;
    }

    /* _CopyPageTable returns VMM status information; validate every PTE
     * itself rather than assuming a particular success count.  That also
     * prevents installing a partial or fabricated PFN list into the GPU. */
    for (i = 0; i < pageCount; ++i)
        ptes[i] = 0;
    (void)_CopyPageTable((uint32_t)(uintptr_t)virtualAddress >> 12,
                         pageCount, ptes, 0);
    for (i = 0; i < pageCount; ++i) {
        if (!(ptes[i] & NVX_WIN98_PTE_PRESENT)) {
            _PageFree(virtualAddress, 0);
            VideoPortFreePool(g_pPrimaryDevExt, ptes);
            VideoPortFreePool(g_pPrimaryDevExt, pfns);
            VideoPortFreePool(g_pPrimaryDevExt, backing);
            return 0;
        }
        pfns[i] = (ULONG_PTR)((ptes[i] & NVX_WIN98_PAGE_MASK) >> 12);
    }

    VideoPortFreePool(g_pPrimaryDevExt, ptes);
    backing->signature = NVX_WIN98_GART_SIGNATURE;
    backing->bytes = (uint32_t)bytes;
    backing->pages = pageCount;
    backing->virtualAddress = virtualAddress;
    backing->pfnArray = pfns;
    *mdlOut = backing;
    *systemVirtualOut = virtualAddress;
    *pfnArrayOut = pfns;
    return 1;
}

void NvxMdlFreeGartBacking(void *mdlHandle, void *systemVirtual)
{
    NVX_WIN98_GART_BACKING *backing = (NVX_WIN98_GART_BACKING *)mdlHandle;
    if (!backing || backing->signature != NVX_WIN98_GART_SIGNATURE)
        return;
    if (systemVirtual == backing->virtualAddress)
        _PageFree(backing->virtualAddress, 0);
    VideoPortFreePool(g_pPrimaryDevExt, backing->pfnArray);
    backing->signature = 0;
    VideoPortFreePool(g_pPrimaryDevExt, backing);
}

/* Win98 per-process page mapping has not been implemented.  The miniport
 * stages user data through its IOCTL path, so never expose a VxD mapping to
 * ring 3 as if it were an MDL user mapping. */
void *NvxMdlMapUser(void *mdlHandle)
{
    NVX_WIN98_GART_BACKING *backing = (NVX_WIN98_GART_BACKING *)mdlHandle;
    if (!backing || backing->signature != NVX_WIN98_GART_SIGNATURE)
        return NULL;
    return NULL;
}

void NvxMdlUnmapUser(void *mdlHandle, void *userVirtual)
{
    (void)mdlHandle;
    (void)userVirtual;
}
