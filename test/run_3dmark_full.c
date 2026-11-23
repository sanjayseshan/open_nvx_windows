#include <windows.h>
#include <stdio.h>
#include <string.h>

static void log_com(const char *msg) {
    HANDLE hCom = CreateFileA("\\\\.\\COM1", GENERIC_WRITE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                              OPEN_EXISTING, 0, NULL);
    if (hCom != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(hCom, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(hCom);
    }
}

static HWND g_hMainWnd = NULL;
static HWND g_hTipWnd = NULL;
static HWND g_hRunBtn = NULL;
static DWORD g_targetPid = 0;

static BOOL CALLBACK EnumTipChildProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    char text[256] = {0};
    char cls[128] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
    GetWindowTextA(hwnd, text, sizeof(text) - 1);
    if (IsWindowVisible(hwnd) && lstrcmpiA(cls, "Button") == 0) {
        if (strstr(text, "Close") || strstr(text, "&Close")) {
            SendMessageA(hwnd, BM_CLICK, 0, 0);
        }
    }
    return TRUE;
}

static BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    char text[256] = {0};
    char cls[128] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
    GetWindowTextA(hwnd, text, sizeof(text) - 1);
    int id = GetDlgCtrlID(hwnd);

    if (IsWindowVisible(hwnd) && lstrcmpiA(cls, "Button") == 0) {
        if (strstr(text, "Run 3DMark") || strstr(text, "Run Benchmark") || strstr(text, "Run") || id == 1) {
            g_hRunBtn = hwnd;
        }
    }
    return TRUE;
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    if (!IsWindowVisible(hwnd))
        return TRUE;
    char text[256] = {0};
    char cls[128] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
    GetWindowTextA(hwnd, text, sizeof(text) - 1);

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != g_targetPid)
        return TRUE;

    if (strstr(text, "Tip of the Day") || strstr(text, "Tip")) {
        g_hTipWnd = hwnd;
        return TRUE;
    }

    if (strstr(text, "Edition")) {
        g_hMainWnd = hwnd;
        return FALSE;
    } else if (!g_hMainWnd && strcmp(cls, "#32770") != 0) {
        g_hMainWnd = hwnd;
    }
    return TRUE;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[512];
    log_com("[3DMARK_AUTOMATION] Starting full 3DMark05 automated benchmark execution...\r\n");

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char cmd[] = "3DMark05.exe -nosysteminfo";
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        snprintf(buf, sizeof(buf), "[3DMARK_AUTOMATION] CreateProcess failed err=%lu\r\n", GetLastError());
        log_com(buf);
        return 1;
    }

    g_targetPid = pi.dwProcessId;
    snprintf(buf, sizeof(buf), "[3DMARK_AUTOMATION] 3DMark05 process created (PID=%lu). Waiting for main window...\r\n", g_targetPid);
    log_com(buf);

    for (int i = 0; i < 90; i++) {
        Sleep(1000);
        g_hMainWnd = NULL;
        g_hTipWnd = NULL;
        EnumWindows(EnumWindowsProc, 0);

        if (g_hTipWnd) {
            log_com("[3DMARK_AUTOMATION] Dismissing 'Tip of the Day' window...\r\n");
            SetForegroundWindow(g_hTipWnd);
            EnumChildWindows(g_hTipWnd, EnumTipChildProc, 0);
            PostMessageA(g_hTipWnd, WM_KEYDOWN, VK_ESCAPE, 0);
            PostMessageA(g_hTipWnd, WM_KEYUP, VK_ESCAPE, 0);
        }

        if (g_hMainWnd && IsWindowVisible(g_hMainWnd)) {
            char mainTitle[256] = {0};
            GetWindowTextA(g_hMainWnd, mainTitle, sizeof(mainTitle) - 1);
            if (strstr(mainTitle, "Edition")) {
                snprintf(buf, sizeof(buf), "[3DMARK_AUTOMATION] Found benchmark main window hwnd=%p title='%s' after %d s\r\n", g_hMainWnd, mainTitle, i + 1);
                log_com(buf);
                break;
            }
        }

        DWORD exitCode = 0;
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            snprintf(buf, sizeof(buf), "[3DMARK_AUTOMATION] Process terminated early exitCode=%lu (0x%08lX)\r\n", exitCode, exitCode);
            log_com(buf);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            return 2;
        }
    }

    if (!g_hMainWnd) {
        log_com("[3DMARK_AUTOMATION] Timed out waiting for main window!\r\n");
    } else {
        SetForegroundWindow(g_hMainWnd);
        SetActiveWindow(g_hMainWnd);
        SetFocus(g_hMainWnd);
        Sleep(2000);

        g_hRunBtn = NULL;
        EnumChildWindows(g_hMainWnd, EnumChildProc, 0);
        if (g_hRunBtn) {
            log_com("[3DMARK_AUTOMATION] Posting one 'Run 3DMark' button click...\r\n");
            /* SendMessage can block in the benchmark's render loop; post a
             * single click and start monitoring without triggering it twice. */
            if (!PostMessageA(g_hRunBtn, BM_CLICK, 0, 0)) {
                log_com("[3DMARK_AUTOMATION] Failed to post benchmark button click.\r\n");
                return 2;
            }
        } else {
            log_com("[3DMARK_AUTOMATION] No Run button found; sending Alt+R fallback once...\r\n");
            keybd_event(VK_MENU, 0, 0, 0);
            keybd_event('R', 0, 0, 0);
            keybd_event('R', 0, KEYEVENTF_KEYUP, 0);
            keybd_event(VK_MENU, 0, KEYEVENTF_KEYUP, 0);
        }
    }

    log_com("[3DMARK_AUTOMATION] Benchmark triggered! Monitoring test progression (up to 20 minutes)...\r\n");

    DWORD waitRes = WaitForSingleObject(pi.hProcess, 1200000); // 20 mins
    DWORD exitCode = 0;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    snprintf(buf, sizeof(buf), "[3DMARK_AUTOMATION] Benchmark execution finished. WaitRes=0x%08lX ExitCode=%lu (0x%08lX)\r\n", waitRes, exitCode, exitCode);
    log_com(buf);

    FILE *result = fopen("C:\\test_accel\\3dmark_automation_result.txt", "w");
    if (result) {
        fprintf(result, "%lu\n", waitRes == WAIT_TIMEOUT ? 259UL : exitCode);
        fclose(result);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (waitRes != WAIT_OBJECT_0)
        return 3;
    return exitCode == 0 ? 0 : 2;
}
