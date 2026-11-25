#ifndef NVX_WINSYS_WIN9X_H
#define NVX_WINSYS_WIN9X_H

#include "nvx_winsys.h"

/* Open the NVX VxD's W32_DeviceIOControl endpoint and initialize the shared
 * Nouveau winsys ABI over synchronous Win9x DIOC calls. */
int NvxWinsysInitWin9xVxd(NVX_WINSYS_DEVICE *device);

#endif
