#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0400
#include <windows.h>
#include <winioctl.h>
#include <tlhelp32.h>
#include <string.h>

static void write_complete_marker(const char *text);

static void report(const char *text)
{
    DWORD written;
    HANDLE serial = CreateFileA("\\\\.\\COM1", GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (serial != INVALID_HANDLE_VALUE) {
        WriteFile(serial, text, (DWORD)strlen(text), &written, NULL);
        CloseHandle(serial);
    }
}

static void report_file(const char *path, DWORD max_bytes)
{
    HANDLE file;
    DWORD size, remaining, count;
    char buffer[257];
    file = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        report("[WIN98-GFX-TEST] unable to open diagnostic file\r\n");
        return;
    }
    size = GetFileSize(file, NULL);
    if (size == INVALID_FILE_SIZE) {
        CloseHandle(file);
        report("[WIN98-GFX-TEST] unable to size diagnostic file\r\n");
        return;
    }
    remaining = size < max_bytes ? size : max_bytes;
    report("[WIN98-GFX-TEST] begin D9Switcher log\r\n");
    while (remaining) {
        DWORD request = remaining > 256 ? 256 : remaining;
        if (!ReadFile(file, buffer, request, &count, NULL) || !count)
            break;
        buffer[count] = '\0';
        report(buffer);
        remaining -= count;
    }
    report("\r\n[WIN98-GFX-TEST] end D9Switcher log\r\n");
    CloseHandle(file);
}

static void report_process_modules(DWORD process_id)
{
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, process_id);
    MODULEENTRY32 module;
    char line[320];
    DWORD count = 0;

    if (snapshot == INVALID_HANDLE_VALUE) {
        wsprintfA(line, "[WIN98-GFX-TEST] module snapshot failed error=%lu\r\n",
            GetLastError());
        report(line);
        return;
    }
    ZeroMemory(&module, sizeof(module));
    module.dwSize = sizeof(module);
    if (Module32First(snapshot, &module)) {
        do {
            wsprintfA(line,
                "[WIN98-MODULE] base=%08lX size=%08lX name=%s path=%s\r\n",
                (DWORD)(ULONG_PTR)module.modBaseAddr, module.modBaseSize,
                module.szModule, module.szExePath);
            report(line);
            ++count;
            module.dwSize = sizeof(module);
        } while (Module32Next(snapshot, &module));
    }
    wsprintfA(line, "[WIN98-GFX-TEST] module snapshot count=%lu\r\n", count);
    report(line);
    CloseHandle(snapshot);
}

typedef struct debug_thread_entry {
    DWORD id;
    HANDLE handle;
} DEBUG_THREAD_ENTRY;

static DEBUG_THREAD_ENTRY *find_debug_thread(DEBUG_THREAD_ENTRY *threads,
                                              DWORD count, DWORD id)
{
    DWORD i;
    for (i = 0; i < count; ++i) {
        if (threads[i].id == id)
            return &threads[i];
    }
    return NULL;
}

static void report_debug_exception(const DEBUG_EVENT *event, HANDLE process,
                                   DEBUG_THREAD_ENTRY *threads,
                                   DWORD thread_count)
{
    static DWORD first_chance_count;
    const EXCEPTION_RECORD *record = &event->u.Exception.ExceptionRecord;
    char line[192];
    BYTE code[16];
    SIZE_T copied = 0;
    DWORD i;
    DEBUG_THREAD_ENTRY *thread;
    CONTEXT context;

    if (event->u.Exception.dwFirstChance && first_chance_count++ >= 16)
        return;

    wsprintfA(line,
        "[WIN98-DEBUG] exception code=%08lX address=%08lX first_chance=%lu thread=%lu\r\n",
        record->ExceptionCode, (DWORD)(ULONG_PTR)record->ExceptionAddress,
        event->u.Exception.dwFirstChance, event->dwThreadId);
    report(line);
    if (record->NumberParameters >= 2) {
        wsprintfA(line, "[WIN98-DEBUG] access kind=%08lX address=%08lX\r\n",
            (DWORD)record->ExceptionInformation[0],
            (DWORD)record->ExceptionInformation[1]);
        report(line);
    }

    thread = find_debug_thread(threads, thread_count, event->dwThreadId);
    if (thread && thread->handle) {
        ZeroMemory(&context, sizeof(context));
        context.ContextFlags = CONTEXT_FULL;
        if (GetThreadContext(thread->handle, &context)) {
            wsprintfA(line,
                "[WIN98-DEBUG] regs eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX esi=%08lX edi=%08lX ebp=%08lX esp=%08lX eip=%08lX\r\n",
                context.Eax, context.Ebx, context.Ecx, context.Edx,
                context.Esi, context.Edi, context.Ebp, context.Esp,
                context.Eip);
            report(line);
            if (ReadProcessMemory(process, (LPCVOID)context.Esp, code,
                                  sizeof(code), &copied) && copied) {
                report("[WIN98-DEBUG] stack-bytes=");
                for (i = 0; i < copied; ++i) {
                    wsprintfA(line, "%02X%s", code[i],
                        (i + 1 == copied) ? "\r\n" : " ");
                    report(line);
                }
            }
            if (context.Esi && ReadProcessMemory(process,
                    (LPCVOID)context.Esi, code, sizeof(code), &copied) && copied) {
                report("[WIN98-DEBUG] esi-target-bytes=");
                for (i = 0; i < copied; ++i) {
                    wsprintfA(line, "%02X%s", code[i],
                        (i + 1 == copied) ? "\r\n" : " ");
                    report(line);
                }
            }
        } else {
            wsprintfA(line, "[WIN98-DEBUG] GetThreadContext failed error=%lu\r\n",
                GetLastError());
            report(line);
        }
    }

    if (ReadProcessMemory(process, record->ExceptionAddress, code,
                          sizeof(code), &copied) && copied) {
        report("[WIN98-DEBUG] bytes-at-exception=");
        for (i = 0; i < copied; ++i) {
            wsprintfA(line, "%02X%s", code[i], (i + 1 == copied) ? "\r\n" : " ");
            report(line);
        }
    }

    /* First-chance exceptions may be part of normal Win32 control flow.
       Preserve the app's behavior while recording the module map so an
       unhandled Win9x IPF can be attributed without opening a DOS prompt. */
    report_process_modules(event->dwProcessId);
}

static BOOL wait_for_debugged_process(const PROCESS_INFORMATION *process,
                                      DWORD report_after_ms)
{
    DEBUG_EVENT event;
    DEBUG_THREAD_ENTRY threads[64];
    DWORD thread_count = 0;
    DWORD start = GetTickCount();
    BOOL reported_running = FALSE;
    ZeroMemory(threads, sizeof(threads));

    for (;;) {
        DWORD status;
        if (!WaitForDebugEvent(&event, 1000)) {
            if (!reported_running && GetTickCount() - start >= report_after_ms) {
                report("[WIN98-GFX-TEST] 3DMark03 still running under debugger; leaving it open\r\n");
                write_complete_marker("3DMARK03 STILL RUNNING UNDER DEBUGGER\r\n");
                reported_running = TRUE;
            }
            continue;
        }

        status = DBG_CONTINUE;
        switch (event.dwDebugEventCode) {
        case EXCEPTION_DEBUG_EVENT:
            if (event.u.Exception.ExceptionRecord.ExceptionCode ==
                    EXCEPTION_BREAKPOINT ||
                event.u.Exception.ExceptionRecord.ExceptionCode ==
                    EXCEPTION_SINGLE_STEP) {
                status = DBG_CONTINUE;
            } else {
                report_debug_exception(&event, process->hProcess, threads,
                                       thread_count);
                status = DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        case CREATE_PROCESS_DEBUG_EVENT:
            if (thread_count < sizeof(threads) / sizeof(threads[0])) {
                threads[thread_count].id = event.dwThreadId;
                threads[thread_count].handle = event.u.CreateProcessInfo.hThread;
                ++thread_count;
            } else if (event.u.CreateProcessInfo.hThread) {
                CloseHandle(event.u.CreateProcessInfo.hThread);
            }
            if (event.u.CreateProcessInfo.hFile)
                CloseHandle(event.u.CreateProcessInfo.hFile);
            break;
        case CREATE_THREAD_DEBUG_EVENT:
            if (thread_count < sizeof(threads) / sizeof(threads[0])) {
                threads[thread_count].id = event.dwThreadId;
                threads[thread_count].handle = event.u.CreateThread.hThread;
                ++thread_count;
            } else if (event.u.CreateThread.hThread) {
                CloseHandle(event.u.CreateThread.hThread);
            }
            break;
        case EXIT_THREAD_DEBUG_EVENT: {
            DEBUG_THREAD_ENTRY *thread = find_debug_thread(
                threads, thread_count, event.dwThreadId);
            if (thread && thread->handle) {
                CloseHandle(thread->handle);
                thread->handle = NULL;
            }
            break;
        }
        case LOAD_DLL_DEBUG_EVENT:
            if (event.u.LoadDll.hFile)
                CloseHandle(event.u.LoadDll.hFile);
            break;
        case EXIT_PROCESS_DEBUG_EVENT: {
            char line[128];
            DWORD i;
            wsprintfA(line, "[WIN98-DEBUG] process exit code=%08lX\r\n",
                event.u.ExitProcess.dwExitCode);
            report(line);
            write_complete_marker(event.u.ExitProcess.dwExitCode ?
                "3DMARK03 EXITED WITH ERROR (DEBUGGED)\r\n" :
                "3DMARK03 EXITED (DEBUGGED)\r\n");
            for (i = 0; i < thread_count; ++i) {
                if (threads[i].handle)
                    CloseHandle(threads[i].handle);
            }
            return TRUE;
        }
        default:
            break;
        }

        if (!ContinueDebugEvent(event.dwProcessId, event.dwThreadId, status)) {
            report("[WIN98-DEBUG] ContinueDebugEvent failed\r\n");
            return FALSE;
        }
    }
}

static void write_complete_marker(const char *text)
{
    HANDLE marker = CreateFileA("C:\\test_accel\\win98\\complete.txt",
        GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (marker != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(marker, text, (DWORD)strlen(text), &written, NULL);
        CloseHandle(marker);
    }
}

static void ensure_opengl_icd_registration(void)
{
    HKEY key;
    BOOL key_open = FALSE;
    LONG status = RegOpenKeyExA(HKEY_LOCAL_MACHINE,
        "Software\\Microsoft\\Windows\\CurrentVersion\\OpenGLDrivers",
        0, KEY_READ | KEY_WRITE, &key);
    DWORD type = 0, size = 128;
    char dll[128];
    if (status == ERROR_SUCCESS) {
        key_open = TRUE;
        status = RegQueryValueExA(key, "NVX", NULL, &type,
                                  (LPBYTE)dll, &size);
        if (status == ERROR_SUCCESS && type == REG_SZ) {
            dll[sizeof(dll) - 1] = '\0';
            report("[WIN98-GFX-TEST] OpenGL ICD registry NVX=");
            report(dll);
            report("\r\n");
            RegCloseKey(key);
            return;
        }
    } else {
        DWORD disposition = 0;
        status = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
            "Software\\Microsoft\\Windows\\CurrentVersion\\OpenGLDrivers",
            0, NULL, REG_OPTION_NON_VOLATILE, KEY_READ | KEY_WRITE,
            NULL, &key, &disposition);
        key_open = status == ERROR_SUCCESS;
    }
    if (status == ERROR_SUCCESS && key_open) {
        static const char dll[] = "nvxogl32.dll";
        status = RegSetValueExA(key, "NVX", 0, REG_SZ,
                                (const BYTE *)dll, sizeof(dll));
        RegCloseKey(key);
    }
    report(status == ERROR_SUCCESS ?
        "[WIN98-GFX-TEST] OpenGL ICD registry NVX repaired\r\n" :
        "[WIN98-GFX-TEST] OpenGL ICD registry update failed\r\n");
}

static void enable_winedd_for_tests(void)
{
    HKEY key;
    DWORD disposition;
    LONG status = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
        "Software\\DDSwitcher", 0, NULL, REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WRITE, NULL, &key, &disposition);
    if (status == ERROR_SUCCESS) {
        static const char wine[] = "wine";
        status = RegSetValueExA(key, "legacy_ddraw.exe", 0, REG_SZ,
                                (const BYTE *)wine, sizeof(wine));
        if (status == ERROR_SUCCESS)
            status = RegSetValueExA(key, "3dmark99.exe", 0, REG_SZ,
                                    (const BYTE *)wine, sizeof(wine));
        RegCloseKey(key);
    }
    report(status == ERROR_SUCCESS ?
        "[WIN98-GFX-TEST] DDSwitcher routes legacy tests to WineD3D\r\n" :
        "[WIN98-GFX-TEST] DDSwitcher registry setup failed\r\n");
}

static void enable_nine_for_test(void)
{
    HKEY key;
    DWORD disposition;
    LONG status = RegCreateKeyExA(HKEY_LOCAL_MACHINE,
        "Software\\D9Switcher", 0, NULL, REG_OPTION_NON_VOLATILE,
        KEY_READ | KEY_WRITE, NULL, &key, &disposition);
    if (status == ERROR_SUCCESS) {
        static const char nine[] = "nineforce";
        status = RegSetValueExA(key, "nine_d3d9.exe", 0, REG_SZ,
                                (const BYTE *)nine, sizeof(nine));
        if (status == ERROR_SUCCESS)
            status = RegSetValueExA(key, "3DMark03.exe", 0, REG_SZ,
                                    (const BYTE *)nine, sizeof(nine));
        RegCloseKey(key);
    }
    report(status == ERROR_SUCCESS ?
        "[WIN98-GFX-TEST] D9Switcher routes Nine smoke test and 3DMark03 to Gallium Nine\r\n" :
        "[WIN98-GFX-TEST] D9Switcher registry setup failed\r\n");
}

static void repaint_desktop(const char *stage)
{
    HWND desktop = GetDesktopWindow();
    HWND progman = FindWindowA("Progman", NULL);
    HWND shell = progman ? FindWindowExA(progman, NULL,
        "SHELLDLL_DefView", NULL) : NULL;
    DEVMODEA mode;
    report("[WIN98-GFX-TEST] desktop repaint "); report(stage); report("\r\n");
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode)) {
        char line[128];
        wsprintfA(line, "[WIN98-GFX-TEST] display mode %ux%ux%u\r\n",
            (unsigned)mode.dmPelsWidth, (unsigned)mode.dmPelsHeight,
            (unsigned)mode.dmBitsPerPel);
        report(line);
    } else {
        report("[WIN98-GFX-TEST] display mode query failed\r\n");
    }
    if (desktop) {
        RedrawWindow(desktop, NULL, NULL,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        UpdateWindow(desktop);
    }
    /* On Win98 Explorer owns the desktop view as a child window of Progman.
       Redraw it explicitly; invalidating GetDesktopWindow alone doesn't
       reliably repaint the shell's backing surface after a D3D window exits. */
    if (progman) {
        InvalidateRect(progman, NULL, TRUE);
        RedrawWindow(progman, NULL, NULL,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        UpdateWindow(progman);
    }
    if (shell) {
        InvalidateRect(shell, NULL, TRUE);
        RedrawWindow(shell, NULL, NULL,
            RDW_INVALIDATE | RDW_ERASE | RDW_UPDATENOW | RDW_ALLCHILDREN);
        UpdateWindow(shell);
    }
}

/* A GUI surface can stay readable after a head stops scanning it. Re-apply
 * the already-active mode after Nine exits; this deliberately does not select
 * another resolution, it asks the display driver to recommit its current
 * scanout state and then lets the shell repaint it. */
static void recommit_current_display_mode(void)
{
    DEVMODEA mode;
    LONG status;
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    if (!EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode)) {
        report("[WIN98-GFX-TEST] display recommit: current mode query failed\r\n");
        return;
    }
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    status = ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN | CDS_RESET);
    {
        char line[128];
        wsprintfA(line,
            "[WIN98-GFX-TEST] display recommit %ux%ux%u status=%ld (0x%08lX)\r\n",
            (unsigned)mode.dmPelsWidth, (unsigned)mode.dmPelsHeight,
            (unsigned)mode.dmBitsPerPel, status, (DWORD)status);
        report(line);
    }
}

static BOOL run_test(const char *exe, const char *args, const char *label,
                     DWORD timeout_ms)
{
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    char command[512];
    DWORD result;
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    lstrcpyA(command, "C:\\test_accel\\win98\\");
    lstrcatA(command, exe);
    lstrcatA(command, " ");
    lstrcatA(command, args);
    report("[WIN98-GFX-TEST] start "); report(label); report("\r\n");
    /* Every child here is a Win32 GUI executable (-mwindows).  CREATE_NEW_CONSOLE
       asks Win9x to create a DOS box for it; after the final D3D test exits,
       that console can remain as a black foreground surface over the desktop. */
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE,
                        0, NULL,
                        "C:\\test_accel\\win98", &startup, &process)) {
        report("[WIN98-GFX-TEST] CreateProcess failed\r\n");
        return FALSE;
    }
    if (WaitForSingleObject(process.hProcess, timeout_ms) == WAIT_TIMEOUT) {
        report("[WIN98-GFX-TEST] timeout "); report(label); report("; terminating\r\n");
        TerminateProcess(process.hProcess, 0xDEAD);
        WaitForSingleObject(process.hProcess, 5000);
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        return FALSE;
    }
    GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    report("[WIN98-GFX-TEST] exit "); report(label);
    report(result ? " FAIL\r\n" : " PASS\r\n");
    return result == 0;
}

static void launch_3dmark03_once(void)
{
    static const char exe[] =
        "C:\\Program Files\\Futuremark\\3DMark03\\3DMark03.exe";
    STARTUPINFOA startup;
    PROCESS_INFORMATION process;
    char command[512];
    DWORD wait_result, exit_code = 0;
    BOOL debug_enabled = GetFileAttributesA(
        "C:\\test_accel\\win98\\debug-3dmark03-once.txt") != INVALID_FILE_ATTRIBUTES;

    DeleteFileA("C:\\test_accel\\win98\\3dmark03-once.txt");
    DeleteFileA("C:\\test_accel\\win98\\debug-3dmark03-once.txt");
    DeleteFileA("C:\\d9switch.log");
    /* Consume the one-shot before starting the app, so a crash/reboot cannot
       launch another copy automatically on the next boot. */
    write_complete_marker("RUNNING 3DMARK03; one-shot consumed\r\n");
    /* Record Nine's exposed caps and verify a small hardware-backed draw
       before the benchmark performs its own game-test capability filtering. */
    run_test("nine_d3d9.exe", "", "GALLIUM-NINE-PRECHECK", 60000);
    report("[WIN98-GFX-TEST] launching 3DMark03 with D9Switcher Gallium Nine route\r\n");
    ZeroMemory(&startup, sizeof(startup));
    ZeroMemory(&process, sizeof(process));
    startup.cb = sizeof(startup);
    wsprintfA(command, "\"%s\"", exe);
    report(debug_enabled ?
        "[WIN98-GFX-TEST] starting 3DMark03 with Win32 debug-event capture\r\n" :
        "[WIN98-GFX-TEST] starting 3DMark03 without debugger\r\n");
    if (!CreateProcessA(NULL, command, NULL, NULL, FALSE,
            debug_enabled ? DEBUG_ONLY_THIS_PROCESS : 0, NULL,
            "C:\\Program Files\\Futuremark\\3DMark03", &startup, &process)) {
        if (debug_enabled) {
            report("[WIN98-DEBUG] debug launch failed; retrying normal launch\r\n");
            if (CreateProcessA(NULL, command, NULL, NULL, FALSE, 0, NULL,
                    "C:\\Program Files\\Futuremark\\3DMark03", &startup, &process))
                debug_enabled = FALSE;
            else
                process.hProcess = NULL;
        }
        if (!process.hProcess) {
        char line[128];
        wsprintfA(line, "[WIN98-GFX-TEST] 3DMark03 CreateProcess failed error=%lu\r\n",
            GetLastError());
        report(line);
        write_complete_marker("FAIL 3DMARK03 CreateProcess\r\n");
        report_file("C:\\d9switch.log", 8192);
        return;
        }
    }

    if (debug_enabled) {
        /* DEBUG_ONLY_THIS_PROCESS pauses at loader events until the debugger
           acknowledges them, so do not sleep before dispatch starts. */
        report("[WIN98-GFX-TEST] debugger event loop active\r\n");
        wait_for_debugged_process(&process, 600000);
        if (GetExitCodeProcess(process.hProcess, &exit_code)) {
            char line[128];
            wsprintfA(line, "[WIN98-GFX-TEST] debugged process exit code=0x%08lX\r\n",
                exit_code);
            report(line);
        }
    } else {
        /* Capture module bases before the app exercises device creation. */
        Sleep(3000);
        report_process_modules(process.dwProcessId);
        wait_result = WaitForSingleObject(process.hProcess, 600000);
    }
    if (!debug_enabled && wait_result == WAIT_TIMEOUT) {
        report("[WIN98-GFX-TEST] 3DMark03 still running after 10 minutes; leaving it open\r\n");
        write_complete_marker("3DMARK03 STILL RUNNING\r\n");
    } else if (!debug_enabled && wait_result == WAIT_OBJECT_0 &&
               GetExitCodeProcess(process.hProcess, &exit_code)) {
        char line[128];
        wsprintfA(line, "[WIN98-GFX-TEST] 3DMark03 exited code=0x%08lX\r\n", exit_code);
        report(line);
        write_complete_marker(exit_code ? "3DMARK03 EXITED WITH ERROR\r\n" :
                                           "3DMARK03 EXITED\r\n");
    } else if (!debug_enabled) {
        report("[WIN98-GFX-TEST] 3DMark03 wait/exit-code query failed\r\n");
        write_complete_marker("3DMARK03 STATUS UNKNOWN\r\n");
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    report_file("C:\\d9switch.log", 8192);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous,
                   LPSTR command_line, int show)
{
    HANDLE mutex;
    HANDLE marker;
    BOOL result = TRUE;
    (void)instance; (void)previous; (void)command_line; (void)show;
    mutex = CreateMutexA(NULL, TRUE, "NVXWin98GraphicsTestRunner");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }
    if (GetFileAttributesA("C:\\test_accel\\win98\\complete.txt") !=
        INVALID_FILE_ATTRIBUTES) {
        CloseHandle(mutex);
        return 0;
    }
    report("[WIN98-GFX-TEST] runner begin\r\n");
    ensure_opengl_icd_registration();
    enable_winedd_for_tests();
    enable_nine_for_test();
    if (GetFileAttributesA("C:\\test_accel\\win98\\3dmark03-once.txt") !=
        INVALID_FILE_ATTRIBUTES) {
        launch_3dmark03_once();
        CloseHandle(mutex);
        return 0;
    }
    if (GetFileAttributesA("C:\\test_accel\\win98\\2d-only-once.txt") !=
        INVALID_FILE_ATTRIBUTES) {
        /* Keep display-path validation isolated from the later 3D tests,
           which can change modes or leave the scanout blank on Win98. */
        DeleteFileA("C:\\test_accel\\win98\\2d-only-once.txt");
        result = run_test("antigravity.exe", "--2d-only", "GDI-2D-ONLY", 30000);
        repaint_desktop("after one-shot 2D test");
        Sleep(1000);
        marker = CreateFileA("C:\\test_accel\\win98\\complete.txt",
            GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
        if (marker != INVALID_HANDLE_VALUE) {
            const char *text = result ? "PASS 2D-ONLY\r\n" : "FAIL 2D-ONLY\r\n";
            DWORD written;
            WriteFile(marker, text, (DWORD)strlen(text), &written, NULL);
            CloseHandle(marker);
        }
        report(result ? "[WIN98-GFX-TEST] one-shot 2D test PASS\r\n" :
                        "[WIN98-GFX-TEST] one-shot 2D test FAIL\r\n");
        CloseHandle(mutex);
        return result ? 0 : 1;
    }
    result &= run_test("antigravity.exe", "--2d-only", "GDI-2D", 30000);
    result &= run_test("antigravity.exe", "--opengl-only", "OPENGL-CUBE", 180000);
    result &= run_test("wglgears.exe", "-info -seconds 10", "WGL-GEARS", 120000);
    result &= run_test("legacy_ddraw.exe", "", "LEGACY-DDRAW-D3D", 60000);
    result &= run_test("nine_d3d9.exe", "", "GALLIUM-NINE-D3D9-FRAME-TRIANGLE", 60000);
    /* Nine presents through the WGL/GDI window path on Win98. Force the
       desktop to repaint after its test window is destroyed so a stale
       invalidated region cannot look like a scanout failure. */
    recommit_current_display_mode();
    repaint_desktop("after Nine");
    result &= run_test("antigravity.exe", "--2d-only", "GDI-2D-POST-NINE", 30000);
    repaint_desktop("after post-Nine GDI");
    Sleep(3000);
    marker = CreateFileA("C:\\test_accel\\win98\\complete.txt",
        GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (marker != INVALID_HANDLE_VALUE) {
        const char *text = result ? "PASS\r\n" : "FAIL (see COM1 log)\r\n";
        DWORD written;
        WriteFile(marker, text, (DWORD)strlen(text), &written, NULL);
        CloseHandle(marker);
    }
    report(result ? "[WIN98-GFX-TEST] runner complete PASS\r\n" :
                    "[WIN98-GFX-TEST] runner complete FAIL\r\n");
    CloseHandle(mutex);
    return result ? 0 : 1;
}
