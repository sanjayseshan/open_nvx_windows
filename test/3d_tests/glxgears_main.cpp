/*
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/* $XFree86: xc/programs/glxgears/glxgears.c,v 1.3tsi Exp $ */

/*
 * This is a port of the infamous "gears" demo to straight GLX (i.e. no GLUT)
 * Port by Brian Paul  23 March 2001
 *
 * Command line options:
 *    -info      print GL implementation information
 *
 */

/* Modified from X11/GLX to Win32/WGL by Ben Skeggs 25th October 2004 */

/* Modified to compile in Visual Studio 2012 by Calvin Hartwell 28th February 2013 */ 
 
#include <windows.h>
#include <GL/gl.h>
// Removed out-dated un-required WGL header file 

#include "nvx_3d_abi.h"

#include <math.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>

/* XXX this probably isn't very portable */
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265
#endif /* !M_PI */

/* Turn a NULL pointer string into an empty string */
#define NULLSTR(x) (((x)!=NULL)?(x):(""))
#define Log(x) { if(verbose) printf x; }

#define Bool int
#define False 0
#define True 1

#if 0
/* wgl extensions */
PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = 0;
#endif

/* Global vars */
static HDC hDC;
static HGLRC hRC;
static HWND hWnd;
static HINSTANCE hInst;
static RECT winrect;
static uint64_t g_nvxClientToken = 0;

static void log_out(const char *fmt, ...);

static void NvxOpen3dClientSession(HDC dc) {
	NVX_3D_CLIENT_SESSION session;
	memset(&session, 0, sizeof(session));
	session.Size = sizeof(session);
	session.Version = NVX_3D_ABI_VERSION;
	session.Operation = NVX_3D_CLIENT_OPEN;
	if (ExtEscape(dc, NVX_ESCAPE_3D_CLIENT_OPEN, sizeof(session),
	              (LPCSTR)&session, sizeof(session), (LPSTR)&session) > 0) {
		if (Nvx3dValidateClientSession(&session, NVX_3D_CLIENT_OPEN) && session.ClientToken) {
			g_nvxClientToken = session.ClientToken;
			log_out("[WGLGEARS] NVX 3D client session established: token=0x%llx\n",
			        (unsigned long long)g_nvxClientToken);
		}
	}
}

static void NvxClose3dClientSession(HDC dc) {
	if (g_nvxClientToken) {
		NVX_3D_CLIENT_SESSION session;
		memset(&session, 0, sizeof(session));
		session.Size = sizeof(session);
		session.Version = NVX_3D_ABI_VERSION;
		session.Operation = NVX_3D_CLIENT_CLOSE;
		session.ClientToken = g_nvxClientToken;
		ExtEscape(dc, NVX_ESCAPE_3D_CLIENT_CLOSE, sizeof(session),
		          (LPCSTR)&session, 0, NULL);
		g_nvxClientToken = 0;
	}
}

static const char *ProgramName;      /* program name (from argv[0]) */
static Bool        verbose = False;  /* verbose output what the program is doing */

static GLfloat view_rotx = 20.0, view_roty = 30.0, view_rotz = 0.0;
static GLint gear1, gear2, gear3;
static GLfloat angle = 0.0;

static
void usage(void)
{
   fprintf (stderr, "usage:  %s [options]\n", ProgramName);
   fprintf (stderr, "-info\tPrint additional GL information.\n");
   fprintf (stderr, "-h\tPrint this help page.\n");
   fprintf (stderr, "-v\tVerbose output.\n");
   fprintf (stderr, "\n");
   exit(EXIT_FAILURE);
}


/* return current time (in seconds) */
static int current_time(void) {
	return (int)(GetTickCount() / 1000);
}

/*
 *
 *  Draw a gear wheel.  You'll probably want to call this function when
 *  building a display list since we do a lot of trig here.
 *
 *  Input:  inner_radius - radius of hole at center
 *          outer_radius - radius at center of teeth
 *          width - width of gear
 *          teeth - number of teeth
 *          tooth_depth - depth of tooth
 */
static void
gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
     GLint teeth, GLfloat tooth_depth)
{
   GLint i;
   GLfloat r0, r1, r2;
   GLfloat angle, da;
   GLfloat u, v, len;

   r0 = inner_radius;
   r1 = outer_radius - tooth_depth / 2.0;
   r2 = outer_radius + tooth_depth / 2.0;

   da = 2.0 * M_PI / teeth / 4.0;

   glShadeModel(GL_FLAT);

   glNormal3f(0.0, 0.0, 1.0);

   /* draw front face */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i <= teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;
      glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
      glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
      if (i < teeth) {
	 glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
	 glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		    width * 0.5);
      }
   }
   glEnd();

   /* draw front sides of teeth */
   glBegin(GL_QUADS);
   da = 2.0 * M_PI / teeth / 4.0;
   for (i = 0; i < teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;

      glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 width * 0.5);
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 width * 0.5);
   }
   glEnd();

   glNormal3f(0.0, 0.0, -1.0);

   /* draw back face */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i <= teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;
      glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
      glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
      if (i < teeth) {
	 glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		    -width * 0.5);
	 glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
      }
   }
   glEnd();

   /* draw back sides of teeth */
   glBegin(GL_QUADS);
   da = 2.0 * M_PI / teeth / 4.0;
   for (i = 0; i < teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;

      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 -width * 0.5);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 -width * 0.5);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
      glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
   }
   glEnd();

   /* draw outward faces of teeth */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i < teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;

      glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
      glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
      u = r2 * cos(angle + da) - r1 * cos(angle);
      v = r2 * sin(angle + da) - r1 * sin(angle);
      len = sqrt(u * u + v * v);
      u /= len;
      v /= len;
      glNormal3f(v, -u, 0.0);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
      glNormal3f(cos(angle), sin(angle), 0.0);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 width * 0.5);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 -width * 0.5);
      u = r1 * cos(angle + 3 * da) - r2 * cos(angle + 2 * da);
      v = r1 * sin(angle + 3 * da) - r2 * sin(angle + 2 * da);
      glNormal3f(v, -u, 0.0);
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 width * 0.5);
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 -width * 0.5);
      glNormal3f(cos(angle), sin(angle), 0.0);
   }

   // VS2012 could not use cos & sin with integers, have to cast to double
   glVertex3f(r1 * cos((double)0), r1 * sin((double)0), width * 0.5);
   glVertex3f(r1 * cos((double)0), r1 * sin((double)0), -width * 0.5);

   glEnd();

   glShadeModel(GL_SMOOTH);

   /* draw inside radius cylinder */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i <= teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;
      glNormal3f(-cos(angle), -sin(angle), 0.0);
      glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
      glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
   }
   glEnd();
}


static void
draw(void)
{
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   glPushMatrix();
   glRotatef(view_rotx, 1.0, 0.0, 0.0);
   glRotatef(view_roty, 0.0, 1.0, 0.0);
   glRotatef(view_rotz, 0.0, 0.0, 1.0);

   glPushMatrix();
   glTranslatef(-3.0, -2.0, 0.0);
   glRotatef(angle, 0.0, 0.0, 1.0);
   glCallList(gear1);
   glPopMatrix();

   glPushMatrix();
   glTranslatef(3.1, -2.0, 0.0);
   glRotatef(-2.0 * angle - 9.0, 0.0, 0.0, 1.0);
   glCallList(gear2);
   glPopMatrix();

   glPushMatrix();
   glTranslatef(-3.1, 4.2, 0.0);
   glRotatef(-2.0 * angle - 25.0, 0.0, 0.0, 1.0);
   glCallList(gear3);
   glPopMatrix();

   glPopMatrix();
}


/* new window size or exposure */
static void
reshape(int width, int height)
{
   GLfloat h = (GLfloat) height / (GLfloat) width;

   glViewport(0, 0, (GLint) width, (GLint) height);
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glFrustum(-1.0, 1.0, -h, h, 5.0, 60.0);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTranslatef(0.0, 0.0, -40.0);
}


static void
init(void)
{
   static GLfloat pos[4] = { 5.0, 5.0, 10.0, 0.0 };
   static GLfloat red[4] = { 0.8, 0.1, 0.0, 1.0 };
   static GLfloat green[4] = { 0.0, 0.8, 0.2, 1.0 };
   static GLfloat blue[4] = { 0.2, 0.2, 1.0, 1.0 };

   glLightfv(GL_LIGHT0, GL_POSITION, pos);
   glEnable(GL_CULL_FACE);
   glEnable(GL_LIGHTING);
   glEnable(GL_LIGHT0);
   glEnable(GL_DEPTH_TEST);

   /* make the gears */
   gear1 = glGenLists(1);
   glNewList(gear1, GL_COMPILE);
   glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
   gear(1.0, 4.0, 1.0, 20, 0.7);
   glEndList();

   gear2 = glGenLists(1);
   glNewList(gear2, GL_COMPILE);
   glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
   gear(0.5, 2.0, 2.0, 10, 0.7);
   glEndList();

   gear3 = glGenLists(1);
   glNewList(gear3, GL_COMPILE);
   glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
   gear(1.3, 2.0, 0.5, 10, 0.7);
   glEndList();

   glEnable(GL_NORMALIZE);
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
	switch (uMsg) {
	case WM_CLOSE:
		PostQuitMessage(0);
		return 0;
	case WM_SIZE:
		reshape(LOWORD(lParam), HIWORD(lParam));
		return 0;
	case WM_KEYDOWN:
		if (wParam == VK_LEFT)
			view_roty += 5.0;
		else if (wParam == VK_RIGHT)
			view_roty -= 5.0;
		else if (wParam == VK_UP)
			view_rotx += 5.0;
		else if (wParam == VK_DOWN)
			view_rotx -= 5.0;
		else if (wParam == VK_ESCAPE)
			PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}

static int g_autoExitSeconds = 0;
static HANDLE g_hCom1 = INVALID_HANDLE_VALUE;

static void log_out(const char *fmt, ...) {
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	printf("%s", buf);
	fflush(stdout);

	if (g_hCom1 == INVALID_HANDLE_VALUE) {
		g_hCom1 = CreateFileA("\\\\.\\COM1", GENERIC_WRITE,
		                      FILE_SHARE_READ | FILE_SHARE_WRITE,
		                      NULL, OPEN_EXISTING, 0, NULL);
	}
	if (g_hCom1 != INVALID_HANDLE_VALUE) {
		DWORD written;
		WriteFile(g_hCom1, buf, (DWORD)strlen(buf), &written, NULL);
	}

	FILE *fp = fopen("C:\\wglgears.log", "a");
	if (fp) {
		fputs(buf, fp);
		fclose(fp);
	}
}

/*
 * Create an RGB, double-buffered window.
 * Return the window and context handles.
 */
static void make_window(const char *name, int x, int y, int width, int height) {
	GLuint PixelFormat;
	WNDCLASS wc;
	DWORD dwExStyle, dwStyle;
	PIXELFORMATDESCRIPTOR pfd;
	PIXELFORMATDESCRIPTOR actualPfd;

	winrect.left = (long)0;
	winrect.right = (long)width;
	winrect.top = (long) 0;
	winrect.bottom = (long)height;

	hInst = GetModuleHandle(NULL);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = (WNDPROC)WndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = hInst;
	wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = NULL;
	wc.lpszMenuName = NULL;
	wc.lpszClassName = name;
	if (!RegisterClass(&wc)) {
		printf("failed to register class\n");
		exit(0);
	}

	dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
	dwStyle = WS_OVERLAPPEDWINDOW | WS_VISIBLE;
	AdjustWindowRectEx(&winrect, dwStyle, False, dwExStyle);

	if (!(hWnd = CreateWindowEx(dwExStyle, name, name,
		WS_CLIPSIBLINGS | WS_CLIPCHILDREN | dwStyle, 0, 0,
		winrect.right - winrect.left, winrect.bottom - winrect.top,
		NULL, NULL, hInst, NULL))) {
		printf("failed to create window\n");
		exit(0);
	}

	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);
	SetForegroundWindow(hWnd);
	SetFocus(hWnd);

	memset(&pfd, 0, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.iLayerType = PFD_MAIN_PLANE;

	if (!(hDC = GetDC(hWnd))) {
		log_out("[WGLGEARS] failed to get DC err=%lu\n", GetLastError());
		exit(0);
	}

	/*
	 * Open NVX 3D client session so the display driver leases the GA104 hardware
	 * 3D channel to Mesa's winsys / DRM layer instead of falling back to softpipe.
	 */
	NvxOpen3dClientSession(hDC);

	/*
	 * Query the display driver's OPENGL_GETINFO escape to warm up OPENGL32's
	 * ICD binding cache and verify the display driver name.
	 */
	{
		DWORD escapeCode = 4353; /* NVX_OPENGL_GETINFO_ESCAPE */
		if (ExtEscape(hDC, QUERYESCSUPPORT, sizeof(escapeCode), (LPCSTR)&escapeCode, 0, NULL) > 0) {
			DWORD subEscape = 0;
			struct {
				DWORD version;
				DWORD driverVersion;
				WCHAR driverName[MAX_PATH + 1];
			} info;
			memset(&info, 0, sizeof(info));
			if (ExtEscape(hDC, 4353, sizeof(subEscape), (LPCSTR)&subEscape, sizeof(info), (LPSTR)&info) > 0) {
				log_out("[WGLGEARS] OPENGL_GETINFO: name=%ls ver=%lu drvVer=%lu\n",
				        info.driverName, (unsigned long)info.version, (unsigned long)info.driverVersion);
			}
		}
	}

	/*
	 * Ensure the Mesa ICD is loaded in process so that ICD pixel formats and entrypoints
	 * are registered with GDI WGL before format selection and context creation.
	 */
	HMODULE hIcd = GetModuleHandleA("nvxogl64.dll");
	if (!hIcd) hIcd = LoadLibraryA("nvxogl64.dll");
	if (!hIcd) {
		hIcd = GetModuleHandleA("nvxmesa.dll");
		if (!hIcd) hIcd = LoadLibraryA("nvxmesa.dll");
	}
	if (!hIcd) {
		hIcd = GetModuleHandleA("nvxogl.dll");
		if (!hIcd) hIcd = LoadLibraryA("nvxogl.dll");
	}

	/*
	 * Warm-up: call OPENGL32's wglDescribePixelFormat on our window DC.
	 * This forces OPENGL32 to initialize its driver format table.
	 */
	typedef int (WINAPI *PFNWGLDPF)(HDC, int, UINT, LPPIXELFORMATDESCRIPTOR);
	PFNWGLDPF wglDPF = NULL;
	int totalFormats = 0;
	{
		HMODULE hGL = GetModuleHandleA("opengl32.dll");
		if (!hGL) hGL = LoadLibraryA("opengl32.dll");
		wglDPF = hGL ?
		    (PFNWGLDPF)(void*)GetProcAddress(hGL, "wglDescribePixelFormat") : NULL;
		if (wglDPF) {
			PIXELFORMATDESCRIPTOR warmPfd;
			memset(&warmPfd, 0, sizeof(warmPfd));
			warmPfd.nSize = sizeof(warmPfd);
			totalFormats = wglDPF(hDC, 1, sizeof(warmPfd), &warmPfd);
			log_out("[WGLGEARS] wglDescribePixelFormat warm-up: %d ICD formats\n", totalFormats);
		} else {
			log_out("[WGLGEARS] wglDescribePixelFormat unavailable; ICD may not load\n");
		}
	}

	if (!(PixelFormat = ChoosePixelFormat(hDC, &pfd))) {
		log_out("[WGLGEARS] failed to choose pixel format err=%lu\n", GetLastError());
		exit(0);
	}

	memset(&actualPfd, 0, sizeof(actualPfd));
	DescribePixelFormat(hDC, PixelFormat, sizeof(actualPfd), &actualPfd);

	/* Log raw flags before any scan so we can verify PFD_GENERIC_FORMAT detection */
	log_out("[WGLGEARS] ChoosePixelFormat->%u; DescribePixelFormat flags=0x%lx PFD_GENERIC_FORMAT=%s\n",
	        PixelFormat, (unsigned long)actualPfd.dwFlags,
	        (actualPfd.dwFlags & PFD_GENERIC_FORMAT) ? "YES" : "NO");

	/* If Windows GDI chose a software/generic format (PFD_GENERIC_FORMAT),
	   search advertised ICD formats for a hardware-accelerated one.
	   Do NOT override PixelFormat if ChoosePixelFormat already picked a driver format! */
	if (actualPfd.dwFlags & PFD_GENERIC_FORMAT) {
		int foundFmt = 0;
		log_out("[WGLGEARS] Generic format %u selected; searching for ICD hardware format\n", PixelFormat);

		/* Fast check: test format 63 directly (standard Mesa hardware format) */
		{
			PIXELFORMATDESCRIPTOR candidate;
			memset(&candidate, 0, sizeof(candidate));
			candidate.nSize = sizeof(candidate);
			int cnt = wglDPF ? wglDPF(hDC, 63, sizeof(candidate), &candidate) : 0;
			if (cnt == 0) cnt = DescribePixelFormat(hDC, 63, sizeof(candidate), &candidate);
			if (cnt > 0 &&
			    (candidate.dwFlags & (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER)) ==
			    (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER) &&
			    !(candidate.dwFlags & PFD_GENERIC_FORMAT)) {
				foundFmt = 63;
				PixelFormat = 63;
				actualPfd = candidate;
				log_out("[WGLGEARS] Directly selected Mesa ICD format 63 (flags=0x%lx color=%u depth=%u)\n",
				        (unsigned long)candidate.dwFlags,
				        (unsigned)candidate.cColorBits, (unsigned)candidate.cDepthBits);
			}
		}

		if (!foundFmt) {
			int maxScan = totalFormats > 0 ? totalFormats : 100;
			for (int fmt = 1; fmt <= maxScan; fmt++) {
				PIXELFORMATDESCRIPTOR candidate;
				memset(&candidate, 0, sizeof(candidate));
				candidate.nSize = sizeof(candidate);
				int cnt = 0;
				if (wglDPF) {
					cnt = wglDPF(hDC, fmt, sizeof(candidate), &candidate);
				}
				if (cnt == 0) {
					cnt = DescribePixelFormat(hDC, fmt, sizeof(candidate), &candidate);
				}
				if (cnt == 0) {
					log_out("[WGLGEARS] Formats exhausted at fmt=%d; stopping scan\n", fmt);
					break;
				}
				if ((candidate.dwFlags & (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER)) ==
				    (PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER) &&
				    !(candidate.dwFlags & PFD_GENERIC_FORMAT) &&
				    candidate.cColorBits == 32 && candidate.cDepthBits >= 24) {
					foundFmt = fmt;
					PixelFormat = fmt;
					actualPfd = candidate;
					log_out("[WGLGEARS] Found HW format at index %d (flags=0x%lx color=%u depth=%u)\n",
					        fmt, (unsigned long)candidate.dwFlags,
					        (unsigned)candidate.cColorBits, (unsigned)candidate.cDepthBits);
					break;
				}
			}
		}
		if (!foundFmt) {
			log_out("[WGLGEARS] WARNING: No HW format found in scan; falling back to fmt=%u\n", PixelFormat);
		}
	}

	if (!SetPixelFormat(hDC, PixelFormat, &actualPfd)) {
		log_out("[WGLGEARS] failed to set pixel format err=%lu\n", GetLastError());
		exit(0);
	}

	log_out("[WGLGEARS] PixelFormat=%u (flags=0x%lx color=%u depth=%u)\n",
	        PixelFormat, (unsigned long)actualPfd.dwFlags,
	        (unsigned)actualPfd.cColorBits, (unsigned)actualPfd.cDepthBits);

	hRC = wglCreateContext(hDC);
	if (!hRC) {
		DWORD err = GetLastError();
		log_out("[WGLGEARS] wglCreateContext returned error=%lu; attempting direct ICD fallback...\n", err);
		/* Direct ICD context fallback (identical to test_gpu_accel.c) */
		if (hIcd) {
			typedef ULONG_PTR (APIENTRY *PFN_DIRECT_DRV_CREATE_CONTEXT)(HDC);
			typedef PVOID (APIENTRY *PFN_DIRECT_DRV_SET_CONTEXT)(HDC, ULONG_PTR, PVOID);
			PFN_DIRECT_DRV_CREATE_CONTEXT drvCreateCtx =
				(PFN_DIRECT_DRV_CREATE_CONTEXT)GetProcAddress(hIcd, "DrvCreateContext");
			PFN_DIRECT_DRV_SET_CONTEXT drvSetCtx =
				(PFN_DIRECT_DRV_SET_CONTEXT)GetProcAddress(hIcd, "DrvSetContext");
			if (drvCreateCtx && drvSetCtx) {
				ULONG_PTR ddiCtx = drvCreateCtx(hDC);
				if (ddiCtx) {
					drvSetCtx(hDC, ddiCtx, NULL);
					log_out("[WGLGEARS] Direct ICD DrvCreateContext/DrvSetContext succeeded (handle=%p)\n",
					        (void*)(UINT_PTR)ddiCtx);
					hRC = (HGLRC)(UINT_PTR)ddiCtx;
				}
			}
		}
	}
	if (!hRC) {
		log_out("[WGLGEARS] failed to create context err=%lu\n", GetLastError());
		exit(0);
	}
	if (!wglMakeCurrent(hDC, hRC)) {
		log_out("[WGLGEARS] wglMakeCurrent returned err=%lu (continuing if direct ICD bound)\n", GetLastError());
	}

	/* Log GL info immediately after context creation */
	{
		const char *renderer = (const char*)glGetString(GL_RENDERER);
		const char *version  = (const char*)glGetString(GL_VERSION);
		const char *vendor   = (const char*)glGetString(GL_VENDOR);
		log_out("GL_RENDERER   = %s\n", renderer ? renderer : "(null)");
		log_out("GL_VERSION    = %s\n", version  ? version  : "(null)");
		log_out("GL_VENDOR     = %s\n", vendor   ? vendor   : "(null)");
		if (renderer && strstr(renderer, "GDI")) {
			log_out("[WGLGEARS] WARNING: GDI Generic context — Mesa ICD not active for this window\n");
		}
	}
}

/* Compare the completed GL back buffer with the client area after SwapBuffers.
 * These captures run once, outside the measured steady rendering sequence. */
static void capture_ppm(const char *path, const unsigned char *pixels,
                        int width, int height, bool bottom_up, bool bgra)
{
   FILE *fp = fopen(path, "wb");
   unsigned red = 0, green = 0, blue = 0;
   if (!fp) {
      log_out("[WGLGEARS_CAPTURE] failed to open %s\n", path);
      return;
   }
   fprintf(fp, "P6\n%d %d\n255\n", width, height);
   for (int y = 0; y < height; y++) {
      int row = bottom_up ? height - 1 - y : y;
      for (int x = 0; x < width; x++) {
         const unsigned char *p = pixels + ((size_t)row * width + x) * 4;
         unsigned char rgb[3] = {p[bgra ? 2 : 0], p[1], p[bgra ? 0 : 2]};
         red += rgb[0] > 24 && rgb[0] > 2U * rgb[1] && rgb[0] > 2U * rgb[2];
         green += rgb[1] > 24 && rgb[1] > 2U * rgb[0] && rgb[1] > 2U * rgb[2];
         blue += rgb[2] > 24 && rgb[2] > 2U * rgb[0] && rgb[2] > 2U * rgb[1];
         fwrite(rgb, 1, 3, fp);
      }
   }
   int failed = ferror(fp);
   fclose(fp);
   log_out("[WGLGEARS_CAPTURE] file=%s size=%dx%d red=%u green=%u blue=%u io_error=%d\n",
           path, width, height, red, green, blue, failed);
}

static void capture_gl_target()
{
   GLint vp[4], read_buffer, alignment;
   glGetIntegerv(GL_VIEWPORT, vp);
   if (vp[2] <= 0 || vp[3] <= 0 || vp[2] > 2048 || vp[3] > 2048) return;
   unsigned char *pixels = (unsigned char *)malloc((size_t)vp[2] * vp[3] * 4);
   if (!pixels) return;
   glGetIntegerv(GL_READ_BUFFER, &read_buffer);
   glGetIntegerv(GL_PACK_ALIGNMENT, &alignment);
   GLenum prior = glGetError();
   glReadBuffer(GL_BACK);
   glPixelStorei(GL_PACK_ALIGNMENT, 1);
   glReadPixels(vp[0], vp[1], vp[2], vp[3], GL_RGBA, GL_UNSIGNED_BYTE, pixels);
   GLenum error = glGetError();
   log_out("[WGLGEARS_CAPTURE] glReadPixels prior_error=%x error=%x\n", prior, error);
   if (error == GL_NO_ERROR)
      capture_ppm("C:\\wglgears-render.ppm", pixels, vp[2], vp[3], true, false);
   glPixelStorei(GL_PACK_ALIGNMENT, alignment);
   glReadBuffer(read_buffer);
   free(pixels);
}

static void capture_presented_target()
{
   RECT rect;
   if (!GetClientRect(hWnd, &rect) || rect.right <= 0 || rect.bottom <= 0) return;
   BITMAPINFO info;
   memset(&info, 0, sizeof(info));
   info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
   info.bmiHeader.biWidth = rect.right;
   info.bmiHeader.biHeight = -rect.bottom;
   info.bmiHeader.biPlanes = 1;
   info.bmiHeader.biBitCount = 32;
   void *pixels = NULL;
   HDC memory = CreateCompatibleDC(hDC);
   HBITMAP bitmap = CreateDIBSection(hDC, &info, DIB_RGB_COLORS, &pixels, NULL, 0);
   if (memory && bitmap) {
      HGDIOBJ old = SelectObject(memory, bitmap);
      BOOL copied = BitBlt(memory, 0, 0, rect.right, rect.bottom, hDC, 0, 0, SRCCOPY);
      GdiFlush();
      if (copied)
         capture_ppm("C:\\wglgears-present.ppm", (const unsigned char *)pixels,
                     rect.right, rect.bottom, false, true);
      else
         log_out("[WGLGEARS_CAPTURE] BitBlt failed error=%lu\n", GetLastError());
      SelectObject(memory, old);
   }
   if (bitmap) DeleteObject(bitmap);
   if (memory) DeleteDC(memory);
}

static void event_loop() {
	MSG msg;
	int t, t0 = current_time();
	int t_start = t0;
	int frames = 0;
	int total_frames = 0;

	while(1) {
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) break;
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		angle += 2.0;
		draw();
        const char *renderer = (const char *)glGetString(GL_RENDERER);
        bool capture = total_frames == 2 && renderer && !strncmp(renderer, "NV", 2);
        if (capture) capture_gl_target();
        if (!SwapBuffers(hDC)) {
            log_out("[WGLGEARS] FAIL: SwapBuffers error=%lu\n", GetLastError());
            NvxClose3dClientSession(hDC);
            ExitProcess(1);
        }
		GdiFlush();
        if (capture) capture_presented_target();

		/* calc framerate */
		t = current_time();
		frames++;
		total_frames++;
		if (t - t0 >= 5.0) {
			GLfloat s = t - t0;
			GLfloat fps = frames / s;
			log_out("[WGLGEARS] %d frames in %3.1f seconds = %6.3f FPS\n", frames, s, fps);
	 		t0 = t;
			frames = 0;
		}

		if (g_autoExitSeconds > 0 && (t - t_start) >= g_autoExitSeconds) {
			GLfloat s = t - t_start;
			if (s > 0) {
				log_out("[WGLGEARS] Completed: %d total frames in %3.1f seconds = %6.3f FPS\n",
				        total_frames, s, (GLfloat)total_frames / s);
			}
			log_out("[WGLGEARS] Reached auto-exit duration of %d seconds. Exiting.\n", g_autoExitSeconds);
			NvxClose3dClientSession(hDC);
			ExitProcess(0);
		}
	}
}

int
main(int argc, char *argv[])
{
   int            i;
   Bool	printInfo = False;
   SetEnvironmentVariableA("GALLIUM_DRIVER", "nouveau");
   FILE *fpReset = fopen("C:\\wglgears.log", "w");
   if (fpReset) fclose(fpReset);

   ProgramName = argv[0];

   for (i = 1; i < argc; i++) {
      const char *arg = argv[i];
      int         len = strlen(arg);

      if (strcmp(argv[i], "-info") == 0) {
         printInfo = GL_TRUE;
      }
      else if (!strncmp("-v", arg, len)) {
         verbose   = True;
         printInfo = GL_TRUE;
      }
      else if (strcmp(argv[i], "-h") == 0) {
         usage();
      }
      else if (strcmp(argv[i], "-seconds") == 0 && i + 1 < argc) {
         g_autoExitSeconds = atoi(argv[++i]);
      }
      else
      {
        fprintf(stderr, "%s: Unsupported option '%s'.\n", ProgramName, argv[i]);
        usage();
      }
   }


	make_window("glxgears", 0, 0, 300, 300);
	reshape(300, 300);

/* force vsync off */
#if 0
	wglSwapIntervalEXT = wglGetProcAddress("wglSwapIntervalEXT");
	if (!wglSwapIntervalEXT) {
		printf("warning: wglSwapIntervalEXT missing, cannot force vsync off\n");
	} else if (!wglSwapIntervalEXT(0)) {
		printf("warning: failed to force vsync off, it may still be on\n");
	}
#endif
	
   if (printInfo) {
      log_out("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER));
      log_out("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION));
      log_out("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR));
      log_out("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS));
   }

   log_out("[WGLGEARS] Starting benchmark. autoExitSeconds=%d\n", g_autoExitSeconds);

   init();

   event_loop();

/* cleanup */
	wglMakeCurrent (NULL, NULL);
	wglDeleteContext (hRC);
	ShowWindow(hWnd, SW_HIDE);
	NvxClose3dClientSession(hDC);
	ReleaseDC (hWnd, hDC);
	DestroyWindow(hWnd);

	ExitProcess(0);
	return EXIT_SUCCESS;
}
