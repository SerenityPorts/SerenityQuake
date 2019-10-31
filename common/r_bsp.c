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
// r_bsp.c

#include "quakedef.h"
#include "r_local.h"
#include "console.h"
#include "model.h"

#ifdef NQ_HACK
#include "host.h"
#endif

//
// current entity info
//

// modelorg is the viewpoint reletive to
// the currently rendering entity
vec3_t modelorg, base_modelorg;

vec3_t r_entorigin;	// the currently rendering entity in world coordinates

static float entity_rotation[3][3];

int r_currentbkey;

typedef enum { touchessolid, drawnode, nodrawnode } solidstate_t;

static qboolean bverts_overflow, bedges_overflow;

static mvertex_t *pfrontenter, *pfrontexit;

static qboolean makeclippededge;

//===========================================================================

/* Save unmodified lighting data into the brushmodel */
static void
SW_BrushModelLoadLighting(brushmodel_t *brushmodel, dheader_t *header)
{
    brushmodel->lightdata = Mod_LoadBytes(brushmodel, header, LUMP_LIGHTING);
}

/*
 * Provide brush model loader
 */
static int SW_BrushModelPadding() { return 0; }
static void SW_BrushModelPostProcess(brushmodel_t *brushmodel) {}
static brush_loader_t SW_BrushModelLoader = {
    .Padding = SW_BrushModelPadding,
    .LoadLighting = SW_BrushModelLoadLighting,
    .PostProcess = SW_BrushModelPostProcess,
    .lightmap_sample_bytes = 1,
};

const brush_loader_t *
R_BrushModelLoader()
{
    return &SW_BrushModelLoader;
}

/*
================
R_EntityRotate
================
*/
static void
R_EntityRotate(vec3_t vec)
{
    vec3_t tvec;

    VectorCopy(vec, tvec);
    vec[0] = DotProduct(entity_rotation[0], tvec);
    vec[1] = DotProduct(entity_rotation[1], tvec);
    vec[2] = DotProduct(entity_rotation[2], tvec);
}


/*
================
R_RotateBmodel
================
*/
void
R_RotateBmodel(const entity_t *e)
{
    float angle, s, c, temp1[3][3], temp2[3][3], temp3[3][3];

// TODO: should use a look-up table
// TODO: should really be stored with the entity instead of being reconstructed
// TODO: could cache lazily, stored in the entity
// TODO: share work with R_SetUpAliasTransform

// yaw
    angle = e->angles[YAW];
    angle = angle * M_PI * 2 / 360;
    s = sin(angle);
    c = cos(angle);

    temp1[0][0] = c;
    temp1[0][1] = s;
    temp1[0][2] = 0;
    temp1[1][0] = -s;
    temp1[1][1] = c;
    temp1[1][2] = 0;
    temp1[2][0] = 0;
    temp1[2][1] = 0;
    temp1[2][2] = 1;

// pitch
    angle = e->angles[PITCH];
    angle = angle * M_PI * 2 / 360;
    s = sin(angle);
    c = cos(angle);

    temp2[0][0] = c;
    temp2[0][1] = 0;
    temp2[0][2] = -s;
    temp2[1][0] = 0;
    temp2[1][1] = 1;
    temp2[1][2] = 0;
    temp2[2][0] = s;
    temp2[2][1] = 0;
    temp2[2][2] = c;

    R_ConcatRotations(temp2, temp1, temp3);

// roll
    angle = e->angles[ROLL];
    angle = angle * M_PI * 2 / 360;
    s = sin(angle);
    c = cos(angle);

    temp1[0][0] = 1;
    temp1[0][1] = 0;
    temp1[0][2] = 0;
    temp1[1][0] = 0;
    temp1[1][1] = c;
    temp1[1][2] = s;
    temp1[2][0] = 0;
    temp1[2][1] = -s;
    temp1[2][2] = c;

    R_ConcatRotations(temp1, temp3, entity_rotation);

//
// rotate modelorg and the transformation matrix
//
    R_EntityRotate(modelorg);
    R_EntityRotate(vpn);
    R_EntityRotate(vright);
    R_EntityRotate(vup);

    R_TransformFrustum();
}

int r_numbclipverts;
int r_numbclipedges;

// Common info to be passed down the stack for the recursive clip
typedef struct {
    const entity_t *entity;
    msurface_t *surf;
    bedge_t *edges;
    mvertex_t *verts;
} bclip_t;

/*
================
R_RecursiveClipBPoly
================
*/
static void
R_RecursiveClipBPoly(bclip_t *bclip, int numbedges, int numbverts, bedge_t *pedges, mnode_t *pnode)
{
    bedge_t *psideedges[2], *pnextedge, *ptedge;
    int i, side, lastside;
    float dist, frac, lastdist;
    mplane_t *splitplane, tplane;
    mvertex_t *pvert, *plastvert, *ptvert;
    mnode_t *pn;

    psideedges[0] = psideedges[1] = NULL;

    makeclippededge = false;

// transform the BSP plane into model space
// FIXME: cache these?
    splitplane = pnode->plane;
    tplane.dist = splitplane->dist - DotProduct(r_entorigin, splitplane->normal);
    tplane.normal[0] = DotProduct(entity_rotation[0], splitplane->normal);
    tplane.normal[1] = DotProduct(entity_rotation[1], splitplane->normal);
    tplane.normal[2] = DotProduct(entity_rotation[2], splitplane->normal);

// clip edges to BSP plane
    for (; pedges; pedges = pnextedge) {
	pnextedge = pedges->pnext;

	// set the status for the last point as the previous point
	// FIXME: cache this stuff somehow?
	plastvert = pedges->v[0];
	lastdist = DotProduct(plastvert->position, tplane.normal) - tplane.dist;
	lastside = (lastdist <= 0);

	pvert = pedges->v[1];
	dist = DotProduct(pvert->position, tplane.normal) - tplane.dist;
        side = (dist <= 0);

	if (side != lastside) {
	    // clipped
	    if (numbverts >= r_numbclipverts) {
                bverts_overflow = true;
		return;
            }

	    // generate the clipped vertex
	    frac = lastdist / (lastdist - dist);
	    ptvert = &bclip->verts[numbverts++];
	    ptvert->position[0] = plastvert->position[0] + frac * (pvert->position[0] - plastvert->position[0]);
	    ptvert->position[1] = plastvert->position[1] + frac * (pvert->position[1] - plastvert->position[1]);
	    ptvert->position[2] = plastvert->position[2] + frac * (pvert->position[2] - plastvert->position[2]);

	    // split into two edges, one on each side, and remember entering
	    // and exiting points
	    // FIXME: share the clip edge by having a winding direction flag?
	    if (numbedges > r_numbclipedges - 2) {
                bedges_overflow = true;
		return;
	    }

	    ptedge = &bclip->edges[numbedges];
	    ptedge->pnext = psideedges[lastside];
	    psideedges[lastside] = ptedge;
	    ptedge->v[0] = plastvert;
	    ptedge->v[1] = ptvert;

	    ptedge = &bclip->edges[numbedges + 1];
	    ptedge->pnext = psideedges[side];
	    psideedges[side] = ptedge;
	    ptedge->v[0] = ptvert;
	    ptedge->v[1] = pvert;

	    numbedges += 2;

	    if (side == 0) {
		// entering for front, exiting for back
		pfrontenter = ptvert;
		makeclippededge = true;
	    } else {
		pfrontexit = ptvert;
		makeclippededge = true;
	    }
	} else {
	    // add the edge to the appropriate side
	    pedges->pnext = psideedges[side];
	    psideedges[side] = pedges;
	}
    }

// if anything was clipped, reconstitute and add the edges along the clip
// plane to both sides (but in opposite directions)
    if (makeclippededge) {
	if (numbedges > r_numbclipedges - 2) {
            bedges_overflow = true;
	    return;
	}

	ptedge = &bclip->edges[numbedges];
	ptedge->pnext = psideedges[0];
	psideedges[0] = ptedge;
	ptedge->v[0] = pfrontexit;
	ptedge->v[1] = pfrontenter;

	ptedge = &bclip->edges[numbedges + 1];
	ptedge->pnext = psideedges[1];
	psideedges[1] = ptedge;
	ptedge->v[0] = pfrontenter;
	ptedge->v[1] = pfrontexit;

	numbedges += 2;
    }

    /* draw or recurse further */
    for (i = 0; i < 2; i++) {
	if (psideedges[i]) {
	    /*
	     * draw if we've reached a non-solid leaf, done if all that's left
	     * is a solid leaf, and continue down the tree if it's not a leaf
	     */
	    pn = pnode->children[i];

	    // we're done with this branch if the node or leaf isn't in the PVS
	    if (pn->visframe == r_visframecount) {
		if (pn->contents < 0) {
		    if (pn->contents != CONTENTS_SOLID) {
			r_currentbkey = ((mleaf_t *)pn)->key;
			R_RenderBmodelFace(bclip->entity, psideedges[i], bclip->surf);
		    }
		} else {
		    R_RecursiveClipBPoly(bclip, numbedges, numbverts, psideedges[i], pn);
		}
	    }
	}
    }
}


/*
================
R_DrawSolidClippedSubmodelPolygons
================
*/
void
R_DrawSolidClippedSubmodelPolygons(const entity_t *entity)
{
    const brushmodel_t *brushmodel = BrushModel(entity->model);
    const int numsurfaces = brushmodel->nummodelsurfaces;
    int i, j;
    msurface_t *surf;
    bclip_t bclip;

    bclip.entity = entity;
    bclip.edges = alloca(r_numbclipedges * sizeof(bedge_t));
    bclip.verts = alloca(r_numbclipverts * sizeof(mvertex_t));

    bverts_overflow = bedges_overflow = false;

    surf = &brushmodel->surfaces[brushmodel->firstmodelsurface];
    for (i = 0; i < numsurfaces; i++, surf++) {
	if (surf->clipflags == BMODEL_FULLY_CLIPPED)
	    continue;

	// draw the polygon
	// copy the edges to bedges, flipping if necessary so always
	// clockwise winding

	for (j = 0; j < surf->numedges; j++) {
	    const int edgenum = brushmodel->surfedges[surf->firstedge + j];
	    if (edgenum > 0) {
		const medge_t *const edge = &brushmodel->edges[edgenum];
		bclip.edges[j].v[0] = &brushmodel->vertexes[edge->v[0]];
		bclip.edges[j].v[1] = &brushmodel->vertexes[edge->v[1]];
	    } else {
		const medge_t *const edge = &brushmodel->edges[-edgenum];
		bclip.edges[j].v[0] = &brushmodel->vertexes[edge->v[1]];
		bclip.edges[j].v[1] = &brushmodel->vertexes[edge->v[0]];
	    }
	    bclip.edges[j].pnext = &bclip.edges[j + 1];
	}
	bclip.edges[j - 1].pnext = NULL;	// mark end of edges

        bclip.surf = surf;
	R_RecursiveClipBPoly(&bclip, surf->numedges, 0, bclip.edges, entity->topnode);
    }

    /*
     * If we didn't have enough space to properly clip this frame,
     * increment the limits for the next frame.
     */
    if (bedges_overflow && r_numbclipedges < MAX_STACK_BMODEL_EDGES) {
        r_numbclipedges += STACK_BMODEL_EDGES_INCREMENT;
        bedges_overflow = false;
    }
    if (bverts_overflow && r_numbclipverts < MAX_STACK_BMODEL_VERTS) {
        r_numbclipverts += STACK_BMODEL_VERTS_INCREMENT;
        bverts_overflow = false;
    }

    if (developer.value && (bedges_overflow || bverts_overflow)) {
            Con_DPrintf("Submodel %s:", entity->model->name);
            if (bedges_overflow)
                Con_DPrintf(" edges overflowed (max %d).", MAX_STACK_BMODEL_EDGES);
            if (bverts_overflow)
                Con_DPrintf(" verts overflowed (max %d).", MAX_STACK_BMODEL_VERTS);
            Con_DPrintf("\n");
        }

}


/*
================
R_DrawSubmodelPolygons
================
*/
void
R_DrawSubmodelPolygons(const entity_t *entity, int clipflags)
{
    const brushmodel_t *brushmodel = BrushModel(entity->model);
    const int numsurfaces = brushmodel->nummodelsurfaces;
    msurface_t *surf;
    int i;

// FIXME: use bounding-box-based frustum clipping info?

    surf = &brushmodel->surfaces[brushmodel->firstmodelsurface];
    for (i = 0; i < numsurfaces; i++, surf++) {
	if (surf->clipflags == BMODEL_FULLY_CLIPPED)
	    continue;

	r_currentkey = ((mleaf_t *)entity->topnode)->key;
	R_RenderFace(entity, surf, clipflags);
    }
}

/*
================
R_RecursiveWorldNode
================
*/
static void
R_RecursiveWorldNode(const entity_t *e, mnode_t *node)
{
    int count, side;
    mplane_t *plane;
    msurface_t *surf;
    mleaf_t *pleaf;
    vec_t dot;

    if (node->contents == CONTENTS_SOLID)
	return;
    if (node->visframe != r_visframecount)
	return;
    if (node->clipflags == BMODEL_FULLY_CLIPPED)
	return;

    /* if a leaf node, draw stuff */
    if (node->contents < 0) {
	pleaf = (mleaf_t *)node;
	pleaf->key = r_currentkey;
	r_currentkey++;		// all bmodels in a leaf share the same key

	return;
    }

    /*
     * The node is a decision point, so go down the apropriate sides.
     * Find which side of the node we are on.
     */
    plane = node->plane;
    switch (plane->type) {
    case PLANE_X:
	dot = modelorg[0] - plane->dist;
	break;
    case PLANE_Y:
	dot = modelorg[1] - plane->dist;
	break;
    case PLANE_Z:
	dot = modelorg[2] - plane->dist;
	break;
    default:
	dot = DotProduct(modelorg, plane->normal) - plane->dist;
	break;
    }
    side = (dot >= 0) ? 0 : 1;

    /* recurse down the children, front side first */
    R_RecursiveWorldNode(e, node->children[side]);

    /* draw stuff */
    count = node->numsurfaces;
    if (count) {
	surf = cl.worldmodel->surfaces + node->firstsurface;
	for (count = node->numsurfaces; count; count--, surf++) {
	    if (surf->visframe != r_visframecount)
		continue;
	    if (surf->clipflags == BMODEL_FULLY_CLIPPED)
		continue;
	    R_RenderFace(e, surf, surf->clipflags);
	}

	/* all surfaces on the same node share the same sequence number */
	r_currentkey++;
    }

    /* recurse down the back side */
    R_RecursiveWorldNode(e, node->children[!side]);
}



/*
================
R_RenderWorld
================
*/
void
R_RenderWorld(void)
{
    brushmodel_t *brushmodel = BrushModel(r_worldentity.model);

    VectorCopy(r_origin, modelorg);
    R_RecursiveWorldNode(&r_worldentity, brushmodel->nodes);
}
