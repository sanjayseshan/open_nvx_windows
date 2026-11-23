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

static BOOL CALLBACK EnumWindowsDump(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    if (!IsWindowVisible(hwnd)) return TRUE;
    char text[512] = {0};
    char cls[128] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
    GetWindowTextA(hwnd, text, sizeof(text) - 1);
    if (text[0] && (strstr(text, "Error") || strstr(text, "Problem") || strstr(text, "3DMark") || strstr(text, "Futuremark") || strcmp(cls, "#32770") == 0)) {
        char buf[1024];
        snprintf(buf, sizeof(buf), "[PROBE_WINDOW] [%s] '%s'\r\n", cls, text);
        log_com(buf);
    }
    return TRUE;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    char buf[512];
    log_com("[RUN_3DMARK_DEBUG] Starting 3DMark05 with live window monitor...\r\n");

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char cmd[] = "3DMark05.exe -nosysteminfo";
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        snprintf(buf, sizeof(buf), "[RUN_3DMARK_DEBUG] CreateProcess failed err=%lu\r\n", GetLastError());
        log_com(buf);
        return 1;
    }

    log_com("[RUN_3DMARK_DEBUG] Process spawned. Polling for windows/dialogs every 2 seconds...\r\n");

    for (int i = 0; i < 60; i++) {
        Sleep(2000);
        snprintf(buf, sizeof(buf), "[POLL %d s]\r\n", (i + 1) * 2);
        log_com(buf);
        EnumWindows(EnumWindowsDump, 0);

        DWORD exitCode = 0;
        if (GetExitCodeProcess(pi.hProcess, &exitCode) && exitCode != STILL_ACTIVE) {
            snprintf(buf, sizeof(buf), "[RUN_3DMARK_DEBUG] 3DMark exited with code %lu (0x%08lX)\r\n", exitCode, exitCode);
            log_com(buf);
            break;
        }
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    log_com("[RUN_3DMARK_DEBUG] Finished.\r\n");
    return 0;
}
