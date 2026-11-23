#include <windows.h>
#include <stdio.h>

static void log_com(const char *msg) {
    HANDLE hCom = CreateFileA("\\\\.\\COM1", GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hCom != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hCom, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(hCom);
    }
}

static HWND g_h3DMarkWnd = NULL;

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    char title[256] = {0};
    GetWindowTextA(hwnd, title, sizeof(title) - 1);
    if (strstr(title, "3DMark05") != NULL) {
        g_h3DMarkWnd = hwnd;
        return FALSE; // stop enumeration
    }
    return TRUE;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[256];
    log_com("[AUTO_3DMARK] Starting 3DMark05 automation runner...\r\n");

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char cmd[] = "3DMark05.exe -nosysteminfo -nosplash";
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        snprintf(buf, sizeof(buf), "[AUTO_3DMARK] Failed to launch 3DMark05.exe err=%lu\r\n", GetLastError());
        log_com(buf);
        return 1;
    }

    log_com("[AUTO_3DMARK] 3DMark05.exe process created. Waiting for UI window...\r\n");

    // Wait up to 60 seconds for 3DMark05 main window
    for (int i = 0; i < 60; i++) {
        Sleep(1000);
        g_h3DMarkWnd = NULL;
        EnumWindows(EnumWindowsProc, 0);
        if (g_h3DMarkWnd && IsWindowVisible(g_h3DMarkWnd)) {
            snprintf(buf, sizeof(buf), "[AUTO_3DMARK] Found 3DMark05 window hwnd=%p after %d s\r\n", g_h3DMarkWnd, i + 1);
            log_com(buf);
            break;
        }

        // Check if process died
        DWORD exitCode = 0;
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            snprintf(buf, sizeof(buf), "[AUTO_3DMARK] 3DMark05 exited early with code %lu\r\n", exitCode);
            log_com(buf);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return 2;
        }
    }

    if (!g_h3DMarkWnd) {
        log_com("[AUTO_3DMARK] Timeout waiting for 3DMark05 window!\r\n");
    } else {
        // Bring to front
        SetForegroundWindow(g_h3DMarkWnd);
        SetActiveWindow(g_h3DMarkWnd);
        SetFocus(g_h3DMarkWnd);
        Sleep(2000);

        log_com("[AUTO_3DMARK] Sending benchmark trigger keys to 3DMark05...\r\n");

        // Send Space / Enter / Tab sequences
        PostMessageA(g_h3DMarkWnd, WM_KEYDOWN, VK_RETURN, 0x001C0001);
        PostMessageA(g_h3DMarkWnd, WM_KEYUP, VK_RETURN, 0xC01C0001);
        Sleep(1000);
        PostMessageA(g_h3DMarkWnd, WM_COMMAND, MAKEWPARAM(1, BN_CLICKED), 0);
        Sleep(500);
        PostMessageA(g_h3DMarkWnd, WM_KEYDOWN, VK_SPACE, 0x00390001);
        PostMessageA(g_h3DMarkWnd, WM_KEYUP, VK_SPACE, 0xC0390001);
    }

    log_com("[AUTO_3DMARK] Monitoring benchmark execution (up to 15 minutes)...\r\n");

    DWORD waitRes = WaitForSingleObject(pi.hProcess, 900000); // 15 mins
    DWORD finalExit = 0;
    GetExitCodeProcess(pi.hProcess, &finalExit);

    snprintf(buf, sizeof(buf), "[AUTO_3DMARK] Benchmark finished. Wait=0x%lx ExitCode=%lu\r\n", waitRes, finalExit);
    log_com(buf);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)finalExit;
}
