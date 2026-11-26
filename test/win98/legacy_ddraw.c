#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0400
#define CINTERFACE
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdio.h>

static void report_hr(const char *step, HRESULT hr)
{
    char line[128];
    HANDLE serial = CreateFileA("\\\\.\\COM1", GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    wsprintfA(line, "[WIN98-DDRAW] %s hr=0x%08lX\r\n", step, (DWORD)hr);
    if (serial != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(serial, line, lstrlenA(line), &written, NULL);
        CloseHandle(serial);
    }
}

static HRESULT CALLBACK enum_device(GUID *guid, char *description, char *name,
        D3DDEVICEDESC *hal, D3DDEVICEDESC *hel, void *ctx)
{
    char line[256];
    HANDLE serial = CreateFileA("\\\\.\\COM1", GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    (void)guid; (void)hal; (void)hel; (void)ctx;
    wsprintfA(line, "[WIN98-DDRAW] device %s / %s\r\n",
        description ? description : "(no description)", name ? name : "(no name)");
    if (serial != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(serial, line, lstrlenA(line), &written, NULL);
        CloseHandle(serial);
    }
    return D3DENUMRET_OK;
}

static void identity(D3DMATRIX *m)
{
    ZeroMemory(m, sizeof(*m));
    m->_11 = m->_22 = m->_33 = m->_44 = 1.0f;
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    HWND hwnd = NULL;
    IDirectDraw *dd = NULL;
    IDirectDrawSurface *surface = NULL;
    IDirectDrawSurface *primary = NULL;
    IDirectDrawSurface4 *surface4 = NULL;
    IDirectDrawClipper *clipper = NULL;
    IDirect3D3 *d3d = NULL;
    IDirect3DDevice3 *device = NULL;
    IDirect3DViewport3 *viewport = NULL;
    DDSURFACEDESC desc;
    RECT dst, src;
    POINT origin;
    D3DVIEWPORT2 vp;
    D3DMATRIX matrix;
    struct { float x, y, z, rhw; DWORD color; } vertices[3];
    HRESULT hr;
    int result = 1;
    (void)instance; (void)previous; (void)command; (void)show;

    report_hr("begin", S_OK);
    hwnd = CreateWindowA("STATIC", "DirectDraw", WS_OVERLAPPEDWINDOW,
        20, 20, 680, 520, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hwnd) { report_hr("CreateWindow", HRESULT_FROM_WIN32(GetLastError())); goto done; }
    ShowWindow(hwnd, SW_SHOW);

    hr = DirectDrawCreate(NULL, &dd, NULL);
    report_hr("DirectDrawCreate", hr);
    if (FAILED(hr)) goto done;
    hr = IDirectDraw_SetCooperativeLevel(dd, hwnd, DDSCL_NORMAL);
    report_hr("SetCooperativeLevel", hr);
    if (FAILED(hr)) goto done;

    /* The D3D device renders to an offscreen surface. Attach a window clipper
     * to the primary surface and blit the result into the client area so this
     * smoke test has a visible result instead of a blank DirectDraw window. */
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = IDirectDraw_CreateSurface(dd, &desc, &primary, NULL);
    report_hr("Create-primary-surface", hr);
    if (FAILED(hr)) goto done;
    hr = IDirectDraw_CreateClipper(dd, 0, &clipper, NULL);
    report_hr("Create-window-clipper", hr);
    if (FAILED(hr)) goto done;
    hr = IDirectDrawClipper_SetHWnd(clipper, 0, hwnd);
    report_hr("Set-clipper-HWND", hr);
    if (FAILED(hr)) goto done;
    hr = IDirectDrawSurface_SetClipper(primary, clipper);
    report_hr("Attach-primary-clipper", hr);
    if (FAILED(hr)) goto done;
    /* Wine9x intentionally follows Wine's COM behavior: it doesn't expose
     * IDirectDraw3 (E_NOINTERFACE), but it does expose the D3D3 interface. */
    hr = IDirectDraw_QueryInterface(dd, &IID_IDirect3D3, (void **)&d3d);
    report_hr("QI-IDirect3D3", hr);
    if (FAILED(hr)) goto done;
    hr = IDirect3D3_EnumDevices(d3d, enum_device, NULL);
    report_hr("EnumDevices", hr);
    if (FAILED(hr)) goto done;

    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = 640;
    desc.dwHeight = 480;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE;
    hr = IDirectDraw_CreateSurface(dd, &desc, &surface, NULL);
    report_hr("Create3D-target-surface", hr);
    if (FAILED(hr)) goto done;
    hr = IDirectDrawSurface_QueryInterface(surface, &IID_IDirectDrawSurface4,
        (void **)&surface4);
    report_hr("QI-IDirectDrawSurface4", hr);
    if (FAILED(hr)) goto done;
    hr = IDirect3D3_CreateDevice(d3d, &IID_IDirect3DHALDevice, surface4,
        &device, NULL);
    report_hr("Create-HAL-device", hr);
    if (FAILED(hr)) goto done;

    hr = IDirect3D3_CreateViewport(d3d, &viewport, NULL);
    report_hr("CreateViewport", hr);
    if (FAILED(hr)) goto done;
    ZeroMemory(&vp, sizeof(vp));
    vp.dwSize = sizeof(vp); vp.dwWidth = 640; vp.dwHeight = 480;
    vp.dvClipWidth = 2.0f; vp.dvClipHeight = 2.0f;
    vp.dvMinZ = 0.0f; vp.dvMaxZ = 1.0f;
    hr = IDirect3DViewport3_SetViewport2(viewport, &vp);
    report_hr("SetViewport2", hr);
    if (FAILED(hr)) goto done;
    hr = IDirect3DDevice3_AddViewport(device, viewport);
    report_hr("AddViewport", hr);
    if (FAILED(hr)) goto done;
    hr = IDirect3DDevice3_SetCurrentViewport(device, viewport);
    report_hr("SetCurrentViewport", hr);
    if (FAILED(hr)) goto done;

    identity(&matrix);
    hr = IDirect3DDevice3_SetTransform(device, D3DTRANSFORMSTATE_WORLD, &matrix);
    report_hr("SetWorld", hr); if (FAILED(hr)) goto done;
    hr = IDirect3DDevice3_SetTransform(device, D3DTRANSFORMSTATE_VIEW, &matrix);
    report_hr("SetView", hr); if (FAILED(hr)) goto done;
    hr = IDirect3DDevice3_SetTransform(device, D3DTRANSFORMSTATE_PROJECTION, &matrix);
    report_hr("SetProjection", hr); if (FAILED(hr)) goto done;

    ZeroMemory(vertices, sizeof(vertices));
    vertices[0].x = 80.0f;  vertices[0].y = 400.0f;
    vertices[1].x = 320.0f; vertices[1].y = 80.0f;
    vertices[2].x = 560.0f; vertices[2].y = 400.0f;
    vertices[0].z = vertices[1].z = vertices[2].z = 0.5f;
    vertices[0].rhw = vertices[1].rhw = vertices[2].rhw = 1.0f;
    vertices[0].color = 0xffff4040; vertices[1].color = 0xff40ff40;
    vertices[2].color = 0xff4040ff;
    hr = IDirect3DDevice3_SetRenderState(device, D3DRENDERSTATE_ZENABLE, D3DZB_FALSE);
    report_hr("Disable-Z", hr);
    if (FAILED(hr)) goto done;
    hr = IDirect3DDevice3_BeginScene(device);
    report_hr("BeginScene", hr); if (FAILED(hr)) goto done;
    hr = IDirect3DDevice3_DrawPrimitive(device, D3DPT_TRIANGLELIST,
        D3DFVF_XYZRHW | D3DFVF_DIFFUSE, vertices, 3, 0);
    report_hr("DrawPrimitive-triangle", hr);
    IDirect3DDevice3_EndScene(device);
    if (FAILED(hr)) goto done;

    SetRect(&src, 0, 0, 640, 480);
    GetClientRect(hwnd, &dst);
    origin.x = dst.left;
    origin.y = dst.top;
    ClientToScreen(hwnd, &origin);
    OffsetRect(&dst, origin.x, origin.y);
    hr = IDirectDrawSurface_Blt(primary, &dst, surface, &src, DDBLT_WAIT, NULL);
    report_hr("Blt-rendered-surface-to-window", hr);
    if (FAILED(hr)) goto done;
    report_hr("PASS-DirectDraw-Direct3D-visible-triangle", S_OK);
    result = 0;
    Sleep(5000);

done:
    if (device) IDirect3DDevice3_Release(device);
    if (viewport) IDirect3DViewport3_Release(viewport);
    if (clipper) IDirectDrawClipper_Release(clipper);
    if (primary) IDirectDrawSurface_Release(primary);
    if (d3d) IDirect3D3_Release(d3d);
    if (surface4) IDirectDrawSurface4_Release(surface4);
    if (surface) IDirectDrawSurface_Release(surface);
    if (dd) IDirectDraw_Release(dd);
    if (hwnd) DestroyWindow(hwnd);
    return result;
}
