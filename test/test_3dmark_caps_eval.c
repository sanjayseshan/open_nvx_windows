#include <windows.h>
#include <stdio.h>
#include <d3d9.h>

static void log_com(const char *msg) {
    HANDLE h = CreateFileA("\\\\.\\COM1", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (h != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(h, msg, (DWORD)strlen(msg), &written, NULL);
        CloseHandle(h);
    }
}

int main(void) {
    char buf[512];
    log_com("[CAPS_EVAL] Starting 3DMark05 capabilities evaluation on Direct3D9...\r\n");

    HMODULE hD3D9 = LoadLibraryA("d3d9.dll");
    if (!hD3D9) {
        log_com("[CAPS_EVAL] FAILED to load d3d9.dll\r\n");
        return 1;
    }

    typedef IDirect3D9* (WINAPI *Direct3DCreate9_t)(UINT);
    Direct3DCreate9_t pCreate = (Direct3DCreate9_t)GetProcAddress(hD3D9, "Direct3DCreate9");
    if (!pCreate) {
        log_com("[CAPS_EVAL] FAILED to get Direct3DCreate9\r\n");
        return 1;
    }

    IDirect3D9 *pD3D = pCreate(D3D_SDK_VERSION);
    if (!pD3D) {
        log_com("[CAPS_EVAL] Direct3DCreate9 returned NULL\r\n");
        return 1;
    }

    D3DCAPS9 c;
    ZeroMemory(&c, sizeof(c));
    HRESULT hr = pD3D->lpVtbl->GetDeviceCaps(pD3D, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &c);
    snprintf(buf, sizeof(buf), "[CAPS_EVAL] GetDeviceCaps hr=0x%08lx VS=0x%08lx PS=0x%08lx\r\n",
             (unsigned long)hr, (unsigned long)c.VertexShaderVersion, (unsigned long)c.PixelShaderVersion);
    log_com(buf);

    #define EVAL(cond, desc) do { \
        if (!(cond)) { \
            snprintf(buf, sizeof(buf), "[CAPS_EVAL] FAIL: %s\r\n", desc); \
            log_com(buf); \
        } else { \
            snprintf(buf, sizeof(buf), "[CAPS_EVAL] PASS: %s\r\n", desc); \
            log_com(buf); \
        } \
    } while(0)

    EVAL(c.VertexShaderVersion >= D3DVS_VERSION(2, 0), "vertex shader version >= 2.0");
    EVAL(c.PixelShaderVersion >= D3DPS_VERSION(2, 0), "pixel shader version >= 2.0");
    if (c.PixelShaderVersion < D3DPS_VERSION(3, 0)) {
        EVAL(c.PS20Caps.NumTemps >= 22, "PS20Caps.NumTemps >= 22");
        EVAL(c.PS20Caps.Caps & D3DPS20CAPS_ARBITRARYSWIZZLE, "PS20Caps ARBITRARYSWIZZLE");
        EVAL(c.PS20Caps.Caps & D3DPS20CAPS_GRADIENTINSTRUCTIONS, "PS20Caps GRADIENTINSTRUCTIONS");
        EVAL(c.PS20Caps.Caps & D3DPS20CAPS_PREDICATION, "PS20Caps PREDICATION");
        EVAL(c.PS20Caps.Caps & D3DPS20CAPS_NODEPENDENTREADLIMIT, "PS20Caps NODEPENDENTREADLIMIT");
        EVAL(c.PS20Caps.Caps & D3DPS20CAPS_NOTEXINSTRUCTIONLIMIT, "PS20Caps NOTEXINSTRUCTIONLIMIT");
    }
    EVAL(c.RasterCaps & D3DPRASTERCAPS_SCISSORTEST, "scissor test");
    EVAL(c.TextureCaps & D3DPTEXTURECAPS_CUBEMAP, "cube textures");
    EVAL(c.TextureCaps & D3DPTEXTURECAPS_MIPCUBEMAP, "mipmapped cube textures");
    EVAL(!(c.TextureCaps & D3DPTEXTURECAPS_SQUAREONLY), "other textures than square");
    EVAL(c.TextureFilterCaps & D3DPTFILTERCAPS_MIPFPOINT, "texture point-sample filtering for mipmaps");
    EVAL(c.TextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR, "texture linear filtering for mipmaps");
    EVAL(c.TextureFilterCaps & D3DPTFILTERCAPS_MINFLINEAR, "texture linear minification filter");
    EVAL(c.CubeTextureFilterCaps & D3DPTFILTERCAPS_MIPFPOINT, "cube texture point-sample mipmaps");
    EVAL(c.CubeTextureFilterCaps & D3DPTFILTERCAPS_MIPFLINEAR, "cube texture linear mipmaps");
    EVAL(c.CubeTextureFilterCaps & D3DPTFILTERCAPS_MINFLINEAR, "cube texture linear minification");
    EVAL(c.TextureAddressCaps & D3DPTADDRESSCAPS_CLAMP, "clamping of textures");
    EVAL(c.TextureAddressCaps & D3DPTADDRESSCAPS_WRAP, "wrapping of textures");
    EVAL(c.MaxTextureWidth >= 2048, "texture widths >= 2048");
    EVAL(c.MaxTextureHeight >= 2048, "texture heights >= 2048");
    EVAL(c.MaxUserClipPlanes >= 1, "user-defined clipping planes");
    EVAL(c.MaxPointSize >= 64.0f, "size of a point primitive >= 64");
    EVAL(c.MaxPrimitiveCount >= 65535, "maximum number of primitives >= 65535");
    EVAL(c.MaxVertexIndex >= 65534, "rendering with the index buffer >= 65534");
    EVAL(c.MaxStreams >= 2, "two or more concurrent data streams");
    EVAL(c.StretchRectFilterCaps & D3DPTFILTERCAPS_MAGFPOINT, "StretchRect MAGFPOINT");
    EVAL(c.StretchRectFilterCaps & D3DPTFILTERCAPS_MINFLINEAR, "StretchRect MINFLINEAR");

    // Format checks
    #define CHK_FMT(fmt, rtype, usage, desc) do { \
        HRESULT r = pD3D->lpVtbl->CheckDeviceFormat(pD3D, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, usage, rtype, fmt); \
        snprintf(buf, sizeof(buf), "[CAPS_EVAL] CheckFormat %s (0x%08x): hr=0x%08lx (%s)\r\n", desc, (unsigned)fmt, (unsigned long)r, SUCCEEDED(r) ? "PASS" : "FAIL"); \
        log_com(buf); \
    } while(0)

    CHK_FMT(MAKEFOURCC('D','X','T','1'), D3DRTYPE_TEXTURE, 0, "DXT1 Texture");
    CHK_FMT(MAKEFOURCC('D','X','T','3'), D3DRTYPE_TEXTURE, 0, "DXT3 Texture");
    CHK_FMT(MAKEFOURCC('D','X','T','5'), D3DRTYPE_TEXTURE, 0, "DXT5 Texture");
    CHK_FMT(MAKEFOURCC('D','X','T','1'), D3DRTYPE_CUBETEXTURE, 0, "DXT1 CubeTexture");
    CHK_FMT(MAKEFOURCC('D','X','T','3'), D3DRTYPE_CUBETEXTURE, 0, "DXT3 CubeTexture");
    CHK_FMT(MAKEFOURCC('D','X','T','5'), D3DRTYPE_CUBETEXTURE, 0, "DXT5 CubeTexture");
    CHK_FMT(D3DFMT_R5G6B5, D3DRTYPE_SURFACE, D3DUSAGE_RENDERTARGET, "R5G6B5 RenderTarget");
    CHK_FMT(D3DFMT_X8R8G8B8, D3DRTYPE_SURFACE, D3DUSAGE_RENDERTARGET, "X8R8G8B8 RenderTarget");

    pD3D->lpVtbl->Release(pD3D);
    FreeLibrary(hD3D9);
    log_com("[CAPS_EVAL] Completed capabilities evaluation.\r\n");
    return 0;
}
