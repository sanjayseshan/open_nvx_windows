#include <windows.h>
#include <stdio.h>
#include <string.h>

static void log_com(const char *msg) {
    HANDLE h = CreateFileA("\\\\.\\COM1", GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(h);
    }
}

static BOOL contains_ci(const char *haystack, const char *needle) {
    if (!haystack || !needle || !*needle)
        return FALSE;
    for (const char *p = haystack; *p; ++p) {
        size_t i = 0;
        while (needle[i] && p[i]) {
            char a = p[i], b = needle[i];
            if (a >= 'A' && a <= 'Z') a = (char)(a + ('a' - 'A'));
            if (b >= 'A' && b <= 'Z') b = (char)(b + ('a' - 'A'));
            if (a != b) break;
            ++i;
        }
        if (!needle[i])
            return TRUE;
    }
    return FALSE;
}

static HWND g_seen[64];
static unsigned g_seen_count;
static char g_child_text[4096];
static BOOL g_click_safe_button;

static BOOL already_seen(HWND hwnd) {
    for (unsigned i = 0; i < g_seen_count; ++i)
        if (g_seen[i] == hwnd)
            return TRUE;
    return FALSE;
}

static void mark_seen(HWND hwnd) {
    if (g_seen_count < sizeof(g_seen) / sizeof(g_seen[0]))
        g_seen[g_seen_count++] = hwnd;
}

static BOOL CALLBACK EnumChildProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    char text[512] = {0};
    char cls[128] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
    GetWindowTextA(hwnd, text, sizeof(text) - 1);
    if (text[0]) {
        size_t used = strlen(g_child_text);
        if (used + strlen(text) + 4 < sizeof(g_child_text)) {
            if (used) strcat(g_child_text, " | ");
            strcat(g_child_text, text);
        }
        char buf[768];
        snprintf(buf, sizeof(buf), "[DIALOG_CHILD] class='%s' text='%s'\r\n", cls, text);
        log_com(buf);
    }
    if (g_click_safe_button && lstrcmpiA(cls, "Button") == 0 &&
        (lstrcmpiA(text, "OK") == 0 || lstrcmpiA(text, "Close") == 0 ||
         lstrcmpiA(text, "Cancel") == 0 || lstrcmpiA(text, "Don't Send") == 0 ||
         lstrcmpiA(text, "&Don't Send") == 0))
        SendMessageA(hwnd, BM_CLICK, 0, 0);
    return TRUE;
}

static BOOL CALLBACK EnumWindowsProc(HWND hwnd, LPARAM lParam) {
    (void)lParam;
    if (!IsWindowVisible(hwnd))
        return TRUE;

    char title[512] = {0};
    char cls[128] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
    GetWindowTextA(hwnd, title, sizeof(title) - 1);
    BOOL is_dialog = lstrcmpiA(cls, "#32770") == 0;
    BOOL is_3dmark = contains_ci(title, "3DMark") || contains_ci(title, "Futuremark");
    if (!is_dialog && !is_3dmark)
        return TRUE;
    if (already_seen(hwnd))
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    char buf[1024];
    snprintf(buf, sizeof(buf), "[WINDOW_CAPTURE] hwnd=%p pid=%lu class='%s' title='%s'\r\n",
             hwnd, pid, cls, title);
    log_com(buf);
    g_child_text[0] = 0;
    EnumChildWindows(hwnd, EnumChildProc, 0);
    mark_seen(hwnd);

    BOOL error_dialog = contains_ci(title, "error") || contains_ci(title, "problem") ||
                        contains_ci(title, "exception") || contains_ci(title, "corrupt") ||
                        contains_ci(g_child_text, "encountered a problem") ||
                        contains_ci(g_child_text, "application error") ||
                        contains_ci(g_child_text, "corrupt file") ||
                        contains_ci(g_child_text, "not a valid win32 application") ||
                        contains_ci(g_child_text, "has encountered");
    if (is_dialog && error_dialog) {
        snprintf(buf, sizeof(buf), "[ERROR_DIALOG] title='%s' text='%s'\r\n", title, g_child_text);
        log_com(buf);
        g_click_safe_button = TRUE;
        EnumChildWindows(hwnd, EnumChildProc, 0);
        g_click_safe_button = FALSE;
    }
    return TRUE;
}

int main(void) {
    log_com("[MODAL_DISMISSER] Capturing visible modal and 3DMark window text...\r\n");
    for (int i = 0; i < 300; ++i) {
        EnumWindows(EnumWindowsProc, 0);
        Sleep(1000);
    }
    return 0;
}
