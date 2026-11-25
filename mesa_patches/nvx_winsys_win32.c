#include "nvx_winsys_win32.h"
#include <string.h>
#if defined(NVX_WIN98_DIOC)
#include "nvx_winsys_win9x.h"
#endif

#if !defined(NVX_WIN98_DIOC)
static int
nvx_winsys_escape_hdc(void *opaque, uint32_t escape, uint32_t input_bytes,
                      const void *input, uint32_t output_bytes, void *output)
{
   HDC hdc = (HDC)opaque;
   /* Preserve ExtEscape's numeric result. Readback callers use it to verify
    * the exact byte count, while other ABI calls only test for nonzero. */
   return ExtEscape(hdc, (int)escape, (int)input_bytes, (LPCSTR)input,
                    (int)output_bytes, (LPSTR)output);
}

static void
nvx_winsys_release_hdc(void *opaque)
{
   DeleteDC((HDC)opaque);
}
#endif

int
NvxWinsysInitHdc(NVX_WINSYS_DEVICE *device, HDC hdc)
{
#if defined(NVX_WIN98_DIOC)
   /* Win9x has no XDDM miniport escape callback behind this HDC. Use the
    * synchronous DIOC transport to the NVX VxD while keeping Mesa's existing
    * winsys and miniport ABI unchanged. */
   (void)hdc;
   return NvxWinsysInitWin9xVxd(device);
#else
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
#endif
}
