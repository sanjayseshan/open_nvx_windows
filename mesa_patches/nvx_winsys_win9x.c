#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winioctl.h>

#include "nvx_winsys_win9x.h"

#define NVX_FILE_DEVICE_VIDEO 0x00000023U
#define NVX_METHOD_BUFFERED 0U
#define NVX_FILE_ANY_ACCESS 0U
#define NVX_CTL_CODE(device, function, method, access) \
   (((device) << 16) | ((access) << 14) | ((function) << 2) | (method))

#define NVX_IOCTL_3D_QUERY_CAPS       NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x207, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_ALLOC_BUFFER     NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x208, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_SUBMIT           NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x209, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_WAIT_FENCE       NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x20a, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_FREE_BUFFER      NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x20b, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_UPLOAD_BUFFER    NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x20c, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_READBACK_BUFFER  NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x20d, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_NVC0_CLEAR       NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x20e, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_DOWNLOAD_BUFFER  NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x20f, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_VALIDATE_BATCH   NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x210, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_NATIVE_CLEAR     NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x211, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_CLIENT_OPEN      NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x212, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_CLIENT_CLOSE     NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x213, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_CHANNEL_LEASE    NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x214, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_SUBMIT_BATCH     NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x215, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_PRESENT_COPY     NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x216, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_UPLOAD_SPANS     NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x217, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_PRESENT_BATCH    NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x218, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)
#define NVX_IOCTL_3D_MAP_BUFFER       NVX_CTL_CODE(NVX_FILE_DEVICE_VIDEO, 0x219, NVX_METHOD_BUFFERED, NVX_FILE_ANY_ACCESS)

static DWORD
nvx_win9x_ioctl(uint32_t escape)
{
   switch (escape) {
   case NVX_ESCAPE_3D_QUERY_CAPS: return NVX_IOCTL_3D_QUERY_CAPS;
   case NVX_ESCAPE_3D_ALLOC: return NVX_IOCTL_3D_ALLOC_BUFFER;
   case NVX_ESCAPE_3D_SUBMIT: return NVX_IOCTL_3D_SUBMIT;
   case NVX_ESCAPE_3D_WAIT: return NVX_IOCTL_3D_WAIT_FENCE;
   case NVX_ESCAPE_3D_FREE: return NVX_IOCTL_3D_FREE_BUFFER;
   case NVX_ESCAPE_3D_UPLOAD: return NVX_IOCTL_3D_UPLOAD_BUFFER;
   case NVX_ESCAPE_3D_READBACK: return NVX_IOCTL_3D_READBACK_BUFFER;
   case NVX_ESCAPE_3D_NVC0_CLEAR: return NVX_IOCTL_3D_NVC0_CLEAR;
   case NVX_ESCAPE_3D_DOWNLOAD: return NVX_IOCTL_3D_DOWNLOAD_BUFFER;
   case NVX_ESCAPE_3D_VALIDATE_BATCH: return NVX_IOCTL_3D_VALIDATE_BATCH;
   case NVX_ESCAPE_3D_NVC0_NATIVE_CLEAR: return NVX_IOCTL_3D_NATIVE_CLEAR;
   case NVX_ESCAPE_3D_CLIENT_OPEN: return NVX_IOCTL_3D_CLIENT_OPEN;
   case NVX_ESCAPE_3D_CLIENT_CLOSE: return NVX_IOCTL_3D_CLIENT_CLOSE;
   case NVX_ESCAPE_3D_CHANNEL_LEASE: return NVX_IOCTL_3D_CHANNEL_LEASE;
   case NVX_ESCAPE_3D_SUBMIT_BATCH: return NVX_IOCTL_3D_SUBMIT_BATCH;
   case NVX_ESCAPE_3D_PRESENT_COPY: return NVX_IOCTL_3D_PRESENT_COPY;
   case NVX_ESCAPE_3D_UPLOAD_SPANS: return NVX_IOCTL_3D_UPLOAD_SPANS;
   case NVX_ESCAPE_3D_PRESENT_BATCH: return NVX_IOCTL_3D_PRESENT_BATCH;
   case NVX_ESCAPE_3D_MAP_BUFFER: return NVX_IOCTL_3D_MAP_BUFFER;
   default: return 0;
   }
}

static int
nvx_winsys_escape_vxd(void *opaque, uint32_t escape, uint32_t input_bytes,
                      const void *input, uint32_t output_bytes, void *output)
{
   HANDLE vxd = (HANDLE)opaque;
   DWORD ioctl_code = nvx_win9x_ioctl(escape);
   DWORD bytes_returned = 0;

   if (!vxd || vxd == INVALID_HANDLE_VALUE || !ioctl_code ||
       (input_bytes && !input) || (output_bytes && !output))
      return 0;

   if (!DeviceIoControl(vxd, ioctl_code, (LPVOID)input, input_bytes,
                        output, output_bytes, &bytes_returned, NULL))
      return 0;
   if (output_bytes && bytes_returned != output_bytes)
      return 0;
   /* Match the XP transport's return convention: exact output bytes when
    * present, and a nonzero success marker for successful no-output calls. */
   return output_bytes ? (int)bytes_returned : 1;
}

static void
nvx_winsys_release_vxd(void *opaque)
{
   HANDLE vxd = (HANDLE)opaque;
   if (vxd && vxd != INVALID_HANDLE_VALUE)
      CloseHandle(vxd);
}

int
NvxWinsysInitWin9xVxd(NVX_WINSYS_DEVICE *device)
{
   HANDLE vxd;

   if (!device)
      return 0;
   vxd = CreateFileA("\\\\.\\NVX", GENERIC_READ | GENERIC_WRITE,
                     FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                     OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   if (vxd == INVALID_HANDLE_VALUE)
      return 0;

   if (!NvxWinsysInit(device, nvx_winsys_escape_vxd, vxd)) {
      CloseHandle(vxd);
      return 0;
   }
   device->release_transport = nvx_winsys_release_vxd;
   return 1;
}
