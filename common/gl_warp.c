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
// gl_warp.c -- sky and water polygons

#include <float.h>

#include "console.h"
#include "glquake.h"
#include "model.h"
#include "qpic.h"
#include "quakedef.h"
#include "sys.h"

#ifdef NQ_HACK
#include "host.h"
#endif

static void
BoundPoly(int numverts, float *verts, vec3_t mins, vec3_t maxs)
{
    int i, j;
    float *v;

    mins[0] = mins[1] = mins[2] = FLT_MAX;
    maxs[0] = maxs[1] = maxs[2] = -FLT_MAX;
    v = verts;
    for (i = 0; i < numverts; i++)
	for (j = 0; j < 3; j++, v++) {
	    if (*v < mins[j])
		mins[j] = *v;
	    if (*v > maxs[j])
		maxs[j] = *v;
	}
}

static void
SubdividePolygon(msurface_t *surf, int numverts, vec_t *verts,
		 const char *hunkname)
{
    int i, j, k, memsize;
    vec3_t mins, maxs;
    float m;
    float *v;
    vec3_t front[64], back[64];
    int front_count, back_count;
    float dist[64];
    float frac;
    glpoly_t *poly;
    float s, t;

    if (numverts > 60)
	Sys_Error("numverts = %i", numverts);

    BoundPoly(numverts, verts, mins, maxs);

    for (i = 0; i < 3; i++) {
	m = (mins[i] + maxs[i]) * 0.5;
	m = floorf(m / gl_subdivide_size.value + 0.5);
	m *= gl_subdivide_size.value;
	if (maxs[i] - m < 8)
	    continue;
	if (m - mins[i] < 8)
	    continue;

	// cut it
	v = verts + i;
	for (j = 0; j < numverts; j++, v += 3)
	    dist[j] = *v - m;

	// wrap cases
	dist[j] = dist[0];
	v -= i;
	VectorCopy(verts, v);

	front_count = back_count = 0;
	v = verts;
	for (j = 0; j < numverts; j++, v += 3) {
	    if (dist[j] >= 0) {
		VectorCopy(v, front[front_count]);
		front_count++;
	    }
	    if (dist[j] <= 0) {
		VectorCopy(v, back[back_count]);
		back_count++;
	    }
	    if (dist[j] == 0 || dist[j + 1] == 0)
		continue;
	    if ((dist[j] > 0) != (dist[j + 1] > 0)) {
		// clip point
		frac = dist[j] / (dist[j] - dist[j + 1]);
		for (k = 0; k < 3; k++)
		    front[front_count][k] = back[back_count][k] =
			v[k] + frac * (v[3 + k] - v[k]);
		front_count++;
		back_count++;
	    }
	}

	SubdividePolygon(surf, front_count, front[0], hunkname);
	SubdividePolygon(surf, back_count, back[0], hunkname);
	return;
    }

    memsize = sizeof(*poly) + numverts * sizeof(poly->verts[0]);
    poly = Hunk_AllocName(memsize, hunkname);
    poly->next = surf->polys;
    surf->polys = poly;
    poly->numverts = numverts;
    for (i = 0; i < numverts; i++, verts += 3) {
	VectorCopy(verts, poly->verts[i]);
	s = DotProduct(verts, surf->texinfo->vecs[0]);
	t = DotProduct(verts, surf->texinfo->vecs[1]);
	poly->verts[i][3] = s;
	poly->verts[i][4] = t;
    }
}

/*
================
GL_SubdivideSurface

Breaks a polygon up along axial 64 unit
boundaries so that turbulent and sky warps
can be done reasonably.
================
*/
void
GL_SubdivideSurface(brushmodel_t *brushmodel, msurface_t *surf)
{
    const model_t *model = &brushmodel->model;
    char hunkname[HUNK_NAMELEN + 1];
    vec3_t verts[64]; /* FIXME!!! */
    int i, edge, numverts;
    vec_t *vert;

    COM_FileBase(model->name, hunkname, sizeof(hunkname));

    //
    // convert edges back to a normal polygon
    //
    numverts = 0;
    for (i = 0; i < surf->numedges; i++) {
	edge = brushmodel->surfedges[surf->firstedge + i];
	if (edge > 0)
	    vert = brushmodel->vertexes[brushmodel->edges[edge].v[0]].position;
	else
	    vert = brushmodel->vertexes[brushmodel->edges[-edge].v[1]].position;
	VectorCopy(vert, verts[numverts]);
	numverts++;
    }
    SubdividePolygon(surf, numverts, verts[0], hunkname);
}

//=========================================================

