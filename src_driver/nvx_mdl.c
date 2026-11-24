#include <ddk/wdm.h>
#include "nvx_mdl.h"

int NvxMdlAllocateGartBacking(unsigned long bytes, void **mdlOut,
                              void **systemVirtualOut, void **pfnArrayOut)
{
    PHYSICAL_ADDRESS lowAddress, highAddress, skipBytes;
    PMDL mdl;
    PVOID systemVirtual;

    if (mdlOut) *mdlOut = NULL;
    if (systemVirtualOut) *systemVirtualOut = NULL;
    if (pfnArrayOut) *pfnArrayOut = NULL;
    if (!bytes || (bytes & (PAGE_SIZE - 1U)) || !mdlOut ||
        !systemVirtualOut || !pfnArrayOut)
        return 0;

    lowAddress.QuadPart = 0;
    highAddress.QuadPart = 0xffffffffULL;
    skipBytes.QuadPart = 0;
    mdl = MmAllocatePagesForMdl(lowAddress, highAddress, skipBytes,
                                (SIZE_T)bytes);
    if (!mdl)
        return 0;
    if (MmGetMdlByteCount(mdl) != bytes) {
        MmFreePagesFromMdl(mdl);
        ExFreePool(mdl);
        return 0;
    }

    /* The user mapping below is write-combined. Use the same cache policy
     * for the kernel alias; conflicting cache types for the same pages make
     * CPU writes and device visibility undefined on x86/x64. */
    systemVirtual = MmMapLockedPagesSpecifyCache(mdl, KernelMode, MmWriteCombined,
                                                  NULL, FALSE,
                                                  NormalPagePriority);
    if (!systemVirtual) {
        MmFreePagesFromMdl(mdl);
        ExFreePool(mdl);
        return 0;
    }

    *mdlOut = mdl;
    *systemVirtualOut = systemVirtual;
    *pfnArrayOut = MmGetMdlPfnArray(mdl);
    return 1;
}

void NvxMdlFreeGartBacking(void *mdlHandle, void *systemVirtual)
{
    PMDL mdl = (PMDL)mdlHandle;
    if (!mdl)
        return;
    if (systemVirtual && (mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA))
        MmUnmapLockedPages(systemVirtual, mdl);
    MmFreePagesFromMdl(mdl);
    ExFreePool(mdl);
}

void *NvxMdlMapUser(void *mdlHandle)
{
    PMDL mdl = (PMDL)mdlHandle;
    if (!mdl)
        return NULL;
    return MmMapLockedPagesSpecifyCache(mdl, UserMode, MmWriteCombined,
                                        NULL, FALSE, NormalPagePriority);
}

void NvxMdlUnmapUser(void *mdlHandle, void *userVirtual)
{
    PMDL mdl = (PMDL)mdlHandle;
    if (mdl && userVirtual)
        MmUnmapLockedPages(userVirtual, mdl);
}

void NvxMdlFlushGartBacking(void *mdlHandle, int deviceWrites)
{
    /* KeFlushIoBuffers is a documented no-op on the driver's x86/x64
     * targets. Keep the call for other architectures supported by WDM. */
#if defined(_M_ARM) || defined(_M_ARM64) || defined(_ARM_) || defined(_ARM64_)
    if (mdlHandle) {
        KeFlushIoBuffers((PMDL)mdlHandle,
                         deviceWrites ? TRUE : FALSE, TRUE);
    }
#else
    (void)mdlHandle;
    (void)deviceWrites;
#endif
}
