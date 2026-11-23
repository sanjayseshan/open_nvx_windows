#include <windows.h>
#include <stdio.h>
#include <d3d9.h>

static void LogCom(const char *str) {
    HANDLE hCom = CreateFileA("\\\\.\\COM1", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (hCom != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hCom, str, (DWORD)strlen(str), &written, NULL);
        CloseHandle(hCom);
    }
}

int main(int argc, char **argv) {
    LogCom("[MIN_D3D9] Loading d3d9.dll...\r\n");
    HMODULE hD3D9 = LoadLibraryA("d3d9.dll");
    if (!hD3D9) {
        LogCom("[MIN_D3D9] Failed to load d3d9.dll\r\n");
        return 1;
    }
    typedef IDirect3D9* (WINAPI *LPDIRECT3DCREATE9)(UINT);
    LPDIRECT3DCREATE9 pDirect3DCreate9 = (LPDIRECT3DCREATE9)GetProcAddress(hD3D9, "Direct3DCreate9");
    if (!pDirect3DCreate9) {
        LogCom("[MIN_D3D9] No Direct3DCreate9\r\n");
        return 1;
    }
    LogCom("[MIN_D3D9] Calling Direct3DCreate9...\r\n");
    IDirect3D9 *pD3D = pDirect3DCreate9(D3D_SDK_VERSION);
    if (!pD3D) {
        LogCom("[MIN_D3D9] Direct3DCreate9 returned NULL\r\n");
        return 1;
    }
    LogCom("[MIN_D3D9] Creating test window...\r\n");
    HWND hwnd = CreateWindowA("STATIC", "TestD3D", WS_OVERLAPPEDWINDOW, 0, 0, 640, 480, NULL, NULL, NULL, NULL);

    D3DPRESENT_PARAMETERS d3dpp;
    memset(&d3dpp, 0, sizeof(d3dpp));
    d3dpp.Windowed = TRUE;
    d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    d3dpp.hDeviceWindow = hwnd;
    d3dpp.BackBufferFormat = D3DFMT_UNKNOWN;

    IDirect3DDevice9 *pDev = NULL;
    LogCom("[MIN_D3D9] Calling CreateDevice...\r\n");
    HRESULT hr = pD3D->lpVtbl->CreateDevice(pD3D, 0, D3DDEVTYPE_HAL, hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &d3dpp, &pDev);
    char buf[128];
    snprintf(buf, sizeof(buf), "[MIN_D3D9] CreateDevice returned 0x%08lX, pDev=%p\r\n", (unsigned long)hr, pDev);
    LogCom(buf);

    if (SUCCEEDED(hr) && pDev) {
        LogCom("[MIN_D3D9] Testing Clear...\r\n");
        hr = pDev->lpVtbl->Clear(pDev, 0, NULL, D3DCLEAR_TARGET, 0xFFFF00FF, 1.0f, 0);
        snprintf(buf, sizeof(buf), "[MIN_D3D9] Clear returned 0x%08lX\r\n", (unsigned long)hr);
        LogCom(buf);

        LogCom("[MIN_D3D9] Testing Present...\r\n");
        hr = pDev->lpVtbl->Present(pDev, NULL, NULL, NULL, NULL);
        snprintf(buf, sizeof(buf), "[MIN_D3D9] Present returned 0x%08lX\r\n", (unsigned long)hr);
        LogCom(buf);

        LogCom("[MIN_D3D9] Releasing Device...\r\n");
        pDev->lpVtbl->Release(pDev);
    }
    pD3D->lpVtbl->Release(pD3D);
    LogCom("[MIN_D3D9] Done!\r\n");
    return 0;
}
