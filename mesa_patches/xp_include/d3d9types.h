#ifndef NVX_XP_D3D9TYPES_H
#define NVX_XP_D3D9TYPES_H
/* MinGW's D3D9 type header expects Win32 scalar types to be declared first.
 * Mesa Nine includes d3d9types.h directly in several headers, so provide the
 * required ordering while continuing to use the toolchain's canonical types. */
#include <windows.h>
#include_next <d3d9types.h>
#endif
