#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0400
#include <windows.h>

static HANDLE probeLog = INVALID_HANDLE_VALUE;

static LRESULT CALLBACK ProbeWndProc(HWND hwnd, UINT msg,
                                     WPARAM wp, LPARAM lp)
{
    PAINTSTRUCT ps;
    HDC dc;
    RECT client;
    static const COLORREF colors[] = {
        RGB(220, 32, 32), RGB(32, 190, 48), RGB(40, 80, 220),
        RGB(240, 210, 32), RGB(220, 32, 200), RGB(24, 190, 200)
    };
    unsigned i;

    switch (msg) {
    case WM_PAINT:
        if (probeLog != INVALID_HANDLE_VALUE) {
            DWORD written;
            WriteFile(probeLog, "PAINT\r\n", 7, &written, NULL);
            FlushFileBuffers(probeLog);
        }
        dc = BeginPaint(hwnd, &ps);
        GetClientRect(hwnd, &client);
        for (i = 0; i < sizeof(colors) / sizeof(colors[0]); ++i) {
            RECT r;
            HBRUSH brush;
            r.left = (LONG)(i % 3) * 180;
            r.top = (LONG)(i / 3) * 150;
            r.right = r.left + 180;
            r.bottom = r.top + 150;
            brush = CreateSolidBrush(colors[i]);
            if (brush) {
                FillRect(dc, &r, brush);
                DeleteObject(brush);
            }
        }
        SetTextColor(dc, RGB(255, 255, 255));
        SetBkMode(dc, TRANSPARENT);
        TextOutA(dc, 12, 310, "NVX Win98 GDI fill bridge probe", 30);
        EndPaint(hwnd, &ps);
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR commandLine, int show)
{
    WNDCLASSA wc;
    HWND hwnd;
    MSG msg;
    (void)previous;
    (void)commandLine;
    probeLog = CreateFileA("C:\\NVXFILL.LOG", GENERIC_WRITE, FILE_SHARE_READ,
                           NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (probeLog != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(probeLog, "START\r\n", 7, &written, NULL);
        FlushFileBuffers(probeLog);
    }
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = ProbeWndProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.lpszClassName = "NvxWin98FillProbe";
    if (!RegisterClassA(&wc)) return 1;
    hwnd = CreateWindowA(wc.lpszClassName,
        "NVX Win98 GDI Fill Probe", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 570, 390,
        NULL, NULL, instance, NULL);
    if (!hwnd) return 2;
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    if (probeLog != INVALID_HANDLE_VALUE) CloseHandle(probeLog);
    return (int)msg.wParam;
}
