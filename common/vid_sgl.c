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

#ifdef APPLE_OPENGL
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <SDL2/SDL.h>

#include "cmd.h"
#include "console.h"
#include "glquake.h"
#include "input.h"
#include "quakedef.h"
#include "sbar.h"
#include "screen.h"
#include "sdl_common.h"
#include "sys.h"
#include "vid.h"

#ifdef NQ_HACK
#include "host.h"
#endif

void *
GL_GetProcAddress(const char *name)
{
    return SDL_GL_GetProcAddress(name);
}

int vid_modenum = VID_MODE_NONE;

static cvar_t vid_mode = {
    .name = "vid_mode",
    .string = stringify(VID_MODE_WINDOWED),
    .archive = false
};

viddef_t vid;

qboolean
VID_IsFullScreen(void)
{
    return vid_modenum != 0;
}

qboolean VID_CheckAdequateMem(int width, int height) { return true; }
void VID_LockBuffer(void) {}
void VID_UnlockBuffer(void) {}

void (*VID_SetGammaRamp)(unsigned short ramp[3][256]) = NULL;

float gldepthmin, gldepthmax;

void VID_Update(vrect_t *rects) {}
void D_BeginDirectRect(int x, int y, const byte *pbitmap, int width, int height) {}
void D_EndDirectRect(int x, int y, int width, int height) {}

/*
 * FIXME!!
 *
 * Move stuff around or create abstractions so these hacks aren't needed
 */

#ifndef _WIN32
void Sys_SendKeyEvents(void)
{
    IN_ProcessEvents();
}
#endif

#ifdef _WIN32
#include <windows.h>

qboolean DDActive;
HWND mainwindow;
void VID_SetDefaultMode(void) {}
qboolean window_visible(void) { return true; }
#endif


/*
 * MODESETTING STUFF (command line only)
 * FIXME - I'm sorry, it's horrible :(
 */
typedef struct {
    typeof(SDL_PIXELFORMAT_UNKNOWN) format;
} qvidformat_t;

static SDL_PixelFormat *sdl_desktop_format = NULL;

static void
VID_InitCvars(void)
{
    Cvar_RegisterVariable(&vid_mode);
    Cvar_RegisterVariable(&gl_npot);
}

static void
VID_InitModeList(void)
{
    int i, err;
    int displays, sdlmodes;
    SDL_DisplayMode sdlmode;
    qvidmode_t *mode;
    qvidformat_t *format;

    displays = SDL_GetNumVideoDisplays();
    if (displays < 1)
	Sys_Error("%s: no displays found (%s)", __func__, SDL_GetError());

    /* FIXME - allow use of more than one display */
    sdlmodes = SDL_GetNumDisplayModes(0);
    if (sdlmodes < 0)
	Con_SafePrintf("%s: error enumerating SDL display modes (%s)\n",
		       __func__, SDL_GetError());

    /*
     * Check availability of fullscreen modes
     * (default to display 0 for now)
     */
    mode = &modelist[1];
    nummodes = 1;
    for (i = 0; i < sdlmodes && nummodes < MAX_MODE_LIST; i++) {
	err = SDL_GetDisplayMode(0, i, &sdlmode);
	if (err)
	    Sys_Error("%s: couldn't get mode %d info (%s)",
		      __func__, i, SDL_GetError());

	Sys_Printf("%s: checking mode %i: %dx%d, %s\n", __func__,
		   i, sdlmode.w, sdlmode.h, SDL_GetPixelFormatName(sdlmode.format));

	if (SDL_PIXELTYPE(sdlmode.format) == SDL_PIXELTYPE_PACKED32)
	    modelist[nummodes].bpp = 32;
	else if (SDL_PIXELTYPE(sdlmode.format) == SDL_PIXELTYPE_PACKED16)
	    modelist[nummodes].bpp = 16;
	else
	    continue;

	mode->modenum = nummodes;
	mode->width = sdlmode.w;
	mode->height = sdlmode.h;
	mode->refresh = sdlmode.refresh_rate;
	format = (qvidformat_t *)mode->driverdata;
	format->format = sdlmode.format;
	nummodes++;
	mode++;
    }

    VID_SortModeList(modelist, nummodes);
}

static SDL_GLContext gl_context = NULL;

qboolean
VID_SetMode(const qvidmode_t *mode, const byte *palette)
{
    Uint32 flags;
    int i, err;
    qboolean reload_textures = false;
    int depths[] = { 32, 24, 16 };

    if (gl_context) {
        GL_Shutdown();
	SDL_GL_DeleteContext(gl_context);
        reload_textures = true;
    }
    if (sdl_window)
	SDL_DestroyWindow(sdl_window);

    flags = SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL;
    if (mode - modelist != 0)
	flags |= SDL_WINDOW_FULLSCREEN;

    /*
     * Try to set the correct attributes for our desired GL context
     * - Ensure we request the compatibility context
     * - Set the requested color buffer BPP (although we may get more?)
     * - Try to get the best depth buffer we can, try 32, 24, then 16 bits.
     */
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, mode->bpp);

    for (i = 0; i < ARRAY_SIZE(depths); i++) {
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, depths[i]);
        sdl_window = SDL_CreateWindow("TyrQuake",
                                      SDL_WINDOWPOS_UNDEFINED,
                                      SDL_WINDOWPOS_UNDEFINED,
                                      mode->width, mode->height, flags);
        if (sdl_window)
            break;
    }
    if (!sdl_window)
	Sys_Error("%s: Unable to create window: %s", __func__, SDL_GetError());

    gl_context = SDL_GL_CreateContext(sdl_window);
    if (!gl_context)
	Sys_Error("%s: Unable to create OpenGL context: %s",
		  __func__, SDL_GetError());

    err = SDL_GL_MakeCurrent(sdl_window, gl_context);
    if (err)
	Sys_Error("%s: SDL_GL_MakeCurrent() failed: %s",
		  __func__, SDL_GetError());

    GL_Init();
    if (reload_textures)
	GL_ReloadTextures();

    vid.numpages = 0; /* Contents of the back buffer are undefined after swap */
    vid.width = vid.conwidth = mode->width;
    vid.height = vid.conheight = mode->height;
    vid.maxwarpwidth = WARP_WIDTH;
    vid.maxwarpheight = WARP_HEIGHT;
    vid.aspect = ((float)vid.height / (float)vid.width) * (320.0 / 240.0);

    vid.colormap = host_colormap;
    vid.fullbright = 256 - LittleLong(*((int *)vid.colormap + 2048));

    vid_modenum = mode - modelist;
    Cvar_SetValue("vid_mode", vid_modenum);

    vid.recalc_refdef = 1;

    SCR_CheckResize();
    Con_CheckResize();

    return true;
}

void
VID_Init(const byte *palette)
{
    int err;
    SDL_DisplayMode desktop_mode;
    qvidmode_t *mode;
    qvidformat_t *format;
    const qvidmode_t *setmode;

    VID_InitCvars();
    VID_InitModeCvars();

    Cmd_AddCommand("vid_describemodes", VID_DescribeModes_f);

    Q_SDL_InitOnce();
    err = SDL_InitSubSystem(SDL_INIT_VIDEO);
    if (err < 0)
	Sys_Error("VID: Couldn't load SDL: %s", SDL_GetError());

    err = SDL_GetDesktopDisplayMode(0, &desktop_mode);
    if (err)
	Sys_Error("%s: Unable to query desktop display mode (%s)",
		  __func__, SDL_GetError());
    sdl_desktop_format = SDL_AllocFormat(desktop_mode.format);
    if (!sdl_desktop_format)
	Sys_Error("%s: Unable to allocate desktop pixel format (%s)",
		  __func__, SDL_GetError());

    /* Init the default windowed mode */
    mode = modelist;
    mode->modenum = 0;
    mode->bpp = sdl_desktop_format->BitsPerPixel;
    format = (qvidformat_t *)mode->driverdata;
    format->format = sdl_desktop_format->format;
    mode->refresh = desktop_mode.refresh_rate;
    mode->width = 640;
    mode->height = 480;
    nummodes = 1;

    /* TODO: read config files first to avoid multiple mode sets */
    VID_InitModeList();
    setmode = VID_GetCmdlineMode();
    if (!setmode)
	setmode = &modelist[0];

    VID_SetMode(setmode, palette);

    VID_SetPalette(palette);

    vid_menudrawfn = VID_MenuDraw;
    vid_menukeyfn = VID_MenuKey;
}

void
VID_Shutdown(void)
{
    if (sdl_window)
	SDL_DestroyWindow(sdl_window);
    if (sdl_desktop_format)
	SDL_FreeFormat(sdl_desktop_format);
}

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
    glFlush();
    SDL_GL_SwapWindow(sdl_window);
}

void
VID_SetPalette(const byte *palette)
{
    QPic32_InitPalettes(palette);
}

void
VID_ShiftPalette(const byte *palette)
{
    /* Done via gl_polyblend instead */
    //VID_SetPalette(palette);
}
