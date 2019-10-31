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

#ifndef RENDER_H
#define RENDER_H

#include "cvar.h"
#include "mathlib.h"
#include "model.h"
#include "vid.h"

#ifdef NQ_HACK
#include "quakedef.h"
#endif
#ifdef QW_HACK
#include "protocol.h"
#endif

// render.h -- public interface to refresh functions

#define	TOP_RANGE	16	// soldier uniform colors
#define	BOTTOM_RANGE	96

void R_InitTranslationTable();
const byte *R_GetTranslationTable(int topcolor, int bottomcolor);

//=============================================================================

typedef struct efrag_s {
    struct mleaf_s *leaf;
    struct efrag_s *leafnext;
    struct entity_s *entity;
    struct efrag_s *entnext;
} efrag_t;

#define LERP_MOVESTEP   (1U << 0) // this is a MOVETYPE_STEP entity, enable movement lerp
#define LERP_RESETANIM  (1U << 1) // disable anim lerping until next anim frame
#define LERP_RESETANIM2 (1U << 2) // set this to disable lerping the next anim frames
#define LERP_RESETANIM3 (1U << 3) // set this to disable lerping the next anim frames
#define LERP_RESETMOVE  (1U << 4) // disable movement lerping until next origin/angles change
#define LERP_FINISH     (1U << 5) // use lerpfinish time from server update instead of assuming interval of 0.1

typedef struct {
    short pose0;
    short pose1;
    float blend;
    vec3_t origin;
    vec3_t angles;
} lerpdata_t;

typedef struct entity_s {
#ifdef GLQUAKE
    struct entity_s *chain;
#endif
#ifdef NQ_HACK
    qboolean forcelink;		// model changed

    int update_type;

    entity_state_t baseline;	// to fill in defaults in updates

    double msgtime;		// time of last update
    vec3_t msg_origins[2];	// last two updates (0 is newest)
    vec3_t msg_angles[2];	// last two updates (0 is newest)
#endif
#ifdef QW_HACK
    int keynum;			// for matching entities in different frames
#endif
    vec3_t origin;
    vec3_t angles;
    struct model_s *model;	// NULL = no model
    int frame;
    byte *colormap;
    int skinnum;		// for Alias models
#ifdef QW_HACK
    struct player_info_s *scoreboard;	// identify player
#endif
    float syncbase;		// for client-side animations

    struct efrag_s *efrag;	// linked list of efrags (FIXME)
    int edictframe;		// last frame this entity was found in an active leaf

#ifdef NQ_HACK
    int effects;		// light, particals, etc
#endif
    int dlightframe;		// dynamic lighting
    int dlightbits;

// FIXME: could turn these into a union
    int trivial_accept;
    struct mnode_s *topnode;	// for bmodels, first world node
				//  that splits bmodel, or NULL if
				//  not split

    byte alpha;                 // translucency (0 = transparent, 255 = opaque)
    depthchain_t depthchain;

    /* Alias model lerping */
    struct {
        uint32_t flags;
        float finish;
        struct {
            float start;
            struct {
                vec3_t current;
                vec3_t previous;
            } origin;
            struct {
                vec3_t current;
                vec3_t previous;
            } angles;
        } move;
        struct {
            float start;
            float duration;
            short current;
            short previous;
        } pose;
    } lerp;
} entity_t;

static inline qboolean
R_EntityIsRotated(const entity_t *entity)
{
    return !!(entity->angles[0] || entity->angles[1] || entity->angles[2]);
}

static inline qboolean
R_EntityIsTranslated(const entity_t *entity)
{
    return !!(entity->origin[0] || entity->origin[1] || entity->origin[2]);
}

void R_AliasSetupTransformLerp(entity_t *entity, lerpdata_t *lerpdata);
void R_AliasSetupAnimationLerp(entity_t *entity, aliashdr_t *aliashdr, lerpdata_t *lerpdata);

extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct {
    vrect_t vrect;		// subwindow in video for refresh
    // FIXME: not need vrect next field here?
    vrect_t aliasvrect;		// scaled Alias version
    int vrectright, vrectbottom;	// right & bottom screen coords
    int aliasvrectright, aliasvrectbottom;	// scaled Alias versions
    float vrectrightedge;	// rightmost right edge we care about,
    //  for use in edge list
    float fvrectx, fvrecty;	// for floating-point compares
    float fvrectx_adj, fvrecty_adj;	// left and top edges, for clamping
    int vrect_x_adj_shift20;	// (vrect.x + 0.5 - epsilon) << 20
    int vrectright_adj_shift20;	// (vrectright + 0.5 - epsilon) << 20
    float fvrectright_adj, fvrectbottom_adj;
    // right and bottom edges, for clamping
    float fvrectright;		// rightmost edge, for Alias clamping
    float fvrectbottom;		// bottommost edge, for Alias clamping
    float horizontalFieldOfView;	// at Z = 1.0, this many X is visible
    // 2.0 = 90 degrees
    float xOrigin;		// should probably always be 0.5
    float yOrigin;		// between be around 0.3 to 0.5

    vec3_t vieworg;
    vec3_t viewangles;

    float fov_x, fov_y;

    int ambientlight;
} refdef_t;


//
// refresh
//

extern refdef_t r_refdef;
extern vec3_t r_origin, vpn, vright, vup;

extern struct texture_s *r_notexture_mip;

extern entity_t r_worldentity;

void R_Init(void);
void R_InitTextures(void);
void R_InitEfrags(void);
void R_RenderView(void); // must set r_refdef first
void R_ViewChanged(const vrect_t *vrect, int lineadj, float aspect); // called whenever r_refdef or vid change

void R_InitSky(struct texture_s *mt);	// called at level load

void R_AddEfrags(entity_t *ent);
void R_RemoveEfrags(entity_t *ent);

void R_NewMap(void);

#ifdef QW_HACK
void R_NetGraph(void);
void R_ZGraph(void);
#endif

void R_ParseParticleEffect(void);
void R_RunParticleEffect(vec3_t org, vec3_t dir, int color, int count);
void R_RocketTrail(vec3_t start, vec3_t end, int type);
void R_BlobExplosion(vec3_t org);
void R_ParticleExplosion(vec3_t org);
void R_LavaSplash(vec3_t org);
void R_TeleportSplash(vec3_t org);

texture_t *R_TextureAnimation(const entity_t *entity, texture_t *base);

#ifdef NQ_HACK
void R_EntityParticles(const entity_t *ent);
void R_ParticleExplosion2(vec3_t org, int colorStart, int colorLength);
#endif

void R_PushDlights(void);

void R_InitParticles(void);
void R_ClearParticles(void);
void R_DrawParticles(void);

/*
 * The renderer supplies callbacks to the model loader
 */
const alias_loader_t *R_AliasModelLoader(void);
const brush_loader_t *R_BrushModelLoader(void);

//
// surface cache related
//
extern qboolean r_cache_thrash;	// set if thrashing the surface cache

int D_SurfaceCacheForRes(int width, int height);
void D_FlushCaches(void);
void D_DeleteSurfaceCache(void);
void D_InitCaches(void *buffer, int size);
void R_SetVrect(const vrect_t *in, vrect_t *out, int lineadj);

//
// translucency related
//
void Alpha_Init();
void Alpha_NewMap();
#ifndef GLQUAKE
const byte *Alpha_Transtable(float alpha);
#endif
float R_GetSurfAlpha(int flags);

extern int r_surfalpha_flags;
extern const byte *transtable_water;
extern const byte *transtable_slime;
extern const byte *transtable_lava;
extern const byte *transtable_tele;

extern cvar_t r_wateralpha;
extern cvar_t r_slimealpha;
extern cvar_t r_lavaalpha;
extern cvar_t r_telealpha;

extern float map_wateralpha;
extern float map_slimealpha;
extern float map_lavaalpha;
extern float map_telealpha;

extern depthchain_t r_depthchain; // Depth sorted chain for alpha blending

#endif /* RENDER_H */
