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
// r_light.c

#include <math.h>

#include "bspfile.h"
#include "client.h"
#include "quakedef.h"

#ifdef GLQUAKE
#include "glquake.h"
#include "view.h"
#else
#include "r_local.h"
#endif

int r_dlightframecount;

/*
==================
R_AnimateLight
==================
*/
void
R_AnimateLight(void)
{
    int i, j, k;

//
// light animations
// 'm' is normal light, 'a' is no light, 'z' is double bright
    i = (int)(cl.time * 10);
    for (j = 0; j < MAX_LIGHTSTYLES; j++) {
	if (!cl_lightstyle[j].length) {
	    d_lightstylevalue[j] = 256;
	    continue;
	}
	k = i % cl_lightstyle[j].length;
	k = cl_lightstyle[j].map[k] - 'a';
	k = k * 22;
	d_lightstylevalue[j] = k;
    }
}

/* --------------------------------------------------------------------------*/
/* Dynamic Lights                                                            */
/* --------------------------------------------------------------------------*/

/*
=============
R_MarkLights
=============
*/
void
R_MarkLights(dlight_t *light, int bit, mnode_t *node)
{
    mplane_t *splitplane;
    float dist;
    msurface_t *surf;
    int i;

 start:
    if (node->contents < 0)
	return;

    splitplane = node->plane;
    dist = DotProduct(light->origin, splitplane->normal) - splitplane->dist;

    if (dist > light->radius) {
        node = node->children[0];
        goto start;
    }
    if (dist < -light->radius) {
        node = node->children[1];
        goto start;
    }

    /* mark the surfaces */
    surf = cl.worldmodel->surfaces + node->firstsurface;
    for (i = 0; i < node->numsurfaces; i++, surf++) {
	if (surf->dlightframe != r_dlightframecount) {
	    surf->dlightbits = 0;
	    surf->dlightframe = r_dlightframecount;
	}
	surf->dlightbits |= bit;
    }

    R_MarkLights(light, bit, node->children[0]);
    R_MarkLights(light, bit, node->children[1]);
}


/*
=============
R_PushDlights
=============
*/
void
R_PushDlights(void)
{
    int i;
    dlight_t *l;

    r_dlightframecount = r_framecount + 1;	// because the count hasn't
    //  advanced yet for this frame
    l = cl_dlights;

    for (i = 0; i < MAX_DLIGHTS; i++, l++) {
	if (l->die < cl.time || !l->radius)
	    continue;
	R_MarkLights(l, 1 << i, cl.worldmodel->nodes);
    }
}

/* --------------------------------------------------------------------------*/
/* Light Sampling                                                            */
/* --------------------------------------------------------------------------*/

__attribute__((noinline))
static qboolean
R_FillLightPoint(const mnode_t *node, const vec3_t surfpoint, surf_lightpoint_t *lightpoint)
{
    const msurface_t *surf;
    int i;

    /* check for impact on this node */
    surf = cl.worldmodel->surfaces + node->firstsurface;
    for (i = 0; i < node->numsurfaces; i++, surf++) {
	const mtexinfo_t *tex;
	float s, t;
        int ds, dt;

	if (surf->flags & SURF_DRAWTILED)
	    continue; /* no lightmaps */

	tex = surf->texinfo;
	s = (DotProduct(surfpoint, tex->vecs[0]) + tex->vecs[0][3] - surf->texturemins[0]) * 0.0625f;
        t = (DotProduct(surfpoint, tex->vecs[1]) + tex->vecs[1][3] - surf->texturemins[1]) * 0.0625f;

        ds = floor(s);
        dt = floor(t);
	if (ds < 0 || dt < 0)
	    continue;
        if (ds > surf->extents[0] >> 4 || dt > surf->extents[1] >> 4)
            continue;
	if (!surf->samples)
	    return false;

        /* Fill in the lightpoint details */
        lightpoint->surf = surf;
        lightpoint->s = s;
        lightpoint->t = t;

        return true;
    }

    return false;
}

static qboolean
RecursiveLightPoint(const mnode_t *node, const vec3_t start, const vec3_t end, surf_lightpoint_t *lightpoint)
{
    const mplane_t *plane;
    float front, back, frac;
    vec3_t surfpoint;
    int side;
    qboolean hit;

 restart:
    if (node->contents < 0)
	return false; /* didn't hit anything */

    /* calculate surface intersection point */
    plane = node->plane;
    switch (plane->type) {
    case PLANE_X:
    case PLANE_Y:
    case PLANE_Z:
	front = start[plane->type - PLANE_X] - plane->dist;
	back = end[plane->type - PLANE_X] - plane->dist;
	break;
    default:
	front = DotProduct(start, plane->normal) - plane->dist;
	back = DotProduct(end, plane->normal) - plane->dist;
	break;
    }
    side = front < 0;

    if ((back < 0) == side) {
	/* Completely on one side - tail recursion optimization */
	node = node->children[side];
	goto restart;
    }

    frac = front / (front - back);
    surfpoint[0] = start[0] + (end[0] - start[0]) * frac;
    surfpoint[1] = start[1] + (end[1] - start[1]) * frac;
    surfpoint[2] = start[2] + (end[2] - start[2]) * frac;

    /* go down front side */
    hit = RecursiveLightPoint(node->children[side], start, surfpoint, lightpoint);
    if (hit)
        return true;

    if ((back < 0) == side)
	return false; /* didn't hit anything */

    hit = R_FillLightPoint(node, surfpoint, lightpoint);
    if (hit)
	return true;

    /* Go down back side */
    return RecursiveLightPoint(node->children[!side], surfpoint, end, lightpoint);
}

/*
 * FIXME - check what the callers do, but I don't think this will check the
 * light value of a bmodel below the point. Models could easily be standing on
 * a func_plat or similar...
 */
qboolean
R_LightSurfPoint(const vec3_t point, surf_lightpoint_t *lightpoint)
{
    vec3_t end;

    if (!cl.worldmodel->lightdata)
	return 255;

    end[0] = point[0];
    end[1] = point[1];
    end[2] = point[2] - (8192 + 2); /* Max distance + error margin */

    return RecursiveLightPoint(cl.worldmodel->nodes, point, end, lightpoint);
}
