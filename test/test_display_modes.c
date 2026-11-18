/* Exercise native display DMA wrap through XP's public mode-switch API. */
#define WINVER 0x0502
#define _WIN32_WINNT 0x0502
#include <windows.h>
#include <stdio.h>

int main(void)
{
    DEVMODEA original = {0}, low = {0}, current = {0};
    int failed = 0, found = 0;
    original.dmSize = sizeof(original);
    if (!EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &original)) return 1;
    for (DWORD index = 0;; index++) {
        low.dmSize = sizeof(low);
        if (!EnumDisplaySettingsA(NULL, index, &low)) break;
        if (low.dmPelsWidth == 1024 && low.dmPelsHeight == 768 &&
            low.dmBitsPerPel == 32 && low.dmDisplayFrequency == 60) {
            found = 1;
            break;
        }
    }
    if (!found || original.dmBitsPerPel != 32) {
        puts("[DISPLAY_CYCLES] FAIL: required 32-bit modes unavailable");
        return 1;
    }
    for (unsigned cycle = 0; cycle < 12; cycle++) {
        DEVMODEA mode = cycle & 1 ? original : low;
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL | DM_DISPLAYFREQUENCY;
        LONG status = ChangeDisplaySettingsExA(NULL, &mode, NULL, CDS_FULLSCREEN, NULL);
        Sleep(200);
        current.dmSize = sizeof(current);
        BOOL queried = EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &current);
        printf("[DISPLAY_CYCLES] cycle=%u requested=%lux%lu status=%ld actual=%lux%lu\n",
               cycle, mode.dmPelsWidth, mode.dmPelsHeight, status,
               current.dmPelsWidth, current.dmPelsHeight);
        fflush(stdout);
        if (status != DISP_CHANGE_SUCCESSFUL || !queried ||
            current.dmPelsWidth != mode.dmPelsWidth || current.dmPelsHeight != mode.dmPelsHeight) {
            failed = 1;
            break;
        }
    }
    if (ChangeDisplaySettingsExA(NULL, &original, NULL, 0, NULL) != DISP_CHANGE_SUCCESSFUL)
        failed = 1;
    printf("[DISPLAY_CYCLES] %s\n", failed ? "FAIL" : "PASS");
    return failed;
}
