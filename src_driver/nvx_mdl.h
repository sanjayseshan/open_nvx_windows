#ifndef NVX_MDL_H
#define NVX_MDL_H

/* Opaque MDL backing helpers. The implementation includes the WDM DDK
 * separately because miniport.h intentionally exposes a curated VideoPort
 * surface and conflicts with the full WDM header. */
int NvxMdlAllocateGartBacking(unsigned long bytes, void **mdl,
                              void **systemVirtual, void **pfnArray);
void NvxMdlFreeGartBacking(void *mdl, void *systemVirtual);
void *NvxMdlMapUser(void *mdl);
void NvxMdlUnmapUser(void *mdl, void *userVirtual);
#if defined(_M_ARM) || defined(_M_ARM64) || defined(_ARM_) || defined(_ARM64_)
void NvxMdlFlushGartBacking(void *mdl, int deviceWrites);
#else
/* KeFlushIoBuffers is a no-op for the driver's supported x86/x64 targets.
 * Inline that fact so the per-submit BO loop does not make one empty call per
 * reference. */
static __inline void NvxMdlFlushGartBacking(void *mdl, int deviceWrites)
{
    (void)mdl;
    (void)deviceWrites;
}
#endif

#endif
