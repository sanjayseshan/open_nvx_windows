#ifndef NVX_EXCEPTION_TRACE_H
#define NVX_EXCEPTION_TRACE_H

/* Test-only first-chance diagnostics. Preserve normal exception handling. */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>

static void nvx_exception_write(const char *text)
{
    DWORD written;
    HANDLE file = CreateFileA("C:\\nvx_exception.log", FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, text, (DWORD)strlen(text), &written, NULL);
        CloseHandle(file);
    }
}

static void nvx_exception_address(const char *label, uintptr_t address)
{
    MEMORY_BASIC_INFORMATION memory;
    char module[MAX_PATH] = "unknown", line[512];
    uintptr_t base = 0;
    if (VirtualQuery((void *)address, &memory, sizeof(memory))) {
        base = (uintptr_t)memory.AllocationBase;
        GetModuleFileNameA((HMODULE)base, module, sizeof(module));
    }
    snprintf(line, sizeof(line), "[NVX_EXCEPTION] %s=%p module=%s base=%p offset=%p\r\n",
        label, (void *)address, module, (void *)base, (void *)(address-base));
    nvx_exception_write(line);
}

static LONG CALLBACK nvx_exception_trace(EXCEPTION_POINTERS *exception)
{
    static LONG count;
    uintptr_t stack;
    char line[256];
    MEMORY_BASIC_INFORMATION memory;
    EXCEPTION_RECORD *record = exception->ExceptionRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        InterlockedIncrement(&count) > 16)
        return EXCEPTION_CONTINUE_SEARCH;
    snprintf(line, sizeof(line),
        "[NVX_EXCEPTION] pid=%lu bits=%u code=%08lx access=%llu address=%p\r\n",
        (unsigned long)GetCurrentProcessId(), (unsigned)(8*sizeof(void *)),
        (unsigned long)record->ExceptionCode,
        (unsigned long long)record->ExceptionInformation[0],
        (void *)(uintptr_t)record->ExceptionInformation[1]);
    nvx_exception_write(line);
    nvx_exception_address("ip", (uintptr_t)record->ExceptionAddress);
#ifdef _WIN64
    stack = (uintptr_t)exception->ContextRecord->Rsp;
#else
    stack = (uintptr_t)exception->ContextRecord->Esp;
#endif
    if (VirtualQuery((void *)stack, &memory, sizeof(memory)) &&
        memory.State == MEM_COMMIT && !(memory.Protect & (PAGE_GUARD|PAGE_NOACCESS))) {
        uintptr_t end = (uintptr_t)memory.BaseAddress + memory.RegionSize;
        unsigned i;
        for (i=0; i<24 && stack <= end-sizeof(uintptr_t); i++,stack+=sizeof(uintptr_t)) {
            uintptr_t value = *(uintptr_t *)stack;
            snprintf(line, sizeof(line), "stack[%u]", i);
            nvx_exception_address(line, value);
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static void nvx_install_exception_trace(void)
{
    AddVectoredExceptionHandler(1, nvx_exception_trace);
}
#endif
