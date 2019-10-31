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

// Shared model functions for renderers

#include "client.h"
#include "mathlib.h"
#include "qtypes.h"
#include "render.h"

#ifdef NQ_HACK
#include "host.h"
#endif
#ifdef QW_HACK
#include "quakedef.h"
#endif

float map_wateralpha;
float map_slimealpha;
float map_lavaalpha;
float map_telealpha;

int r_surfalpha_flags;
depthchain_t r_depthchain;

#ifndef GLQUAKE
static int num_transtables;
const byte *transtable_water;
const byte *transtable_slime;
const byte *transtable_lava;
const byte *transtable_tele;

/*
 * Returns the closest matching translation table for the given alpha
 * value between zero and one.
 */
const byte *
Alpha_Transtable(float alpha)
{
    if (!alpha || alpha == 1.0f)
        return NULL;

    int tablenum = qclamp((int)floorf(alpha * num_transtables), 0, num_transtables - 1);

    return host_transtables[tablenum];
}
#endif

void
Alpha_Init()
{
#ifndef GLQUAKE
    int i;
    float alphastep;
    byte *tables;

    num_transtables = 4; // TODO: Add cvar to configure
    host_transtables = Hunk_AllocName((sizeof(byte *) + 65536) * num_transtables, "translucency");
    tables = (byte *)&host_transtables[num_transtables];

    alphastep = 1.0f / (num_transtables + 1);
    for (i = 0; i < num_transtables; i++) {
        host_transtables[i] = &tables[i * 65536];
        QPal_CreateTranslucencyTable(host_transtables[i], host_basepal, alphastep * (i + 1));
    }
#endif
    r_surfalpha_flags = 0;
}

static void
Alpha_Updated()
{
    r_surfalpha_flags = 0;
    if (map_wateralpha < 1.0f)
        r_surfalpha_flags |= SURF_DRAWWATER;
    if (map_slimealpha < 1.0f)
        r_surfalpha_flags |= SURF_DRAWSLIME;
    if (map_lavaalpha < 1.0f)
        r_surfalpha_flags |= SURF_DRAWLAVA;
    if (map_telealpha < 1.0f)
        r_surfalpha_flags |= SURF_DRAWTELE;

#ifndef GLQUAKE
    transtable_water = Alpha_Transtable(map_wateralpha);
    transtable_slime = Alpha_Transtable(map_slimealpha);
    transtable_lava  = Alpha_Transtable(map_lavaalpha);
    transtable_tele  = Alpha_Transtable(map_telealpha);
#endif
}

static void R_WaterAlpha_f(cvar_t *cvar) { map_wateralpha = qclamp(cvar->value, 0.0f, 1.0f); Alpha_Updated(); }
static void R_SlimeAlpha_f(cvar_t *cvar) { map_slimealpha = qclamp(cvar->value, 0.0f, 1.0f); Alpha_Updated(); }
static void R_LavaAlpha_f(cvar_t *cvar)  { map_lavaalpha  = qclamp(cvar->value, 0.0f, 1.0f); Alpha_Updated(); }
static void R_TeleAlpha_f(cvar_t *cvar)  { map_telealpha  = qclamp(cvar->value, 0.0f, 1.0f); Alpha_Updated(); }

cvar_t r_wateralpha = { "r_wateralpha", "1", true, .callback = R_WaterAlpha_f };
cvar_t r_slimealpha = { "r_slimealpha", "1", true, .callback = R_SlimeAlpha_f };
cvar_t r_lavaalpha  = { "r_lavaalpha",  "1", true, .callback = R_LavaAlpha_f  };
cvar_t r_telealpha  = { "r_telealpha",  "1", true, .callback = R_TeleAlpha_f  };

void
Alpha_NewMap()
{
    char buffer[16];
    char *value;

    map_wateralpha = r_wateralpha.value;
    map_slimealpha = r_slimealpha.value;
    map_lavaalpha = r_lavaalpha.value;
    map_telealpha = r_telealpha.value;

    value = Entity_ValueForKey(cl.worldmodel->entities, "wateralpha", buffer, sizeof(buffer));
    map_wateralpha = value ? atof(value) : r_wateralpha.value;

    value = Entity_ValueForKey(cl.worldmodel->entities, "slimealpha", buffer, sizeof(buffer));
    map_slimealpha = value ? atof(value) : r_slimealpha.value;

    value = Entity_ValueForKey(cl.worldmodel->entities, "lavaalpha", buffer, sizeof(buffer));
    map_lavaalpha = value ? atof(value) : r_lavaalpha.value;

    value = Entity_ValueForKey(cl.worldmodel->entities, "telealpha", buffer, sizeof(buffer));
    map_telealpha = value ? atof(value) : r_telealpha.value;

    Alpha_Updated();
}

/*
 * Find the correct interval based on time
 * Used for Alias model frame/sprite/skin group animations
 */
int
Mod_FindInterval(const float *intervals, int numintervals, float time)
{
    int i;
    float fullinterval, targettime;

    /*
     * when loading models/skins/sprites, we guaranteed all interval values
     * are positive, so we don't have to worry about division by 0
     */
    fullinterval = intervals[numintervals - 1];
    targettime = time - (int)(time / fullinterval) * fullinterval;
    for (i = 0; i < numintervals - 1; i++)
	if (intervals[i] > targettime)
	    break;

    return i;
}

/* Player colour translation table */
static byte translation_table[256];

void
R_InitTranslationTable()
{
    int i;

    for (i = 0; i < 256; i++)
        translation_table[i] = i;
}

const byte *
R_GetTranslationTable(int topcolor, int bottomcolor)
{
    int i, top, bottom;

    top = qclamp(topcolor, 0, 13) * 16;
    bottom = qclamp(bottomcolor, 0, 13) * 16;
    for (i = 0; i < 16; i++) {
        translation_table[TOP_RANGE + i] = (top < 128) ? top + i : top + 15 - i;
        translation_table[BOTTOM_RANGE + i] = (bottom < 128) ? bottom + i : bottom + 15 -i;
    }

    return translation_table;
}

float
R_GetSurfAlpha(int flags)
{
    float alpha;

    if (!(flags & (SURF_DRAWWATER | SURF_DRAWSLIME | SURF_DRAWLAVA | SURF_DRAWTELE)))
        alpha = 1.0f;
    else if (flags & SURF_DRAWWATER)
        alpha = map_wateralpha;
    else if (flags & SURF_DRAWSLIME)
        alpha = map_slimealpha;
    else if (flags & SURF_DRAWLAVA)
        alpha = map_lavaalpha;
    else //if (flags & SURF_DRAWTELE)
        alpha = map_telealpha;

    return alpha;
}

//
// Depth sorting helpers
//
static void
DepthChain_Insert(depthchain_t *head, depthchain_t *entry)
{
    head = head->next;
    while (entry->key < head->key)
        head = head->next;

    entry->next = head;
    entry->prev = head->prev;
    head->prev->next = entry;
    head->prev = entry;
}

void
DepthChain_Init(depthchain_t *head)
{
    head->key = INT_MIN;
    head->type = depthchain_head;
    head->entity = NULL;
    head->next = head->prev = head;
}

#ifdef GLQUAKE
void
DepthChain_AddSurf(depthchain_t *head, entity_t *entity, msurface_t *surf, depthchain_type_t type)
{
    float alpha;
    vec3_t vec;
    int i, key, vkey;
    const float *vertex;
    brushmodel_t *brushmodel = BrushModel(entity->model);

    // This is probably terribly inefficient, but for proper depth
    // sorting we really need to know the point at which the surf is
    // deepest into the scene to use as the sort key.
    key = INT_MIN;
    for (i = 0; i < surf->numedges; i++) {
	const int edgenum = brushmodel->surfedges[surf->firstedge + i];
	if (edgenum >= 0) {
	    const medge_t *const edge = &brushmodel->edges[edgenum];
	    vertex = brushmodel->vertexes[edge->v[0]].position;
	} else {
	    const medge_t *const edge = &brushmodel->edges[-edgenum];
	    vertex = brushmodel->vertexes[edge->v[1]].position;
	}
        VectorSubtract(vertex, r_refdef.vieworg, vec);
        vkey = DotProduct(vec, vec); // depth value squared
        if (vkey > key)
            key = vkey;
    }

    surf->depthchain.key = key;
    surf->depthchain.type = type;
    surf->depthchain.entity = entity;
    if (surf->flags & r_surfalpha_flags) {
        alpha = ENTALPHA_DECODE(entity->alpha) * R_GetSurfAlpha(surf->flags);
        surf->depthchain.alpha = ENTALPHA_ENCODE(alpha);
    } else {
        surf->depthchain.alpha = entity->alpha;
    }
    DepthChain_Insert(head, &surf->depthchain);
}
#endif

void
DepthChain_AddEntity(depthchain_t *head, entity_t *entity, depthchain_type_t type)
{
    vec3_t vec;

    VectorAdd(entity->model->mins, entity->model->maxs, vec);
    VectorScale(vec, 0.5f, vec);
    VectorAdd(entity->origin, vec, vec);            // centre of the bounding box
    VectorSubtract(vec, r_refdef.vieworg, vec);     // dist vector from view origin
    entity->depthchain.key = DotProduct(vec, vec);  // depth value squared
    entity->depthchain.type = type;
    entity->depthchain.entity = entity;
    DepthChain_Insert(head, &entity->depthchain);
}
