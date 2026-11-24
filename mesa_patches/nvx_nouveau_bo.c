#include "nvx_nouveau_bo.h"

#include <stdlib.h>
#include <string.h>

#if defined(NVX_XP_WIN32_SHIMS) && \
    (defined(_WIN32) || defined(_WIN64))
void NvxWglRecordNouveauPushbufItem(uint32_t kind, uint32_t index,
                                   uint32_t a, uint32_t b, uint32_t c,
                                   uint32_t d, uint32_t e, uint32_t f);
void NvxWglRecordNouveauShadowProfile(uint32_t scanned_bytes,
                                      uint32_t uploaded_bytes,
                                      uint32_t upload_calls);
static uint32_t nvx_bo_readmap_trace_count;

#endif

#define NVX_GART_BO_POOL_CAPACITY 512
#define NVX_VRAM_BO_POOL_CAPACITY 512

typedef struct {
   NVX_WINSYS_DEVICE *device;
   NVX_3D_ALLOC_BUFFER allocation;
} NVX_POOLED_BO;

/* General size-matched GART and VRAM BO recycling pools */
static NVX_POOLED_BO s_nvx_gart_pool[NVX_GART_BO_POOL_CAPACITY];
static uint32_t s_nvx_gart_pool_count = 0;

static NVX_POOLED_BO s_nvx_vram_pool[NVX_VRAM_BO_POOL_CAPACITY];
static uint32_t s_nvx_vram_pool_count = 0;

static int s_nvx_bo_pool_enabled = 0;

void
NvxNouveauBoPoolEnable(int enable)
{
   s_nvx_bo_pool_enabled = enable;
}

int
NvxNouveauBoCreate(NVX_WINSYS_DEVICE *device,
                   const NVX_3D_ALLOC_BUFFER *description,
                   uint64_t client_token, NVX_NOUVEAU_BO *bo)
{
   NVX_3D_ALLOC_BUFFER request;

   if (!bo)
      return 0;

   memset(bo, 0, sizeof(*bo));
   if (!device || !description || !client_token ||
       client_token != device->ClientToken ||
       description->Bytes > device->caps.MaxBufferBytes) {
#if defined(NVX_XP_WIN32_SHIMS) && \
    (defined(_WIN32) || defined(_WIN64))
      NvxWglRecordNouveauPushbufItem(24U, 1U,
         device ? (uint32_t)device->ClientToken : 0U,
         (uint32_t)client_token,
         description ? description->Bytes : 0U,
         device ? device->caps.MaxBufferBytes : 0U,
         (device && client_token == device->ClientToken) ? 1U : 0U,
         0U);
#endif
      return 0;
   }

   if (s_nvx_bo_pool_enabled) {
      if (description->Usage == NVX_3D_USAGE_GART_BUFFER) {
         uint32_t i;
         for (i = s_nvx_gart_pool_count; i > 0; i--) {
            if (s_nvx_gart_pool[i - 1].device == device &&
                s_nvx_gart_pool[i - 1].allocation.Bytes == description->Bytes) {
               uint32_t align = description->Alignment ? description->Alignment : 0x1000U;
               if (align && (s_nvx_gart_pool[i - 1].allocation.GpuVa & (align - 1U)))
                  continue;
               bo->allocation = s_nvx_gart_pool[i - 1].allocation;
               s_nvx_gart_pool[i - 1] = s_nvx_gart_pool[s_nvx_gart_pool_count - 1];
               s_nvx_gart_pool_count--;
               bo->device = device;
               bo->references = 1;
               return 1;
            }
         }
      } else {
         /* VRAM BO recycling: match exact size, usage, tile mode, and memory kind */
         uint32_t i;
         for (i = s_nvx_vram_pool_count; i > 0; i--) {
            if (s_nvx_vram_pool[i - 1].device == device &&
                s_nvx_vram_pool[i - 1].allocation.Bytes == description->Bytes &&
                s_nvx_vram_pool[i - 1].allocation.Usage == description->Usage &&
                s_nvx_vram_pool[i - 1].allocation.TileMode == description->TileMode &&
                s_nvx_vram_pool[i - 1].allocation.MemoryKind == description->MemoryKind) {
               uint32_t align = description->Alignment ? description->Alignment : 0x1000U;
               if (align && (s_nvx_vram_pool[i - 1].allocation.GpuVa & (align - 1U)))
                  continue;
               bo->allocation = s_nvx_vram_pool[i - 1].allocation;
               s_nvx_vram_pool[i - 1] = s_nvx_vram_pool[s_nvx_vram_pool_count - 1];
               s_nvx_vram_pool_count--;
               bo->device = device;
               bo->references = 1;
               return 1;
            }
         }
      }
   }

   request = *description;
   request.Size = sizeof(request);
   request.Version = NVX_3D_ABI_VERSION;
   request.BufferId = 0;
   request.GpuVa = 0;
   request.ClientToken = client_token;
   if (!NvxWinsysAllocBuffer(device, &request, &bo->allocation)) {
#if defined(NVX_XP_WIN32_SHIMS) && \
    (defined(_WIN32) || defined(_WIN64))
      NvxWglRecordNouveauPushbufItem(24U, 2U,
         request.Usage, request.Bytes, request.Alignment,
         request.TileMode, request.MemoryKind, 0U);
#endif
      return 0;
   }

   bo->device = device;
   bo->references = 1;
   return 1;
}

void
NvxNouveauBoReference(NVX_NOUVEAU_BO *bo)
{
   if (bo && bo->references && bo->references != UINT32_MAX)
      bo->references++;
}

int
NvxNouveauBoMarkUsed(NVX_NOUVEAU_BO *bo, uint64_t fence_value)
{
   if (!bo || !bo->references || !fence_value ||
       fence_value < bo->last_fence)
      return 0;
   bo->last_fence = fence_value;
   return 1;
}

int
NvxNouveauBoRefreshRead(NVX_NOUVEAU_BO *bo)
{
   uint8_t *gpu;
   uint32_t offset;
   if (!bo || !bo->references || !bo->shadow || !bo->map_count)
      return 0;
   /* A direct GART mapping already names GPU-visible system memory. There
    * is no shadow baseline to refresh, but GPU completion and CPU ordering
    * are still required before callers consume it. */
   if (bo->direct_mapped) {
      if (bo->last_fence &&
          !NvxWinsysWaitFence(bo->device, bo->last_fence, 5000))
         return 0;
      __sync_synchronize();
      bo->shadow_fence = bo->last_fence;
      bo->contents_valid = 1;
      return 1;
   }
   if (!bo->shadow_base)
      return 0;
   if (bo->shadow_fence >= bo->last_fence)
      return 1;
   if (bo->last_fence &&
       !NvxWinsysWaitFence(bo->device, bo->last_fence, 5000))
      return 0;
   gpu = (uint8_t *)malloc(bo->allocation.Bytes);
   if (!gpu)
      return 0;
   if (!NvxWinsysDownloadBuffer(bo->device, bo->allocation.BufferId,
                               bo->allocation.Bytes, 0, gpu,
                               bo->allocation.Bytes)) {
      free(gpu);
      return 0;
   }
   /* Preserve unpublished CPU words while refreshing words owned by the GPU.
    * Nouveau can write a cached mapping without issuing another CPU_PREP. */
   for (offset = 0; offset < bo->allocation.Bytes; offset += 4U) {
      if (!memcmp(bo->shadow + offset, bo->shadow_base + offset, 4U))
         memcpy(bo->shadow + offset, gpu + offset, 4U);
   }
   memcpy(bo->shadow_base, gpu, bo->allocation.Bytes);
   bo->shadow_base_valid = 1U;
   free(gpu);
   bo->shadow_fence = bo->last_fence;
   bo->contents_valid = 1;
   return 1;
}

int
NvxNouveauBoRefreshReadIfStale(NVX_NOUVEAU_BO *bo)
{
   if (!bo || !bo->references || !bo->shadow || !bo->map_count ||
       !(bo->map_access & NVX_NOUVEAU_BO_READ))
      return 0;
   return NvxNouveauBoRefreshRead(bo);
}

int
NvxNouveauBoGetAddress(const NVX_NOUVEAU_BO *bo, uint64_t offset,
                      uint64_t bytes, uint32_t access, uint64_t *gpu_va)
{
   uint64_t end;

   if (gpu_va)
      *gpu_va = 0;
   if (!bo || !bo->references || !gpu_va || !bytes || !access ||
       (access & ~(NVX_NOUVEAU_BO_READ | NVX_NOUVEAU_BO_WRITE)) ||
       !bo->allocation.BufferId || !bo->allocation.GpuVa ||
       offset > bo->allocation.Bytes ||
       bytes > (uint64_t)bo->allocation.Bytes - offset ||
       bo->allocation.GpuVa > UINT64_MAX - offset)
      return 0;

   end = bo->allocation.GpuVa + offset;
   if (end > UINT64_MAX - bytes)
      return 0;

   *gpu_va = end;
   return 1;
}

int
NvxNouveauBoBuildRelocation(const NVX_NOUVEAU_BO *bo,
                           const uint32_t *command_dwords,
                           uint32_t command_count, uint32_t command_word,
                           uint64_t offset, uint64_t bytes, uint32_t access,
                           NVX_NOUVEAU_RELOCATION *relocation)
{
   NVX_NOUVEAU_RELOCATION result;

   if (relocation)
      memset(relocation, 0, sizeof(*relocation));
   if (!command_dwords || !command_count || command_word >= command_count ||
       !relocation || !bo || !bo->references)
      return 0;

   memset(&result, 0, sizeof(result));
   result.CommandWord = command_word;
   result.Access = access;
   result.BufferId = bo->allocation.BufferId;
   result.BufferOffset = offset;
   result.Bytes = bytes;
   if (!Nvx3dValidateRelocation(&result, command_count, &bo->allocation))
      return 0;
   *relocation = result;
   return 1;
}

void *
NvxNouveauBoMap(NVX_NOUVEAU_BO *bo, uint32_t access)
{
   if (!bo || !bo->references || !access ||
       (access & ~(NVX_NOUVEAU_BO_READ | NVX_NOUVEAU_BO_WRITE)) ||
       !bo->allocation.Bytes)
      return NULL;

   if (bo->map_count) {
      /* Widen the access flags if the caller requests permissions the
       * current mapping does not have. The shadow is an ordinary malloc'd
       * CPU buffer, so READ and WRITE are always physically possible once
       * the shadow exists. Widening avoids failing a diagnostic READ map
       * on a BO that Mesa already holds mapped WRITE-only. */
      bo->map_access |= access;
      bo->map_count++;
      if ((access & NVX_NOUVEAU_BO_READ) &&
          !NvxNouveauBoRefreshReadIfStale(bo)) {
         bo->map_count--;
         return NULL;
      }
      return bo->shadow;
   }

   /* Unmap releases a CPU access, while the process-owned VA remains valid. */
   if (bo->direct_mapped && bo->shadow) {
      bo->map_access = access;
      bo->map_count = 1;
      if ((access & NVX_NOUVEAU_BO_READ) && !NvxNouveauBoRefreshRead(bo)) {
         bo->map_count = 0;
         return NULL;
      }
      return bo->shadow;
   }

   /* If direct mapping is available for this GART buffer, map user VA */
   if ((bo->allocation.Usage & NVX_3D_USAGE_GART_BUFFER) && !bo->shadow) {
      void *direct_va = NvxWinsysMapBuffer(bo->device, bo->allocation.BufferId);
      if (direct_va) {
         bo->direct_mapped = 1;
         bo->shadow = (uint8_t *)direct_va;
         bo->shadow_base = NULL;
         bo->map_access = access;
         bo->map_count = 1;
         bo->contents_valid = 1;
         return bo->shadow;
      }
   }

   if (bo->last_fence &&
       !NvxWinsysWaitFence(bo->device, bo->last_fence, 5000))
      return NULL;

   bo->shadow = (uint8_t *)malloc(bo->allocation.Bytes);
   if (!bo->shadow)
      return NULL;

   /* Nouveau BO allocation leaves contents undefined. A never-used BO has
    * nothing to preserve, so start its shadow at zero instead of forcing a
    * synchronous CE readback (the NVX bridge copies through 4 KiB windows).
    * Once the BO has been uploaded or used by the GPU, preserve its contents
    * with a readback before allowing a partial CPU update. */
   if (bo->contents_valid || bo->last_fence) {
      if (!NvxWinsysDownloadBuffer(bo->device, bo->allocation.BufferId,
                                   bo->allocation.Bytes, 0, bo->shadow,
                                   bo->allocation.Bytes)) {
#if defined(NVX_XP_WIN32_SHIMS) && \
    (defined(_WIN32) || defined(_WIN64))
         if ((access & NVX_NOUVEAU_BO_READ) &&
             nvx_bo_readmap_trace_count < 32U)
            NvxWglRecordNouveauPushbufItem(12U,
               nvx_bo_readmap_trace_count++,
               (uint32_t)bo->allocation.BufferId,
               (uint32_t)bo->last_fence, 0U, bo->allocation.Bytes,
               access, 0U);
#endif
         free(bo->shadow);
         bo->shadow = NULL;
         return NULL;
      }
#if defined(NVX_XP_WIN32_SHIMS) && \
    (defined(_WIN32) || defined(_WIN64))
      if ((access & NVX_NOUVEAU_BO_READ) &&
          nvx_bo_readmap_trace_count < 32U) {
         uint32_t first_dword = 0;
         uint32_t nonblack_words = 0;
         uint32_t offset;
         if (bo->allocation.Bytes >= sizeof(first_dword))
            memcpy(&first_dword, bo->shadow, sizeof(first_dword));
         /* Color BOs often retain opaque black as 0xff000000. Count words
          * with any RGB bits set to distinguish a successful zero image
          * readback from a rendered target. */
         for (offset = 0; offset + sizeof(uint32_t) <= bo->allocation.Bytes;
              offset += sizeof(uint32_t)) {
            uint32_t pixel;
            memcpy(&pixel, bo->shadow + offset, sizeof(pixel));
            if (pixel & 0x00ffffffU)
               nonblack_words++;
         }
         NvxWglRecordNouveauPushbufItem(12U,
            nvx_bo_readmap_trace_count++,
            (uint32_t)bo->allocation.BufferId,
            (uint32_t)bo->last_fence, 1U, bo->allocation.Bytes,
            nonblack_words, first_dword);
      }
#endif
   } else {
      memset(bo->shadow, 0, bo->allocation.Bytes);
   }

   bo->shadow_base = (uint8_t *)malloc(bo->allocation.Bytes);
   if (!bo->shadow_base) {
      free(bo->shadow);
      bo->shadow = NULL;
      return NULL;
   }
   memcpy(bo->shadow_base, bo->shadow, bo->allocation.Bytes);
   bo->shadow_base_valid = bo->contents_valid || bo->last_fence;
   bo->map_access = access;
   bo->map_count = 1;
   bo->shadow_fence = bo->last_fence;
   return bo->shadow;
}

/* Publish one packet, then advance the baseline only for acknowledged bytes. */
static int
nvx_nouveau_bo_publish_spans(NVX_NOUVEAU_BO *bo, NVX_3D_UPLOAD_SPANS *r,
                             uint32_t *uploaded_bytes,
                             uint32_t *upload_calls)
{
   uint32_t i, cursor=0;
   uint32_t bytes = r->DataBytes;
   const uint8_t *data=(const uint8_t *)(r+1);
   if (!NvxWinsysUploadSpans(bo->device, r, bo->allocation.Bytes))
      return 0;
   for (i=0; i<r->Count; i++) {
      memcpy(bo->shadow_base+r->Spans[i].Offset, data+cursor,
             r->Spans[i].Bytes);
      cursor+=r->Spans[i].Bytes;
   }
   if (uploaded_bytes)
      *uploaded_bytes += bytes;
   if (upload_calls)
      (*upload_calls)++;
   bo->contents_valid=1;
   r->Count=0;
   r->DataBytes=0;
   return 1;
}

static int
nvx_nouveau_bo_bytes_equal(const uint8_t *a, const uint8_t *b,
                           uint32_t bytes, uint32_t *scanned_bytes)
{
   if (scanned_bytes)
      *scanned_bytes += bytes;
   return memcmp(a, b, bytes) == 0;
}

static int
nvx_nouveau_bo_flush_range(NVX_NOUVEAU_BO *bo, uint32_t offset,
                           uint32_t end)
{
   NVX_3D_UPLOAD_SPANS *r=NULL;
   int waited=0, ok=0;
   uint32_t scanned_bytes=0, uploaded_bytes=0, upload_calls=0;
   if (!bo || !bo->references)
      return 0;
   if (bo->direct_mapped) {
      __sync_synchronize();
      return 1;
   }
   if (!bo->map_count && !bo->shadow)
      return 1;
   if (!bo->shadow || !bo->shadow_base ||
       end>bo->allocation.Bytes || offset>end || (offset&3U) || (end&3U))
      return 0;
   while (offset<end) {
      uint32_t start=offset, bytes;
      if (bo->shadow_base_valid && !(offset&4095U) &&
          end-offset>=4096U) {
         if (nvx_nouveau_bo_bytes_equal(bo->shadow+offset,
                                        bo->shadow_base+offset, 4096U,
                                        &scanned_bytes)) {
            offset+=4096U;
            continue;
         }
      }
      if (bo->shadow_base_valid) {
         if (nvx_nouveau_bo_bytes_equal(bo->shadow+offset,
                                       bo->shadow_base+offset, 4U,
                                       &scanned_bytes)) {
            offset+=4U;
            continue;
         }
      }
      if (!r) {
         r=(NVX_3D_UPLOAD_SPANS *)malloc(sizeof(*r)+NVX_3D_MAX_UPLOAD_BYTES);
         if (!r) goto done;
         memset(r,0,sizeof(*r));
         r->Size=sizeof(*r);
         r->Version=NVX_3D_ABI_VERSION;
         r->BufferId=bo->allocation.BufferId;
         r->ClientToken=bo->allocation.ClientToken;
      }
      do {
         offset+=4U;
      } while (offset<end &&
               offset-start<NVX_3D_MAX_UPLOAD_BYTES-r->DataBytes &&
               (!bo->shadow_base_valid ||
                !nvx_nouveau_bo_bytes_equal(bo->shadow+offset,
                                            bo->shadow_base+offset, 4U,
                                            &scanned_bytes)));
      bytes=offset-start;
      r->Spans[r->Count].Offset=start;
      r->Spans[r->Count++].Bytes=bytes;
      memcpy((uint8_t *)(r+1)+r->DataBytes, bo->shadow+start, bytes);
      r->DataBytes+=bytes;
      if (!waited && bo->last_fence &&
          !NvxWinsysWaitFence(bo->device, bo->last_fence, 5000))
         goto done;
      waited=1;
      if (r->Count==NVX_3D_MAX_UPLOAD_SPANS ||
          r->DataBytes==NVX_3D_MAX_UPLOAD_BYTES)
         if (!nvx_nouveau_bo_publish_spans(bo,r,&uploaded_bytes,
                                           &upload_calls)) goto done;
   }
   if (r && r->Count &&
       !nvx_nouveau_bo_publish_spans(bo,r,&uploaded_bytes,&upload_calls))
      goto done;
   if (r) bo->shadow_base_valid=1U;
   /* Untouched GPU-written bytes still require an actual readback. */
   ok=1;
done:
   free(r);
#if defined(NVX_XP_WIN32_SHIMS) && \
    (defined(_WIN32) || defined(_WIN64))
   NvxWglRecordNouveauShadowProfile(scanned_bytes, uploaded_bytes,
                                    upload_calls);
#endif
   return ok;
}

int
NvxNouveauBoFlush(NVX_NOUVEAU_BO *bo)
{
   if (!bo || !bo->references)
      return 0;
   return nvx_nouveau_bo_flush_range(bo, 0U, bo->allocation.Bytes);
}

int
NvxNouveauBoFlushRange(NVX_NOUVEAU_BO *bo, uint64_t offset, uint64_t bytes)
{
   if (!bo || !bo->references || !bo->map_count || !bo->shadow || !bytes ||
       offset > bo->allocation.Bytes ||
       bytes > (uint64_t)bo->allocation.Bytes - offset ||
       offset > UINT32_MAX || bytes > UINT32_MAX || (offset & 3U) ||
       (bytes & 3U))
      return 0;
   /* The caller supplies the bytes that its upcoming GPU operation consumes.
    * Upload only changes in this span here; the batch's common BO flush pass
    * still publishes any other dirty ranges before submission. This mirrors
    * Nouveau's range-based pushbuf validation and avoids scanning a large
    * vertex BO once for every vertex-array address. */
   return nvx_nouveau_bo_flush_range(bo, (uint32_t)offset,
                                     (uint32_t)(offset + bytes));
}

int
NvxNouveauBoFlushDirty(NVX_NOUVEAU_BO *bo)
{
   uint32_t offset, end;

   if (!bo || !bo->references)
      return 0;
   if (!bo->dirty)
      return NvxNouveauBoFlush(bo);

   offset = bo->dirty_offset;
   end = bo->dirty_end;
   if (offset >= end || end > bo->allocation.Bytes || (offset & 3U) ||
       (end & 3U))
      return 0;
   if (!nvx_nouveau_bo_flush_range(bo, offset, end))
      return 0;

   bo->dirty = 0;
   bo->dirty_offset = 0;
   bo->dirty_end = 0;
   return 1;
}

int
NvxNouveauBoUnmap(NVX_NOUVEAU_BO *bo)
{
   int result = 1;

   if (!bo || !bo->map_count || !bo->shadow)
      return 0;
   if (bo->map_count > 1U) {
      bo->map_count--;
      return 1;
   }
   if (bo->direct_mapped) {
      result = NvxNouveauBoFlushDirty(bo);
      if (!result)
         return 0;
      bo->map_count = 0;
      bo->map_access = 0;
      return 1;
   }
   result = NvxNouveauBoFlushDirty(bo);
   if (!result) return 0;
   bo->map_count = 0;

   free(bo->shadow);
   free(bo->shadow_base);
   bo->shadow_base = NULL;
   bo->shadow = NULL;
   bo->map_access = 0;
   return result;
}

int
NvxNouveauBoDestroy(NVX_NOUVEAU_BO *bo)
{
   if (!bo || !bo->references)
      return 0;
   if (--bo->references)
      return 1;

   if (bo->map_count) {
      bo->references = 1;
      return 0;
   }
   if (bo->last_fence &&
       !NvxWinsysWaitFence(bo->device, bo->last_fence, 5000)) {
      bo->references = 1;
      return 0;
   }
   if (bo->shadow && !bo->direct_mapped) {
      free(bo->shadow);
      bo->shadow = NULL;
   }
   if (s_nvx_bo_pool_enabled) {
      if (bo->allocation.Usage == NVX_3D_USAGE_GART_BUFFER) {
         if (s_nvx_gart_pool_count < NVX_GART_BO_POOL_CAPACITY) {
            s_nvx_gart_pool[s_nvx_gart_pool_count].device = bo->device;
            s_nvx_gart_pool[s_nvx_gart_pool_count].allocation = bo->allocation;
            s_nvx_gart_pool_count++;
            memset(bo, 0, sizeof(*bo));
            return 1;
         }
      } else if (s_nvx_vram_pool_count < NVX_VRAM_BO_POOL_CAPACITY) {
         /* Recycle VRAM buffer */
         s_nvx_vram_pool[s_nvx_vram_pool_count].device = bo->device;
         s_nvx_vram_pool[s_nvx_vram_pool_count].allocation = bo->allocation;
         s_nvx_vram_pool_count++;
         memset(bo, 0, sizeof(*bo));
         return 1;
      }
   }
   if (!NvxWinsysFreeBuffer(bo->device, bo->allocation.BufferId)) {
      bo->references = 1;
      return 0;
   }
   memset(bo, 0, sizeof(*bo));
   return 1;
}

void
NvxNouveauBoPoolFlush(NVX_WINSYS_DEVICE *device)
{
   uint32_t i = 0;
   while (i < s_nvx_gart_pool_count) {
      if (!device || s_nvx_gart_pool[i].device == device) {
         (void)NvxWinsysFreeBuffer(s_nvx_gart_pool[i].device,
                                  s_nvx_gart_pool[i].allocation.BufferId);
         s_nvx_gart_pool[i] = s_nvx_gart_pool[s_nvx_gart_pool_count - 1];
         s_nvx_gart_pool_count--;
      } else {
         i++;
      }
   }
   i = 0;
   while (i < s_nvx_vram_pool_count) {
      if (!device || s_nvx_vram_pool[i].device == device) {
         (void)NvxWinsysFreeBuffer(s_nvx_vram_pool[i].device,
                                  s_nvx_vram_pool[i].allocation.BufferId);
         s_nvx_vram_pool[i] = s_nvx_vram_pool[s_nvx_vram_pool_count - 1];
         s_nvx_vram_pool_count--;
      } else {
         i++;
      }
   }
}
