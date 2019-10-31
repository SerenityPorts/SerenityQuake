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

#include "client.h"
#include "common.h"
#include "console.h"
#include "in_x11.h"
#include "keys.h"
#include "quakedef.h"
#include "x11_core.h"
#include "vid.h"
#include "sys.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

static qboolean mouse_available = false;	// Mouse available for use
static qboolean keyboard_grab_active = false;
qboolean mouse_grab_active = false;

int mouse_x, mouse_y;
static int old_mouse_x, old_mouse_y;

static void
windowed_mouse_f(struct cvar_s *var)
{
    if (var->value) {
	Con_DPrintf("Callback: _windowed_mouse ON\n");
	if (!VID_IsFullScreen()) {
	    IN_GrabMouse();
	    IN_GrabKeyboard();
	}
    } else {
	Con_DPrintf("Callback: _windowed_mouse OFF\n");
	if (!VID_IsFullScreen()) {
	    IN_UngrabMouse();
	    IN_UngrabKeyboard();
	}
    }
}


cvar_t in_mouse = { "in_mouse", "1", false };
cvar_t _windowed_mouse = { "_windowed_mouse", "0", true, false, 0, windowed_mouse_f };
static cvar_t m_filter = { "m_filter", "0" };

static Cursor
CreateNullCursor(void)
{
    Pixmap cursormask;
    XGCValues xgc;
    GC gc;
    XColor dummycolor;
    Cursor cursor;

    cursormask = XCreatePixmap(x_disp, x_win, 1, 1, 1 /*depth */ );
    xgc.function = GXclear;
    gc = XCreateGC(x_disp, cursormask, GCFunction, &xgc);
    XFillRectangle(x_disp, cursormask, gc, 0, 0, 1, 1);
    dummycolor.pixel = 0;
    dummycolor.flags = 0;	// ~(DoRed | DoGreen | DoBlue)
    cursor = XCreatePixmapCursor(x_disp, cursormask, cursormask,
				 &dummycolor, &dummycolor, 0, 0);
    XFreePixmap(x_disp, cursormask);
    XFreeGC(x_disp, gc);
    return cursor;
}

void
IN_CenterMouse(void)
{
    // FIXME - work with the current mask...
    // FIXME - check active mouse, etc.
    XSelectInput(x_disp, x_win, (X_CORE_MASK | X_KEY_MASK | X_MOUSE_MASK)
		 & ~PointerMotionMask);
    XWarpPointer(x_disp, None, x_win, 0, 0, 0, 0,
		 vid.width / 2, vid.height / 2);
    XSelectInput(x_disp, x_win, X_CORE_MASK | X_KEY_MASK | X_MOUSE_MASK);
}

void
IN_GrabMouse(void)
{
    int result;

    /* Should never be called if no mouse or grab already active */
    assert(mouse_available);
    assert(!mouse_grab_active);

    result = XGrabPointer(x_disp, x_win, True, 0, GrabModeAsync, GrabModeAsync, x_win, None, CurrentTime);
    switch (result) {
        case GrabSuccess:
            XDefineCursor(x_disp, x_win, CreateNullCursor());
            IN_CenterMouse();
            mouse_x = old_mouse_x = 0;
            mouse_y = old_mouse_y = 0;
            mouse_grab_active = true;
            break;
        case GrabNotViewable:
            Con_DPrintf("%s: GrabNotViewable\n", __func__);
            break;
        case AlreadyGrabbed:
            Con_DPrintf("%s: AlreadyGrabbed\n", __func__);
            break;
        case GrabFrozen:
            Con_DPrintf("%s: GrabFrozen\n", __func__);
            break;
        case GrabInvalidTime:
            Con_DPrintf("%s: GrabInvalidTime\n", __func__);
            break;
    }
}

void
IN_UngrabMouse(void)
{
    if (mouse_grab_active) {
        XSelectInput(x_disp, x_win, X_CORE_MASK | X_KEY_MASK);
	XUngrabPointer(x_disp, CurrentTime);
	XUndefineCursor(x_disp, x_win);
	mouse_grab_active = false;
    }
}

void
IN_GrabKeyboard(void)
{
    if (!keyboard_grab_active) {
	int err;

	err = XGrabKeyboard(x_disp, x_win, False,
			    GrabModeAsync, GrabModeAsync, CurrentTime);
	if (err) {
	    Con_DPrintf("%s: Couldn't grab keyboard!\n", __func__);
	    keyboard_grab_active = true;
	    return;
	}

	keyboard_grab_active = true;
    }
}

void
IN_UngrabKeyboard(void)
{
    if (keyboard_grab_active) {
	XUngrabKeyboard(x_disp, CurrentTime);
	keyboard_grab_active = false;
    }
}

// XLateKey - Transform from X key symbols to Quake's symbols
int
XLateKey(XKeyEvent *event)
{
    KeySym keysym;
    knum_t key;
    char keychar;

    keysym = XLookupKeysym(event, 0);
    switch (keysym) {
        case XK_KP_Page_Up:
        case XK_Page_Up:
            key = K_PGUP;
            break;
        case XK_KP_Page_Down:
        case XK_Page_Down:
            key = K_PGDN;
            break;
        case XK_KP_Home:
        case XK_Home:
            key = K_HOME;
            break;
        case XK_KP_End:
        case XK_End:
            key = K_END;
            break;
        case XK_KP_Left:
        case XK_Left:
            key = K_LEFTARROW;
            break;
        case XK_KP_Right:
        case XK_Right:
            key = K_RIGHTARROW;
            break;
        case XK_KP_Down:
        case XK_Down:
            key = K_DOWNARROW;
            break;
        case XK_KP_Up:
        case XK_Up:
            key = K_UPARROW;
            break;
        case XK_Escape:
            key = K_ESCAPE;
            break;
        case XK_KP_Enter:
        case XK_Return:
            key = K_ENTER;
            break;
        case XK_Tab:
            key = K_TAB;
            break;
        case XK_F1:
            key = K_F1;
            break;
        case XK_F2:
            key = K_F2;
            break;
        case XK_F3:
            key = K_F3;
            break;
        case XK_F4:
            key = K_F4;
            break;
        case XK_F5:
            key = K_F5;
            break;
        case XK_F6:
            key = K_F6;
            break;
        case XK_F7:
            key = K_F7;
            break;
        case XK_F8:
            key = K_F8;
            break;
        case XK_F9:
            key = K_F9;
            break;
        case XK_F10:
            key = K_F10;
            break;
        case XK_F11:
            key = K_F11;
            break;
        case XK_F12:
            key = K_F12;
            break;
        case XK_BackSpace:
            key = K_BACKSPACE;
            break;
        case XK_KP_Delete:
        case XK_Delete:
            key = K_DEL;
            break;
        case XK_Pause:
            key = K_PAUSE;
            break;
        case XK_Shift_L:
            key = K_LSHIFT;
            break;
        case XK_Shift_R:
            key = K_RSHIFT;
            break;
        case XK_Execute:
        case XK_Control_L:
            key = K_LCTRL;
            break;
        case XK_Control_R:
            key = K_RCTRL;
            break;
        case XK_Alt_L:
            key = K_LALT;
            break;
        case XK_Meta_L:
            key = K_LMETA;
            break;
        case XK_Alt_R:
            key = K_RALT;
            break;
        case XK_Meta_R:
            key = K_RMETA;
            break;
        case XK_KP_Begin:
        case XK_KP_5:
            key = '5';
            break;
        case XK_Insert:
        case XK_KP_Insert:
            key = K_INS;
            break;
        case XK_KP_Multiply:
            key = '*';
            break;
        case XK_KP_Add:
            key = '+';
            break;
        case XK_KP_Subtract:
            key = '-';
            break;
        case XK_KP_Divide:
            key = '/';
            break;
        default:
            XLookupString(event, &keychar, sizeof(keychar), &keysym, NULL);
            key = (unsigned char)keychar;
            if (key >= 'A' && key <= 'Z')
                key = key - 'A' + 'a';
            break;
    }

    return key;
}

static qboolean
IN_X11_EventIsKeyRepeat(XEvent *event)
{
    XEvent next;

    /* Check if there is another queued event */
    if (!XEventsQueued(x_disp, QueuedAfterReading))
        return false;

    /* Check if the event is a key press that exactly matches the current key release */
    XPeekEvent(x_disp, &next);
    if (next.type != KeyPress)
        return false;
    if (next.xkey.time != event->xkey.time)
        return false;
    if (next.xkey.keycode != event->xkey.keycode)
        return false;

    return true;
}

void
IN_X11_HandleInputEvent(XEvent *event)
{
    XEvent dummy;
    qboolean down;

    switch (event->type) {
	case KeyRelease:
            /* Discard any key repeat events in-game, allow for console/menu */
            if (key_dest == key_game && IN_X11_EventIsKeyRepeat(event)) {
                XNextEvent(x_disp, &dummy);
            } else {
                Key_Event(XLateKey(&event->xkey), event->type == KeyPress);
            }
            break;
	case KeyPress:
	    Key_Event(XLateKey(&event->xkey), event->type == KeyPress);
	    break;

	case MotionNotify:
	    if (mouse_grab_active) {
                mouse_x = event->xmotion.x - (int)(vid.width / 2);
                mouse_y = event->xmotion.y - (int)(vid.height / 2);

                if (mouse_x || mouse_y)
                    IN_CenterMouse();
	    }
	    break;

	case ButtonPress:
	case ButtonRelease:
            down = (event->type == ButtonPress);
	    if (event->xbutton.button == 1)
		Key_Event(K_MOUSE1, down);
	    else if (event->xbutton.button == 2)
		Key_Event(K_MOUSE3, down);
	    else if (event->xbutton.button == 3)
		Key_Event(K_MOUSE2, down);
	    else if (event->xbutton.button == 4)
		Key_Event(K_MWHEELUP, down);
	    else if (event->xbutton.button == 5)
		Key_Event(K_MWHEELDOWN, down);
	    else if (event->xbutton.button == 6)
		Key_Event(K_MOUSE4, down);
	    else if (event->xbutton.button == 7)
		Key_Event(K_MOUSE5, down);
	    else if (event->xbutton.button == 8)
		Key_Event(K_MOUSE6, down);
	    else if (event->xbutton.button == 9)
		Key_Event(K_MOUSE7, down);
	    else if (event->xbutton.button == 10)
		Key_Event(K_MOUSE8, down);
	    break;
    }
}

static void
IN_InitCvars(void)
{
    Cvar_RegisterVariable(&in_mouse);
    Cvar_RegisterVariable(&m_filter);
    Cvar_RegisterVariable(&_windowed_mouse);
}

void
IN_Init(void)
{
    keyboard_grab_active = false;
    mouse_grab_active = false;

    // FIXME - do proper detection?
    //       - Also, look at other vid_*.c files for clues
    mouse_available = (COM_CheckParm("-nomouse")) ? false : true;

    if (x_disp == NULL)
	Sys_Error("x_disp not initialised before input...");

    IN_InitCvars();

    if (VID_IsFullScreen()) {
	if (!mouse_grab_active)
	    IN_GrabMouse();
	if (!keyboard_grab_active)
	    IN_GrabKeyboard();
    }
}

void
IN_Shutdown(void)
{
    IN_UngrabMouse();
    IN_UngrabKeyboard();
    mouse_available = 0;
}

static void
IN_MouseMove(usercmd_t *cmd)
{
    if (!mouse_available)
	return;

    if (m_filter.value) {
	mouse_x = (mouse_x + old_mouse_x) * 0.5;
	mouse_y = (mouse_y + old_mouse_y) * 0.5;
    }
    old_mouse_x = mouse_x;
    old_mouse_y = mouse_y;

    mouse_x *= sensitivity.value;
    mouse_y *= sensitivity.value;

    /* add mouse X/Y movement to cmd */
    if ((in_strafe.state & 1) || (lookstrafe.value && ((in_mlook.state & 1) ^ (int)m_freelook.value)))
	cmd->sidemove += m_side.value * mouse_x;
    else
	cl.viewangles[YAW] -= m_yaw.value * mouse_x;

    if ((in_mlook.state & 1) ^ (int)m_freelook.value)
	if (mouse_x || mouse_y)
	    V_StopPitchDrift ();

    if (((in_mlook.state & 1) ^ (int)m_freelook.value) && !(in_strafe.state & 1)) {
	cl.viewangles[PITCH] += m_pitch.value * mouse_y;
	if (cl.viewangles[PITCH] > 80)
	    cl.viewangles[PITCH] = 80;
	if (cl.viewangles[PITCH] < -70)
	    cl.viewangles[PITCH] = -70;
    } else {
	if ((in_strafe.state & 1) && noclip_anglehack)
	    cmd->upmove -= m_forward.value * mouse_y;
	else
	    cmd->forwardmove -= m_forward.value * mouse_y;
    }
    mouse_x = mouse_y = 0;
}

void
IN_Move(usercmd_t *cmd)
{
    IN_MouseMove(cmd);
}

void
IN_Commands(void)
{
    if (!mouse_available)
	return;

    if (mouse_grab_active) {
        if (key_dest != key_game && !VID_IsFullScreen()) {
            IN_UngrabMouse();
            IN_UngrabKeyboard();
        }
    } else {
        if ((key_dest == key_game && _windowed_mouse.value) || VID_IsFullScreen()) {
            IN_GrabKeyboard();
            IN_GrabMouse();
            IN_CenterMouse();
        }
    }
}
