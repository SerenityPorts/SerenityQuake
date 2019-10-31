/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
#include <termios.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <stdarg.h>
#include <stdio.h>
#include <signal.h>

#define GLX_GLXEXT_PROTOTYPES
#include <GL/glx.h>

#include <X11/keysym.h>
#include <X11/cursorfont.h>
#include <X11/extensions/xf86vmode.h>

#include "common.h"
#include "console.h"
#include "glquake.h"
#include "input.h"
#include "keys.h"
#include "quakedef.h"
#include "sbar.h"
#include "screen.h"
#include "sys.h"
#include "vid.h"

#ifdef NQ_HACK
#include "host.h"
#endif

// FIXME - complete the refactoring of core X stuff into here
#include "x11_core.h"
#include "in_x11.h"

/*
 * glXGetProcAddress - This function is defined in GLX version 1.4, but this
 * is not common enough yet to rely on it being present in any old libGL.so
 * (e.g. Nvidia's proprietary drivers). glXGetProcAddressARB has been around
 * longer and actually forms part of the current Linux OpenGL ABI
 * - http://oss.sgi.com/projects/ogl-sample/ABI/
 */
#ifndef GLX_ARB_get_proc_address
#error "glXGetProcAddressARB is REQUIRED"
#endif
#define glXGetProcAddress glXGetProcAddressARB

void *
GL_GetProcAddress(const char *name)
{
    return glXGetProcAddress((GLubyte *)name);
}

/* compatibility cludges for new menu code */
qboolean VID_CheckAdequateMem(int width, int height) { return true; }
int vid_modenum;

static int scrnum;
static GLXContext ctx = NULL;

viddef_t vid;			// global video state

cvar_t vid_mode = { "vid_mode", "0", false };

// FIXME - useless, or for vidmode changes?
static int win_x, win_y;

static XF86VidModeModeInfo saved_vidmode;
static qboolean vidmode_active = false;
static XVisualInfo *x_visinfo;

/*-----------------------------------------------------------------------*/

float gldepthmin, gldepthmax;

/*-----------------------------------------------------------------------*/
void
D_BeginDirectRect(int x, int y, const byte *pbitmap, int width, int height)
{
// direct drawing of the "accessing disk" icon isn't supported under GLX
}

void
D_EndDirectRect(int x, int y, int width, int height)
{
// direct drawing of the "accessing disk" icon isn't supported under GLX
}

static void
HandleEvents(void)
{
    XEvent event;

    if (!x_disp)
	return;

    while (XPending(x_disp)) {
	XNextEvent(x_disp, &event);

        switch (event.type) {
            case KeyPress:
            case KeyRelease:
            case MotionNotify:
            case ButtonPress:
            case ButtonRelease:
                IN_X11_HandleInputEvent(&event);
                break;

            case CreateNotify:
                win_x = event.xcreatewindow.x;
                win_y = event.xcreatewindow.y;
                break;

            case ConfigureNotify:
                win_x = event.xconfigure.x;
                win_y = event.xconfigure.y;
                break;
	}
    }
}

void (*VID_SetGammaRamp)(unsigned short ramp[3][256]);
static unsigned short *x11_gamma_ramp;
static int x11_gamma_size;

void
signal_handler(int sig)
{
    printf("Received signal %d, exiting...\n", sig);
    if (VID_SetGammaRamp)
	XF86VidModeSetGammaRamp(x_disp, scrnum, x11_gamma_size,
				x11_gamma_ramp,
				x11_gamma_ramp + x11_gamma_size,
				x11_gamma_ramp + x11_gamma_size * 2);
    XCloseDisplay(x_disp);
    Sys_Quit();
}

void
InitSig(void)
{
    signal(SIGHUP, signal_handler);
    signal(SIGINT, signal_handler);
    signal(SIGQUIT, signal_handler);
    signal(SIGILL, signal_handler);
    signal(SIGTRAP, signal_handler);
    signal(SIGIOT, signal_handler);
    signal(SIGBUS, signal_handler);
    signal(SIGFPE, signal_handler);
    signal(SIGSEGV, signal_handler);
    signal(SIGTERM, signal_handler);
}

/*
 * VID_ShiftPalette
 * - Updates hardware gamma
 */
void
VID_ShiftPalette(const byte *palette)
{
//      VID_SetPalette(palette);
}

void
VID_SetPalette(const byte *palette)
{
    QPic32_InitPalettes(palette);
}

static void
VID_SetXF86GammaRamp(unsigned short ramp[3][256])
{
    int i;
    unsigned short *r, *g, *b;

    if (!x_disp)
	Sys_Error("%s: x_disp == NULL!", __func__);

    /*
     * Need to scale the gamma ramp to the hardware size
     */
    r = Hunk_TempAlloc(3 * x11_gamma_size * sizeof(unsigned short));
    g = r + x11_gamma_size;
    b = r + x11_gamma_size * 2;
    for (i = 0; i < x11_gamma_size; i++) {
	r[i] = ramp[0][i * 256 / x11_gamma_size];
	g[i] = ramp[1][i * 256 / x11_gamma_size];
	b[i] = ramp[2][i * 256 / x11_gamma_size];
    }

    XF86VidModeSetGammaRamp(x_disp, scrnum, x11_gamma_size, r, g, b);
}

/*
 * Gamma_Init
 * - Checks to see if gamma settings are available
 * - Saves the current gamma settings
 * - Sets the default gamma ramp function
 */
static void
Gamma_Init()
{
    Bool ret;
    int size;

    ret = XF86VidModeGetGammaRampSize(x_disp, scrnum, &x11_gamma_size);
    if (!ret|| !x11_gamma_size) {
	VID_SetGammaRamp = NULL;
	return;
    }

    size = 3 * x11_gamma_size * sizeof(unsigned short);
    x11_gamma_ramp = Hunk_AllocName(size, "x11_gamma_ramp");

    ret = XF86VidModeGetGammaRamp(x_disp, scrnum, x11_gamma_size,
				  x11_gamma_ramp,
				  x11_gamma_ramp + x11_gamma_size,
				  x11_gamma_ramp + x11_gamma_size * 2);
    if (ret)
	VID_SetGammaRamp = VID_SetXF86GammaRamp;
    else
	VID_SetGammaRamp = NULL;
}

/*
=================
GL_BeginRendering

=================
*/
void
GL_BeginRendering(int *x, int *y, int *width, int *height)
{
    *x = *y = 0;
    *width = vid.width;
    *height = vid.height;
}


void
GL_EndRendering(void)
{
    glXSwapBuffers(x_disp, x_win);
}

#if 0
/* FIXME - re-enable? */
static void
Check_Gamma(byte *palette)
{
    float f, inf;
    byte newpalette[768];
    int i;

    if ((i = COM_CheckParm("-gamma")) == 0)
	vid_gamma = 1.0;
    else
	vid_gamma = Q_atof(com_argv[i + 1]);

    for (i = 0; i < 768; i++) {
	f = pow((palette[i] + 1) / 256.0, vid_gamma);
	inf = f * 255 + 0.5;
	if (inf < 0)
	    inf = 0;
	if (inf > 255)
	    inf = 255;
	newpalette[i] = inf;
    }

    memcpy(palette, newpalette, sizeof(newpalette));
}
#endif

static void
VID_InitCvars(void)
{
    Cvar_RegisterVariable(&vid_mode);
    Cvar_RegisterVariable(&gl_npot);
}

static void
VID_InitModeList(void)
{
    XF86VidModeModeInfo **xmodes, *xmode;
    qvidmode_t *mode;
    int i, numxmodes;

    nummodes = 1;
    mode = &modelist[1];

    XF86VidModeGetAllModeLines(x_disp, x_visinfo->screen, &numxmodes, &xmodes);
    xmode = *xmodes;
    for (i = 0; i < numxmodes; i++, xmode++) {
	if (nummodes == MAX_MODE_LIST)
	    break;
	if (xmode->hdisplay > MAXWIDTH || xmode->vdisplay > MAXHEIGHT)
	    continue;

	mode->modenum = nummodes;
	mode->width = xmode->hdisplay;
	mode->height = xmode->vdisplay;
	mode->bpp = x_visinfo->depth;
	mode->refresh = 1000 * xmode->dotclock / xmode->htotal / xmode->vtotal;
	nummodes++;
	mode++;
    }
    XFree(xmodes);

    VID_SortModeList(modelist, nummodes);
}

/*
 * Before setting a fullscreen mode, save the current video mode so we
 * can try restore it later.  I'm not sure if I've understood
 * correctly, but we can only query the current video mode by getting
 * the VidModeModeLine and the dotclock, from which we have to build
 * our own VidModeModeInfo struct in order to set it later?!?  Let's
 * try it...
 */
static void
VID_save_vidmode()
{
    int dotclock;
    XF86VidModeModeLine modeline;

    XF86VidModeGetModeLine(x_disp, x_visinfo->screen, &dotclock, &modeline);
    saved_vidmode.dotclock = dotclock;
    saved_vidmode.hdisplay = modeline.hdisplay;
    saved_vidmode.hsyncstart = modeline.hsyncstart;
    saved_vidmode.hsyncend = modeline.hsyncend;
    saved_vidmode.htotal = modeline.htotal;
    saved_vidmode.vdisplay = modeline.vdisplay;
    saved_vidmode.vsyncstart = modeline.vsyncstart;
    saved_vidmode.vsyncend = modeline.vsyncend;
    saved_vidmode.vtotal = modeline.vtotal;
    saved_vidmode.flags = modeline.flags;
    saved_vidmode.privsize = modeline.privsize;
    saved_vidmode.private = modeline.private;
}

static void
VID_restore_vidmode()
{
    if (vidmode_active) {
	XF86VidModeSwitchToMode(x_disp, x_visinfo->screen, &saved_vidmode);
        XFlush(x_disp);
	if (saved_vidmode.privsize && saved_vidmode.private)
	    XFree(saved_vidmode.private);
        vidmode_active = false;
    }
}

qboolean
VID_SetMode(const qvidmode_t *mode, const byte *palette)
{
    qboolean reload_textures = false;
    unsigned long valuemask;
    XSetWindowAttributes attributes = {0};
    Window root;

    /* Free the existing structures */
    if (ctx) {
        GL_Shutdown();
	glXDestroyContext(x_disp, ctx);
	ctx = NULL;
        reload_textures = true;
    }
    if (x_win) {
        IN_UngrabKeyboard();
        IN_UngrabMouse();
	XDestroyWindow(x_disp, x_win);
	x_win = 0;
    }

    vid_modenum = mode - modelist;

    root = RootWindow(x_disp, scrnum);

    /* common window attributes */
    valuemask = CWBackPixel | CWColormap | CWEventMask;
    attributes.background_pixel = 0;
    attributes.colormap = XCreateColormap(x_disp, root, x_visinfo->visual, AllocNone);
    attributes.event_mask = X_CORE_MASK | X_KEY_MASK;

    if (VID_IsFullScreen()) {
	XF86VidModeModeInfo **xmodes, *xmode;
	int i, numxmodes, refresh;
	Bool result;

        /* Attempt to set the vid mode */
	XF86VidModeGetAllModeLines(x_disp, x_visinfo->screen, &numxmodes, &xmodes);
	xmode = *xmodes;
	for (i = 0; i < numxmodes; i++, xmode++) {
	    if (xmode->hdisplay != mode->width || xmode->vdisplay != mode->height)
		continue;
	    refresh = 1000 * xmode->dotclock / xmode->htotal / xmode->vtotal;
	    if (refresh == mode->refresh)
		break;
	}
	if (i == numxmodes)
	    Sys_Error("%s: unable to find matching X display mode", __func__);

	result = XF86VidModeSwitchToMode(x_disp, x_visinfo->screen, xmode);
	if (!result)
	    Sys_Error("%s: mode switch failed", __func__);
	XFree(xmodes);
        XFlush(x_disp);

        /* Fullscreen mode is now active */
        vidmode_active = true;

	valuemask |= CWSaveUnder | CWBackingStore | CWOverrideRedirect;
	attributes.override_redirect = True;
	attributes.backing_store = NotUseful;
	attributes.save_under = False;
    } else {
	/* Windowed */
	valuemask |= CWBorderPixel;
	attributes.border_pixel = 0;

        /* Restore the desktop mode, if we were previously fullscreen */
        VID_restore_vidmode();
    }

    /* create the main window */
    x_win = XCreateWindow(x_disp, XRootWindow(x_disp, x_visinfo->screen),
                          0, 0, // x, y
                          mode->width, mode->height, 0, //borderwidth
                          mode->bpp,
                          InputOutput, x_visinfo->visual, valuemask, &attributes);
    XFreeColormap(x_disp, attributes.colormap);
    XStoreName(x_disp, x_win, "TyrQuake");

    XMapWindow(x_disp, x_win);
    if (VID_IsFullScreen()) {
	XMoveWindow(x_disp, x_win, 0, 0);
	XRaiseWindow(x_disp, x_win);
	XF86VidModeSetViewPort(x_disp, x_visinfo->screen, 0, 0);
    }

    /* Wait for first expose event */
    XEvent event;
    do {
        XNextEvent(x_disp, &event);
    } while (event.type != Expose || event.xexpose.count);

    ctx = glXCreateContext(x_disp, x_visinfo, NULL, True);
    glXMakeCurrent(x_disp, x_win, ctx);

    GL_Init();
    if (reload_textures)
	GL_ReloadTextures();

    vid.width = vid.conwidth = mode->width;
    vid.height = vid.conheight = mode->height;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);
    vid.numpages = 0; /* Contents of the back buffer are undefined after swap */
    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

    Con_SafePrintf("Video mode %dx%d initialized.\n", mode->width, mode->height);

    vid.recalc_refdef = true;

    SCR_CheckResize();
    Con_CheckResize();

    /* Ensure the new window has the focus */
    XSetInputFocus(x_disp, x_win, RevertToParent, CurrentTime);
    IN_Commands(); // update grabs (FIXME - this is a wierd function call to do that!)

    return true;
}

void
VID_Init(const byte *palette)
{
    int attrib[] = {
	GLX_RGBA,
	GLX_RED_SIZE, 1,
	GLX_GREEN_SIZE, 1,
	GLX_BLUE_SIZE, 1,
	GLX_DOUBLEBUFFER,
	GLX_DEPTH_SIZE, 1,
	None
    };
    char gldir[MAX_OSPATH];
    int MajorVersion, MinorVersion;
    const qvidmode_t *setmode;
    qvidmode_t *mode;

    VID_InitCvars();
    VID_InitModeCvars();

    x_disp = XOpenDisplay(NULL);
    if (!x_disp) {
	if (getenv("DISPLAY"))
	    Sys_Error("VID: Could not open display [%s]", getenv("DISPLAY"));
	else
	    Sys_Error("VID: Could not open local display\n");
    }
    scrnum = DefaultScreen(x_disp);

    // Check video mode extension
    MajorVersion = MinorVersion = 0;
    if (XF86VidModeQueryVersion(x_disp, &MajorVersion, &MinorVersion)) {
	Con_Printf("Using XFree86-VidModeExtension Version %i.%i\n",
		   MajorVersion, MinorVersion);
    }

    x_visinfo = glXChooseVisual(x_disp, scrnum, attrib);
    if (!x_visinfo) {
	fprintf(stderr,
		"qkHack: Error couldn't get an RGB, Double-buffered, "
		"Depth visual\n");
	exit(EXIT_FAILURE);
    }

    Gamma_Init();

    /* Save the current video mode so we can restore when moving to windowed modes */
    VID_save_vidmode();

    /* Init a default windowed mode */
    mode = modelist;
    mode->modenum = 0;
    mode->width = 640;
    mode->height = 480;
    mode->bpp = x_visinfo->depth;
    mode->refresh = 0;
    nummodes = 1;

    VID_InitModeList();
    setmode = VID_GetCmdlineMode();
    if (!setmode)
	setmode = &modelist[0];

    VID_SetMode(setmode, palette);

    VID_SetPalette(palette);

    vid_menudrawfn = VID_MenuDraw;
    vid_menukeyfn = VID_MenuKey;

    InitSig();			// trap evil signals

    qsnprintf(gldir, sizeof(gldir), "%s/glquake", com_gamedir);
    Sys_mkdir(gldir);
}

void
VID_Shutdown(void)
{
    if (VID_SetGammaRamp) {
	XF86VidModeSetGammaRamp(x_disp, scrnum, x11_gamma_size,
				x11_gamma_ramp,
				x11_gamma_ramp + x11_gamma_size,
				x11_gamma_ramp + x11_gamma_size * 2);
    }
    if (x_disp != NULL) {
	if (ctx != NULL)
	    glXDestroyContext(x_disp, ctx);
	if (x_win != None)
	    XDestroyWindow(x_disp, x_win);
	if (vidmode_active)
	    VID_restore_vidmode();
	XCloseDisplay(x_disp);
    }
    vidmode_active = false;
    x_disp = NULL;
    x_win = None;
    ctx = NULL;
}

void
Sys_SendKeyEvents(void)
{
    HandleEvents();
}

void
Force_CenterView_f(void)
{
    cl.viewangles[PITCH] = 0;
}

void
VID_UnlockBuffer()
{
}

void
VID_LockBuffer()
{
}

qboolean
VID_IsFullScreen()
{
    return vid_modenum != 0;
}
