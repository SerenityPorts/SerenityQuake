/*
Copyright (C) 1996-1997 Id Software, Inc.
Copyright (C) 2019 Kevin Shanahan

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

#ifndef DEVELOPER_H
#define DEVELOPER_H

#include "cvar.h"
#include "mathlib.h"
#include "render.h"

extern cvar_t _debug_models;

/*
 * Panel of text to be drawn in worldspace.
 * The panel will auto-size to fit the text added.
 * Set scale to scale the text size relative to world coordinates.
 */
#define DEBUG_PANEL_MAX_LINES 20
#define DEBUG_PANEL_MAX_LINE_LENGTH 100
typedef struct {
    float scale;       // Text scale (1.0 is 8 world units per char)
    vec3_t origin;     // Origin in worldspace (center, bottom of the panel)
    vec3_t textorigin; // We offset the text slightly rather than polygonoffset nonsense
    vec3_t up, right;  // Orientation
    float alpha;

    char text[DEBUG_PANEL_MAX_LINES][DEBUG_PANEL_MAX_LINE_LENGTH];
    int lines, drawwidth, drawheight;
} debug_panel_t;

void DbgPanel_Init(debug_panel_t *panel);
void DbgPanel_SetOrientation(debug_panel_t *panel, const vec3_t origin, const vec3_t up, const vec3_t right);
void DbgPanel_Printf(debug_panel_t *panel, const char *fmt, ...) __attribute__((format(printf,2,3)));
void DbgPanel_Draw(debug_panel_t *panel);

/* MODEL DEBUG - Draw a panel above a model with some information about it. */
void DEBUG_DrawModelInfo(const entity_t *entity, const vec3_t modelorigin);

#endif
