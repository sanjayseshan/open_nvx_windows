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

int main() {
    char buf[512];
    log_com("[CRASH_PROBE] Starting 3DMark05 under debugger with module symbol resolving...\r\n");

    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    ZeroMemory(&pi, sizeof(pi));

    char cmd[] = "3DMark05.exe -nosysteminfo";
    if (!CreateProcessA(NULL, cmd, NULL, NULL, FALSE, DEBUG_PROCESS | DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) {
        snprintf(buf, sizeof(buf), "[CRASH_PROBE] Failed to create process err=%lu\r\n", GetLastError());
        log_com(buf);
        return 1;
    }

    DEBUG_EVENT de;
    BOOL running = TRUE;

    while (running && WaitForDebugEvent(&de, 120000)) {
        DWORD continueStatus = DBG_CONTINUE;
        switch (de.dwDebugEventCode) {
            case LOAD_DLL_DEBUG_EVENT: {
                if (de.u.LoadDll.hFile) {
                    char path[MAX_PATH] = {0};
                    // On XP, GetFinalPathNameByHandle may not exist, so just close handle
                    CloseHandle(de.u.LoadDll.hFile);
                }
                break;
            }
            case OUTPUT_DEBUG_STRING_EVENT: {
                char str[512] = {0};
                DWORD toRead = de.u.DebugString.nDebugStringLength;
                if (toRead >= sizeof(str)) toRead = sizeof(str) - 1;
                ReadProcessMemory(pi.hProcess, de.u.DebugString.lpDebugStringData, str, toRead, NULL);
                snprintf(buf, sizeof(buf), "[3DMARK_OUT] %s\r\n", str);
                log_com(buf);
                break;
            }
            case EXCEPTION_DEBUG_EVENT: {
                DWORD code = de.u.Exception.ExceptionRecord.ExceptionCode;
                PVOID addr = de.u.Exception.ExceptionRecord.ExceptionAddress;
                if (code == EXCEPTION_BREAKPOINT) {
                    continueStatus = DBG_CONTINUE;
                } else {
                    snprintf(buf, sizeof(buf), "[CRASH_PROBE] EXCEPTION code=0x%08lX addr=%p firstChance=%lu\r\n",
                             code, addr, de.u.Exception.dwFirstChance);
                    log_com(buf);

                    // Print context EIP, ESP, registers
                    CONTEXT ctx;
                    ctx.ContextFlags = CONTEXT_FULL;
                    HANDLE hThread = OpenThread(THREAD_GET_CONTEXT, FALSE, de.dwThreadId);
                    if (hThread) {
                        if (GetThreadContext(hThread, &ctx)) {
                            snprintf(buf, sizeof(buf), "  EAX=%08lX EBX=%08lX ECX=%08lX EDX=%08lX ESI=%08lX EDI=%08lX EBP=%08lX ESP=%08lX EIP=%08lX\r\n",
                                     ctx.Eax, ctx.Ebx, ctx.Ecx, ctx.Edx, ctx.Esi, ctx.Edi, ctx.Ebp, ctx.Esp, ctx.Eip);
                            log_com(buf);
                        }
                        CloseHandle(hThread);
                    }

                    if (!de.u.Exception.dwFirstChance) {
                        snprintf(buf, sizeof(buf), "[CRASH_PROBE] CRASH IS SECOND-CHANCE FATAL at %p\r\n", addr);
                        log_com(buf);
                        continueStatus = DBG_EXCEPTION_NOT_HANDLED;
                    } else {
                        continueStatus = DBG_EXCEPTION_NOT_HANDLED;
                    }
                }
                break;
            }
            case EXIT_PROCESS_DEBUG_EVENT:
                snprintf(buf, sizeof(buf), "[CRASH_PROBE] Process terminated exitCode=%lu (0x%08lX)\r\n",
                         de.u.ExitProcess.dwExitCode, de.u.ExitProcess.dwExitCode);
                log_com(buf);
                running = FALSE;
                break;
        }
        ContinueDebugEvent(de.dwProcessId, de.dwThreadId, continueStatus);
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    log_com("[CRASH_PROBE] Done.\r\n");
    return 0;
}
