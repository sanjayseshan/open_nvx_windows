#include "nvx_winsys_win32.h"
#include <string.h>

static int
nvx_winsys_escape_hdc(void *opaque, uint32_t escape, uint32_t input_bytes,
                      const void *input, uint32_t output_bytes, void *output)
{
   HDC hdc = (HDC)opaque;
   return ExtEscape(hdc, (int)escape, (int)input_bytes, (LPCSTR)input,
                    (int)output_bytes, (LPSTR)output) > 0;
}

static void
nvx_winsys_release_hdc(void *opaque)
{
   DeleteDC((HDC)opaque);
}

int
NvxWinsysInitHdc(NVX_WINSYS_DEVICE *device, HDC hdc)
{
   HDC transport;
   MONITORINFOEXA monitor;
   HWND window;
   if (!hdc)
      return 0;
   /* WineD3D destroys its adapter-probe window/DC while Mesa retains the
    * process screen. Own the transport DC for the whole client lifetime. */
   memset(&monitor, 0, sizeof(monitor));
   monitor.cbSize = sizeof(monitor);
   window = WindowFromDC(hdc);
   if (!GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY),
                        (MONITORINFO *)&monitor))
      return 0;
   transport = CreateDCA("DISPLAY", monitor.szDevice, NULL, NULL);
   if (!transport)
      return 0;
   if (!NvxWinsysInit(device, nvx_winsys_escape_hdc, transport)) {
      DeleteDC(transport);
      return 0;
   }
   device->release_transport = nvx_winsys_release_hdc;
   return 1;
}
