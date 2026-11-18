#include "nvx_winsys.h"

/* Fence values exposed by the NVX ABI are 32-bit. Reserve UINT64_MAX as a
 * local marker for a staging BO whose submission failed ambiguously: the
 * kernel may have queued it before a later fence wait timed out. */
#define NVX_WINSYS_FENCE_QUARANTINED UINT64_MAX
#include "nvx_nouveau_bo.h"

#include <stdlib.h>
#include <string.h>

#if defined(NVX_XP_WIN32_SHIMS)
extern uint64_t nvx_wgl_present_timer(void);
extern uint32_t nvx_wgl_present_elapsed_us(uint64_t start);
extern void NvxWglRecordNouveauWinsysProfile(uint32_t total_us,
                                             uint32_t ring_wait_us,
                                             uint32_t escape_us);
#define NVX_PROFILE_TIMER() nvx_wgl_present_timer()
#define NVX_PROFILE_ELAPSED(start) nvx_wgl_present_elapsed_us(start)
#define NVX_RECORD_WINSYS_PROFILE(total, wait, escape) \
   NvxWglRecordNouveauWinsysProfile(total, wait, escape)
#else
#define NVX_PROFILE_TIMER() 0U
#define NVX_PROFILE_ELAPSED(start) ((void)(start), 0U)
#define NVX_RECORD_WINSYS_PROFILE(total, wait, escape) \
   ((void)(total), (void)(wait), (void)(escape))
#endif

/* NVX_WINSYS_DEVICE values are copied into the xf86drm registry and the
 * WGL Nouveau adapter. The kernel scopes AllocationToken by ClientToken, so
 * a counter stored in each copy can collide for different BO requests. */
static volatile uint32_t nvx_global_allocation_token;

#if defined(NVX_XP_WIN32_SHIMS) && \
    (defined(_WIN32) || defined(_WIN64))
void NvxWglRecordNouveauPushbufItem(uint32_t kind, uint32_t index,
                                   uint32_t a, uint32_t b, uint32_t c,
                                   uint32_t d, uint32_t e, uint32_t f);
static volatile uint32_t nvx_alloc_failure_trace_count;
static void
nvx_winsys_trace_alloc_failure(uint32_t stage,
                               const NVX_3D_ALLOC_BUFFER *request,
                               const NVX_3D_ALLOC_BUFFER *output)
{
   if (__sync_add_and_fetch(&nvx_alloc_failure_trace_count, 1U) > 32U)
      return;
   NvxWglRecordNouveauPushbufItem(24U, stage,
      request ? request->Usage : 0U,
      request ? request->Bytes : 0U,
      request ? request->Alignment : 0U,
      output ? output->BufferId : 0U,
      output ? (uint32_t)output->GpuVa : 0U,
      output ? output->Version : 0U);
}
#endif

static int
nvx_winsys_ready(const NVX_WINSYS_DEVICE *device)
{
   return device && device->escape && device->opaque;
}

int
NvxWinsysInit(NVX_WINSYS_DEVICE *device, NVX_WINSYS_ESCAPE escape,
              void *opaque)
{
   if (!device || !escape || !opaque)
      return 0;

   memset(device, 0, sizeof(*device));
   device->escape = escape;
   device->opaque = opaque;
   device->caps.Size = sizeof(device->caps);
   device->caps.Version = NVX_3D_ABI_VERSION;
   if (!escape(opaque, NVX_ESCAPE_3D_QUERY_CAPS, 0, NULL,
               sizeof(device->caps), &device->caps) ||
       device->caps.Size != sizeof(device->caps) ||
       device->caps.Version != NVX_3D_ABI_VERSION ||
       !(device->caps.Features & NVX_3D_FEATURE_USER_ALLOC) ||
       !(device->caps.Features & NVX_3D_FEATURE_UPLOAD)) {
      memset(device, 0, sizeof(*device));
      return 0;
   }
   {
      NVX_3D_CLIENT_SESSION session;
      memset(&session, 0, sizeof(session));
      session.Size = sizeof(session);
      session.Version = NVX_3D_ABI_VERSION;
      session.Operation = NVX_3D_CLIENT_OPEN;
      if (!escape(opaque, NVX_ESCAPE_3D_CLIENT_OPEN, sizeof(session),
                  &session, sizeof(session), &session) ||
          !Nvx3dValidateClientSession(&session, NVX_3D_CLIENT_OPEN) ||
          !session.ClientToken) {
         memset(device, 0, sizeof(*device));
         return 0;
      }
      device->ClientToken = session.ClientToken;
   }
   return 1;
}

int
NvxWinsysOpenNouveauChannel(NVX_WINSYS_DEVICE *device)
{
   NVX_3D_CHANNEL_LEASE request, result;
   if (!nvx_winsys_ready(device) || !device->ClientToken ||
       !(device->caps.Features & NVX_3D_FEATURE_GR_CHANNEL))
      return 0;
   if (device->NouveauChannelHandle && device->NouveauGraphicsObjectHandle &&
       device->NouveauComputeObjectHandle)
      return 1;
   memset(&request, 0, sizeof(request));
   request.Size = sizeof(request);
   request.Version = NVX_3D_ABI_VERSION;
   request.Operation = NVX_3D_CHANNEL_OPEN;
   request.ClientToken = device->ClientToken;
   memset(&result, 0, sizeof(result));
   if (!Nvx3dValidateChannelLease(&request, NVX_3D_CHANNEL_OPEN) ||
       !device->escape(device->opaque, NVX_ESCAPE_3D_CHANNEL_LEASE,
                       sizeof(request), &request, sizeof(result), &result) ||
       !Nvx3dValidateChannelLease(&request, NVX_3D_CHANNEL_OPEN) ||
       result.Size != sizeof(result) ||
       result.Version != NVX_3D_ABI_VERSION ||
       result.Operation != NVX_3D_CHANNEL_OPEN || result.Reserved ||
       result.ClientToken != device->ClientToken ||
       !result.ChannelHandle || !result.GraphicsObjectHandle ||
       !result.ComputeObjectHandle || !result.GraphicsClassEngineId ||
       !result.ComputeClassEngineId || !result.M2mfClassEngineId ||
       !result.CopyClassEngineId || !result.TwoDClassEngineId)
      return 0;
   device->NouveauChannelHandle = result.ChannelHandle;
   device->NouveauGraphicsObjectHandle = result.GraphicsObjectHandle;
   device->NouveauComputeObjectHandle = result.ComputeObjectHandle;
   device->NouveauObjectBindings[0].Subchannel = 0U;
   device->NouveauObjectBindings[0].UserClass = 0x0000c797U;
   device->NouveauObjectBindings[0].ClassEngineId =
      result.GraphicsClassEngineId;
   device->NouveauObjectBindings[1].Subchannel = 1U;
   device->NouveauObjectBindings[1].UserClass = 0x0000c7c0U;
   device->NouveauObjectBindings[1].ClassEngineId =
      result.ComputeClassEngineId;
   device->NouveauObjectBindings[2].Subchannel = 2U;
   device->NouveauObjectBindings[2].UserClass = 0x0000a140U;
   device->NouveauObjectBindings[2].ClassEngineId =
      result.M2mfClassEngineId;
   device->NouveauObjectBindings[3].Subchannel = 3U;
   device->NouveauObjectBindings[3].UserClass = 0x0000902dU;
   device->NouveauObjectBindings[3].ClassEngineId =
      result.TwoDClassEngineId;
   device->NouveauObjectBindings[4].Subchannel = 4U;
   device->NouveauObjectBindings[4].UserClass = 0x0000c7b5U;
   device->NouveauObjectBindings[4].ClassEngineId =
      result.CopyClassEngineId;
   device->NouveauObjectBindingCount = 5U;
   return 1;
}

int
NvxWinsysCloseNouveauChannel(NVX_WINSYS_DEVICE *device)
{
   NVX_3D_CHANNEL_LEASE request, result;
   uint32_t i;
   int had_channel;
   if (!nvx_winsys_ready(device) || !device->ClientToken)
      return 0;
   had_channel = device->NouveauChannelHandle ||
                 device->NouveauGraphicsObjectHandle ||
                 device->NouveauComputeObjectHandle;
   if (had_channel) {
      if (!device->NouveauChannelHandle ||
          !device->NouveauGraphicsObjectHandle ||
          !device->NouveauComputeObjectHandle)
         return 0;
      memset(&request, 0, sizeof(request));
      request.Size = sizeof(request);
      request.Version = NVX_3D_ABI_VERSION;
      request.Operation = NVX_3D_CHANNEL_CLOSE;
      request.ClientToken = device->ClientToken;
      request.ChannelHandle = device->NouveauChannelHandle;
      request.GraphicsObjectHandle = device->NouveauGraphicsObjectHandle;
      request.ComputeObjectHandle = device->NouveauComputeObjectHandle;
      request.GraphicsClassEngineId =
         device->NouveauObjectBindings[0].ClassEngineId;
      request.ComputeClassEngineId =
         device->NouveauObjectBindings[1].ClassEngineId;
      request.M2mfClassEngineId =
         device->NouveauObjectBindings[2].ClassEngineId;
      request.TwoDClassEngineId =
         device->NouveauObjectBindings[3].ClassEngineId;
      request.CopyClassEngineId =
         device->NouveauObjectBindings[4].ClassEngineId;
      memset(&result, 0, sizeof(result));
      if (!device->escape(device->opaque, NVX_ESCAPE_3D_CHANNEL_LEASE,
                          sizeof(request), &request, sizeof(result), &result) ||
          result.Size != sizeof(result) ||
          result.Version != NVX_3D_ABI_VERSION ||
          result.Operation != NVX_3D_CHANNEL_CLOSE || result.Reserved ||
          result.ClientToken != device->ClientToken ||
          result.ChannelHandle != device->NouveauChannelHandle ||
          result.GraphicsObjectHandle != device->NouveauGraphicsObjectHandle ||
          result.ComputeObjectHandle != device->NouveauComputeObjectHandle ||
          result.GraphicsClassEngineId != request.GraphicsClassEngineId ||
          result.ComputeClassEngineId != request.ComputeClassEngineId ||
          result.M2mfClassEngineId != request.M2mfClassEngineId ||
          result.CopyClassEngineId != request.CopyClassEngineId ||
          result.TwoDClassEngineId != request.TwoDClassEngineId)
         return 0;
      device->NouveauChannelHandle = 0;
      device->NouveauGraphicsObjectHandle = 0;
      device->NouveauComputeObjectHandle = 0;
      memset(device->NouveauObjectBindings, 0,
             sizeof(device->NouveauObjectBindings));
      device->NouveauObjectBindingCount = 0;
   }
   for (i = 0; i < 4; i++) {
      if (device->NouveauBatchPushBuffer[i].BufferId) {
         /* Closing the channel retires its in-flight BO pins. If the channel
          * was already closed, wait before freeing its last submitted slot. */
         if (!had_channel &&
             (device->NouveauBatchLastFence[i] ==
                 NVX_WINSYS_FENCE_QUARANTINED ||
              (device->NouveauBatchLastFence[i] &&
               !NvxWinsysWaitFence(device,
                                   device->NouveauBatchLastFence[i],
                                   5000U))))
            return 0;
         if (!NvxWinsysFreeBuffer(device,
                                  device->NouveauBatchPushBuffer[i].BufferId))
            return 0;
         memset(&device->NouveauBatchPushBuffer[i], 0,
                sizeof(device->NouveauBatchPushBuffer[i]));
      }
      device->NouveauBatchLastFence[i] = 0;
   }
   device->NouveauBatchPushIndex = 0;
   return 1;
}

int
NvxWinsysCloseClient(NVX_WINSYS_DEVICE *device)
{
   NVX_3D_CLIENT_SESSION session;
   int ok;
   if (!nvx_winsys_ready(device) || !device->ClientToken)
      return 0;
   NvxNouveauBoPoolFlush(device);
   if (!NvxWinsysCloseNouveauChannel(device))
      return 0;
   memset(&session, 0, sizeof(session));
   session.Size = sizeof(session);
   session.Version = NVX_3D_ABI_VERSION;
   session.Operation = NVX_3D_CLIENT_CLOSE;
   session.ClientToken = device->ClientToken;
   ok = device->escape(device->opaque, NVX_ESCAPE_3D_CLIENT_CLOSE,
                       sizeof(session), &session, 0, NULL);
   if (ok) {
      device->ClientToken = 0;
      if (device->release_transport) {
         device->release_transport(device->opaque);
         device->release_transport = NULL;
         device->opaque = NULL;
      }
   }
   return ok;
}

uint64_t
NvxWinsysNextRequestToken(NVX_WINSYS_DEVICE *device)
{
   if (!device || !device->ClientToken)
      return 0;
   if (++device->NextRequestToken == 0)
      ++device->NextRequestToken;
   return device->NextRequestToken;
}

int
NvxWinsysAllocBuffer(NVX_WINSYS_DEVICE *device,
                     const NVX_3D_ALLOC_BUFFER *request,
                     NVX_3D_ALLOC_BUFFER *result)
{
   NVX_3D_ALLOC_BUFFER input;
   NVX_3D_ALLOC_BUFFER output;

   if (!nvx_winsys_ready(device) || !request || !result ||
       !device->ClientToken || request->ClientToken != device->ClientToken ||
       !Nvx3dValidateAlloc(request) || !device->ClientToken) {
#if defined(NVX_XP_WIN32_SHIMS) && \
    (defined(_WIN32) || defined(_WIN64))
      nvx_winsys_trace_alloc_failure(3U, request, NULL);
#endif
      return 0;
   }

   input = *request;
   input.ClientToken = device->ClientToken;
   if (!input.Alignment)
      input.Alignment = NVX_3D_PAGE_BYTES;
   input.AllocationToken = __sync_add_and_fetch(&nvx_global_allocation_token, 1U);
   if (!input.AllocationToken)
      input.AllocationToken = __sync_add_and_fetch(&nvx_global_allocation_token, 1U);
   device->NextAllocationToken = input.AllocationToken;
   memset(&output, 0, sizeof(output));
   if (!device->escape(device->opaque, NVX_ESCAPE_3D_ALLOC,
                       sizeof(input), &input, sizeof(output), &output)) {
#if defined(NVX_XP_WIN32_SHIMS) && \
    (defined(_WIN32) || defined(_WIN64))
      nvx_winsys_trace_alloc_failure(4U, &input, &output);
#endif
      return 0;
   }
   if (output.Size != sizeof(output) ||
       output.Version != NVX_3D_ABI_VERSION ||
       output.BufferId == 0 || output.GpuVa == 0 ||
       output.Bytes != request->Bytes || output.Usage != request->Usage ||
       output.Width != request->Width || output.Height != request->Height ||
       output.PitchBytes != request->PitchBytes ||
       output.TileMode != request->TileMode ||
       output.MemoryKind != request->MemoryKind ||
       output.Alignment != input.Alignment ||
       output.ClientToken != input.ClientToken ||
       output.AllocationToken != input.AllocationToken) {
#if defined(NVX_XP_WIN32_SHIMS) && \
    (defined(_WIN32) || defined(_WIN64))
      nvx_winsys_trace_alloc_failure(5U, &input, &output);
#endif
      return 0;
   }

   *result = output;
   return 1;
}

int
NvxWinsysFreeBuffer(NVX_WINSYS_DEVICE *device, uint64_t buffer_id)
{
   NVX_3D_FREE_BUFFER request;

   if (!nvx_winsys_ready(device) || !buffer_id)
      return 0;
   memset(&request, 0, sizeof(request));
   request.Size = sizeof(request);
   request.Version = NVX_3D_ABI_VERSION;
   request.BufferId = buffer_id;
   request.ClientToken = device->ClientToken;
   if (!Nvx3dValidateFree(&request))
      return 0;
   return device->escape(device->opaque, NVX_ESCAPE_3D_FREE,
                         sizeof(request), &request, 0, NULL);
}

int
NvxWinsysUploadBuffer(NVX_WINSYS_DEVICE *device, uint64_t buffer_id,
                      uint32_t allocated_bytes, uint32_t offset,
                      const void *data, uint32_t bytes)
{
   NVX_3D_UPLOAD_BUFFER *request;
   uint32_t remaining = bytes;
   uint32_t current_offset = offset;
   uint32_t capacity;
   const uint8_t *source = (const uint8_t *)data;
   int ok = 0;

   if (!nvx_winsys_ready(device) || !buffer_id || !data || !bytes ||
       (offset & 3U) || (bytes & 3U) || offset > allocated_bytes ||
       bytes > allocated_bytes - offset)
      return 0;

   capacity = bytes < NVX_3D_MAX_UPLOAD_BYTES ?
      bytes : NVX_3D_MAX_UPLOAD_BYTES;
   request = (NVX_3D_UPLOAD_BUFFER *)malloc(sizeof(*request) + capacity);
   if (!request)
      return 0;

   while (remaining) {
      uint32_t chunk = remaining;
      uint32_t input_bytes;
      if (chunk > NVX_3D_MAX_UPLOAD_BYTES)
         chunk = NVX_3D_MAX_UPLOAD_BYTES;
      chunk &= ~3U;
      if (!chunk)
         goto done;

      memset(request, 0, sizeof(*request));
      request->Size = sizeof(*request);
      request->Version = NVX_3D_ABI_VERSION;
      request->BufferId = buffer_id;
      request->ClientToken = device->ClientToken;
      request->Offset = current_offset;
      request->Bytes = chunk;
      memcpy((uint8_t *)request + sizeof(*request), source, chunk);
      input_bytes = sizeof(*request) + chunk;
      if (!Nvx3dValidateUpload(request, input_bytes, allocated_bytes) ||
          !device->escape(device->opaque, NVX_ESCAPE_3D_UPLOAD,
                          input_bytes, request, 0, NULL))
         goto done;
      current_offset += chunk;
      source += chunk;
      remaining -= chunk;
   }
   ok = 1;

done:
   free(request);
   return ok;
}

int
NvxWinsysDownloadBuffer(NVX_WINSYS_DEVICE *device, uint64_t buffer_id,
                        uint32_t allocated_bytes, uint32_t offset,
                        void *data, uint32_t bytes)
{
   NVX_3D_DOWNLOAD_BUFFER request;
   uint8_t *destination = (uint8_t *)data;
   uint32_t remaining = bytes;
   uint32_t current_offset = offset;

   if (!nvx_winsys_ready(device) ||
       !(device->caps.Features & NVX_3D_FEATURE_DOWNLOAD) ||
       !buffer_id || !data || !bytes || (offset & 3U) || (bytes & 3U) ||
       offset > allocated_bytes || bytes > allocated_bytes - offset)
      return 0;

   while (remaining) {
      uint32_t chunk = remaining;
      if (chunk > NVX_3D_MAX_DOWNLOAD_BYTES)
         chunk = NVX_3D_MAX_DOWNLOAD_BYTES;
      chunk &= ~3U;
      if (!chunk)
         return 0;

      memset(&request, 0, sizeof(request));
      request.Size = sizeof(request);
      request.Version = NVX_3D_ABI_VERSION;
      request.BufferId = buffer_id;
      request.ClientToken = device->ClientToken;
      request.Offset = current_offset;
      request.Bytes = chunk;
      if (!Nvx3dValidateDownload(&request, sizeof(request), chunk,
                                 allocated_bytes) ||
          !device->escape(device->opaque, NVX_ESCAPE_3D_DOWNLOAD,
                          sizeof(request), &request, chunk, destination))
         return 0;
      current_offset += chunk;
      destination += chunk;
      remaining -= chunk;
   }
   return 1;
}

int
NvxWinsysWaitFence(NVX_WINSYS_DEVICE *device, uint64_t fence_value,
                   uint32_t timeout_ms)
{
   NVX_3D_WAIT_FENCE request;
   NVX_3D_WAIT_FENCE result;

   if (!nvx_winsys_ready(device) || !fence_value || timeout_ms > 30000U)
      return 0;
   memset(&request, 0, sizeof(request));
   request.Size = sizeof(request);
   request.Version = NVX_3D_ABI_VERSION;
   request.FenceValue = fence_value;
   request.TimeoutMs = timeout_ms;
   memset(&result, 0, sizeof(result));
   return device->escape(device->opaque, NVX_ESCAPE_3D_WAIT,
                         sizeof(request), &request, sizeof(result), &result) &&
          result.Size == sizeof(result) &&
          result.Version == NVX_3D_ABI_VERSION &&
          result.Status == NVX_3D_STATUS_SUCCESS &&
          result.FenceValue == fence_value;
}

int
NvxWinsysValidateBatch(NVX_WINSYS_DEVICE *device,
                       const void *batch, uint32_t batch_bytes)
{
   NVX_3D_BATCH_HEADER header;
   uint32_t required_bytes;

   if (!nvx_winsys_ready(device) || !batch ||
       batch_bytes < sizeof(header) ||
       batch_bytes > NVX_3D_MAX_BATCH_BYTES)
      return 0;

   memcpy(&header, batch, sizeof(header));
   if (!Nvx3dBatchRequiredBytes(&header, &required_bytes) ||
       required_bytes != batch_bytes)
      return 0;

   /* The kernel copies and validates this batch. This escape does not submit
    * commands and must not be treated as a pushbuffer kick. */
   return device->escape(device->opaque, NVX_ESCAPE_3D_VALIDATE_BATCH,
                         batch_bytes, batch, 0, NULL);
}

int
NvxWinsysSubmitBatch(NVX_WINSYS_DEVICE *device,
                     void *batch, uint32_t batch_bytes,
                     uint32_t batch_capacity,
                     uint64_t *fence_value)
{
   NVX_3D_BATCH_HEADER header;
   NVX_3D_BATCH_RESULT result;
   NVX_3D_ALLOC_BUFFER allocation, allocation_result;
   NVX_3D_BUFFER_REFERENCE push_reference;
   uint64_t required_bytes, references_bytes, dwords_bytes, relocations_bytes;
   uint64_t profile_start, ring_wait_start = 0, escape_start;
   uint32_t required;
   uint32_t ring_wait_us = 0, escape_us = 0;
   int escape_ok;

   profile_start = NVX_PROFILE_TIMER();

   if (fence_value)
      *fence_value = 0;
   if (!fence_value || !nvx_winsys_ready(device) || !batch ||
       !device->NouveauChannelHandle ||
       batch_bytes < sizeof(header) || batch_bytes > NVX_3D_MAX_BATCH_BYTES)
      return 0;
   uint32_t slot_idx = device->NouveauBatchPushIndex % 4U;
   if (!device->NouveauBatchPushBuffer[slot_idx].BufferId) {
      memset(&allocation, 0, sizeof(allocation));
      allocation.Size = sizeof(allocation);
      allocation.Version = NVX_3D_ABI_VERSION;
      allocation.Usage = NVX_3D_USAGE_GART_BUFFER;
      allocation.Bytes = 20U * 1024U;
      allocation.ClientToken = device->ClientToken;
      if (!NvxWinsysAllocBuffer(device, &allocation, &allocation_result))
         return 0;
      device->NouveauBatchPushBuffer[slot_idx] = allocation_result;
   } else if (device->NouveauBatchLastFence[slot_idx] ==
              NVX_WINSYS_FENCE_QUARANTINED) {
      /* The submit ioctl failed after it may have made this BO visible to the
       * GPU. Keep it allocated and never overwrite/recycle it this session. */
      return 0;
   } else if (device->NouveauBatchLastFence[slot_idx]) {
      ring_wait_start = NVX_PROFILE_TIMER();
      if (!NvxWinsysWaitFence(device,
                              device->NouveauBatchLastFence[slot_idx],
                              5000U)) {
         /* Never overwrite a pushbuffer BO while the GPU may still be reading
          * it. A timed-out prior submission must stop this submission path. */
         return 0;
      }
      ring_wait_us = NVX_PROFILE_ELAPSED(ring_wait_start);
   }
   memcpy(&header, batch, sizeof(header));
   if (header.BufferReferenceCount >= NVX_3D_MAX_BATCH_BUFFERS ||
       !Nvx3dBatchRequiredBytes(&header, &required) || required != batch_bytes)
      return 0;
   references_bytes=(uint64_t)header.BufferReferenceCount*
                    sizeof(NVX_3D_BUFFER_REFERENCE);
   dwords_bytes=(uint64_t)header.DwordCount*sizeof(uint32_t);
   relocations_bytes=(uint64_t)header.RelocationCount*sizeof(NVX_3D_RELOCATION);
   required_bytes=(uint64_t)batch_bytes+sizeof(push_reference);
   if (required_bytes > NVX_3D_MAX_BATCH_BYTES || required_bytes > UINT32_MAX ||
       required_bytes > batch_capacity)
      return 0;

   /* The caller owns a reusable max-size translation buffer. Insert the
    * staging BO reference in that buffer instead of allocating and copying
    * the entire batch for every kick. memmove handles the overlapping source
    * and destination ranges. */
   memmove((uint8_t *)batch + sizeof(header) + references_bytes +
              sizeof(push_reference),
           (uint8_t *)batch + sizeof(header) + references_bytes,
           (size_t)(dwords_bytes + relocations_bytes));
   header.BufferReferenceCount++;
   push_reference.BufferId=device->NouveauBatchPushBuffer[slot_idx].BufferId;
   push_reference.Access=NVX_3D_RELOC_WRITE;
   push_reference.Reserved=0;
   memcpy((uint8_t *)batch + sizeof(header) + references_bytes,
          &push_reference, sizeof(push_reference));
   memcpy(batch, &header, sizeof(header));
   memset(&result, 0, sizeof(result));
   escape_start = NVX_PROFILE_TIMER();
   escape_ok = device->escape(device->opaque, NVX_ESCAPE_3D_SUBMIT_BATCH,
                              (uint32_t)required_bytes, batch,
                              sizeof(result), &result);
   escape_us = NVX_PROFILE_ELAPSED(escape_start);
   if (!escape_ok ||
       result.Size != sizeof(result) || result.Version != NVX_3D_ABI_VERSION ||
       !result.FenceValue) {
      device->NouveauBatchLastFence[slot_idx] =
         NVX_WINSYS_FENCE_QUARANTINED;
      device->NouveauBatchPushIndex = (slot_idx + 1U) % 4U;
      return 0;
   }
   device->NouveauBatchLastFence[slot_idx] = result.FenceValue;
   device->NouveauBatchPushIndex = (slot_idx + 1U) % 4U;
   *fence_value = result.FenceValue;
   NVX_RECORD_WINSYS_PROFILE(NVX_PROFILE_ELAPSED(profile_start),
                             ring_wait_us, escape_us);
   return 1;
}
