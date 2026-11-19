#ifndef NVX_NOUVEAU_BO_H
#define NVX_NOUVEAU_BO_H

#include "nvx_winsys.h"

#define NVX_NOUVEAU_BO_READ  NVX_3D_RELOC_READ
#define NVX_NOUVEAU_BO_WRITE NVX_3D_RELOC_WRITE

typedef struct NVX_NOUVEAU_BO {
   NVX_WINSYS_DEVICE *device;
   NVX_3D_ALLOC_BUFFER allocation;
   uint8_t *shadow;
   /* Last CPU/GPU image known to be published. Detect writes through cached
    * mappings independently of Nouveau's synchronization-only access flags. */
   uint8_t *shadow_base;
   uint32_t shadow_base_valid;
   uint64_t last_fence;
   uint64_t shadow_fence;
   uint32_t references;
   uint32_t map_access;
   uint32_t map_count;
   uint32_t contents_valid;
   uint32_t direct_mapped;
   uint32_t dirty;
   uint32_t dirty_offset;
   uint32_t dirty_end;
} NVX_NOUVEAU_BO;

/* User-mode description of one NVC0 address relocation. The kernel batch ABI
 * must revalidate the BO id, offset, span, access, and command-word index
 * before patching a GPU address. */
typedef NVX_3D_RELOCATION NVX_NOUVEAU_RELOCATION;

int NvxNouveauBoCreate(NVX_WINSYS_DEVICE *device,
                       const NVX_3D_ALLOC_BUFFER *description,
                       uint64_t client_token, NVX_NOUVEAU_BO *bo);
void NvxNouveauBoReference(NVX_NOUVEAU_BO *bo);
int NvxNouveauBoMarkUsed(NVX_NOUVEAU_BO *bo, uint64_t fence_value);
/* Refresh a persistent CPU shadow after GPU writes have completed. */
int NvxNouveauBoRefreshRead(NVX_NOUVEAU_BO *bo);
int NvxNouveauBoRefreshReadIfStale(NVX_NOUVEAU_BO *bo);
/* Resolve a checked BO-relative range to the NVX-owned GPUVA. Command
 * relocation records should retain BufferId + offset and use this only when
 * building a validated submission; callers must not accept arbitrary VAs. */
int NvxNouveauBoGetAddress(const NVX_NOUVEAU_BO *bo, uint64_t offset,
                           uint64_t bytes, uint32_t access,
                           uint64_t *gpu_va);
int NvxNouveauBoBuildRelocation(const NVX_NOUVEAU_BO *bo,
                                const uint32_t *command_dwords,
                                uint32_t command_count,
                                uint32_t command_word, uint64_t offset,
                                uint64_t bytes, uint32_t access,
                                NVX_NOUVEAU_RELOCATION *relocation);
void *NvxNouveauBoMap(NVX_NOUVEAU_BO *bo, uint32_t access);
/* Publish changed CPU words regardless of synchronization access flags. */
int NvxNouveauBoFlush(NVX_NOUVEAU_BO *bo);
/* Validate an input range and publish pending CPU changes. Mesa may modify a
 * cached map without widening its original map access flags. Unchanged words
 * retain the GPU's current contents. */
int NvxNouveauBoFlushRange(NVX_NOUVEAU_BO *bo, uint64_t offset,
                           uint64_t bytes);
int NvxNouveauBoUnmap(NVX_NOUVEAU_BO *bo);
int NvxNouveauBoDestroy(NVX_NOUVEAU_BO *bo);
void NvxNouveauBoPoolEnable(int enable);
void NvxNouveauBoPoolFlush(NVX_WINSYS_DEVICE *device);

#endif
