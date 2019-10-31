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

#ifdef GLQUAKE

#include "developer.h"
#include "draw.h"
#include "glquake.h"
#include "render.h"
#include "sys.h"

/*
 * Random debugging aids that are useful for development.
 * Don't care too much about efficiency, just get things on the screen...
 * GLQuake only for now...
 */

void
DbgPanel_Init(debug_panel_t *panel)
{
    memset(panel, 0, sizeof(*panel));
    panel->scale = 0.5f;
    panel->alpha = 0.6f;
}

void
DbgPanel_SetOrientation(debug_panel_t *panel, const vec3_t origin, const vec3_t up, const vec3_t right)
{
    VectorCopy(origin, panel->origin);
    VectorCopy(up, panel->up);
    VectorCopy(right, panel->right);
}

void
DbgPanel_Printf(debug_panel_t *panel, const char *fmt, ...)
{
    va_list argptr;
    char msg[MAX_PRINTMSG];
    int in_len, out_len;
    const char *src;
    char *dst;

    if (panel->lines == DEBUG_PANEL_MAX_LINES)
        return;

    va_start(argptr, fmt);
    qvsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    src = msg;
    while (*src) {
        dst = panel->text[panel->lines];
        in_len = strcspn(msg, "\n");
        out_len = DEBUG_PANEL_MAX_LINE_LENGTH - strlen(dst) - 1;
        strncat(dst, src, qmin(in_len, out_len));
        src += in_len;
        if (*src == '\n') {
            src++;
            panel->lines++;
        }
    }
}

#define MAX_DBG_PANEL_CHARS    (DEBUG_PANEL_MAX_LINES * DEBUG_PANEL_MAX_LINE_LENGTH)
#define MAX_DBG_BUFFER_VERTS   (MAX_DBG_PANEL_CHARS * 4)
#define MAX_DBG_BUFFER_INDICES (MAX_DBG_PANEL_CHARS * 6)

typedef struct {
    int numindices;
    int numverts;
    float verts[MAX_DBG_BUFFER_VERTS][5];
    uint16_t indices[MAX_DBG_BUFFER_INDICES];
} dbgpanel_buffer_t;

static void
DbgPanel_BufferChar(dbgpanel_buffer_t *buffer, const debug_panel_t *panel, vec3_t origin, int x, int y, int num)
{
    int row, col;
    float frow, fcol, size, textsize;
    vec3_t corner;
    float *vertex;

    assert(buffer->numverts + 4 <= MAX_DBG_BUFFER_VERTS);
    assert(buffer->numindices + 6 <= MAX_DBG_BUFFER_INDICES);

    // Adjust cordinates for top-down text
    y = panel->drawheight - y - 1;

    assert(num >= 0 && num <= 255);
    if (num == 32)
        return;

    row = num >> 4;
    col = num & 15;

    frow = row * 0.0625f;
    fcol = col * 0.0625f;
    size = 0.0625f;

    textsize = 8.0f * panel->scale;
    vertex = buffer->verts[buffer->numverts];

    VectorMA(origin, x * textsize, panel->right, corner);
    VectorMA(corner, y * textsize, panel->up, vertex);
    vertex[3] = fcol;
    vertex[4] = frow + size;
    vertex += 5;

    VectorMA(origin, x * textsize, panel->right, corner);
    VectorMA(corner, (y + 1) * textsize, panel->up, vertex);
    vertex[3] = fcol;
    vertex[4] = frow;
    vertex += 5;

    VectorMA(origin, (x + 1) * textsize, panel->right, corner);
    VectorMA(corner, (y + 1) * textsize, panel->up, vertex);
    vertex[3] = fcol + size;
    vertex[4] = frow;
    vertex += 5;

    VectorMA(origin, (x + 1) * textsize, panel->right, corner);
    VectorMA(corner, y * textsize, panel->up, vertex);
    vertex[3] = fcol + size;
    vertex[4] = frow + size;

    uint16_t *index = &buffer->indices[buffer->numindices];
    index[0] = buffer->numverts + 0;
    index[1] = buffer->numverts + 1;
    index[2] = buffer->numverts + 2;
    index[3] = buffer->numverts + 0;
    index[4] = buffer->numverts + 2;
    index[5] = buffer->numverts + 3;

    buffer->numverts += 4;
    buffer->numindices += 6;
}

void
DbgPanel_Draw(debug_panel_t *panel)
{
    const glpic_t *glpic = const_container_of(draw_backtile, glpic_t, pic);
    int row, col, width, height;
    float scale;
    vec3_t base, corner;
    dbgpanel_buffer_t buffer;

    /* Find the width */
    width = 0;
    for (row = 0; row < panel->lines; row++)
        width = qmax(width, (int)strlen(panel->text[row]));
    if (!width)
        return;

    /* Find the height (remove empty row if finished with newline */
    height = panel->lines;
    if (!panel->text[panel->lines - 1][0])
        height--;

    panel->drawwidth = width;
    panel->drawheight = height;

    /* Stuff the background into the buffer first */
    scale = 8.0f * panel->scale;
    VectorMA(panel->origin, -width / 2.0f * scale, panel->right, base);

    VectorCopy(base, buffer.verts[0]);
    buffer.verts[1][3] = 0.0f;
    buffer.verts[1][4] = height * scale / 64.0f;

    VectorMA(base, height * scale, panel->up, corner);
    VectorCopy(corner, buffer.verts[1]);
    buffer.verts[0][3] = 0.0f;
    buffer.verts[0][4] = 0.0f;

    VectorMA(corner, width * scale, panel->right, corner);
    VectorCopy(corner, buffer.verts[2]);
    buffer.verts[3][3] = width * scale / 64.0f;
    buffer.verts[3][4] = 0.0f;

    VectorMA(base, width * scale, panel->right, corner);
    VectorCopy(corner, buffer.verts[3]);
    buffer.verts[2][3] = width * scale / 64.0f;
    buffer.verts[2][4] = height * scale / 64.0f;

    buffer.indices[0] = 0;
    buffer.indices[1] = 1;
    buffer.indices[2] = 2;
    buffer.indices[3] = 0;
    buffer.indices[4] = 2;
    buffer.indices[5] = 3;

    buffer.numverts = 4;
    buffer.numindices = 6;

    GL_DisableMultitexture();
    if (gl_mtexable)
	qglClientActiveTexture(GL_TEXTURE0);

    if (panel->alpha < 1.0f) {
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glColor4f(1, 1, 1, qmax(panel->alpha, 0.0f));
    }

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    GL_Bind(glpic->texnum);
    glEnable(GL_VERTEX_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    glVertexPointer(3, GL_FLOAT, 5 * sizeof(float), &buffer.verts[0][0]);
    glTexCoordPointer(2, GL_FLOAT, 5 * sizeof(float), &buffer.verts[0][3]);
    glDrawElements(GL_TRIANGLES, buffer.numindices, GL_UNSIGNED_SHORT, buffer.indices);

    /* Now buffer up the text and draw */
    buffer.numverts = 0;
    buffer.numindices = 0;

    vec3_t forward;
    CrossProduct(panel->up, panel->right, forward);
    VectorMA(base, -0.5f, forward, base);

    for (row = 0; row < panel->lines; row++) {
        const char *src = panel->text[row];
        for (col = 0; *src; col++, src++) {
            DbgPanel_BufferChar(&buffer, panel, base, col, row, (byte)(*src));
        }
    }

    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.666 * panel->alpha); // FIXME - assumes knowledge of original alphafunc

    GL_Bind(charset_texture);
    glDrawElements(GL_TRIANGLES, buffer.numindices, GL_UNSIGNED_SHORT, buffer.indices);

    glAlphaFunc(GL_GREATER, 0.666); // FIXME - assumes knowledge of original alphafunc
    glDisable(GL_ALPHA_TEST);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisable(GL_VERTEX_ARRAY);

    if (panel->alpha < 1.0f) {
        glDisable(GL_BLEND);
        glColor4f(1, 1, 1, 1);
    }
}

cvar_t _debug_models = { "_debug_models", "0", };
void
DEBUG_DrawModelInfo(const entity_t *entity, const vec3_t modelorigin)
{
    vec3_t up = { 0, 0, 1 };
    vec3_t origin, centre;
    debug_panel_t panel;

    if (entity == &cl.viewent)
        return;
    if (!entity->model)
        return;

    VectorAdd(entity->model->mins, entity->model->maxs, centre);
    VectorScale(centre, 0.5, centre);

    if (entity->model->type == mod_brush) {
        VectorCopy(centre, origin);
        VectorMA(origin, -40, vpn, origin);
    } else {
        /* Centre the panel above the entity */
        VectorCopy(modelorigin, origin);
        origin[2] += entity->model->maxs[2] + 2;
        VectorMA(origin, -16, vpn, origin);
    }

    DbgPanel_Init(&panel);
    DbgPanel_SetOrientation(&panel, origin, up, vright);

    /* DEBUGGING STUFF */
    int edictnum = -1;
    const char *type = "unknown";
#ifdef NQ_HACK
    ptrdiff_t staticnum = entity - cl_static_entities;
    ptrdiff_t dynamicnum = entity - cl_entities;
    if (staticnum > 0 && staticnum < cl.num_statics) {
        edictnum = staticnum;
        type = "static";
    } else if (dynamicnum > 0 && dynamicnum < cl.num_entities) {
        edictnum = dynamicnum;
        type = "dynamic";
    }
#endif

    DbgPanel_Printf(&panel, "edict: %d (%s), origin (%.0f, %.0f, %.0f)\n",
                    edictnum, type, entity->origin[0], entity->origin[1], entity->origin[2]);
    if (entity->model->type == mod_brush || entity->model->type == mod_alias) {
        DbgPanel_Printf(&panel, "model: %s, bb centre (%.0f, %.0f, %.0f)\n",
                        entity->model->name, centre[0], centre[1], centre[2]);
    } else {
        DbgPanel_Printf(&panel, "model: %s\n", entity->model->name);
    }
    DbgPanel_Printf(&panel, "alpha: %.3f\n", ENTALPHA_DECODE(entity->alpha));
    DbgPanel_Draw(&panel);
}

#endif // DEBUG
