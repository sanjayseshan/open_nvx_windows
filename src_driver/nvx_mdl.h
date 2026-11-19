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
void NvxMdlFlushGartBacking(void *mdl, int deviceWrites);

#endif
