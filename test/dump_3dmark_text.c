#include <windows.h>
#include <stdio.h>

static FILE *g_log = NULL;

static BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    char text[512] = {0};
    char cls[128] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
    GetWindowTextA(hwnd, text, sizeof(text) - 1);
    if (text[0]) {
        fprintf(g_log, "  [%s] %s\n", cls, text);
        fflush(g_log);
    }
    return TRUE;
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    char text[512] = {0};
    GetWindowTextA(hwnd, text, sizeof(text) - 1);
    if (strstr(text, "3DMark") || strstr(text, "Futuremark") || strstr(text, "Result")) {
        fprintf(g_log, "WINDOW: %s\n", text);
        EnumChildWindows(hwnd, EnumChildProc, 0);
    }
    return TRUE;
}

int main(int argc, char **argv) {
    const char *out_path = (argc > 1) ? argv[1] : "C:\\3dmark_text.log";
    g_log = fopen(out_path, "w");
    if (!g_log) return 1;
    EnumWindows(EnumWindowsProc, 0);
    fclose(g_log);
    return 0;
}
