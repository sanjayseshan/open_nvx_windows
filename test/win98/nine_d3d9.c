#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include <d3d9.h>
#include <stdlib.h>
#include <string.h>

#define NINE_TEST_FRAMES 60

static HANDLE serial_handle = INVALID_HANDLE_VALUE;

static void report_text(const char *line)
{
    DWORD written;
    if (serial_handle == INVALID_HANDLE_VALUE)
        serial_handle = CreateFileA("\\\\.\\COM1", GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (serial_handle != INVALID_HANDLE_VALUE)
        WriteFile(serial_handle, line, lstrlenA(line), &written, NULL);
}

static void report_hr(const char *step, HRESULT hr)
{
    char line[128];
    wsprintfA(line, "[WIN98-NINE] %s hr=0x%08lX\r\n", step, (DWORD)hr);
    report_text(line);
}

static void report_dword(const char *name, DWORD value)
{
    char line[128];
    wsprintfA(line, "[WIN98-NINE-CAPS] %s=0x%08lX (%lu)\r\n",
        name, (unsigned long)value, (unsigned long)value);
    report_text(line);
}

static void report_caps(IDirect3D9 *d3d)
{
    D3DCAPS9 caps;
    UINT adapter_count = IDirect3D9_GetAdapterCount(d3d);
    UINT modes = IDirect3D9_GetAdapterModeCount(d3d, D3DADAPTER_DEFAULT,
        D3DFMT_X8R8G8B8);
    HRESULT hr;

    report_dword("adapter_count", adapter_count);
    report_dword("x8r8g8b8_mode_count", modes);
    ZeroMemory(&caps, sizeof(caps));
    hr = IDirect3D9_GetDeviceCaps(d3d, D3DADAPTER_DEFAULT,
        D3DDEVTYPE_HAL, &caps);
    report_hr("GetDeviceCaps-HAL", hr);
    if (FAILED(hr))
        return;
    report_dword("max_texture_width", caps.MaxTextureWidth);
    report_dword("max_texture_height", caps.MaxTextureHeight);
    report_dword("simultaneous_textures", caps.MaxSimultaneousTextures);
    report_dword("vertex_shader_version", caps.VertexShaderVersion);
    report_dword("pixel_shader_version", caps.PixelShaderVersion);
    report_dword("texture_caps", caps.TextureCaps);
    report_dword("texture_filter_caps", caps.TextureFilterCaps);
    report_dword("stencil_caps", caps.StencilCaps);
}

struct vertex { float x, y, z, rhw; DWORD color; };

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR command, int show)
{
    HWND hwnd = NULL;
    IDirect3D9 *d3d = NULL;
    IDirect3DDevice9 *device = NULL;
    D3DPRESENT_PARAMETERS pp;
    struct vertex triangle[3];
    HRESULT hr;
    DWORD start_tick, elapsed;
    unsigned frame;
    int result = 1;
    (void)instance; (void)previous; (void)command; (void)show;

    report_hr("begin", S_OK);
    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) { report_hr("Direct3DCreate9-NULL", E_FAIL); goto done; }
    report_hr("Direct3DCreate9", S_OK);
    report_caps(d3d);
    hwnd = CreateWindowA("STATIC", "Win98 Gallium Nine smoke", WS_OVERLAPPEDWINDOW,
        30, 30, 640, 480, NULL, NULL, GetModuleHandleA(NULL), NULL);
    if (!hwnd) { report_hr("CreateWindow", HRESULT_FROM_WIN32(GetLastError())); goto done; }
    ShowWindow(hwnd, SW_SHOW);

    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = 640;
    pp.BackBufferHeight = 480;
    pp.hDeviceWindow = hwnd;
    hr = IDirect3D9_CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL,
        hwnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
    report_hr("CreateDevice-HAL-HWVP", hr);
    if (FAILED(hr)) goto done;

    hr = IDirect3DDevice9_SetFVF(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    report_hr("SetFVF", hr);
    if (FAILED(hr)) goto done;
    triangle[0].x = 80.0f; triangle[0].y = 400.0f;
    triangle[1].x = 320.0f; triangle[1].y = 80.0f;
    triangle[2].x = 560.0f; triangle[2].y = 400.0f;
    triangle[0].z = triangle[1].z = triangle[2].z = 0.5f;
    triangle[0].rhw = triangle[1].rhw = triangle[2].rhw = 1.0f;
    triangle[0].color = 0xffff4040; triangle[1].color = 0xff40ff40;
    triangle[2].color = 0xff4040ff;
    IDirect3DDevice9_SetRenderState(device, D3DRS_CULLMODE, D3DCULL_NONE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_ZENABLE, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_LIGHTING, FALSE);
    IDirect3DDevice9_SetRenderState(device, D3DRS_SCISSORTESTENABLE, FALSE);

    report_hr("Frame-loop-begin", S_OK);
    start_tick = GetTickCount();
    for (frame = 0; frame < NINE_TEST_FRAMES; ++frame) {
        BYTE red = (BYTE)((frame * 4) & 0xff);
        D3DCOLOR clear = D3DCOLOR_XRGB(red, 32, 255 - red);

        hr = IDirect3DDevice9_Clear(device, 0, NULL, D3DCLEAR_TARGET,
            clear, 1.0f, 0);
        if (FAILED(hr)) { report_hr("Frame-Clear", hr); goto done; }
        hr = IDirect3DDevice9_BeginScene(device);
        if (FAILED(hr)) { report_hr("Frame-BeginScene", hr); goto done; }
        hr = IDirect3DDevice9_DrawPrimitiveUP(device, D3DPT_TRIANGLELIST, 1,
            triangle, sizeof(triangle[0]));
        if (FAILED(hr)) { report_hr("Frame-DrawPrimitiveUP-triangle", hr); goto done; }
        hr = IDirect3DDevice9_EndScene(device);
        if (FAILED(hr)) { report_hr("Frame-EndScene", hr); goto done; }
        hr = IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL);
        if (FAILED(hr)) { report_hr("Frame-Present", hr); goto done; }

        if ((frame + 1) % 10 == 0 || frame + 1 == NINE_TEST_FRAMES) {
            char line[96];
            wsprintfA(line, "[WIN98-NINE] frames=%u/%u elapsed_ms=%lu\r\n",
                frame + 1, NINE_TEST_FRAMES,
                (unsigned long)(GetTickCount() - start_tick));
            report_text(line);
        }
    }
    elapsed = GetTickCount() - start_tick;
    if (!elapsed) elapsed = 1;
    {
        char line[128];
        wsprintfA(line, "[WIN98-NINE] RESULT frames=%u elapsed_ms=%lu fps=%lu\r\n",
            NINE_TEST_FRAMES, (unsigned long)elapsed,
            (unsigned long)((NINE_TEST_FRAMES * 1000UL) / elapsed));
        report_text(line);
    }
    report_hr("PASS-Nine-D3D9-60-frame-triangle", S_OK);
    result = 0;

done:
    if (device) IDirect3DDevice9_Release(device);
    if (d3d) IDirect3D9_Release(d3d);
    if (hwnd) DestroyWindow(hwnd);
    if (serial_handle != INVALID_HANDLE_VALUE) {
        CloseHandle(serial_handle);
        serial_handle = INVALID_HANDLE_VALUE;
    }
    return result;
}
