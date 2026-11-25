/* Win98 replacement for the XP WDM MDL-backed GART allocator.
 * Win9x has no WDM MDL API, so use VMM-owned contiguous pages and expose
 * their PFNs to the shared GART PTE builder. */
#include <stdint.h>
#include "videoprt_shim.h"
#include "nvx_mdl.h"
#include "nvx_gsp.h"

extern NVX_DEVICE_EXTENSION *g_pPrimaryDevExt;

typedef struct NVX_WIN98_GART_BACKING {
    uint32_t signature;
    uint32_t bytes;
    void *virtualAddress;
    void *pfnArray;
    PHYSICAL_ADDRESS physicalAddress;
} NVX_WIN98_GART_BACKING;

#define NVX_WIN98_GART_SIGNATURE 0x394d564eU /* "NVM9" */

int NvxMdlAllocateGartBacking(unsigned long bytes, void **mdlOut,
                              void **systemVirtualOut, void **pfnArrayOut)
{
    NVX_WIN98_GART_BACKING *backing;
    ULONG_PTR *pfns;
    PHYSICAL_ADDRESS physical;
    uint32_t pageCount, i;
    void *virtualAddress;

    if (mdlOut) *mdlOut = NULL;
    if (systemVirtualOut) *systemVirtualOut = NULL;
    if (pfnArrayOut) *pfnArrayOut = NULL;
    if (!bytes || (bytes & 4095U) || !mdlOut || !systemVirtualOut ||
        !pfnArrayOut || bytes > 0xffffffffUL)
        return 0;

    physical.QuadPart = 0;
    virtualAddress = VideoPortAllocateCommonBuffer(g_pPrimaryDevExt,
        g_pPrimaryDevExt->DmaAdapter, (ULONG)bytes, &physical, FALSE, NULL);
    if (!virtualAddress || physical.QuadPart < 0 ||
        ((uint64_t)physical.QuadPart & 4095U) ||
        (uint64_t)physical.QuadPart + bytes > 0x100000000ULL) {
        if (virtualAddress)
            VideoPortFreeCommonBuffer(g_pPrimaryDevExt, (ULONG)bytes,
                virtualAddress, physical, FALSE);
        return 0;
    }

    pageCount = bytes >> 12;
    pfns = (ULONG_PTR *)VideoPortAllocatePool(g_pPrimaryDevExt,
        VpNonPagedPool, (SIZE_T)(pageCount * sizeof(*pfns)), 0x394d564eU);
    if (!pfns) {
        VideoPortFreeCommonBuffer(g_pPrimaryDevExt, (ULONG)bytes,
            virtualAddress, physical, FALSE);
        return 0;
    }
    for (i = 0; i < pageCount; ++i)
        pfns[i] = ((ULONG_PTR)physical.QuadPart >> 12) + i;

    backing = (NVX_WIN98_GART_BACKING *)VideoPortAllocatePool(
        g_pPrimaryDevExt, VpNonPagedPool, sizeof(*backing), 0x394d564eU);
    if (!backing) {
        VideoPortFreePool(g_pPrimaryDevExt, pfns);
        VideoPortFreeCommonBuffer(g_pPrimaryDevExt, (ULONG)bytes,
            virtualAddress, physical, FALSE);
        return 0;
    }
    backing->signature = NVX_WIN98_GART_SIGNATURE;
    backing->bytes = (uint32_t)bytes;
    backing->virtualAddress = virtualAddress;
    backing->pfnArray = pfns;
    backing->physicalAddress = physical;
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
        VideoPortFreeCommonBuffer(g_pPrimaryDevExt, backing->bytes,
            backing->virtualAddress, backing->physicalAddress, FALSE);
    VideoPortFreePool(g_pPrimaryDevExt, backing->pfnArray);
    backing->signature = 0;
    VideoPortFreePool(g_pPrimaryDevExt, backing);
}

/* _PageAllocate gives the VxD a system mapping, not a caller-process mapping.
 * Win9x per-process page mapping has not been implemented, so fail closed
 * instead of returning a kernel virtual address to a ring-3 client. */
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
