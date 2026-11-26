#include <ddk/wdm.h>
#include "nvx_mdl.h"

int NvxMdlAllocateGartBacking(unsigned long bytes, void **mdlOut,
                              void **systemVirtualOut, void **pfnArrayOut,
                              unsigned long *failureStage)
{
    PHYSICAL_ADDRESS lowAddress, highAddress, skipBytes;
    PMDL mdl;
    PVOID systemVirtual;

    if (mdlOut) *mdlOut = NULL;
    if (systemVirtualOut) *systemVirtualOut = NULL;
    if (pfnArrayOut) *pfnArrayOut = NULL;
    if (failureStage) *failureStage = 1; /* invalid request */
    if (!bytes || (bytes & (PAGE_SIZE - 1U)) || !mdlOut ||
        !systemVirtualOut || !pfnArrayOut)
        return 0;

    lowAddress.QuadPart = 0;
    highAddress.QuadPart = 0xffffffffULL;
    skipBytes.QuadPart = 0;
    mdl = MmAllocatePagesForMdl(lowAddress, highAddress, skipBytes,
                                (SIZE_T)bytes);
    if (failureStage) *failureStage = 2; /* page allocation */
    if (!mdl)
        return 0;
    if (failureStage) *failureStage = 3; /* MDL length */
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
    if (failureStage) *failureStage = 4; /* system mapping */
    if (!systemVirtual) {
        MmFreePagesFromMdl(mdl);
        ExFreePool(mdl);
        return 0;
    }

    *mdlOut = mdl;
    *systemVirtualOut = systemVirtual;
    *pfnArrayOut = MmGetMdlPfnArray(mdl);
    if (failureStage) *failureStage = 0;
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

#if defined(_M_ARM) || defined(_M_ARM64) || defined(_ARM_) || defined(_ARM64_)
void NvxMdlFlushGartBacking(void *mdlHandle, int deviceWrites)
{
    if (mdlHandle) {
        KeFlushIoBuffers((PMDL)mdlHandle,
                         deviceWrites ? TRUE : FALSE, TRUE);
    }
}
#endif
