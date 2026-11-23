/*
 * Antigravity Native Win32 ID3DPresent Implementation for Gallium Nine
 * Direct hardware scanout presentation for Windows XP x64 on RTX 3070
 */

#include <windows.h>
#include "d3dadapter/present.h"
#include "pipe/p_screen.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct D3DWindowBuffer {
    struct pipe_resource *resource;
    void *cpu_map;
    int width;
    int height;
    int stride;
    uint32_t handle;
};

struct nvx_d3d_present {
    ID3DPresent base;
    LONG refcount;
    HWND hwnd;
    HDC hdc;
    D3DPRESENT_PARAMETERS params;
    struct pipe_screen *screen;
    struct pipe_context *pipe;
};

static HRESULT WINAPI nvx_present_QueryInterface(ID3DPresent *This, REFIID riid, void **ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG WINAPI nvx_present_AddRef(ID3DPresent *This) {
    struct nvx_d3d_present *p = (struct nvx_d3d_present *)This;
    return InterlockedIncrement(&p->refcount);
}

static ULONG WINAPI nvx_present_Release(ID3DPresent *This) {
    struct nvx_d3d_present *p = (struct nvx_d3d_present *)This;
    ULONG ref = InterlockedDecrement(&p->refcount);
    if (!ref) {
        if (p->hdc && p->hwnd) ReleaseDC(p->hwnd, p->hdc);
        free(p);
    }
    return ref;
}

static HRESULT WINAPI nvx_present_SetPresentParameters(ID3DPresent *This, D3DPRESENT_PARAMETERS *pParams, D3DDISPLAYMODEEX *pMode) {
    struct nvx_d3d_present *p = (struct nvx_d3d_present *)This;
    (void)pMode;
    if (pParams) {
        HWND target = pParams->hDeviceWindow ? pParams->hDeviceWindow : p->hwnd;
        RECT r;
        if (target && GetClientRect(target, &r)) {
            int w = r.right - r.left;
            int h = r.bottom - r.top;
            if (w <= 0) w = 640;
            if (h <= 0) h = 480;
            if (!pParams->BackBufferWidth) pParams->BackBufferWidth = w;
            if (!pParams->BackBufferHeight) pParams->BackBufferHeight = h;
        } else {
            if (!pParams->BackBufferWidth) pParams->BackBufferWidth = 640;
            if (!pParams->BackBufferHeight) pParams->BackBufferHeight = 480;
        }
        if (pParams->BackBufferCount == 0)
            pParams->BackBufferCount = 1;
        p->params = *pParams;
    }
    return S_OK;
}

static HRESULT WINAPI nvx_present_NewD3DWindowBufferFromDmaBuf(ID3DPresent *This, int dmaBufFd, int width, int height, int stride, int depth, int bpp, D3DWindowBuffer **out) {
    (void)depth;
    (void)bpp;
    struct D3DWindowBuffer *buf = (struct D3DWindowBuffer *)calloc(1, sizeof(struct D3DWindowBuffer));
    buf->width = width;
    buf->height = height;
    buf->stride = stride;
    buf->handle = (uint32_t)dmaBufFd;
    *out = buf;
    return S_OK;
}

static HRESULT WINAPI nvx_present_DestroyD3DWindowBuffer(ID3DPresent *This, D3DWindowBuffer *buffer) {
    if (buffer) free(buffer);
    return S_OK;
}

static HRESULT WINAPI nvx_present_WaitBufferReleased(ID3DPresent *This, D3DWindowBuffer *buffer) {
    return S_OK;
}

static HRESULT WINAPI nvx_present_FrontBufferCopy(ID3DPresent *This, D3DWindowBuffer *buffer) {
    return S_OK;
}

extern int nvx_wgl_present_gpu_copy(HDC hdc, uint32_t handle, uint32_t stride,
                                    uint32_t width, uint32_t height);
extern void nvx_wgl_set_hdc(HDC hdc);

static HRESULT WINAPI nvx_present_PresentBuffer(ID3DPresent *This, D3DWindowBuffer *buffer, HWND hWndOverride, const RECT *pSourceRect, const RECT *pDestRect, const RGNDATA *pDirtyRegion, DWORD Flags) {
    struct nvx_d3d_present *p = (struct nvx_d3d_present *)This;
    HWND target_hwnd = hWndOverride ? hWndOverride : p->hwnd;
    if (buffer && buffer->handle && target_hwnd) {
        HDC hdc = GetDC(target_hwnd);
        if (hdc) {
            nvx_wgl_set_hdc(hdc);
            nvx_wgl_present_gpu_copy(hdc, buffer->handle, buffer->stride,
                                     buffer->width, buffer->height);
            ReleaseDC(target_hwnd, hdc);
        }
    }
    return S_OK;
}

static HRESULT WINAPI nvx_present_GetRasterStatus(ID3DPresent *This, D3DRASTER_STATUS *pRasterStatus) {
    if (pRasterStatus) {
        pRasterStatus->InVBlank = FALSE;
        pRasterStatus->ScanLine = 0;
    }
    return S_OK;
}

static HRESULT WINAPI nvx_present_GetDisplayMode(ID3DPresent *This, D3DDISPLAYMODEEX *pMode, D3DDISPLAYROTATION *pRotation) {
    if (pMode) {
        pMode->Size = sizeof(D3DDISPLAYMODEEX);
        pMode->Width = 1024;
        pMode->Height = 768;
        pMode->RefreshRate = 60;
        pMode->Format = D3DFMT_X8R8G8B8;
        pMode->ScanLineOrdering = D3DSCANLINEORDERING_PROGRESSIVE;
    }
    if (pRotation) *pRotation = D3DDISPLAYROTATION_IDENTITY;
    return S_OK;
}

static HRESULT WINAPI nvx_present_GetPresentStats(ID3DPresent *This, D3DPRESENTSTATS *pStats) { return S_OK; }
static HRESULT WINAPI nvx_present_GetCursorPos(ID3DPresent *This, POINT *pPoint) { if (pPoint) GetCursorPos(pPoint); return S_OK; }
static HRESULT WINAPI nvx_present_SetCursorPos(ID3DPresent *This, POINT *pPoint) { if (pPoint) SetCursorPos(pPoint->x, pPoint->y); return S_OK; }
static HRESULT WINAPI nvx_present_SetCursor(ID3DPresent *This, void *pBitmap, POINT *pHotspot, BOOL bShow) { return S_OK; }
static HRESULT WINAPI nvx_present_SetGammaRamp(ID3DPresent *This, const D3DGAMMARAMP *pRamp, HWND hWndOverride) { return S_OK; }

static HRESULT WINAPI nvx_present_GetWindowInfo(ID3DPresent *This, HWND hWnd, int *width, int *height, int *depth) {
    RECT r;
    if (!hWnd) {
        struct nvx_d3d_present *p = (struct nvx_d3d_present *)This;
        hWnd = p->hwnd;
    }
    GetClientRect(hWnd, &r);
    if (width) *width = r.right - r.left;
    if (height) *height = r.bottom - r.top;
    if (depth) *depth = 32;
    return S_OK;
}

static BOOL WINAPI nvx_present_GetWindowOccluded(ID3DPresent *This) { return FALSE; }
static BOOL WINAPI nvx_present_ResolutionMismatch(ID3DPresent *This) { return FALSE; }
static HANDLE WINAPI nvx_present_CreateThread(ID3DPresent *This, void *pThreadfunc, void *pParam) {
    return CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE)pThreadfunc, pParam, 0, NULL);
}
static BOOL WINAPI nvx_present_WaitForThread(ID3DPresent *This, HANDLE thread) {
    WaitForSingleObject(thread, INFINITE);
    CloseHandle(thread);
    return TRUE;
}
static HRESULT WINAPI nvx_present_SetPresentParameters2(ID3DPresent *This, D3DPRESENT_PARAMETERS2 *pParameters) { return S_OK; }
static BOOL WINAPI nvx_present_IsBufferReleased(ID3DPresent *This, D3DWindowBuffer *buffer) { return TRUE; }
static HRESULT WINAPI nvx_present_WaitBufferReleaseEvent(ID3DPresent *This) { return S_OK; }

static ID3DPresentVtbl nvx_present_vtbl = {
    nvx_present_QueryInterface,
    nvx_present_AddRef,
    nvx_present_Release,
    nvx_present_SetPresentParameters,
    nvx_present_NewD3DWindowBufferFromDmaBuf,
    nvx_present_DestroyD3DWindowBuffer,
    nvx_present_WaitBufferReleased,
    nvx_present_FrontBufferCopy,
    nvx_present_PresentBuffer,
    nvx_present_GetRasterStatus,
    nvx_present_GetDisplayMode,
    nvx_present_GetPresentStats,
    nvx_present_GetCursorPos,
    nvx_present_SetCursorPos,
    nvx_present_SetCursor,
    nvx_present_SetGammaRamp,
    nvx_present_GetWindowInfo,
    nvx_present_GetWindowOccluded,
    nvx_present_ResolutionMismatch,
    nvx_present_CreateThread,
    nvx_present_WaitForThread,
    nvx_present_SetPresentParameters2,
    nvx_present_IsBufferReleased,
    nvx_present_WaitBufferReleaseEvent
};

ID3DPresent *nvx_create_win32_present(HWND hwnd, struct pipe_screen *screen, struct pipe_context *pipe) {
    struct nvx_d3d_present *p = (struct nvx_d3d_present *)calloc(1, sizeof(struct nvx_d3d_present));
    p->base.lpVtbl = &nvx_present_vtbl;
    p->refcount = 1;
    p->hwnd = hwnd;
    p->hdc = GetDC(hwnd);
    p->screen = screen;
    p->pipe = pipe;
    return &p->base;
}
