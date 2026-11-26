/* Included by nvx_gsp.c: XP ownership adapter for OpenRM channel resources.
 * Uses the same ALLOC/CONTROL/FREE transport as boot, without borrowing its
 * USERD, FIFO, context backing, channel ID or render state. */

NVX_GR_CLIENT_CHANNEL *NvxGspFindClientChannel(PVOID extension,ULONG64 token)
{
    PNVX_DEVICE_EXTENSION dev=extension;
    if(!dev||!token) return NULL;
    for(ULONG i=0;i<NVX_3D_CLIENT_SESSION_SLOTS;i++)
        if(dev->Nvx3dClientSessions[i].ClientToken==token&&
           dev->Nvx3dClientSessions[i].Graphics.ClientToken==token)
            return &dev->Nvx3dClientSessions[i].Graphics;
    return NULL;
}

BOOLEAN NvxGspGetClientChannel(PVOID extension,NVX_3D_CHANNEL_LEASE *lease)
{
    NVX_GR_CLIENT_CHANNEL *c;
    if(!lease) return FALSE;
    c=NvxGspFindClientChannel(extension,lease->ClientToken);
    if(!c||!c->Ready||c->Faulted) return FALSE;
    lease->ChannelHandle=c->Channel;
    lease->GraphicsObjectHandle=c->Objects[0];
    lease->ComputeObjectHandle=c->Objects[1];
    lease->GraphicsClassEngineId=c->ClassIds[0];
    lease->ComputeClassEngineId=c->ClassIds[1];
    lease->M2mfClassEngineId=c->ClassIds[2];
    lease->TwoDClassEngineId=c->ClassIds[3];
    lease->CopyClassEngineId=c->ClassIds[4];
    return TRUE;
}

/* Caller owns NvxGrUserSubmitBusy. On any unacknowledged teardown, preserve
 * backing and handles: a failed FREE is not permission to recycle GPU memory. */
static BOOLEAN NvxGrDestroyClient(PNVX_DEVICE_EXTENSION dev,NVX_GR_CLIENT_CHANNEL *c)
{
    NVA06F_CTRL_STOP_CHANNEL_PARAMS stop={0};
    NV2080_CTRL_GPU_EVICT_CTX_PARAMS evict={0};
    /* Match GspRmFree: the object identifies its parent, so the wire parent
       is NV01_NULL_OBJECT, including frees of children of a TSG. */
    ULONG release[4]={dev->GspClientHandle,0,0,0};
    c->Ready=FALSE;
    if(c->Scheduled) {
        if(!NvxGspControl(dev,c->Channel,NVA06F_CTRL_CMD_STOP_CHANNEL,&stop,sizeof(stop)))
            goto unsafe;
        c->Scheduled=FALSE;
    }
    /* Match nvGpuOpsStopChannel: stop submission, then retire the promoted
       GR context before releasing its channel and backing memory. A host
       flag change does not remove firmware context bindings. */
    if(c->Promoted) {
        evict.engineType=NV2080_ENGINE_TYPE_GRAPHICS;
        evict.hClient=evict.hChanClient=dev->GspClientHandle;
        evict.hObject=c->Channel;
        if(!NvxGspControl(dev,dev->GspSubdeviceHandle,
                         NV2080_CTRL_CMD_GPU_EVICT_CTX,&evict,sizeof(evict)))
            goto unsafe;
        c->Promoted=FALSE;
    }
    if(c->Channel) {
        /* kchannelDestruct sends the channel FREE before freeing its group.
           Its acknowledgement retires the GSP mapping of the method buffer.
           Clear the handle only on success so failed cleanup is retryable. */
        release[2]=c->Channel;
        if(!NvxGspCall(dev,10,release,sizeof(release))) goto unsafe;
        LogStr("NVX [GR-CLIENT]: channel FREE acknowledged handle=");
        LogHex(c->Channel);LogStr(" chid=");LogHex(c->HardwareChannelId);
        LogStr("\n");
        c->Channel=0;
    }
    if(c->TsgAllocated) {
        release[2]=c->Tsg;
        if(!NvxGspCall(dev,10,release,sizeof(release))) goto unsafe;
        c->TsgAllocated=FALSE; /* Retain identity for a retry of BO unmap. */
    }
    if(c->NotifierAllocated) {
        release[2]=c->Notifier;
        if(!NvxGspCall(dev,10,release,sizeof(release))) goto unsafe;
        c->NotifierAllocated=FALSE;
    }
    if(c->MemoryAllocated) {
        release[2]=c->Memory;
        if(!NvxGspCall(dev,10,release,sizeof(release))) goto unsafe;
        c->MemoryAllocated=FALSE;
    }
    if(c->HardwareIdAllocated) {
        /* Drain any pending GSP notifications before releasing the ChID */
        (void)NvxGspGetName(dev);
        if(nvx_openrm_chid_free(dev->GspChannelIdHeap,&c->ClientToken,
                               c->HardwareChannelId)!=NV_OK) goto unsafe;
        c->HardwareIdAllocated=FALSE;
    }
    c->Faulted=FALSE;
    return TRUE;
unsafe:
    c->Faulted=TRUE;
    LogStr("NVX [GR-CLIENT]: teardown unconfirmed; retaining backing token=");
    LogHex(c->ClientToken);LogStr("\n");
    return FALSE;
}

BOOLEAN NvxGspCloseClientChannel(PVOID extension,ULONG64 token)
{
    PNVX_DEVICE_EXTENSION dev=extension;
    NVX_GR_CLIENT_CHANNEL *c;
    BOOLEAN ok;
    ULONG wait;
    if(!dev) return FALSE;
    for(wait=0;wait<20000U;wait++) {
        if(!__sync_lock_test_and_set(&NvxGrUserSubmitBusy,1)) break;
        VideoPortStallExecution(10U);
    }
    if(wait==20000U) return FALSE;
    c=NvxGspFindClientChannel(dev,token);
    ok=!c||NvxGrDestroyClient(dev,c);
    __sync_lock_release(&NvxGrUserSubmitBusy);
    return ok;
}

/* Bind only the class and fence the private queue. No boot draw state is
 * inherited. This also makes a 3D completion tail valid if Mesa's first batch
 * contains only auxiliary-engine setup before its own 3D SET_OBJECT. */
#if NVX_ENABLE_GR_CHANNEL_PROBE
/* Caller holds NvxGrUserSubmitBusy. Use CE to distinguish stale GART
 * translations from FIFO/context state, without modifying the failed queue. */
static VOID NvxGrCheckQueueMapping(PNVX_DEVICE_EXTENSION dev,
                                  NVX_GR_CLIENT_CHANNEL *c)
{
    const ULONG offsets[2]={0x9000,0xf000};
    const ULONG sizes[2]={8,28};
    PUCHAR readback=(PUCHAR)dev->GspDmaVirtual+GSP_SCRATCH_OFFSET+0xe000;
    if(!NvxGspLockEngine()) return;
    for(ULONG part=0;part<2;part++) {
        ULONG mismatch=0;
        BOOLEAN copied=NvxGspSubmitCopyUnlocked(dev,
            dev->GspScratchGpuVa+0xe000,c->GpuVa+offsets[part],
            sizes[part],sizes[part],sizes[part],1);
        __sync_synchronize();
        if(copied) for(ULONG i=0;i<sizes[part];i++)
            if(readback[i]!=((PUCHAR)c->Virtual)[offsets[part]+i]) mismatch++;
        LogStr("NVX [GR-CLIENT-MAP]: offset=");LogHex(offsets[part]);
        LogStr(" copy=");LogHex(copied);LogStr(" mismatches=");LogHex(mismatch);
        LogStr(" instancePA=");LogHex((ULONG64)c->Physical.QuadPart+0xa000);
        LogStr("\n");
        if(!copied) break;
    }
    NvxGspUnlockEngine();
}

static BOOLEAN NvxGrVerifyClientQueue(PNVX_DEVICE_EXTENSION dev,NVX_GR_CLIENT_CHANNEL *c)
{
    PUCHAR scratch=c->Virtual;
    PULONG push=(PULONG)(scratch+0xf000);
    volatile ULONG *fence=(volatile ULONG *)(scratch+0x5000);
    volatile ULONG *userd=(volatile ULONG *)(scratch+NVX_GR_CLIENT_USERD_OFFSET);
    volatile ULONG64 *fifo=(volatile ULONG64 *)(scratch+0x9000);
    ULONG64 va=c->GpuVa+0xf000,fenceVa=c->GpuVa+0x5000;
    fence[0]=0;
    push[0]=(NVC56F_DMA_INCR_OPCODE_VALUE<<29)|(1U<<16)|(NVC56F_SET_OBJECT>>2);
    push[1]=c->ClassIds[0];
    push[2]=(NVC56F_DMA_INCR_OPCODE_VALUE<<29)|(4U<<16)|(NVC797_SET_REPORT_SEMAPHORE_A>>2);
    push[3]=(ULONG)(fenceVa>>32);push[4]=(ULONG)fenceVa;push[5]=0x4752434cU;
    push[6]=DRF_DEF(C797,_SET_REPORT_SEMAPHORE_D,_OPERATION,_RELEASE)|
        DRF_DEF(C797,_SET_REPORT_SEMAPHORE_D,_RELEASE,_AFTER_ALL_PRECEEDING_WRITES_COMPLETE)|
        DRF_DEF(C797,_SET_REPORT_SEMAPHORE_D,_PIPELINE_LOCATION,_ALL)|
        DRF_DEF(C797,_SET_REPORT_SEMAPHORE_D,_STRUCTURE_SIZE,_ONE_WORD);
    fifo[0]=(va&0xfffffffcULL)|(((va>>32)&0xffULL)<<32)|(7ULL<<42);
    __sync_synchronize();
    c->Put=1;userd[0x8c/4]=1;
    __sync_synchronize();
    if(nvx_openrm_kick(dev->Bar0MmioVirtual,dev->Bar0Length,c->RunlistBase,c->HardwareChannelId,c->SubmitToken)!=NV_OK) {
        c->Faulted=TRUE;
        return FALSE;
    }
    for(ULONG wait=0;wait<50000U;wait++) {
        if(fence[0]==0x4752434cU) {
            __sync_synchronize();
            return NvxGspGetName(dev)&&!c->Faulted&&!dev->GspGraphicsDeviceFaulted&&!dev->GspMmuFaultQueued;
        }
        if(c->Faulted||dev->GspGraphicsDeviceFaulted||dev->GspMmuFaultQueued) break;
        VideoPortStallExecution(100U);
    }
    LogStr("NVX [GR-CLIENT]: queue verification timeout chid=");LogHex(c->HardwareChannelId);
    LogStr(" USERD_GET=");LogHex(userd[0x88/4]);
    LogStr(" USERD_PUT=");LogHex(userd[0x8c/4]);
    LogStr(" FIFO0=");LogHex(fifo[0]);
    LogStr(" RAMFC_CHID=");LogHex(((volatile ULONG *)(scratch+0xa000))[0xe8/4]);
    LogStr("\n");
    /* A userd GPGet may have been retained in the PBDMA diagnostics without
       a firmware RC notification. Capture channel fetch/interrupt state
       before STOP_CHANNEL and FREE erase the evidence. */
    NvxGspLogPbdmaFault(dev);
    NvxGrCheckQueueMapping(dev,c);
    if(c->RunlistBase&&dev->Bar0Length>=c->RunlistBase+0x10U) {
        ULONG cfg=VideoPortReadRegisterUlong((PULONG)
            ((PUCHAR)dev->Bar0MmioVirtual+c->RunlistBase+4U));
        ULONG chram=(cfg&0xfffffff0U)+c->HardwareChannelId*4U;
        if(chram<=dev->Bar0Length-4U) {
            ULONG state=VideoPortReadRegisterUlong((PULONG)
                ((PUCHAR)dev->Bar0MmioVirtual+chram));
            LogStr("NVX [GR-CLIENT]: CHRAM config=");LogHex(cfg);
            LogStr(" state=");LogHex(state);LogStr("\n");
        }
    }
    (void)NvxGspGetName(dev); /* Drain delayed firmware errors before teardown. */
    c->Faulted=TRUE;
    return FALSE;
}

#endif

BOOLEAN NvxGspOpenClientChannel(PVOID extension,NVX_3D_CHANNEL_LEASE *lease,
    PVOID backing,PHYSICAL_ADDRESS physical,ULONG64 gpuVa,ULONG64 bufferId)
{
#if NVX_ENABLE_GR_CHANNEL_PROBE
    PNVX_DEVICE_EXTENSION dev=extension;
    NVX_GR_CLIENT_CHANNEL *c=NULL;
    NV_CHANNEL_ALLOC_PARAMS chan={0};
    NV_CONTEXT_DMA_ALLOCATION_PARAMS dma={0};
    NV_CHANNEL_GROUP_ALLOCATION_PARAMETERS tsg={0};
    NV_CTXSHARE_ALLOCATION_PARAMETERS share={0};
    NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS *promote;
    const char *stage="preconditions";
    ULONG registration[46]={0};
    NVA06F_CTRL_BIND_PARAMS bind={NV2080_ENGINE_TYPE_GRAPHICS};
    ULONG slot,base,doorbellConfig,classes[5]={AMPERE_B,
        NVX_RM_CLASS_AMPERE_COMPUTE_B,NVX_RM_CLASS_KEPLER_I2M_B,
        NVX_RM_CLASS_FERMI_2D_A,NVX_RM_CLASS_AMPERE_DMA_COPY_B};
    NvU32 expectedToken,channelId;
    NVA06F_CTRL_GPFIFO_SCHEDULE_PARAMS schedule={NV_TRUE,NV_FALSE};
    BOOLEAN ok=FALSE;
    if(!dev||!lease||!backing||!gpuVa||!bufferId||!dev->GspGrPromotionAccepted||
       !dev->GspClientGrLayout.count||!dev->GspGartReady||
       dev->GspGraphicsDeviceFaulted||dev->GspMmuFaultQueued||
       __sync_lock_test_and_set(&NvxGrUserSubmitBusy,1)) return FALSE;
    for(slot=0;slot<NVX_3D_CLIENT_SESSION_SLOTS;slot++)
        if(dev->Nvx3dClientSessions[slot].ClientToken==lease->ClientToken) break;
    if(slot==NVX_3D_CLIENT_SESSION_SLOTS) goto done;
    c=&dev->Nvx3dClientSessions[slot].Graphics;
    if(c->ClientToken) goto done; /* Never replace an existing live context. */
    VideoPortZeroMemory(c,sizeof(*c));
    c->ClientToken=lease->ClientToken;c->Virtual=backing;c->Physical=physical;
    c->GpuVa=gpuVa;c->BackingBufferId=bufferId;
    /* Handles are unique in the RM client namespace, including child objects. */
    base=0x61000000U+slot*0x100U;
    c->Memory=base+1;c->Notifier=base+2;c->Tsg=base+3;c->ContextShare=base+4;
    registration[0]=dev->GspClientHandle;registration[1]=dev->GspDeviceHandle;
    registration[2]=c->Memory;registration[3]=0x81;
    registration[4]=0x40000010U;registration[8]=0x10000U;
    registration[10]=16;registration[12]=16U<<16;
    for(ULONG i=0;i<16;i++) {
        ULONG64 page=((ULONG64)physical.QuadPart>>12)+i;
        registration[14+i*2]=(ULONG)page;registration[15+i*2]=(ULONG)(page>>32);
    }
    stage="scratch registration";
    if(!NvxGspCall(dev,4,registration,sizeof(registration))) goto fail;
    c->MemoryAllocated=TRUE;
    dma.flags=DRF_DEF(OS03,_FLAGS,_MAPPING,_KERNEL)|DRF_DEF(OS03,_FLAGS,_HASH_TABLE,_DISABLE);
    dma.hMemory=c->Memory;dma.offset=0xe000;dma.limit=0xfff;
    stage="notifier";
    if(!NvxGspAlloc(dev,dev->GspDeviceHandle,c->Notifier,NV01_CONTEXT_DMA,&dma,sizeof(dma))) goto fail;
    c->NotifierAllocated=TRUE;
    tsg.hVASpace=dev->GspVaSpaceHandle;tsg.engineType=NV2080_ENGINE_TYPE_GRAPHICS;
    stage="TSG";
    if(!NvxGspAlloc(dev,dev->GspDeviceHandle,c->Tsg,KEPLER_CHANNEL_GROUP_A,&tsg,sizeof(tsg))) goto fail;
    c->TsgAllocated=TRUE;
    share.hVASpace=dev->GspVaSpaceHandle;
    share.flags=NV_CTXSHARE_ALLOCATION_FLAGS_SUBCONTEXT_SYNC;
    stage="context share";
    if(!NvxGspAlloc(dev,c->Tsg,c->ContextShare,FERMI_CONTEXT_SHARE_A,&share,sizeof(share))) goto fail;
    /* Direct GSP allocation needs the explicit context object, as does the
     * verified boot setup; full host RM normally creates it in the TSG path. */
    stage="kernel graphics context";
    if(!NvxGspAlloc(dev,c->Tsg,base+6,KERNEL_GRAPHICS_CONTEXT,NULL,0)) goto fail;
    chan.gpFifoOffset=gpuVa+0x9000;chan.gpFifoEntries=512;
    /* The actual upstream allocator chooses the ChID and enforces USERD
     * owner isolation. This adapter supplies OS ownership, not page arithmetic. */
    stage="upstream channel-ID allocation";
    if(!dev->GspChannelIdHeap&&
       nvx_openrm_chid_heap_create(&dev->GspChannelIdHeap)!=NV_OK) goto fail;
    if(nvx_openrm_chid_alloc(dev->GspChannelIdHeap,&c->ClientToken,
            NVX_GR_CLIENT_USERD_OFFSET,&channelId)!=NV_OK) goto fail;
    c->HardwareChannelId=channelId;c->HardwareIdAllocated=TRUE;
    if(!nvx_openrm_channel_identity(channelId,0,&chan.flags,&expectedToken)) goto fail;
    chan.hContextShare=c->ContextShare;chan.engineType=NV2080_ENGINE_TYPE_GRAPHICS;
    chan.hObjectError=c->Notifier;chan.hUserdMemory[0]=c->Memory;
    chan.userdOffset[0]=NVX_GR_CLIENT_USERD_OFFSET;chan.internalFlags=nvx_openrm_channel_internal_flags();
    stage="channel memory descriptors";
    if(!nvx_openrm_channel_memory(&chan,(NvU64)physical.QuadPart+0xa000,
        (NvU64)physical.QuadPart+NVX_GR_CLIENT_USERD_OFFSET,
        (NvU64)physical.QuadPart+0xc000,(NvU64)physical.QuadPart+0xe000)) goto fail;
    /* Explicitly zero USERD, RAMFC, GPFIFO, and pushbuffer scratch regions
       before RM publishes descriptors and the hardware channel is scheduled. */
    VideoPortZeroMemory((PUCHAR)backing+0x9000,0x7000);
    __sync_synchronize();
    stage="GPFIFO";
    if(!NvxGspAlloc(dev,c->Tsg,base+5,AMPERE_CHANNEL_GPFIFO_A,&chan,sizeof(chan))) goto fail;
    c->Channel=base+5;
    stage="engine objects";
    for(ULONG i=0;i<5;i++) {
        ULONG object=base+0x10U+i;
        if(!NvxGspAlloc(dev,c->Channel,object,classes[i],NULL,0)) goto fail;
        c->Objects[i]=object;
        if(!NvxGspGetClassEngineId(dev,c->Channel,object,&c->ClassIds[i])) goto fail;
    }
    stage="bind";
    if(!NvxGspControl(dev,c->Channel,NVA06F_CTRL_CMD_BIND,&bind,sizeof(bind))) goto fail;
    /* Upstream selects which buffers need physical initialization. All global
     * IDs use the boot backing, already initialized before client admission. */
    promote=(NV2080_CTRL_GPU_PROMOTE_CTX_PARAMS *)&dev->GrContextInfoParams;
    VideoPortZeroMemory(promote,sizeof(*promote));
    promote->engineType=NV2080_ENGINE_TYPE_GRAPHICS;
    promote->hChanClient=dev->GspClientHandle;promote->hObject=c->Channel;
    for(ULONG i=0;i<dev->GspClientGrLayout.count;i++) {
        const NVX_GR_BUFFER *b=&dev->GspClientGrLayout.buffers[i];
        NvBool global=nvx_openrm_context_is_global(b->id),add;
        NvU64 physicalBase=global?(NvU64)dev->GspGrContextPhysical.QuadPart:
            (NvU64)physical.QuadPart+NVX_GR_CLIENT_CONTEXT_OFFSET;
        if(nvx_openrm_context_init_entry(b->id,physicalBase+b->offset,b->size,
            global,&promote->promoteEntry[promote->entryCount],&add)!=NV_OK) goto fail;
        if(add) promote->entryCount++;
    }
    stage="physical context initialization";
    __sync_synchronize();
    if(!NvxGspControl(dev,dev->GspSubdeviceHandle,NV2080_CTRL_CMD_GPU_PROMOTE_CTX,promote,sizeof(*promote))) goto fail;
    VideoPortZeroMemory(promote,sizeof(*promote));
    promote->engineType=NV2080_ENGINE_TYPE_GRAPHICS;
    promote->hChanClient=dev->GspClientHandle;promote->hObject=c->Channel;
    promote->entryCount=dev->GspClientGrLayout.count;
    for(ULONG i=0;i<dev->GspClientGrLayout.count;i++) {
        const NVX_GR_BUFFER *b=&dev->GspClientGrLayout.buffers[i];
        promote->promoteEntry[i].bufferId=(NvU16)b->id;
        promote->promoteEntry[i].gpuVirtAddr=(nvx_openrm_context_is_global(b->id)?GSP_GR_CONTEXT_GPU_VA:
            gpuVa+NVX_GR_CLIENT_CONTEXT_OFFSET)+b->offset;
    }
    /* A failed promote may have installed a subset of bindings. Evict before
     * releasing backing even when the control returns failure. */
    stage="context VA binding";
    c->Promoted=TRUE;
    if(!NvxGspControl(dev,dev->GspSubdeviceHandle,NV2080_CTRL_CMD_GPU_PROMOTE_CTX,promote,sizeof(*promote))) goto fail;
    stage="runlist identity";
    if(!NvxGspQueryGraphicsRunlist(dev,&c->RunlistBase)) goto fail;
    if(!c->RunlistBase||dev->Bar0Length<0x30094||c->RunlistBase>dev->Bar0Length-0x304) goto fail;
    doorbellConfig=VideoPortReadRegisterUlong((PULONG)((PUCHAR)dev->Bar0MmioVirtual+c->RunlistBase+8));
    if(doorbellConfig==0xffffffffU||(doorbellConfig&0xffff0000U)==0xbadf0000U) goto fail;
    if(!nvx_openrm_channel_identity(channelId,(doorbellConfig>>16)&0x7fU,
            &chan.flags,&expectedToken)) goto fail;
    /* GET_WORK_SUBMIT_TOKEN is not implemented by the pinned direct GSP
     * path. Supply the host-RM service using the explicitly reserved ChID.
     * Avoid sending the known-unsupported firmware control on each open. */
    c->SubmitToken=expectedToken;
    stage="schedule";
    if(!NvxGspControl(dev,c->Channel,NVA06F_CTRL_CMD_GPFIFO_SCHEDULE,&schedule,sizeof(schedule))) goto fail;
    c->Scheduled=TRUE;
    stage="private queue fence verification";
    if(!NvxGrVerifyClientQueue(dev,c)) goto fail;
    c->Ready=TRUE;
    ok=NvxGspGetClientChannel(dev,lease);
    LogStr("NVX [GR-CLIENT]: private queue verified token=");LogHex(c->ClientToken);
    LogStr(" channel=");LogHex(c->Channel);LogStr(" submit-token=");LogHex(c->SubmitToken);
    LogStr(" USERD-page=");LogHex(channelId/8U);
    LogStr(" USERD-slot=");LogHex(channelId%8U);
    LogStr(" queueVA=");LogHex(c->GpuVa);LogStr(" contextVA=");
    LogHex(c->GpuVa+NVX_GR_CLIENT_CONTEXT_OFFSET);LogStr("\n");
    goto done;
fail:
    LogStr("NVX [GR-CLIENT]: creation failed token=");LogHex(c->ClientToken);
    LogStr(" stage=");LogStr(stage);LogStr("\n");
    (void)NvxGrDestroyClient(dev,c);
done:
    __sync_lock_release(&NvxGrUserSubmitBusy);
    return ok;
#else
    (void)extension;(void)lease;(void)backing;(void)physical;(void)gpuVa;(void)bufferId;
    return FALSE;
#endif
}
