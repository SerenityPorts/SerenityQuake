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
// r_edge.c

#include "quakedef.h"
#include "r_local.h"
#include "render.h"
#include "sound.h"

int r_numedges;
int r_numsurfaces;

edge_t *auxedges;
edge_t *r_edges, *edge_p, *edge_max;

surf_t *auxsurfaces;
surf_t *surfaces, *surface_p, *surf_max, *bmodel_surfaces;

// surfaces are generated in back to front order by the bsp, so if a surf
// pointer is greater than another one, it should be drawn in front
// surfaces[1] is the background, and is used as the active surface stack

edge_t *newedges[MAXHEIGHT];
edge_t *removeedges[MAXHEIGHT];

espan_t *span_p;
static espan_t *max_span_p;

int r_currentkey;

int current_iv;

int edge_head_u_shift20, edge_tail_u_shift20;

static int (*GenerateSpans)();
static void (*GenerateSpansWithFlag)(int);

edge_t edge_head;
edge_t edge_tail;
edge_t edge_aftertail;
edge_t edge_sentinel;

float fv;

int R_GenerateSpans();
static int R_GenerateSpansBackward();
static void R_GenerateSpansWithFlag(int flags);

//=============================================================================

/*
 * When scanning edges, if surfaces with these flags are encountered
 * do not emit a span and instead return the corresponding surface
 * flag.
 */
#ifdef USE_X86_ASM
int r_translucent_flags;
#else
static int r_translucent_flags;
#endif

/*
==============
R_BeginEdgeFrame
==============
*/
void
R_BeginEdgeFrame(void)
{
    int v;

    edge_p = r_edges;
    edge_max = &r_edges[r_numedges];

    surface_p = &surfaces[2];	// background is surface 1, surface 0 is a dummy
    surfaces[1].spans = NULL;	// no background spans yet
    surfaces[1].flags = SURF_DRAWBACKGROUND;

// put the background behind everything in the world
    if (r_draworder.value) {
	GenerateSpans = R_GenerateSpansBackward;
        GenerateSpansWithFlag = R_GenerateSpansWithFlag;
	surfaces[1].key = 0;
	r_currentkey = 1;
    } else {
	GenerateSpans = R_GenerateSpans;
        GenerateSpansWithFlag = R_GenerateSpansWithFlag;
	surfaces[1].key = 0x7FFFFFFF;
	r_currentkey = 0;
    }

    v = r_refdef.vrect.y;
    memset(&newedges[v], 0, (r_refdef.vrectbottom - v) * sizeof(newedges[0]));
    memset(&removeedges[v], 0, (r_refdef.vrectbottom - v) * sizeof(removeedges[0]));

    /* Setup scan flags for translucent surfaces */
    r_translucent_flags = SURF_DRAWENTALPHA | SURF_DRAWFENCE | r_surfalpha_flags;
}


#ifndef USE_X86_ASM

/*
==============
R_InsertNewEdges

Adds the edges in the linked list edgestoadd, adding them to the edges in the
linked list edgelist.  edgestoadd is assumed to be sorted on u, and non-empty
(this is actually newedges[v]).  edgelist is assumed to be sorted on u, with a
sentinel at the end (actually, this is the active edge table starting at
edge_head.next).
==============
*/
static void
R_InsertNewEdges(edge_t *edgestoadd, edge_t *edgelist)
{
    edge_t *next_edge;

    do {
	next_edge = edgestoadd->next;
      edgesearch:
	if (edgelist->u >= edgestoadd->u)
	    goto addedge;
	edgelist = edgelist->next;
	if (edgelist->u >= edgestoadd->u)
	    goto addedge;
	edgelist = edgelist->next;
	if (edgelist->u >= edgestoadd->u)
	    goto addedge;
	edgelist = edgelist->next;
	if (edgelist->u >= edgestoadd->u)
	    goto addedge;
	edgelist = edgelist->next;
	goto edgesearch;

	// insert edgestoadd before edgelist
      addedge:
	edgestoadd->next = edgelist;
	edgestoadd->prev = edgelist->prev;
	edgelist->prev->next = edgestoadd;
	edgelist->prev = edgestoadd;
    } while ((edgestoadd = next_edge) != NULL);
}

#endif /* USE_X86_ASM */


#ifndef USE_X86_ASM

/*
==============
R_RemoveEdges
==============
*/
static void
R_RemoveEdges(edge_t *pedge)
{

    do {
	pedge->next->prev = pedge->prev;
	pedge->prev->next = pedge->next;
    } while ((pedge = pedge->nextremove) != NULL);
}

#endif /* USE_X86_ASM */


#ifndef USE_X86_ASM

/*
==============
R_StepActiveU
==============
*/
static void
R_StepActiveU(edge_t *pedge)
{
    edge_t *pnext_edge, *pwedge;

    while (1) {
      nextedge:
	pedge->u += pedge->u_step;
	if (pedge->u < pedge->prev->u)
	    goto pushback;
	pedge = pedge->next;

	pedge->u += pedge->u_step;
	if (pedge->u < pedge->prev->u)
	    goto pushback;
	pedge = pedge->next;

	pedge->u += pedge->u_step;
	if (pedge->u < pedge->prev->u)
	    goto pushback;
	pedge = pedge->next;

	pedge->u += pedge->u_step;
	if (pedge->u < pedge->prev->u)
	    goto pushback;
	pedge = pedge->next;

	goto nextedge;

      pushback:
	if (pedge == &edge_aftertail)
	    return;

	// push it back to keep it sorted
	pnext_edge = pedge->next;

	// pull the edge out of the edge list
	pedge->next->prev = pedge->prev;
	pedge->prev->next = pedge->next;

	// find out where the edge goes in the edge list
	pwedge = pedge->prev->prev;

	while (pwedge->u > pedge->u) {
	    pwedge = pwedge->prev;
	}

	// put the edge back into the edge list
	pedge->next = pwedge->next;
	pedge->prev = pwedge;
	pedge->next->prev = pedge;
	pwedge->next = pedge;

	pedge = pnext_edge;
	if (pedge == &edge_tail)
	    return;
    }
}

#endif /* USE_X86_ASM */

/*
==============
R_CleanupSpan
==============
*/
static void
R_CleanupSpan()
{
    surf_t *surf;
    int iu;
    espan_t *span;

// now that we've reached the right edge of the screen, we're done with any
// unfinished surfaces, so emit a span for whatever's on top
    surf = surfaces[1].next;
    iu = edge_tail_u_shift20;
    if (iu > surf->last_u && !(surf->flags & r_translucent_flags)) {
	span = span_p++;
	span->u = surf->last_u;
	span->count = iu - span->u;
	span->v = current_iv;
	span->pnext = surf->spans;
	surf->spans = span;
    }
// reset spanstate for all surfaces in the surface stack
    do {
	surf->spanstate = 0;
	surf = surf->next;
    } while (surf != &surfaces[1]);
}


/*
==============
R_LeadingEdgeBackwards
==============
*/
static int
R_LeadingEdgeBackwards(surf_t *surf, edge_t *edge)
{
    espan_t *span;
    surf_t *surf2;
    int iu;

// it's adding a new surface in, so find the correct place
    if (surf->flags & r_translucent_flags)
        return surf->flags & r_translucent_flags;

// don't start a span if this is an inverted span, with the end
// edge preceding the start edge (that is, we've already seen the
// end edge)
    if (++surf->spanstate != 1)
	return 0;

    surf2 = surfaces[1].next;

    if (surf->key > surf2->key)
	goto newtop;

    // if it's two surfaces on the same plane, the one that's already
    // active is in front, so keep going unless it's a bmodel
    if (surf->insubmodel && (surf->key == surf2->key)) {
	// must be two bmodels in the same leaf; don't care, because they'll
	// never be farthest anyway
	goto newtop;
    }

 continue_search:

    do {
	surf2 = surf2->next;
    } while (surf->key < surf2->key);

    if (surf->key == surf2->key) {
	// if it's two surfaces on the same plane, the one that's already
	// active is in front, so keep going unless it's a bmodel
	if (!surf->insubmodel)
	    goto continue_search;

	// must be two bmodels in the same leaf; don't care which is really
	// in front, because they'll never be farthest anyway
    }

    goto gotposition;

 newtop:
    // emit a span (obscures current top)
    iu = edge->u >> 20;

    if (iu > surf2->last_u) {
	span = span_p++;
	span->u = surf2->last_u;
	span->count = iu - span->u;
	span->v = current_iv;
	span->pnext = surf2->spans;
	surf2->spans = span;
    }
    // set last_u on the new span
    surf->last_u = iu;

 gotposition:
    // insert before surf2
    surf->next = surf2;
    surf->prev = surf2->prev;
    surf2->prev->next = surf;
    surf2->prev = surf;

    return 0;
}


/*
==============
R_TrailingEdge
==============
*/
static int
R_TrailingEdge(surf_t *surf, edge_t *edge)
{
    espan_t *span;
    int iu;

    if (surf->flags & r_translucent_flags)
        return surf->flags & r_translucent_flags;

// don't generate a span if this is an inverted span, with the end
// edge preceding the start edge (that is, we haven't seen the
// start edge yet)
    if (--surf->spanstate != 0)
	return 0;

    if (surf->insubmodel)
	r_bmodelactive--;

    if (surf == surfaces[1].next) {
	// emit a span (current top going away)
	iu = edge->u >> 20;
	if (iu > surf->last_u) {
	    span = span_p++;
	    span->u = surf->last_u;
	    span->count = iu - span->u;
	    span->v = current_iv;
	    span->pnext = surf->spans;
	    surf->spans = span;
	}
	// set last_u on the surface below
	surf->next->last_u = iu;
    }
    surf->prev->next = surf->next;
    surf->next->prev = surf->prev;

    return 0;
}


#ifndef USE_X86_ASM

/*
==============
R_LeadingEdge
==============
*/
static int
R_LeadingEdge(surf_t *surf, edge_t *edge)
{
    espan_t *span;
    surf_t *surf2;
    int iu;
    double fu, newzi, testzi, newzitop, newzibottom;

    // it's adding a new surface in, so find the correct place
    if (surf->flags & r_translucent_flags)
        return surf->flags & r_translucent_flags;

    // don't start a span if this is an inverted span, with the end
    // edge preceding the start edge (that is, we've already seen the
    // end edge)
    if (++surf->spanstate != 1)
	return 0;

    if (surf->insubmodel)
	r_bmodelactive++;

    surf2 = surfaces[1].next;

    if (surf->key < surf2->key)
	goto newtop;

    // if it's two surfaces on the same plane, the one that's already
    // active is in front, so keep going unless it's a bmodel
    if (surf->insubmodel && (surf->key == surf2->key)) {
	// must be two bmodels in the same leaf; sort on 1/z
	fu = (float)(edge->u - 0xFFFFF) * (1.0 / 0x100000);
	newzi = surf->d_ziorigin + fv * surf->d_zistepv + fu * surf->d_zistepu;
	newzibottom = newzi * 0.99;

	testzi = surf2->d_ziorigin + fv * surf2->d_zistepv + fu * surf2->d_zistepu;

	if (newzibottom >= testzi)
	    goto newtop;

	newzitop = newzi * 1.01;
	if (newzitop >= testzi && surf->d_zistepu >= surf2->d_zistepu)
	    goto newtop;
    }

 continue_search:

    do {
	surf2 = surf2->next;
    } while (surf->key > surf2->key);

    if (surf->key != surf2->key)
	goto gotposition;

    // if it's two surfaces on the same plane, the one that's already
    // active is in front, so keep going unless it's a bmodel
    if (!surf->insubmodel)
	goto continue_search;

    // must be two bmodels in the same leaf; sort on 1/z
    fu = (float)(edge->u - 0xFFFFF) * (1.0 / 0x100000);
    newzi = surf->d_ziorigin + fv * surf->d_zistepv + fu * surf->d_zistepu;
    newzibottom = newzi * 0.99;

    testzi = surf2->d_ziorigin + fv * surf2->d_zistepv + fu * surf2->d_zistepu;
    if (newzibottom >= testzi)
	goto gotposition;

    newzitop = newzi * 1.01;
    if (newzitop >= testzi && surf->d_zistepu >= surf2->d_zistepu)
	goto gotposition;

    goto continue_search;

 newtop:
    // emit a span (obscures current top)
    iu = edge->u >> 20;

    if (iu > surf2->last_u && !(surf2->flags & r_translucent_flags)) {
	span = span_p++;
	span->u = surf2->last_u;
	span->count = iu - span->u;
	span->v = current_iv;
	span->pnext = surf2->spans;
	surf2->spans = span;
    }
    // set last_u on the new span
    surf->last_u = iu;

 gotposition:
    // insert before surf2
    surf->next = surf2;
    surf->prev = surf2->prev;
    surf2->prev->next = surf;
    surf2->prev = surf;

    return 0;
}

/*
==============
R_GenerateSpans
==============
*/
int
R_GenerateSpans(void)
{
    edge_t *edge;
    int flags;

    flags = 0;
    r_bmodelactive = 0;

// clear active surfaces to just the background surface
    surfaces[1].next = surfaces[1].prev = &surfaces[1];
    surfaces[1].last_u = edge_head_u_shift20;

// generate spans
    for (edge = edge_head.next; edge != &edge_tail; edge = edge->next) {
	if (edge->surfs[0])
	    flags |= R_TrailingEdge(&surfaces[edge->surfs[0]], edge);
        if (edge->surfs[1])
            flags |= R_LeadingEdge(&surfaces[edge->surfs[1]], edge);
    }

    R_CleanupSpan();

    return flags;
}

#endif /* USE_X86_ASM */


/*
==============
R_GenerateSpansBackward
==============
*/
static int
R_GenerateSpansBackward(void)
{
    edge_t *edge;
    int flags;

    flags = 0;
    r_bmodelactive = 0;

// clear active surfaces to just the background surface
    surfaces[1].next = surfaces[1].prev = &surfaces[1];
    surfaces[1].last_u = edge_head_u_shift20;

// generate spans
    for (edge = edge_head.next; edge != &edge_tail; edge = edge->next) {
	if (edge->surfs[0])
	    flags |= R_TrailingEdge(&surfaces[edge->surfs[0]], edge);
	if (edge->surfs[1])
	    flags |= R_LeadingEdgeBackwards(&surfaces[edge->surfs[1]], edge);
    }

    R_CleanupSpan();

    return flags;
}

/*
 * ==========================================================
 * Span generation for flagged surfaces (fence, transparency)
 * ==========================================================
 */

static void
R_CleanupSpanWithFlag(int flags)
{
    surf_t *surf;
    int iu;
    espan_t *span;

// now that we've reached the right edge of the screen, we're done with any
// unfinished surfaces, so emit a span for whatever's on top
    surf = surfaces[1].next;
    iu = edge_tail_u_shift20;

    while (surf->flags & r_translucent_flags) {
        if ((surf->flags & flags) && iu > surf->last_u) {
            span = span_p++;
            span->u = surf->last_u;
            span->count = iu - span->u;
            span->v = current_iv;
            span->pnext = surf->spans;
            surf->spans = span;
        }
        surf = surf->next;
    }

// reset spanstate for all surfaces in the surface stack
    surf = surfaces[1].next;
    do {
	surf->spanstate = 0;
	surf = surf->next;
    } while (surf != &surfaces[1]);
}

static void
R_TrailingEdgeWithFlag(surf_t *surf, edge_t *edge, int flags)
{
    espan_t *span;
    surf_t *surf2;
    int iu;
    qboolean visible;

// don't generate a span if this is an inverted span, with the end
// edge preceding the start edge (that is, we haven't seen the
// start edge yet)
    if (--surf->spanstate != 0)
	return;

    if (surf->insubmodel)
	r_bmodelactive--;

    // emit a span if the ending surf was visible (not behind a solid surf)
    visible = false;
    surf2 = surfaces[1].next;
    while (1) {
        if (surf2 == surf) {
            iu = edge->u >> 20;
            if (surf->flags & flags) {
                if (iu > surf->last_u) {
                    span = span_p++;
                    span->u = surf->last_u;
                    span->count = iu - span->u;
                    span->v = current_iv;
                    span->pnext = surf->spans;
                    surf->spans = span;
                }
            }
            visible = true;
            break;
        }
        if (!(surf2->flags & r_translucent_flags))
            break;
        surf2 = surf2->next;
    }

    // If the ending surf was visible AND solid...
    // Set last_u on all surfaces below, down to the first solid
    if (visible && !(surf->flags & r_translucent_flags)) {
        surf2 = surf->next;
        while (1) {
            surf2->last_u = iu;
            if (!(surf2->flags & r_translucent_flags))
                break;
            surf2 = surf2->next;
        }
    }

    /* Completed.  Remove the surf from the stack */
    surf->prev->next = surf->next;
    surf->next->prev = surf->prev;
}

static void
R_LeadingEdgeWithFlag(surf_t *surf, edge_t *edge, int flags)
{
    espan_t *span;
    surf_t *surf2, *search;
    int iu;
    double fu, newzi, testzi, newzitop, newzibottom;
    qboolean visible;

    // don't start a span if this is an inverted span, with the end
    // edge preceding the start edge (that is, we've already seen the
    // end edge)
    if (++surf->spanstate != 1)
	return;

    if (surf->insubmodel)
	r_bmodelactive++;

    surf2 = &surfaces[1];

 continue_search:
    do {
	surf2 = surf2->next;
    } while (surf->key > surf2->key);

    if (surf->key != surf2->key)
	goto gotposition;

    // if it's two surfaces on the same plane, the one that's already
    // active is in front, so keep going unless it's a bmodel
    if (!surf->insubmodel)
	goto continue_search;

    // must be two bmodels in the same leaf; sort on 1/z
    fu = (float)(edge->u - 0xFFFFF) * (1.0 / 0x100000);
    newzi = surf->d_ziorigin + fv * surf->d_zistepv + fu * surf->d_zistepu;
    newzibottom = newzi * 0.99;

    testzi = surf2->d_ziorigin + fv * surf2->d_zistepv + fu * surf2->d_zistepu;
    if (newzibottom >= testzi)
	goto gotposition;

    newzitop = newzi * 1.01;
    if (newzitop >= testzi && surf->d_zistepu >= surf2->d_zistepu)
	goto gotposition;

    goto continue_search;

 gotposition:

    /*
     * Determine if our surf is visible above the current top solid
     * surf.  As we look through the surface stack, if we hit the
     * surface underneath us before finding another solid surf then we
     * are visible.
     */
    visible = false;
    search = surfaces[1].next;
    while (1) {
        if (search == surf2) {
            visible = true;
            break;
        }
        if (!(search->flags & r_translucent_flags))
            break;
        search = search->next;
    }

    if (visible) {
        /*
         * If this is a solid surface then we emit the spans for any
         * non-solid surfaces between this surface and the next solid.
         */
        iu = edge->u >> 20;

        if (!(surf->flags & r_translucent_flags)) {
            surf_t *emit = surf2;
            while (emit->flags & r_translucent_flags) {
                if ((emit->flags & flags) && iu > emit->last_u) {
                    span = span_p++;
                    span->u = emit->last_u;
                    span->count = iu - span->u;
                    span->v = current_iv;
                    span->pnext = emit->spans;
                    emit->spans = span;
                }
                emit = emit->next;
            }
        }
        // set last_u on the new span
        surf->last_u = iu;
    }

    // insert before surf2
    surf->next = surf2;
    surf->prev = surf2->prev;
    surf2->prev->next = surf;
    surf2->prev = surf;
}

static void
R_GenerateSpansWithFlag(int flags)
{
    edge_t *edge;

    r_bmodelactive = 0;

// clear active surfaces to just the background surface
    surfaces[1].next = surfaces[1].prev = &surfaces[1];
    surfaces[1].last_u = edge_head_u_shift20;

// generate spans
    for (edge = edge_head.next; edge != &edge_tail; edge = edge->next) {
	if (edge->surfs[0])
	    R_TrailingEdgeWithFlag(&surfaces[edge->surfs[0]], edge, flags);
        if (edge->surfs[1])
            R_LeadingEdgeWithFlag(&surfaces[edge->surfs[1]], edge, flags);
    }

    R_CleanupSpanWithFlag(flags);
}

/* ===================================================== */

/*
==============
R_ScanEdges

Input:
newedges[] array
	this has links to edges, which have links to surfaces

Output:
Each surface has a linked list of its visible spans
Scanlines having fence surfaces are flagged in the scanline flags struct.
==============
*/
void
R_ScanEdges(int drawflags, scanflags_t *scanflags)
{
    int iv, bottom;
    espan_t basespans[CACHE_PAD_ARRAY(MAXSPANS, espan_t)];
    espan_t *basespan_p;
    surf_t *s;

    basespan_p = CACHE_ALIGN_PTR(basespans);
    max_span_p = &basespan_p[MAXSPANS - r_refdef.vrect.width];

    span_p = basespan_p;

// clear active edges to just the background edges around the whole screen
// FIXME: most of this only needs to be set up once
    edge_head.u = r_refdef.vrect.x << 20;
    edge_head_u_shift20 = edge_head.u >> 20;
    edge_head.u_step = 0;
    edge_head.prev = NULL;
    edge_head.next = &edge_tail;
    edge_head.surfs[0] = 0;
    edge_head.surfs[1] = 1;

    edge_tail.u = (r_refdef.vrectright << 20) + 0xFFFFF;
    edge_tail_u_shift20 = edge_tail.u >> 20;
    edge_tail.u_step = 0;
    edge_tail.prev = &edge_head;
    edge_tail.next = &edge_aftertail;
    edge_tail.surfs[0] = 1;
    edge_tail.surfs[1] = 0;

    edge_aftertail.u = -1;	// force a move
    edge_aftertail.u_step = 0;
    edge_aftertail.next = &edge_sentinel;
    edge_aftertail.prev = &edge_tail;

// FIXME: do we need this now that we clamp x in r_draw.c?
    edge_sentinel.u = FIXED16_MAX;	// make sure nothing sorts past this
    edge_sentinel.prev = &edge_aftertail;

//
// process all scan lines
//
    bottom = r_refdef.vrectbottom - 1;

    for (iv = r_refdef.vrect.y; iv < bottom; iv++) {
	current_iv = iv;
	fv = (float)iv;

	// mark that the head (background start) span is pre-included
	surfaces[1].spanstate = 1;

	if (newedges[iv]) {
	    R_InsertNewEdges(newedges[iv], edge_head.next);
	}

        if (!drawflags) {
            int flags = GenerateSpans();
            scanflags->lines[iv] = flags;
            scanflags->found |= flags;
        } else if (scanflags->lines[iv] & drawflags) {
            GenerateSpansWithFlag(drawflags);
        }

	// flush the span list if we can't be sure we have enough spans left
	// for the next scan
	if (span_p >= max_span_p) {
	    VID_UnlockBuffer();
	    S_ExtraUpdate();	// don't let sound get messed up if going slow
	    VID_LockBuffer();

	    D_DrawSurfaces(!!drawflags);

	    // clear the surface span pointers
	    for (s = &surfaces[1]; s < surface_p; s++)
		s->spans = NULL;

	    span_p = basespan_p;
	}

	if (removeedges[iv])
	    R_RemoveEdges(removeedges[iv]);

	if (edge_head.next != &edge_tail)
	    R_StepActiveU(edge_head.next);
    }

// do the last scan (no need to step or sort or remove on the last scan)

    current_iv = iv;
    fv = (float)iv;

// mark that the head (background start) span is pre-included
    surfaces[1].spanstate = 1;

    if (newedges[iv])
	R_InsertNewEdges(newedges[iv], edge_head.next);

    if (!drawflags) {
        int flags = GenerateSpans();
        scanflags->lines[iv] = flags;
        scanflags->found |= flags;
    } else if (scanflags->lines[iv] & drawflags) {
        GenerateSpansWithFlag(drawflags);
    }

// draw whatever's left in the span list
    D_DrawSurfaces(!!drawflags);
}
