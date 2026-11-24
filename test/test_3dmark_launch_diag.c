#include <windows.h>
#include <psapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MODULES 192
struct module_info {
    DWORD base;
    DWORD size;
    char path[MAX_PATH];
};

static struct module_info modules[MAX_MODULES];
static unsigned module_count;
static HANDLE benchmark_process;
static DWORD benchmark_pid;
static void log_com(const char *msg);

static HWND tip_window;
static HWND main_window;
static HWND fallback_window;
static HWND run_button;
static HWND first_button;
static unsigned window_trace_count;
static unsigned child_trace_count;

static BOOL CALLBACK find_run_button(HWND hwnd, LPARAM unused) {
    (void)unused;
    if (!IsWindowVisible(hwnd)) return TRUE;
    char cls[96] = {0};
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
    char text[192] = {0};
    DWORD_PTR text_result = 0;
    SendMessageTimeoutA(hwnd, WM_GETTEXT, sizeof(text), (LPARAM)text,
                        SMTO_ABORTIFHUNG, 150, &text_result);
    if (child_trace_count++ < 32) {
        char msg[384];
        snprintf(msg, sizeof(msg),
                 "[3DMARK_DIAG_AUTO] child class='%s' id=%d text='%s'\r\n",
                 cls, GetDlgCtrlID(hwnd), text);
        log_com(msg);
    }
    if (IsWindowEnabled(hwnd) &&
        (strstr(text, "Run") || strstr(text, "run") ||
         strstr(text, "Start") || strstr(text, "start"))) {
        run_button = hwnd;
        return FALSE;
    }
    if (!lstrcmpiA(cls, "Button")) {
        if (!first_button)
            first_button = hwnd;
        if (GetDlgCtrlID(hwnd) == 1) {
            run_button = hwnd;
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL CALLBACK find_benchmark_window(HWND hwnd, LPARAM unused) {
    (void)unused;
    if (!IsWindowVisible(hwnd)) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid != benchmark_pid) return TRUE;
    char title[256] = {0};
    char cls[128] = {0};
    GetWindowTextA(hwnd, title, sizeof(title) - 1);
    GetClassNameA(hwnd, cls, sizeof(cls) - 1);
    if (strstr(title, "Tip of the Day") || strstr(title, "Tip")) {
        tip_window = hwnd;
        return TRUE;
    }
    if (strstr(title, "Edition")) {
        main_window = hwnd;
        return FALSE;
    }
    /* The Free Edition may not include "Edition" in its window caption.
     * Keep a same-process top-level fallback and select the one that exposes
     * the default Run button below. */
    if (!fallback_window && strcmp(cls, "#32770") != 0 &&
        GetWindow(hwnd, GW_OWNER) == NULL)
        fallback_window = hwnd;
    if (window_trace_count++ < 8) {
        char msg[512];
        snprintf(msg, sizeof(msg),
                 "[3DMARK_DIAG_AUTO] candidate hwnd=%p class='%s' title='%s'\r\n",
                 hwnd, cls, title);
        log_com(msg);
    }
    return TRUE;
}

static int auto_start_benchmark(void) {
    log_com("[3DMARK_DIAG_AUTO] Waiting for the benchmark selection window...\r\n");
    for (unsigned second = 0; second < 180; ++second) {
        DWORD exit_code = STILL_ACTIVE;
        if (!GetExitCodeProcess(benchmark_process, &exit_code) || exit_code != STILL_ACTIVE)
            break;
        tip_window = main_window = fallback_window = NULL;
        EnumWindows(find_benchmark_window, 0);
        if (!main_window)
            main_window = fallback_window;
        if (tip_window) {
            PostMessageA(tip_window, WM_KEYDOWN, VK_ESCAPE, 0);
            PostMessageA(tip_window, WM_KEYUP, VK_ESCAPE, 0);
        }
        if (main_window) {
            Sleep(2000);
            run_button = first_button = NULL;
            EnumChildWindows(main_window, find_run_button, 0);
            if (!run_button)
                run_button = first_button;
            if (run_button) {
                char msg[256];
                snprintf(msg, sizeof(msg),
                         "[3DMARK_DIAG_AUTO] Posting button click hwnd=%p id=%d pid=%lu\r\n",
                         run_button, GetDlgCtrlID(run_button),
                         (unsigned long)benchmark_pid);
                log_com(msg);
                SetForegroundWindow(main_window);
                PostMessageA(run_button, BM_CLICK, 0, 0);
                return 0;
            }
            /* The x86 debugger suspends the target while it services debug
             * events, so avoid waiting forever on a cross-process control
             * query. The benchmark exposes Alt+R as its Run accelerator. */
            log_com("[3DMARK_DIAG_AUTO] Run button not found; posting Alt+R.\r\n");
            SetForegroundWindow(main_window);
            PostMessageA(main_window, WM_SYSKEYDOWN, VK_MENU, 0x20380001L);
            PostMessageA(main_window, WM_SYSKEYDOWN, 'R', 0x00130001L);
            PostMessageA(main_window, WM_SYSKEYUP, 'R', 0xC0130001L);
            PostMessageA(main_window, WM_SYSKEYUP, VK_MENU, 0xC0380001L);
            return 0;
        }
        Sleep(1000);
    }
    log_com("[3DMARK_DIAG_AUTO] No Run button was clicked before timeout/exit.\r\n");
    return 1;
}

static BOOL spawn_ui_helper(DWORD pid) {
    char image[MAX_PATH], command[MAX_PATH + 64], msg[256];
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    DWORD length = GetModuleFileNameA(NULL, image, sizeof(image));
    if (!length || length >= sizeof(image)) return FALSE;
    snprintf(command, sizeof(command), "\"%s\" --click %lu", image,
             (unsigned long)pid);
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    if (!CreateProcessA(image, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, NULL, &si, &pi)) {
        snprintf(msg, sizeof(msg),
                 "[3DMARK_DIAG_AUTO] Helper CreateProcess failed err=%lu\r\n",
                 (unsigned long)GetLastError());
        log_com(msg);
        return FALSE;
    }
    snprintf(msg, sizeof(msg), "[3DMARK_DIAG_AUTO] External UI helper pid=%lu\r\n",
             (unsigned long)pi.dwProcessId);
    log_com(msg);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return TRUE;
}

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

static DWORD image_size(HANDLE process, DWORD base) {
    IMAGE_DOS_HEADER dos;
    IMAGE_NT_HEADERS32 nt;
    SIZE_T read = 0;
    if (!ReadProcessMemory(process, (LPCVOID)(ULONG_PTR)base, &dos, sizeof(dos), &read) ||
        read != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE)
        return 0;
    DWORD nt_addr = base + (DWORD)dos.e_lfanew;
    if (!ReadProcessMemory(process, (LPCVOID)(ULONG_PTR)nt_addr, &nt, sizeof(nt), &read) ||
        read < sizeof(nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
        nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        return 0;
    return nt.OptionalHeader.SizeOfImage;
}

static void log_module(HANDLE process, DWORD base, HANDLE file) {
    if (!base || module_count >= MAX_MODULES)
        return;
    struct module_info *m = &modules[module_count++];
    m->base = base;
    m->size = image_size(process, base);
    DWORD n = GetMappedFileNameA(process, (LPVOID)(ULONG_PTR)base, m->path,
                                 sizeof(m->path));
    if (!n && file)
        lstrcpynA(m->path, "(mapped image path unavailable)", sizeof(m->path));
    char buf[768];
    snprintf(buf, sizeof(buf), "[3DMARK_MODULE] base=%08lx size=%08lx path='%s'\r\n",
             (unsigned long)m->base, (unsigned long)m->size, m->path);
    log_com(buf);
}

static void log_exception(HANDLE process, const DEBUG_EVENT *de, unsigned number) {
    DWORD code = de->u.Exception.ExceptionRecord.ExceptionCode;
    DWORD address = (DWORD)(ULONG_PTR)de->u.Exception.ExceptionRecord.ExceptionAddress;
    DWORD module_base = 0;
    char mapped_path[MAX_PATH] = "(unmapped)";
    const char *module_path = mapped_path;
    DWORD module_offset = 0;
    for (unsigned i = 0; i < module_count; ++i) {
        if (modules[i].size && address >= modules[i].base &&
            address - modules[i].base < modules[i].size) {
            module_base = modules[i].base;
            module_path = modules[i].path;
            module_offset = address - modules[i].base;
            break;
        }
    }
    if (!module_base)
        GetMappedFileNameA(process, (LPVOID)(ULONG_PTR)address, mapped_path,
                           sizeof(mapped_path));

    char buf[1024];
    snprintf(buf, sizeof(buf),
             "[3DMARK_EXCEPTION] #%u code=%08lx address=%08lx firstChance=%lu module='%s' base=%08lx offset=%08lx\r\n",
             number, (unsigned long)code, (unsigned long)address,
             (unsigned long)de->u.Exception.dwFirstChance, module_path,
             (unsigned long)module_base, (unsigned long)module_offset);
    log_com(buf);

    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                               FALSE, de->dwThreadId);
    if (thread) {
        CONTEXT ctx;
        ZeroMemory(&ctx, sizeof(ctx));
        ctx.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread, &ctx)) {
            snprintf(buf, sizeof(buf),
                     "[3DMARK_CONTEXT] EIP=%08lx ESP=%08lx EBP=%08lx EAX=%08lx EBX=%08lx ECX=%08lx EDX=%08lx ESI=%08lx EDI=%08lx\r\n",
                     (unsigned long)ctx.Eip, (unsigned long)ctx.Esp,
                     (unsigned long)ctx.Ebp, (unsigned long)ctx.Eax,
                     (unsigned long)ctx.Ebx, (unsigned long)ctx.Ecx,
                     (unsigned long)ctx.Edx, (unsigned long)ctx.Esi,
                     (unsigned long)ctx.Edi);
            log_com(buf);

            DWORD stack[24] = {0};
            SIZE_T stack_bytes = 0;
            if (ReadProcessMemory(process, (LPCVOID)(ULONG_PTR)ctx.Esp,
                                  stack, sizeof(stack), &stack_bytes)) {
                unsigned count = (unsigned)(stack_bytes / sizeof(stack[0]));
                for (unsigned i = 0; i < count; ++i) {
                    DWORD value = stack[i];
                    DWORD caller_base = 0, caller_offset = 0;
                    const char *caller_path = "(non-image)";
                    for (unsigned m = 0; m < module_count; ++m) {
                        if (modules[m].size && value >= modules[m].base &&
                            value - modules[m].base < modules[m].size) {
                            caller_base = modules[m].base;
                            caller_offset = value - modules[m].base;
                            caller_path = modules[m].path;
                            break;
                        }
                    }
                    snprintf(buf, sizeof(buf),
                             "[3DMARK_STACK] +%02x=%08lx module=%08lx+%08lx '%s'\r\n",
                             i * (unsigned)sizeof(stack[0]), (unsigned long)value,
                             (unsigned long)caller_base,
                             (unsigned long)caller_offset, caller_path);
                    log_com(buf);
                }
            } else {
                snprintf(buf, sizeof(buf),
                         "[3DMARK_STACK] ReadProcessMemory failed err=%lu\r\n",
                         (unsigned long)GetLastError());
                log_com(buf);
            }
        }
        CloseHandle(thread);
    }
}

static int is_interesting_exception(DWORD code) {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
           code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_STACK_OVERFLOW ||
           code == 0xC0000374UL;
}

int main(int argc, char **argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    char buf[512];
    if (argc == 3 && !strcmp(argv[1], "--click")) {
        benchmark_pid = strtoul(argv[2], NULL, 10);
        benchmark_process = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_INFORMATION,
                                        FALSE, benchmark_pid);
        if (!benchmark_pid || !benchmark_process) {
            snprintf(buf, sizeof(buf),
                     "[3DMARK_DIAG_AUTO] OpenProcess pid=%lu failed err=%lu\r\n",
                     (unsigned long)benchmark_pid, (unsigned long)GetLastError());
            log_com(buf);
            return 2;
        }
        int result = auto_start_benchmark();
        CloseHandle(benchmark_process);
        return result;
    }
    log_com("[3DMARK_DIAG] Launching 3DMark05 under x86 debugger...\r\n");

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));
    char cmd[] = "3DMark05.exe -nosysteminfo -nosplash";
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE,
                        DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS,
                        NULL, NULL, &si, &pi)) {
        snprintf(buf, sizeof(buf), "[3DMARK_DIAG] CreateProcess failed err=%lu\r\n",
                 (unsigned long)GetLastError());
        log_com(buf);
        return 1;
    }

    snprintf(buf, sizeof(buf), "[3DMARK_DIAG] PID=%lu process=%p\r\n",
             (unsigned long)pi.dwProcessId, pi.hProcess);
    log_com(buf);
    if (!spawn_ui_helper(pi.dwProcessId))
        log_com("[3DMARK_DIAG_AUTO] Could not launch helper; benchmark needs a manual Run click.\r\n");

    DEBUG_EVENT de;
    BOOL running = TRUE;
    unsigned exception_count = 0;
    while (running) {
        if (!WaitForDebugEvent(&de, 180000)) {
            DWORD exit_code = STILL_ACTIVE;
            if (!GetExitCodeProcess(pi.hProcess, &exit_code) || exit_code != STILL_ACTIVE) {
                snprintf(buf, sizeof(buf), "[3DMARK_DIAG] Process ended without EXIT_PROCESS event code=%08lx\r\n",
                         (unsigned long)exit_code);
                log_com(buf);
                break;
            }
            log_com("[3DMARK_DIAG] Debug event wait timed out; process still active.\r\n");
            continue;
        }

        DWORD continue_status = DBG_CONTINUE;
        switch (de.dwDebugEventCode) {
        case CREATE_PROCESS_DEBUG_EVENT:
            log_module(pi.hProcess, (DWORD)(ULONG_PTR)de.u.CreateProcessInfo.lpBaseOfImage,
                       de.u.CreateProcessInfo.hFile);
            if (de.u.CreateProcessInfo.hFile)
                CloseHandle(de.u.CreateProcessInfo.hFile);
            break;
        case LOAD_DLL_DEBUG_EVENT:
            log_module(pi.hProcess, (DWORD)(ULONG_PTR)de.u.LoadDll.lpBaseOfDll,
                       de.u.LoadDll.hFile);
            if (de.u.LoadDll.hFile)
                CloseHandle(de.u.LoadDll.hFile);
            break;
        case OUTPUT_DEBUG_STRING_EVENT: {
            char text[512] = {0};
            DWORD length = de.u.DebugString.nDebugStringLength;
            if (length >= sizeof(text)) length = sizeof(text) - 1;
            SIZE_T read = 0;
            ReadProcessMemory(pi.hProcess, de.u.DebugString.lpDebugStringData,
                              text, length, &read);
            text[read < sizeof(text) ? read : sizeof(text) - 1] = 0;
            snprintf(buf, sizeof(buf), "[3DMARK_DEBUG_STRING] %s\r\n", text);
            log_com(buf);
            break;
        }
        case EXCEPTION_DEBUG_EVENT:
            if (is_interesting_exception(de.u.Exception.ExceptionRecord.ExceptionCode) &&
                (exception_count < 12 || !de.u.Exception.dwFirstChance)) {
                log_exception(pi.hProcess, &de, ++exception_count);
            }
            if (de.u.Exception.ExceptionRecord.ExceptionCode == EXCEPTION_BREAKPOINT)
                continue_status = DBG_CONTINUE;
            else
                continue_status = DBG_EXCEPTION_NOT_HANDLED;
            break;
        case EXIT_PROCESS_DEBUG_EVENT:
            snprintf(buf, sizeof(buf), "[3DMARK_DIAG] EXIT_PROCESS code=%08lx\r\n",
                     (unsigned long)de.u.ExitProcess.dwExitCode);
            log_com(buf);
            running = FALSE;
            break;
        default:
            break;
        }
        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, continue_status);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    log_com("[3DMARK_DIAG] Finished.\r\n");
    return 0;
}
