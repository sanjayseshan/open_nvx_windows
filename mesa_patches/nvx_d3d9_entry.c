
#include <stdio.h>
#include <stdarg.h>

/*
 * Antigravity Native Direct3D 9 Entry Point for Windows XP
 * Connects Direct3DCreate9 -> IDirect3D9 COM vtable -> Gallium Nine (st/nine) -> Nouveau pipe_screen -> RTX 3070
 */

#include <windows.h>
#include <d3d9.h>
#include <stdio.h>
#include "d3dadapter/present.h"
#include "d3dadapter/d3dadapter9.h"
#include "pipe/p_screen.h"
#include "adapter9.h"
#include <stdarg.h>

static void log_d3d9(const char *fmt, ...);

static LONG WINAPI nvx_d3d9_veh_filter(EXCEPTION_POINTERS *ep) {
    if (ep && ep->ExceptionRecord) {
        log_d3d9("[D3D9_VEH] Exception 0x%08lX at address %p flags=0x%lx\n",
                 ep->ExceptionRecord->ExceptionCode,
                 ep->ExceptionRecord->ExceptionAddress,
                 ep->ExceptionRecord->ExceptionFlags);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void log_d3d9(const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    HANDLE hFile = CreateFileA("C:\\d3d9_debug.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, 0, NULL);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hFile, buf, (DWORD)len, &written, NULL);
        CloseHandle(hFile);
    }
    HANDLE hCom = CreateFileA("\\\\.\\COM1", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hCom != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hCom, buf, (DWORD)len, &written, NULL);
        CloseHandle(hCom);
    }
}

extern struct pipe_screen *nvx_wgl_create_native_nouveau_screen(void);
extern void nvx_wgl_set_hdc(HDC hdc);
extern int nvx_wgl_xf86drm_fd(void);
extern ID3DPresent *nvx_create_win32_present(HWND hwnd, struct pipe_screen *screen, struct pipe_context *pipe);

static struct d3dadapter9_context g_ctx;
static struct NineAdapter9 *g_adapter = NULL;

static void ctx_destroy(struct d3dadapter9_context *ctx) {
    (void)ctx;
}

/* Win32 ID3DPresentGroup implementation for Gallium Nine */
struct nvx_d3d_present_group {
    ID3DPresentGroup base;
    LONG refcount;
    HWND hwnd;
    struct pipe_screen *screen;
};

static HRESULT WINAPI group_QueryInterface(ID3DPresentGroup *This, REFIID riid, void **ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG WINAPI group_AddRef(ID3DPresentGroup *This) {
    struct nvx_d3d_present_group *g = (struct nvx_d3d_present_group *)This;
    return InterlockedIncrement(&g->refcount);
}

static ULONG WINAPI group_Release(ID3DPresentGroup *This) {
    struct nvx_d3d_present_group *g = (struct nvx_d3d_present_group *)This;
    ULONG ref = InterlockedDecrement(&g->refcount);
    if (!ref) {
        free(g);
    }
    return ref;
}

static UINT WINAPI group_GetMultiheadCount(ID3DPresentGroup *This) {
    return 1;
}

static HRESULT WINAPI group_GetPresent(ID3DPresentGroup *This, UINT Index, ID3DPresent **ppPresent) {
    struct nvx_d3d_present_group *g = (struct nvx_d3d_present_group *)This;
    if (Index != 0 || !ppPresent) return D3DERR_INVALIDCALL;
    *ppPresent = nvx_create_win32_present(g->hwnd, g->screen, NULL);
    return S_OK;
}

static HRESULT WINAPI group_CreateAdditionalPresent(ID3DPresentGroup *This, D3DPRESENT_PARAMETERS *pPresentationParameters, ID3DPresent **ppPresent) {
    struct nvx_d3d_present_group *g = (struct nvx_d3d_present_group *)This;
    HWND target_hwnd = (pPresentationParameters && pPresentationParameters->hDeviceWindow) ? pPresentationParameters->hDeviceWindow : g->hwnd;
    if (!ppPresent) return D3DERR_INVALIDCALL;
    *ppPresent = nvx_create_win32_present(target_hwnd, g->screen, NULL);
    return S_OK;
}

static void WINAPI group_GetVersion(ID3DPresentGroup *This, int *major, int *minor) {
    if (major) *major = 1;
    if (minor) *minor = 4;
}

static ID3DPresentGroupVtbl group_vtbl = {
    group_QueryInterface,
    group_AddRef,
    group_Release,
    group_GetMultiheadCount,
    group_GetPresent,
    group_CreateAdditionalPresent,
    group_GetVersion
};

static ID3DPresentGroup *create_win32_present_group(HWND hwnd, struct pipe_screen *screen) {
    struct nvx_d3d_present_group *g = (struct nvx_d3d_present_group *)calloc(1, sizeof(struct nvx_d3d_present_group));
    g->base.lpVtbl = &group_vtbl;
    g->refcount = 1;
    g->hwnd = hwnd;
    g->screen = screen;
    return &g->base;
}

/* IDirect3D9 COM implementation forwarding to Gallium Nine adapter */
struct nvx_d3d9_obj {
    IDirect3D9 base;
    LONG refcount;
    struct NineAdapter9 *adapter;
    struct pipe_screen *screen;
};

static HRESULT WINAPI d3d9_QueryInterface(IDirect3D9 *This, REFIID riid, void **ppvObject) {
    if (!ppvObject) return E_POINTER;
    *ppvObject = This;
    This->lpVtbl->AddRef(This);
    return S_OK;
}

static ULONG WINAPI d3d9_AddRef(IDirect3D9 *This) {
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)This;
    return InterlockedIncrement(&o->refcount);
}

static ULONG WINAPI d3d9_Release(IDirect3D9 *This) {
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)This;
    ULONG ref = InterlockedDecrement(&o->refcount);
    if (!ref) {
        free(o);
    }
    return ref;
}

static HRESULT WINAPI d3d9_RegisterSoftwareDevice(IDirect3D9 *This, void *pInitializeFunction) {
    return D3D_OK;
}

static UINT WINAPI d3d9_GetAdapterCount(IDirect3D9 *This) {
    return 1;
}

static HRESULT WINAPI d3d9_GetAdapterIdentifier(IDirect3D9 *This, UINT Adapter, DWORD Flags, D3DADAPTER_IDENTIFIER9 *pIdentifier) {
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)This;
    if (Adapter != 0 || !pIdentifier) return D3DERR_INVALIDCALL;
    return NineAdapter9_GetAdapterIdentifier(o->adapter, Flags, pIdentifier);
}

static UINT WINAPI d3d9_GetAdapterModeCount(IDirect3D9 *This, UINT Adapter, D3DFORMAT Format) {
    if (Adapter != 0) return 0;
    return 1;
}

static HRESULT WINAPI d3d9_EnumAdapterModes(IDirect3D9 *This, UINT Adapter, D3DFORMAT Format, UINT Mode, D3DDISPLAYMODE *pMode) {
    if (Adapter != 0 || !pMode) return D3DERR_INVALIDCALL;
    pMode->Width = 1024;
    pMode->Height = 768;
    pMode->RefreshRate = 60;
    pMode->Format = D3DFMT_X8R8G8B8;
    return D3D_OK;
}

static HRESULT WINAPI d3d9_GetAdapterDisplayMode(IDirect3D9 *This, UINT Adapter, D3DDISPLAYMODE *pMode) {
    if (Adapter != 0 || !pMode) return D3DERR_INVALIDCALL;
    pMode->Width = 1024;
    pMode->Height = 768;
    pMode->RefreshRate = 60;
    pMode->Format = D3DFMT_X8R8G8B8;
    return D3D_OK;
}

static HRESULT WINAPI d3d9_CheckDeviceType(IDirect3D9 *This, UINT iAdapter, D3DDEVTYPE DevType, D3DFORMAT DisplayFormat, D3DFORMAT BackBufferFormat, BOOL bWindowed) {
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)This;
    if (iAdapter != 0) return D3DERR_INVALIDCALL;
    return NineAdapter9_CheckDeviceType(o->adapter, DevType, DisplayFormat, BackBufferFormat, bWindowed);
}

static HRESULT WINAPI d3d9_CheckDeviceFormat(IDirect3D9 *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, DWORD Usage, D3DRESOURCETYPE RType, D3DFORMAT CheckFormat) {
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)This;
    if (Adapter != 0) return D3DERR_INVALIDCALL;
    return NineAdapter9_CheckDeviceFormat(o->adapter, DeviceType, AdapterFormat, Usage, RType, CheckFormat);
}

static HRESULT WINAPI d3d9_CheckDeviceMultiSampleType(IDirect3D9 *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SurfaceFormat, BOOL Windowed, D3DMULTISAMPLE_TYPE MultiSampleType, DWORD *pQualityLevels) {
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)This;
    if (Adapter != 0) return D3DERR_INVALIDCALL;
    return NineAdapter9_CheckDeviceMultiSampleType(o->adapter, DeviceType, SurfaceFormat, Windowed, MultiSampleType, pQualityLevels);
}

static HRESULT WINAPI d3d9_CheckDepthStencilMatch(IDirect3D9 *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT AdapterFormat, D3DFORMAT RenderTargetFormat, D3DFORMAT DepthStencilFormat) {
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)This;
    if (Adapter != 0) return D3DERR_INVALIDCALL;
    return NineAdapter9_CheckDepthStencilMatch(o->adapter, DeviceType, AdapterFormat, RenderTargetFormat, DepthStencilFormat);
}

static HRESULT WINAPI d3d9_CheckDeviceFormatConversion(IDirect3D9 *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DFORMAT SourceFormat, D3DFORMAT TargetFormat) {
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)This;
    if (Adapter != 0) return D3DERR_INVALIDCALL;
    return NineAdapter9_CheckDeviceFormatConversion(o->adapter, DeviceType, SourceFormat, TargetFormat);
}

static HRESULT WINAPI d3d9_GetDeviceCaps(IDirect3D9 *This, UINT Adapter, D3DDEVTYPE DeviceType, D3DCAPS9 *pCaps) {
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)This;
    if (Adapter != 0 || !pCaps) return D3DERR_INVALIDCALL;
    return NineAdapter9_GetDeviceCaps(o->adapter, DeviceType, pCaps);
}

static HMONITOR WINAPI d3d9_GetAdapterMonitor(IDirect3D9 *This, UINT Adapter) {
    if (Adapter != 0) return (HMONITOR)0;
    return MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
}

static HRESULT WINAPI d3d9_CreateDevice(IDirect3D9 *This, UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS *pPresentationParameters, IDirect3DDevice9 **ppReturnedDeviceInterface) {
    if (pPresentationParameters) {
        log_d3d9("[D3D9] CreateDevice Adapter=%u DeviceType=%u Focus=%p Flags=0x%lx W=%u H=%u Fmt=%u BackBuffers=%u Win=%d AutoDS=%d DSFormat=%u\n",
                 Adapter, DeviceType, hFocusWindow, BehaviorFlags,
                 pPresentationParameters->BackBufferWidth,
                 pPresentationParameters->BackBufferHeight,
                 pPresentationParameters->BackBufferFormat,
                 pPresentationParameters->BackBufferCount,
                 pPresentationParameters->Windowed,
                 pPresentationParameters->EnableAutoDepthStencil,
                 pPresentationParameters->AutoDepthStencilFormat);
    } else {
        log_d3d9("[D3D9] CreateDevice Adapter=%u DeviceType=%u Focus=%p Flags=0x%lx (NULL params)\n",
                 Adapter, DeviceType, hFocusWindow, BehaviorFlags);
    }
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)This;
    if (Adapter != 0 || !ppReturnedDeviceInterface) return D3DERR_INVALIDCALL;

    HWND target_hwnd = (pPresentationParameters && pPresentationParameters->hDeviceWindow) ? pPresentationParameters->hDeviceWindow : hFocusWindow;
    ID3DPresentGroup *present_group = create_win32_present_group(target_hwnd, o->screen);

    /* Sanitize presentation parameters */
    D3DPRESENT_PARAMETERS sanitized_pp;
    D3DPRESENT_PARAMETERS *pPP = pPresentationParameters;
    if (pPresentationParameters) {
        sanitized_pp = *pPresentationParameters;
        if (sanitized_pp.BackBufferFormat == D3DFMT_UNKNOWN) {
            sanitized_pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        }
        if (sanitized_pp.BackBufferWidth == 0 || sanitized_pp.BackBufferHeight == 0) {
            RECT rc;
            if (target_hwnd && GetClientRect(target_hwnd, &rc) && (rc.right - rc.left) > 0) {
                if (!sanitized_pp.BackBufferWidth) sanitized_pp.BackBufferWidth = rc.right - rc.left;
                if (!sanitized_pp.BackBufferHeight) sanitized_pp.BackBufferHeight = rc.bottom - rc.top;
            } else {
                if (!sanitized_pp.BackBufferWidth) sanitized_pp.BackBufferWidth = 640;
                if (!sanitized_pp.BackBufferHeight) sanitized_pp.BackBufferHeight = 480;
            }
        }
        pPP = &sanitized_pp;
    }

    /* Mask out multithreaded flag to avoid futex recursion on XP */
    DWORD sanitized_flags = BehaviorFlags & ~D3DCREATE_MULTITHREADED;

    log_d3d9("[D3D9] Calling NineAdapter9_CreateDevice (Flags=0x%lx, W=%u, H=%u, Fmt=%u)...\n",
             sanitized_flags, pPP ? pPP->BackBufferWidth : 0, pPP ? pPP->BackBufferHeight : 0, pPP ? pPP->BackBufferFormat : 0);

    HRESULT hr = NineAdapter9_CreateDevice(o->adapter, Adapter, DeviceType, hFocusWindow,
                                           sanitized_flags, pPP,
                                           This, present_group, ppReturnedDeviceInterface);

    /* If software vertex processing failed (e.g. lack of streamout on SW screen), retry with HWVP */
    if (FAILED(hr) && (sanitized_flags & D3DCREATE_SOFTWARE_VERTEXPROCESSING)) {
        log_d3d9("[D3D9] SWVP failed (hr=0x%08lx), retrying with HARDWARE_VERTEXPROCESSING...\n", (unsigned long)hr);
        sanitized_flags &= ~D3DCREATE_SOFTWARE_VERTEXPROCESSING;
        sanitized_flags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
        hr = NineAdapter9_CreateDevice(o->adapter, Adapter, DeviceType, hFocusWindow,
                                       sanitized_flags, pPP,
                                       This, present_group, ppReturnedDeviceInterface);
    }

    log_d3d9("[D3D9] NineAdapter9_CreateDevice returned hr=0x%08lx, dev=%p\n", (unsigned long)hr, ppReturnedDeviceInterface ? *ppReturnedDeviceInterface : NULL);
    if (SUCCEEDED(hr) && pPresentationParameters && pPP) {
        *pPresentationParameters = *pPP;
    }
    if (FAILED(hr)) {
        ID3DPresentGroup_Release(present_group);
    }

    return hr;
}

static IDirect3D9Vtbl d3d9_vtbl = {
    d3d9_QueryInterface,
    d3d9_AddRef,
    d3d9_Release,
    d3d9_RegisterSoftwareDevice,
    d3d9_GetAdapterCount,
    d3d9_GetAdapterIdentifier,
    d3d9_GetAdapterModeCount,
    d3d9_EnumAdapterModes,
    d3d9_GetAdapterDisplayMode,
    d3d9_CheckDeviceType,
    d3d9_CheckDeviceFormat,
    d3d9_CheckDeviceMultiSampleType,
    d3d9_CheckDepthStencilMatch,
    d3d9_CheckDeviceFormatConversion,
    d3d9_GetDeviceCaps,
    d3d9_GetAdapterMonitor,
    d3d9_CreateDevice
};

static HRESULT init_nine_adapter(void) {
    if (g_adapter) return S_OK;

    if (nvx_wgl_xf86drm_fd() <= 0) {
        HDC hdc = CreateDCA("DISPLAY", NULL, NULL, NULL);
        if (!hdc) hdc = GetDC(NULL);
        if (hdc) nvx_wgl_set_hdc(hdc);
    }

    struct pipe_screen *screen = nvx_wgl_create_native_nouveau_screen();
    if (!screen) return D3DERR_NOTAVAILABLE;

    ZeroMemory(&g_ctx, sizeof(g_ctx));
    g_ctx.hal = screen;
    g_ctx.ref = screen;
    g_ctx.destroy = ctx_destroy;
    g_ctx.linear_framebuffer = FALSE;
    g_ctx.throttling = FALSE;
    g_ctx.csmt_force = 1;
    g_ctx.override_vram_size = 2048; /* 2048 MB VRAM window to prevent 32-bit atomic counter overflow */

    /* Fill device identifier */
    strcpy(g_ctx.identifier.Driver, "d3d9.dll");
    strcpy(g_ctx.identifier.Description, "NVIDIA GeForce RTX 3070 (Gallium Nine XP)");
    strcpy(g_ctx.identifier.DeviceName, "\\\\.\\DISPLAY1");
    g_ctx.identifier.VendorId = 0x10de;
    g_ctx.identifier.DeviceId = 0x2488;
    g_ctx.identifier.SubSysId = 0;
    g_ctx.identifier.Revision = 0xa1;

    return NineAdapter9_new(&g_ctx, &g_adapter);
}

/*
 * Standard Microsoft Direct3D 9 Exports
 */
IDirect3D9 * WINAPI Direct3DCreate9(UINT SDKVersion) {
    log_d3d9("[D3D9] Direct3DCreate9 called SDKVersion=%u\n", SDKVersion);
    (void)SDKVersion;
    if (FAILED(init_nine_adapter())) {
        return NULL;
    }
    struct nvx_d3d9_obj *o = (struct nvx_d3d9_obj *)calloc(1, sizeof(struct nvx_d3d9_obj));
    o->base.lpVtbl = &d3d9_vtbl;
    o->refcount = 1;
    o->adapter = g_adapter;
    o->screen = g_ctx.hal;
    return &o->base;
}

HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex **ppD3D) {
    (void)SDKVersion;
    if (!ppD3D) return D3DERR_INVALIDCALL;
    IDirect3D9 *d3d = Direct3DCreate9(SDKVersion);
    if (!d3d) {
        *ppD3D = NULL;
        return D3DERR_NOTAVAILABLE;
    }
    *ppD3D = (IDirect3D9Ex *)d3d;
    return S_OK;
}

void WINAPI DebugSetMute(void) {}
int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName) { (void)col; (void)wszName; return 0; }
int WINAPI D3DPERF_EndEvent(void) { return 0; }
void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName) { (void)col; (void)wszName; }
void WINAPI D3DPERF_SetOptions(DWORD dwOptions) { (void)dwOptions; }
DWORD WINAPI D3DPERF_GetStatus(void) { return 0; }
WINBOOL WINAPI D3DPERF_QueryRepeatFrame(void) { return 0; }
void WINAPI D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName) { (void)col; (void)wszName; }
void * WINAPI Direct3DShaderValidatorCreate9(void) {
    log_d3d9("[D3D9] Direct3DShaderValidatorCreate9 called!\n");
    return NULL;
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hinstDLL);
        AddVectoredExceptionHandler(1, nvx_d3d9_veh_filter);
        char exe_name[MAX_PATH] = {0};
        GetModuleFileNameA(NULL, exe_name, sizeof(exe_name));
        log_d3d9("[D3D9_ATTACH] Process: %s\n", exe_name);
        // MessageBoxA removed
    }
    return TRUE;
}
