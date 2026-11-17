# Native XP WineD3D compatibility

The `wine/` submodule is pinned to Wine 1.7.55, commit
`a786dca935c3e9b3d85853db4b61700e590a1c8b`, matching the original x64 D3D wrappers. The original x86 package was
1.7.52; mixing it with 1.7.55 WineD3D is incompatible. Source license: `wine/COPYING.LIB`.

`0001-native-wgl-pixel-format-query.patch` initializes the native Windows
`p_wglGetPixelFormat` callback. Upstream `context_enter()` uses this callback,
but the `USE_WIN32_OPENGL` initialization only populated the core GL functions
and WGL SwapBuffers. The installed x64 library crashed calling NULL here.
The patch binds XP's existing OpenGL32 export, falling back to GDI's equivalent.

Run `JOBS=8 ./test/build_wined3d_xp.sh`. This builds Wine's host tools and the
x64/x86 WineD3D, D3D8, D3D9, DirectDraw and libwine DLLs in `/tmp/nvx-wined3d-xp`, leaving the submodule clean.
The five packaged DLLs in `test/wined3d_xp/{x64,x86}/` are replaced as a
matching set. The full driver test audits
all packaged imports against the installed XP image before booting.

A newer WineD3D should be evaluated with matching D3D wrappers and XP import
checks after this matching-version baseline is verified. This pin is a
reproducible compatibility baseline, not a claim that 1.7.55 is the best
long-term release.
