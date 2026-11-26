Optional Direct3D 9 runtime for Windows 98 SE

This is a build bundle, not yet validated on the Win98 guest.

  d3d9.dll     Wine9x D3D9 switcher for Win98
  mesa99.dll   Mesa Gallium Nine D3D9 frontend
  wined3d.dll  Wine9x shared D3D implementation
  wined9.dll   Wine9x D3D9 compatibility backend

For a controlled test, back up the existing Windows\\System\\d3d9.dll as
msd3d9.dll, install these DLLs in Windows\\System, and set the test program's
executable name under HKLM\\Software\\D9Switcher to the string "nineforce".
The Wine9x switcher README documents per-application routing. Restore the
backup to undo the test. Runtime compatibility and accelerated D3D9 are not
confirmed until a Win98 D3D9 application completes a test with GPU submissions.
