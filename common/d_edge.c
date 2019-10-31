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

#include <float.h>
#include <stdint.h>

#include "d_local.h"
#include "quakedef.h"
#include "r_local.h"

#ifdef NQ_HACK
#include "client.h"
#endif

static int miplevel;
static vec3_t transformed_modelorg;

float scale_for_mip;
int screenwidth;
int ubasestep, errorterm, erroradjustup, erroradjustdown;

/*
=============
D_MipLevelForScale
=============
*/
static int
D_MipLevelForScale(float scale)
{
    int lmiplevel;

    if (scale >= d_scalemip[0])
	lmiplevel = 0;
    else if (scale >= d_scalemip[1])
	lmiplevel = 1;
    else if (scale >= d_scalemip[2])
	lmiplevel = 2;
    else
	lmiplevel = 3;

    if (lmiplevel < d_minmip)
	lmiplevel = d_minmip;

    return lmiplevel;
}


/*
==============
D_DrawSolidSurface
==============
*/

// FIXME: clean this up

static void
D_DrawSolidSurface(surf_t *surf, int color)
{
    espan_t *span;
    byte *pdest;
    int u, u2, pix;

    pix = (color << 24) | (color << 16) | (color << 8) | color;
    for (span = surf->spans; span; span = span->pnext) {
	pdest = (byte *)d_viewbuffer + screenwidth * span->v;
	u = span->u;
	u2 = span->u + span->count - 1;
	((byte *)pdest)[u] = pix;

	if (u2 - u < 8) {
	    for (u++; u <= u2; u++)
		((byte *)pdest)[u] = pix;
	} else {
	    for (u++; u & 3; u++)
		((byte *)pdest)[u] = pix;

	    u2 -= 4;
	    for (; u <= u2; u += 4)
		*(int *)((byte *)pdest + u) = pix;
	    u2 += 4;
	    for (; u <= u2; u++)
		((byte *)pdest)[u] = pix;
	}
    }
}


/*
==============
D_CalcGradients
==============
*/
static void
D_CalcGradients(msurface_t *pface)
{
    float mipscale;
    vec3_t p_temp1;
    vec3_t p_saxis, p_taxis;
    float t;

    mipscale = 1.0 / (float)(1 << miplevel);

    TransformVector(pface->texinfo->vecs[0], p_saxis);
    TransformVector(pface->texinfo->vecs[1], p_taxis);

    t = xscaleinv * mipscale;
    d_sdivzstepu = p_saxis[0] * t;
    d_tdivzstepu = p_taxis[0] * t;

    t = yscaleinv * mipscale;
    d_sdivzstepv = -p_saxis[1] * t;
    d_tdivzstepv = -p_taxis[1] * t;

    d_sdivzorigin = p_saxis[2] * mipscale - xcenter * d_sdivzstepu -
	ycenter * d_sdivzstepv;
    d_tdivzorigin = p_taxis[2] * mipscale - xcenter * d_tdivzstepu -
	ycenter * d_tdivzstepv;

    VectorScale(transformed_modelorg, mipscale, p_temp1);

    t = 0x10000 * mipscale;
    sadjust = ((fixed16_t)(DotProduct(p_temp1, p_saxis) * 0x10000 + 0.5)) -
	((pface->texturemins[0] << 16) >> miplevel)
	+ pface->texinfo->vecs[0][3] * t;
    tadjust = ((fixed16_t)(DotProduct(p_temp1, p_taxis) * 0x10000 + 0.5)) -
	((pface->texturemins[1] << 16) >> miplevel)
	+ pface->texinfo->vecs[1][3] * t;

//
// -1 (-epsilon) so we never wander off the edge of the texture
//
    bbextents = ((pface->extents[0] << 16) >> miplevel) - 1;
    bbextentt = ((pface->extents[1] << 16) >> miplevel) - 1;
}

void
D_DrawSurface(surf_t *surf, const entity_t *entity, vec3_t world_transformed_modelorg)
{
    msurface_t *pface;
    surfcache_t *pcurrentcache;
    vec3_t local_modelorg;

    d_zistepu = surf->d_zistepu;
    d_zistepv = surf->d_zistepv;
    d_ziorigin = surf->d_ziorigin;

    if (surf->flags & SURF_DRAWSKY) {
        if (!r_skymade) {
            R_MakeSky();
        }

        D_DrawSkyScans8(surf->spans);
        D_DrawZSpans(surf->spans);
    } else if (surf->flags & SURF_DRAWBACKGROUND) {
        // set up a gradient for the background surface that places it
        // effectively at infinity distance from the viewpoint
        d_zistepu = 0;
        d_zistepv = 0;
        d_ziorigin = -0.9;

        D_DrawSolidSurface(surf, (int)r_clearcolor.value & 0xFF);
        D_DrawZSpans(surf->spans);
    } else if (surf->flags & SURF_DRAWTURB) {

        // Set the translucency table to trigger alpha blending
        if ((surf->flags & r_surfalpha_flags) && !surf->alphatable)
            return; // Completely transparent
        r_transtable = surf->alphatable;

        pface = surf->data;
        miplevel = 0;
        cacheblock = (pixel_t *)((byte *)pface->texinfo->texture + pface->texinfo->texture->offsets[0]);
        cachewidth = pface->texinfo->texture->width;
        cacheheight = pface->texinfo->texture->height;

        /* Set the appropriate span drawing function */
        if (cachewidth == 64 && cacheheight == 64) {
            if (r_transtable)
                D_DrawTurbSpanFunc = D_DrawTurbulentTranslucent8Span;
            else
                D_DrawTurbSpanFunc = D_DrawTurbulent8Span;
        } else {
            if (r_transtable)
                D_DrawTurbSpanFunc = D_DrawTurbulentTranslucent8Span_NonStd;
            else
                D_DrawTurbSpanFunc = D_DrawTurbulent8Span_NonStd;
        }

            if (surf->insubmodel) {
            // FIXME: we don't want to do all this for every polygon!
            // TODO: store once at start of frame
            VectorSubtract(r_origin, entity->origin, local_modelorg);
            TransformVector(local_modelorg, transformed_modelorg);

            R_RotateBmodel(entity); // FIXME: don't mess with the frustum
        }

        D_CalcGradients(pface);
        Turbulent8(surf->spans);

        // Only need to fill Z for opaque surfaces
        if (!r_transtable)
            D_DrawZSpans(surf->spans);

        if (surf->insubmodel) {
            //
            // restore the old drawing state
            // FIXME: we don't want to do this every time!
            // TODO: speed up
            //
            VectorCopy(world_transformed_modelorg, transformed_modelorg);
            VectorCopy(base_vpn, vpn);
            VectorCopy(base_vup, vup);
            VectorCopy(base_vright, vright);
            VectorCopy(base_modelorg, modelorg);
            R_TransformFrustum();
        }
    } else if (surf->flags & SURF_DRAWFENCE) {
        if (surf->insubmodel) {
            // FIXME: we don't want to do all this for every polygon!
            // TODO: store once at start of frame
            VectorSubtract(r_origin, entity->origin, local_modelorg);
            TransformVector(local_modelorg, transformed_modelorg);

            R_RotateBmodel(entity); // FIXME: don't mess with the frustum
        }

        r_transtable = surf->alphatable;
        pface = surf->data;
        miplevel = D_MipLevelForScale(surf->nearzi * scale_for_mip * pface->texinfo->mipadjust);

        // FIXME: make this passed in to D_CacheSurface
        pcurrentcache = D_CacheSurface(entity, pface, miplevel);

        cacheblock = (pixel_t *)pcurrentcache->data;
        cachewidth = pcurrentcache->width;

        D_CalcGradients(pface);
        if (r_transtable) {
            D_DrawSpans8_Fence_Translucent(surf->spans);
        } else {
            D_DrawSpans8_Fence(surf->spans);
        }

        if (surf->insubmodel) {
            //
            // restore the old drawing state
            // FIXME: we don't want to do this every time!
            // TODO: speed up
            //
            VectorCopy(world_transformed_modelorg, transformed_modelorg);
            VectorCopy(base_vpn, vpn);
            VectorCopy(base_vup, vup);
            VectorCopy(base_vright, vright);
            VectorCopy(base_modelorg, modelorg);
            R_TransformFrustum();
        }
    } else {
        if (surf->insubmodel) {
            // FIXME: we don't want to do all this for every polygon!
            // TODO: store once at start of frame
            VectorSubtract(r_origin, entity->origin, local_modelorg);
            TransformVector(local_modelorg, transformed_modelorg);

            R_RotateBmodel(entity); // FIXME: don't mess with the frustum
        }

        pface = surf->data;
        miplevel = D_MipLevelForScale(surf->nearzi * scale_for_mip * pface->texinfo->mipadjust);

        // FIXME: make this passed in to D_CacheSurface
        pcurrentcache = D_CacheSurface(entity, pface, miplevel);

        cacheblock = (pixel_t *)pcurrentcache->data;
        cachewidth = pcurrentcache->width;

        D_CalcGradients(pface);

        r_transtable = surf->alphatable;
        if (r_transtable) {
            D_DrawSpans8_Translucent(surf->spans);
        } else {
            D_DrawSpans(surf->spans);
            D_DrawZSpans(surf->spans);
        }

        if (surf->insubmodel) {
            //
            // restore the old drawing state
            // FIXME: we don't want to do this every time!
            // TODO: speed up
            //
            VectorCopy(world_transformed_modelorg, transformed_modelorg);
            VectorCopy(base_vpn, vpn);
            VectorCopy(base_vup, vup);
            VectorCopy(base_vright, vright);
            VectorCopy(base_modelorg, modelorg);
            R_TransformFrustum();
        }
    }

    r_drawnpolycount++;
}


/*
==============
D_DrawSurfaces
==============
*/
void
D_DrawSurfaces(qboolean sort_submodels)
{
    const entity_t *entity;
    surf_t *surf;
    vec3_t world_transformed_modelorg;

    TransformVector(modelorg, transformed_modelorg);
    VectorCopy(transformed_modelorg, world_transformed_modelorg);

// TODO: could preset a lot of this at mode set time
    if (r_drawflat.value) {
	for (surf = &surfaces[1]; surf < surface_p; surf++) {
	    if (!surf->spans)
		continue;

	    d_zistepu = surf->d_zistepu;
	    d_zistepv = surf->d_zistepv;
	    d_ziorigin = surf->d_ziorigin;

	    D_DrawSolidSurface(surf, (intptr_t)surf->data & 0xFF);
	    D_DrawZSpans(surf->spans);
	}
    } else if (sort_submodels && bmodel_surfaces < surface_p) {
        surf_t bsurfs;
        surf_t *surf2;

        /* Sort bmodel surfaces by depth */
        bsurfs.nearzi = FLT_MAX;
        bsurfs.next = bsurfs.prev = &bsurfs;
        for (surf = bmodel_surfaces; surf < surface_p; surf++) {
            if (!surf->spans)
                continue;
            surf2 = bsurfs.next;
            while (surf->nearzi > surf2->nearzi)
                surf2 = surf2->next;
            surf->next = surf2;
            surf2->prev->next = surf;
            surf->prev = surf2->prev;
            surf2->prev = surf;
        }

        /* Draw the world back to front, inserting bmodel surfs at the correct depth */
        bsurfs.nearzi = -FLT_MAX;
        surf2 = bsurfs.next;
	for (surf = bmodel_surfaces - 1; surf >= &surfaces[1]; surf--) {
            while (surf2->nearzi > surf->nearzi) {
                D_DrawSurface(surf2, surf2->entity, world_transformed_modelorg);
                surf2 = surf2->next;
            }
            if (!surf->spans)
		continue;
            D_DrawSurface(surf, &r_worldentity, world_transformed_modelorg);
        }
    } else {
        /* Draw the world, back to front */
	for (surf = surface_p - 1; surf >= &surfaces[1]; surf--) {
	    if (!surf->spans)
		continue;
            entity = surf->insubmodel ? surf->entity : &r_worldentity;
            D_DrawSurface(surf, entity, world_transformed_modelorg);
        }
    }
}
