#include <windows.h>
#include <d3d9.h>
#include <stdio.h>

static void log_com(const char *msg) {
    HANDLE hCom = CreateFileA("\\\\.\\COM1", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hCom != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hCom, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(hCom);
    }
}

typedef IDirect3D9 * (WINAPI *pfnDirect3DCreate9)(UINT SDKVersion);

int main() {
    char buf[512];
    log_com("[TRACE_D3D9_DEVICE] Starting CreateDevice diagnostic test...\r\n");

    HMODULE hD3D9 = LoadLibraryA("d3d9.dll");
    if (!hD3D9) {
        snprintf(buf, sizeof(buf), "[TRACE_D3D9_DEVICE] Failed to load d3d9.dll err=%lu\r\n", GetLastError());
        log_com(buf);
        return 1;
    }

    pfnDirect3DCreate9 pDirect3DCreate9 = (pfnDirect3DCreate9)GetProcAddress(hD3D9, "Direct3DCreate9");
    if (!pDirect3DCreate9) {
        log_com("[TRACE_D3D9_DEVICE] Failed to get Direct3DCreate9 proc\r\n");
        return 1;
    }

    IDirect3D9 *pD3D = pDirect3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        log_com("[TRACE_D3D9_DEVICE] Direct3DCreate9 returned NULL\r\n");
        return 1;
    }

    // Check MultiSampleType for X8R8G8B8
    DWORD qualityLevels = 0;
    HRESULT hr = pD3D->lpVtbl->CheckDeviceMultiSampleType(pD3D, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, FALSE, D3DMULTISAMPLE_NONE, &qualityLevels);
    snprintf(buf, sizeof(buf), "[TRACE_D3D9_DEVICE] CheckDeviceMultiSampleType(X8R8G8B8, NONE) hr=0x%08lX quality=%lu\r\n", hr, qualityLevels);
    log_com(buf);

    hr = pD3D->lpVtbl->CheckDeviceMultiSampleType(pD3D, 0, D3DDEVTYPE_HAL, D3DFMT_A8R8G8B8, FALSE, D3DMULTISAMPLE_NONE, &qualityLevels);
    snprintf(buf, sizeof(buf), "[TRACE_D3D9_DEVICE] CheckDeviceMultiSampleType(A8R8G8B8, NONE) hr=0x%08lX quality=%lu\r\n", hr, qualityLevels);
    log_com(buf);

    // Create a dummy hidden window for CreateDevice
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = "D3D9TestWnd";
    RegisterClassA(&wc);
    HWND hWnd = CreateWindowA("D3D9TestWnd", "D3D9Test", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, NULL, NULL, GetModuleHandle(NULL), NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    ZeroMemory(&d3dpp, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
    d3dpp.BackBufferCount = 1;
    d3dpp.BackBufferWidth = 640;
    d3dpp.BackBufferHeight = 480;
    d3dpp.hDeviceWindow = hWnd;
    d3dpp.EnableAutoDepthStencil = TRUE;
    d3dpp.AutoDepthStencilFormat = D3DFMT_D24S8;

    IDirect3DDevice9 *pDevice = NULL;
    log_com("[TRACE_D3D9_DEVICE] Calling CreateDevice(HAL, HARDWARE_VERTEXPROCESSING)...\r\n");
    hr = pD3D->lpVtbl->CreateDevice(pD3D, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hWnd, D3DCREATE_HARDWARE_VERTEXPROCESSING, &d3dpp, &pDevice);
    snprintf(buf, sizeof(buf), "[TRACE_D3D9_DEVICE] CreateDevice hr=0x%08lX pDevice=%p\r\n", hr, pDevice);
    log_com(buf);

    if (SUCCEEDED(hr) && pDevice) {
        log_com("[TRACE_D3D9_DEVICE] Testing Clear, BeginScene, EndScene, Present...\r\n");
        hr = pDevice->lpVtbl->Clear(pDevice, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, D3DCOLOR_XRGB(0, 128, 255), 1.0f, 0);
        snprintf(buf, sizeof(buf), "[TRACE_D3D9_DEVICE] Clear hr=0x%08lX\r\n", hr);
        log_com(buf);

        hr = pDevice->lpVtbl->BeginScene(pDevice);
        snprintf(buf, sizeof(buf), "[TRACE_D3D9_DEVICE] BeginScene hr=0x%08lX\r\n", hr);
        log_com(buf);

        hr = pDevice->lpVtbl->EndScene(pDevice);
        snprintf(buf, sizeof(buf), "[TRACE_D3D9_DEVICE] EndScene hr=0x%08lX\r\n", hr);
        log_com(buf);

        hr = pDevice->lpVtbl->Present(pDevice, NULL, NULL, NULL, NULL);
        snprintf(buf, sizeof(buf), "[TRACE_D3D9_DEVICE] Present hr=0x%08lX\r\n", hr);
        log_com(buf);

        pDevice->lpVtbl->Release(pDevice);
        log_com("[TRACE_D3D9_DEVICE] SUCCESS: Direct3D 9 hardware accelerated rendering operational!\r\n");
    }

    pD3D->lpVtbl->Release(pD3D);
    DestroyWindow(hWnd);
    return 0;
}
