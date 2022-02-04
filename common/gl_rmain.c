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
// r_main.c

#include "console.h"
#include "developer.h"
#include "draw.h"
#include "gl_model.h"
#include "glquake.h"
#include "mathlib.h"
#include "model.h"
#include "quakedef.h"
#include "render.h"
#include "screen.h"
#include "sound.h"
#include "sys.h"
#include "vid.h"
#include "view.h"

entity_t r_worldentity;
qboolean r_cache_thrash;	// compatability

vec3_t r_entorigin;
int r_visframecount;		// bumped when going to a new PVS
int r_framecount = 1;		// used for dlight push checking

static mplane_t frustum[4];

int c_lightmaps_uploaded;
int c_brush_polys;
static int c_alias_polys;

qboolean envmap;		// true during envmap command capture

GLuint currenttexture = -1;	// to avoid unnecessary texture sets
playertexture_t playertextures[MAX_CLIENTS];// up to 16 color translated skins

int mirrortexturenum;		// quake texturenum, not gltexturenum
qboolean mirror;
mplane_t *mirror_plane;

//
// view origin
//
vec3_t vup;
vec3_t vpn;
vec3_t vright;
vec3_t r_origin;

float r_world_matrix[16];

//
// screen size info
//
refdef_t r_refdef;

mleaf_t *r_viewleaf, *r_oldviewleaf;
texture_t *r_notexture_mip;
int d_lightstylevalue[256];	// 8.8 fraction of base light value

cvar_t r_norefresh = { "r_norefresh", "0" };
cvar_t r_drawentities = { "r_drawentities", "1" };
cvar_t r_drawviewmodel = { "r_drawviewmodel", "1" };
cvar_t r_speeds = { "r_speeds", "0" };
cvar_t r_lightmap = { "r_lightmap", "0" };
cvar_t r_mirroralpha = { "r_mirroralpha", "1" };
cvar_t r_dynamic = { "r_dynamic", "1" };
cvar_t r_novis = { "r_novis", "0" };
#ifdef QW_HACK
cvar_t r_netgraph = { "r_netgraph", "0" };
#endif
cvar_t r_waterwarp = { "r_waterwarp", "1" };

cvar_t r_fullbright = {
    .name = "r_fullbright",
    .string = "0",
    .flags = CVAR_DEVELOPER
};
cvar_t gl_keeptjunctions = {
    .name = "gl_keeptjunctions",
    .string = "1",
    .flags = CVAR_OBSOLETE
};
cvar_t gl_reporttjunctions = {
    .name = "gl_reporttjunctions",
    .string = "0",
    .flags = CVAR_OBSOLETE
};
cvar_t gl_texsort = {
    .name = "gl_texsort",
    .string = "1",
    .flags = CVAR_OBSOLETE
};

cvar_t gl_finish = { "gl_finish", "0" };
cvar_t gl_smoothmodels = { "gl_smoothmodels", "1" };
cvar_t gl_affinemodels = { "gl_affinemodels", "0" };
cvar_t gl_polyblend = { "gl_polyblend", "1" };
cvar_t gl_playermip = { "gl_playermip", "0" };
cvar_t gl_nocolors = { "gl_nocolors", "0" };
cvar_t gl_zfix = { "gl_zfix", "0" };
#ifdef NQ_HACK
cvar_t gl_doubleeyes = { "gl_doubleeyes", "1" };
#endif
cvar_t gl_fullbrights = { "gl_fullbrights", "1", true };
cvar_t gl_farclip = { "gl_farclip", "16384", true };

cvar_t _gl_allowgammafallback = { "_gl_allowgammafallback", "1" };

/*
 * model interpolation support
 */
cvar_t r_lerpmodels = { "r_lerpmodels", "1", false };
cvar_t r_lerpmove = { "r_lerpmove", "1", false };

qboolean gl_mtexable = false;
int gl_num_texture_units;

/*
===============
GL_Init
===============
*/
void
GL_Init(void)
{
    const char *gl_vendor;
    const char *gl_renderer;
    const char *gl_version;

    gl_vendor = (char *)glGetString(GL_VENDOR);
    Con_Printf("GL_VENDOR: %s\n", gl_vendor);
    gl_renderer = (char *)glGetString(GL_RENDERER);
    Con_Printf("GL_RENDERER: %s\n", gl_renderer);

    gl_version = (char *)glGetString(GL_VERSION);
    Con_Printf("GL_VERSION: %s\n", gl_version);

    GL_ExtensionCheck_MultiTexture();
    GL_ExtensionCheck_NPoT();

    glClearColor(0.5, 0.5, 0.5, 0);
    glFrontFace(GL_CW);
    glCullFace(GL_BACK);
    glEnable(GL_CULL_FACE);
    glEnable(GL_TEXTURE_2D);

    glEnable(GL_ALPHA_TEST);
    glAlphaFunc(GL_GREATER, 0.666);

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glShadeModel(GL_FLAT);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
}

/*
 * Call before deleting the OpenGL context
 */
void
GL_Shutdown()
{
    GL_FreeTextures();
}

/*
 * Called when context changes and we need to re-upload all textures
 */
void
GL_ReloadTextures()
{
    GL_LoadNoTexture();
    Draw_InitGLTextures();
    Draw_ReloadPicTextures();
    Mod_ReloadTextures();
    R_ResetPlayerTextures();
    Sky_LoadSkyboxTextures(map_skyboxname);
#ifdef QW_HACK
    R_ResetNetGraphTexture();
#endif
}


/*
=================
R_CullBox

Returns true if the box is completely outside the frustum
=================
*/
qboolean
R_CullBox(const vec3_t mins, const vec3_t maxs)
{
    int i;

    for (i = 0; i < 4; i++)
	/* Not using macro since frustum planes generally not axis aligned */
	if (BoxOnPlaneSide(mins, maxs, &frustum[i]) == 2)
	    return true;
    return false;
}


void
R_RotateForEntity(const vec3_t origin, const vec3_t angles)
{
    glTranslatef(origin[0], origin[1], origin[2]);

    glRotatef(angles[1], 0, 0, 1);
    glRotatef(-angles[0], 0, 1, 0);
    glRotatef(angles[2], 1, 0, 0);
}

/*
=============================================================

  SPRITE MODELS

=============================================================
*/

typedef struct {
    GLuint texture;
    byte pixels[];
} gl_spritedata_t;

int R_SpriteDataSize(int numpixels)
{
    return offsetof(gl_spritedata_t, pixels[numpixels]);
}

static void
GL_LoadSpriteTexture(const char *name, const mspriteframe_t *frame)
{
    gl_spritedata_t *spritedata;
    qpic8_t pic;

    spritedata = (gl_spritedata_t *)frame->rdata;

    pic.width = pic.stride = frame->width;
    pic.height = frame->height;
    pic.pixels = spritedata->pixels;

    spritedata->texture = GL_LoadTexture8(name, &pic, TEXTURE_TYPE_SPRITE);
}

void
GL_LoadSpriteTextures(const model_t *model)
{
    char hunkname[HUNK_NAMELEN + 1];
    msprite_t *sprite;
    mspriteframedesc_t *framedesc;
    mspriteframe_t *frame;
    mspritegroup_t *group;
    int i, j;

    COM_FileBase(model->name, hunkname, sizeof(hunkname));
    sprite = model->cache.data;
    for (i = 0; i < sprite->numframes; i++) {
        framedesc = &sprite->frames[i];

        /* Single frame */
        if (framedesc->type == SPR_SINGLE) {
            frame = framedesc->frame.frame;
            GL_LoadSpriteTexture(va("%s_%i", hunkname, i), frame);
            continue;
        }

        /* Frame group */
        group = framedesc->frame.group;
        for (j = 0; j < group->numframes; j++) {
            frame = group->frames[j];
            GL_LoadSpriteTexture(va("%s_%i", hunkname, i * 100 + j), frame);
        }
    }
}

void
R_SpriteDataStore(mspriteframe_t *frame, const char *modelname, int framenum, byte *pixels)
{
    gl_spritedata_t *spritedata;

    spritedata = (gl_spritedata_t *)frame->rdata;
    memcpy(spritedata->pixels, pixels, frame->width * frame->height);
    GL_LoadSpriteTexture(va("%s_%i", modelname, framenum), frame);
}

/*
=================
R_DrawSpriteModel

=================
*/
static void
R_DrawSpriteModel(const entity_t *entity)
{
    const msprite_t *sprite;
    const mspriteframe_t *frame;
    const float *s_up, *s_right;
    const gl_spritedata_t *spritedata;
    vec3_t point, v_forward, v_right, v_up;
    float angle, sr, cr;
    float alpha;

    sprite = entity->model->cache.data;
    frame = Mod_GetSpriteFrame(entity, sprite, cl.time + entity->syncbase);
    spritedata = (gl_spritedata_t *)frame->rdata;

    /*
     * Don't even bother culling, because it's just a single polygon
     * without a surface cache
     */
    switch (sprite->type) {
        case SPR_VP_PARALLEL_UPRIGHT: //faces view plane, up is towards the heavens
            v_up[0] = 0;
            v_up[1] = 0;
            v_up[2] = 1;
            s_up = v_up;
            s_right = vright;
            break;
        case SPR_FACING_UPRIGHT: //faces camera origin, up is towards the heavens
            VectorSubtract(entity->origin, r_origin, v_forward);
            v_forward[2] = 0;
            VectorNormalize(v_forward);
            v_right[0] = v_forward[1];
            v_right[1] = -v_forward[0];
            v_right[2] = 0;
            v_up[0] = 0;
            v_up[1] = 0;
            v_up[2] = 1;
            s_up = v_up;
            s_right = v_right;
            break;
	case SPR_VP_PARALLEL: //faces view plane, up is towards the top of the screen
            s_up = vup;
            s_right = vright;
            break;
	case SPR_ORIENTED: //pitch yaw roll are independent of camera
            AngleVectors (entity->angles, v_forward, v_right, v_up);
            s_up = v_up;
            s_right = v_right;
            break;
	case SPR_VP_PARALLEL_ORIENTED: //faces view plane, but obeys roll value
            angle = entity->angles[ROLL] * (M_PI / 180.0f);
            sr = sin(angle);
            cr = cos(angle);
            v_right[0] = vright[0] * cr + vup[0] * sr;
            v_right[1] = vright[1] * cr + vup[1] * sr;
            v_right[2] = vright[2] * cr + vup[2] * sr;
            v_up[0] = vright[0] * -sr + vup[0] * cr;
            v_up[1] = vright[1] * -sr + vup[1] * cr;
            v_up[2] = vright[2] * -sr + vup[2] * cr;
            s_up = v_up;
            s_right = v_right;
            break;
        default:
            Sys_Error("%s: Bad sprite type %d", __func__, sprite->type);
    }

    alpha = ENTALPHA_DECODE(entity->alpha);
    alpha = 1.0f;
    if (alpha < 1.0f) {
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glColor4f(1, 1, 1, alpha);

    GL_DisableMultitexture();
    GL_Bind(spritedata->texture);

    glEnable(GL_ALPHA_TEST);
    glBegin(GL_QUADS);

    glTexCoord2f(0, 1);
    VectorMA(entity->origin, frame->down, s_up, point);
    VectorMA(point, frame->left, s_right, point);
    glVertex3fv(point);

    glTexCoord2f(0, 0);
    VectorMA(entity->origin, frame->up, s_up, point);
    VectorMA(point, frame->left, s_right, point);
    glVertex3fv(point);

    glTexCoord2f(1, 0);
    VectorMA(entity->origin, frame->up, s_up, point);
    VectorMA(point, frame->right, s_right, point);
    glVertex3fv(point);

    glTexCoord2f(1, 1);
    VectorMA(entity->origin, frame->down, s_up, point);
    VectorMA(point, frame->right, s_right, point);
    glVertex3fv(point);

    glEnd();

    if (alpha < 1.0f) {
        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }
    glDisable(GL_ALPHA_TEST);

    if (_debug_models.value)
        DEBUG_DrawModelInfo(entity, entity->origin);
}

/*
=============================================================

  ALIAS MODELS

=============================================================
*/

#define NUMVERTEXNORMALS 162

// quantized vertex normals for alias models
float r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

// precalculated dot products for quantized angles
#define SHADEDOT_QUANT 16
static float r_avertexnormal_dots[SHADEDOT_QUANT][256] =
#include "anorm_dots.h"
     ;

typedef struct {
    vec3_t shade;
    float ambient;
    float *shadedots;
} alias_light_t;

/*
 * Upload alias skin textures
 */
void
GL_LoadAliasSkinTextures(const model_t *model, aliashdr_t *aliashdr)
{
    int i, skinsize;
    qgltexture_t *textures;
    byte *pixels;
    qpic8_t pic;

    if (!aliashdr) {
        aliashdr = Cache_Check(&model->cache);
        if (!aliashdr)
            return;
    }

    skinsize = aliashdr->skinwidth * aliashdr->skinheight;
    textures = (qgltexture_t *)((byte *)aliashdr + GL_Aliashdr(aliashdr)->textures);
    pixels = (byte *)aliashdr + aliashdr->skindata;
    pic.pixels = pixels;
    pic.stride = aliashdr->skinwidth;

    /*
     * FIXME: This is a bit ugly, having to reset the width/height
     * each time around the loop, due to the way the expanded texture
     * width/height is returned for non-POT textures
     */
    for (i = 0; i < aliashdr->numskins; i++) {
        pic.width = aliashdr->skinwidth;
        pic.height = aliashdr->skinheight;
        textures[i].base = GL_LoadTexture8(va("%s_%i", model->name, i), &pic, TEXTURE_TYPE_ALIAS_SKIN);
        GL_Aliashdr(aliashdr)->texturewidth = pic.width;
        GL_Aliashdr(aliashdr)->textureheight = pic.height;
        if (QPic_HasFullbrights(&pic, TEXTURE_TYPE_ALIAS_SKIN)) {
            pic.width = aliashdr->skinwidth;
            pic.height  = aliashdr->skinheight;
            textures[i].fullbright = GL_LoadTexture8(va("%s_%i:fullbright", model->name, i), &pic, TEXTURE_TYPE_ALIAS_SKIN_FULLBRIGHT);
        } else {
            textures[i].fullbright = 0;
        }
        pic.pixels += skinsize;
    }
}

/*
===============
R_AliasSetupSkin
===============
*/
static const qgltexture_t *
R_AliasSetupSkin(const entity_t *entity, aliashdr_t *aliashdr)
{
    const maliasskindesc_t *skindesc;
    const float *intervals;
    int skinnum, numframes, frame;
    const qgltexture_t *textures;

    skinnum = entity->skinnum;
    if ((skinnum >= aliashdr->numskins) || (skinnum < 0)) {
	Con_DPrintf("%s: %s has no such skin (%d)\n",
		    __func__, entity->model->name, skinnum);
	skinnum = 0;
    }

    skindesc = (maliasskindesc_t *)((byte *)aliashdr + aliashdr->skindesc);
    skindesc += skinnum;
    frame = skindesc->firstframe;
    numframes = skindesc->numframes;

    if (numframes > 1) {
	const float frametime = cl.time + entity->syncbase;
	intervals = (float *)((byte *)aliashdr + aliashdr->skinintervals);
	frame += Mod_FindInterval(intervals + frame, numframes, frametime);
    }

    textures = (qgltexture_t *)((byte *)aliashdr + GL_Aliashdr(aliashdr)->textures);

    return &textures[frame];
}

static void
R_LightPoint(const vec3_t point, alias_light_t *light)
{
    surf_lightpoint_t lightpoint;
    qboolean hit;

    if (!cl.worldmodel->lightdata) {
        light->ambient  = 255.0f;
        light->shade[0] = 255.0f;
        light->shade[1] = 255.0f;
        light->shade[2] = 255.0f;
        return;
    }

    hit = R_LightSurfPoint(point, &lightpoint);
    if (!hit) {
        light->shade[0] = light->ambient;
        light->shade[1] = light->ambient;
        light->shade[2] = light->ambient;
        return;
    }

    const msurface_t *surf = lightpoint.surf;
    const int surfwidth = (surf->extents[0] >> 4) + 1;
    const int surfheight = (surf->extents[1] >> 4) + 1;
    const int ds = qmin((int)floorf(lightpoint.s), surfwidth - 2);
    const int dt = qmin((int)floorf(lightpoint.t), surfheight - 2);
    const int surfbytes = surfwidth * surfheight * gl_lightmap_bytes;
    const byte *row0 = surf->samples + (dt * surfwidth + ds) * gl_lightmap_bytes;
    const byte *row1 = row0 + surfwidth * gl_lightmap_bytes;
    vec3_t samples[2][2];

    /* Calculate a 2x2 sample, adding the light styles together */
    memset(samples, 0, sizeof(samples));

    int maps;
    foreach_surf_lightstyle(surf, maps) {
        const float scale = d_lightstylevalue[surf->styles[maps]] * (1.0f / 256.0f);
        samples[0][0][0] += row0[0] * scale;
        samples[0][0][1] += row0[1] * scale;
        samples[0][0][2] += row0[2] * scale;

        samples[0][1][0] += row0[3] * scale;
        samples[0][1][1] += row0[4] * scale;
        samples[0][1][2] += row0[5] * scale;

        samples[1][0][0] += row1[0] * scale;
        samples[1][0][1] += row1[1] * scale;
        samples[1][0][2] += row1[2] * scale;

        samples[1][1][0] += row1[3] * scale;
        samples[1][1][1] += row1[4] * scale;
        samples[1][1][2] += row1[5] * scale;

        row0 += surfbytes;
        row1 += surfbytes;
    }

    /* Interpolate within the 2x2 sample */
    const float dsfrac = lightpoint.s - ds;
    const float dtfrac = lightpoint.t - dt;
    float weight00 = (1.0f - dsfrac) * (1.0f - dtfrac);
    float weight01 = dsfrac * (1.0f - dtfrac);
    float weight10 = (1.0f - dsfrac) * dtfrac;
    float weight11 = dsfrac * dtfrac;

    int i;
    for (i = 0; i < 3; i++) {
        light->shade[i] =
            samples[0][0][i] * weight00 +
            samples[0][1][i] * weight01 +
            samples[1][0][i] * weight10 +
            samples[1][1][i] * weight11;
    }

    /* Clamp to minimum ambient lighting */
    if (light->ambient) {
        light->shade[0] = qmax(light->shade[0], light->ambient);
        light->shade[1] = qmax(light->shade[1], light->ambient);
        light->shade[2] = qmax(light->shade[2], light->ambient);
    }
}

static void
R_AliasCalcLight(const entity_t *entity, const vec3_t origin, const vec3_t angles, alias_light_t *light)
{
    /* Set minimum light level on viewmodel (gun) */
    if (entity == &cl.viewent && light->ambient < 24)
        light->ambient = 24;

    /* Set minimum light level on players */
#ifdef NQ_HACK
    if (CL_PlayerEntity(entity)) {
#endif
#ifdef QW_HACK
    if (!strcmp(entity->model->name, "progs/player.mdl")) {
#endif
	if (light->ambient < 8)
	    light->ambient = 8;
    }

    /* Calculate light level below the model */
    R_LightPoint(origin, light);

    /* Add dynamic lights */
    int i;
    const dlight_t *dlight = cl_dlights;
    for (i = 0; i < MAX_DLIGHTS; i++, dlight++) {
	if (dlight->die >= cl.time) {
	    vec3_t lightvec;
	    VectorSubtract(origin, dlight->origin, lightvec);
	    float add = dlight->radius - Length(lightvec);
	    if (add > 0)
                VectorMA(light->shade, add, dlight->color, light->shade);
	}
    }

    // clamp lighting so it doesn't overbright as much
    light->shade[0] = qmin(light->shade[0], 192.0f);
    light->shade[1] = qmin(light->shade[1], 192.0f);
    light->shade[2] = qmin(light->shade[2], 192.0f);

    int shadequant = (int)(angles[1] * (SHADEDOT_QUANT / 360.0));
    light->shadedots = r_avertexnormal_dots[shadequant & (SHADEDOT_QUANT - 1)];
    VectorScale(light->shade, 1.0f / 200.0f, light->shade);
}

/*
=================
R_AliasDrawModel
=================
*/
static void
R_AliasDrawModel(entity_t *entity)
{
    const model_t *model = entity->model;
    vec3_t mins, maxs;
    int i;
    float radius;
    aliashdr_t *aliashdr;
    alias_light_t light;
    lerpdata_t lerpdata;
    float alpha;

    /* Calculate lerped position and cull if out of view */
    R_AliasSetupTransformLerp(entity, &lerpdata);
    if (lerpdata.angles[0] || lerpdata.angles[2]) {
	radius = model->radius;
	for (i = 0; i < 3; i++) {
	    mins[i] = lerpdata.origin[i] - radius;
	    maxs[i] = lerpdata.origin[i] + radius;
	}
    } else if (lerpdata.angles[1]) {
	radius = model->xy_radius;
	mins[0] = lerpdata.origin[0] - radius;
	mins[1] = lerpdata.origin[1] - radius;
	mins[2] = lerpdata.origin[2] + model->mins[2];
	maxs[0] = lerpdata.origin[0] + radius;
	maxs[1] = lerpdata.origin[1] + radius;
	maxs[2] = lerpdata.origin[2] + model->maxs[2];
    } else {
	VectorAdd(lerpdata.origin, model->mins, mins);
	VectorAdd(lerpdata.origin, model->maxs, maxs);
    }
    if (R_CullBox(mins, maxs))
	return;

    /* Calculate lighting at the lerp origin */
    memset(&light, 0, sizeof(light));
    R_AliasCalcLight(entity, lerpdata.origin, lerpdata.angles, &light);

    /* locate/load the data in the model cache */
    aliashdr = Mod_Extradata(entity->model);
    R_AliasSetupAnimationLerp(entity, aliashdr, &lerpdata);

    /* Generate the vertex/color buffers */
    int numverts = aliashdr->numverts;
    vec_t *vertexbuf = alloca(numverts * sizeof(vec3_t));
    vec_t *colorbuf = alloca(numverts * 4 * sizeof(float));

    alpha = ENTALPHA_DECODE(entity->alpha);

    if (lerpdata.blend == 1.0f) {
        const trivertx_t *posedata = (trivertx_t *)((byte *)aliashdr + aliashdr->posedata);
        const trivertx_t *src = posedata + lerpdata.pose1 * numverts;
        vec_t *dstvert = vertexbuf;
        vec_t *dstcolor = colorbuf;
        for (i = 0; i < numverts; i++, src++) {
            *dstvert++ = src->v[0];
            *dstvert++ = src->v[1];
            *dstvert++ = src->v[2];

            float lightscale = light.shadedots[src->lightnormalindex];
            *dstcolor++ = lightscale * light.shade[0];
            *dstcolor++ = lightscale * light.shade[1];
            *dstcolor++ = lightscale * light.shade[2];
            *dstcolor++ = alpha;
        }
    } else {
        const trivertx_t *posedata = (trivertx_t *)((byte *)aliashdr + aliashdr->posedata);
        const trivertx_t *src0 = posedata + lerpdata.pose0 * numverts;
        const trivertx_t *src1 = posedata + lerpdata.pose1 * numverts;
        vec_t *dstvert = vertexbuf;
        vec_t *dstcolor = colorbuf;
        for (i = 0; i < numverts; i++, src0++, src1++) {
            *dstvert++ = src0->v[0] * (1.0f - lerpdata.blend) + src1->v[0] * lerpdata.blend;
            *dstvert++ = src0->v[1] * (1.0f - lerpdata.blend) + src1->v[1] * lerpdata.blend;
            *dstvert++ = src0->v[2] * (1.0f - lerpdata.blend) + src1->v[2] * lerpdata.blend;

            float lightscale;
            lightscale  = light.shadedots[src0->lightnormalindex] * (1.0f - lerpdata.blend);
            lightscale += light.shadedots[src1->lightnormalindex] * lerpdata.blend;
            *dstcolor++ = lightscale * light.shade[0];
            *dstcolor++ = lightscale * light.shade[1];
            *dstcolor++ = lightscale * light.shade[2];
            *dstcolor++ = alpha;
        }
    }

    uint16_t *indices = (uint16_t *)((byte *)aliashdr + GL_Aliashdr(aliashdr)->indices);
    float *texcoords = (float *)((byte *)aliashdr + GL_Aliashdr(aliashdr)->texcoords);

    /* Setup state for drawing */
    VectorCopy(lerpdata.origin, r_entorigin);
    GL_DisableMultitexture();
    glPushMatrix();
    R_RotateForEntity(lerpdata.origin, lerpdata.angles);

    /* double size of eyes, since they are really hard to see in gl */
#ifdef NQ_HACK
    if (!strcmp(model->name, "progs/eyes.mdl") && gl_doubleeyes.value) {
#endif
#ifdef QW_HACK
    if (!strcmp(model->name, "progs/eyes.mdl")) {
#endif
        const vec_t *scale = aliashdr->scale;
        const vec_t *origin = aliashdr->scale_origin;
	glTranslatef(origin[0], origin[1], origin[2] - (22 + 8));
	glScalef(scale[0] * 2, scale[1] * 2, scale[2] * 2);
    } else {
        const vec_t *scale = aliashdr->scale;
        const vec_t *origin = aliashdr->scale_origin;
	glTranslatef(origin[0], origin[1], origin[2]);
	glScalef(scale[0], scale[1], scale[2]);
    }

    const qgltexture_t *skin = R_AliasSetupSkin(entity, aliashdr);
    qboolean fullbright = !!skin->fullbright;
    GL_Bind(skin->base);

    /*
     * We can't dynamically colormap textures, so they are cached
     * seperately for the players.  Heads are just uncolored.
     */
#ifdef NQ_HACK
    if (entity->colormap != vid.colormap && !gl_nocolors.value) {
	const int playernum = CL_PlayerEntity(entity);
	if (playernum) {
	    fullbright = playertextures[playernum - 1].fullbright;
	    skin = &playertextures[playernum - 1].texture;
            assert(skin->base);
	    GL_Bind(skin->base);
        }
    }
#endif
#ifdef QW_HACK
    if (entity->scoreboard && !gl_nocolors.value) {
	const int playernum = entity->scoreboard - cl.players;
	if (!entity->scoreboard->skin) {
	    Skin_Find(entity->scoreboard);
	    R_TranslatePlayerSkin(playernum);
	}
	if (playernum >= 0 && playernum < MAX_CLIENTS) {
	    fullbright = playertextures[playernum].fullbright;
	    skin = &playertextures[playernum].texture;
            assert(skin->base);
	    GL_Bind(skin->base);
        }
    }
#endif

    if (gl_smoothmodels.value)
	glShadeModel(GL_SMOOTH);
    if (gl_affinemodels.value)
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_FASTEST);

    if (alpha < 1.0f) {
	glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    /* Draw */
    if (gl_mtexable)
        qglClientActiveTexture(GL_TEXTURE0);

    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);
    glVertexPointer(3, GL_FLOAT, 0, vertexbuf);
    glTexCoordPointer(2, GL_FLOAT, 0, texcoords);
    if (r_fullbright.value) {
        glColor3f(1.0f, 1.0f, 1.0f);
    } else {
        glEnableClientState(GL_COLOR_ARRAY);
        glColorPointer(4, GL_FLOAT, 0, colorbuf);
    }

    if (gl_mtexable && fullbright && gl_fullbrights.value) {
        GL_EnableMultitexture();
        GL_Bind(skin->fullbright);
        qglClientActiveTexture(GL_TEXTURE1);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glTexCoordPointer(2, GL_FLOAT, 0, texcoords);

        glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_DECAL);
    }

    glDrawElements(GL_TRIANGLES, aliashdr->numtris * 3, GL_UNSIGNED_SHORT, indices);
    gl_draw_calls++;
    gl_verts_submitted += numverts;
    gl_indices_submitted += aliashdr->numtris * 3;

    if (gl_mtexable && fullbright && gl_fullbrights.value) {
        glDisableClientState(GL_TEXTURE_COORD_ARRAY);
        qglClientActiveTexture(GL_TEXTURE0);
        GL_DisableMultitexture();
    }

    glDisableClientState(GL_COLOR_ARRAY);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);

    if (!gl_mtexable && fullbright && gl_fullbrights.value && alpha == 1.0f) {
        glDepthMask(GL_FALSE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        GL_Bind(skin->fullbright);

        glDrawElements(GL_TRIANGLES, aliashdr->numtris * 3, GL_UNSIGNED_SHORT, indices);
        gl_draw_calls++;
        gl_verts_submitted += numverts;
        gl_indices_submitted += aliashdr->numtris * 3;

        glDisable(GL_BLEND);
        glDepthMask(GL_TRUE);
    }

    if (alpha < 1.0f) {
	glColor4f(1, 1, 1, 1);
        glDepthMask(GL_TRUE);
	glDisable(GL_BLEND);
    }

    glShadeModel(GL_FLAT);
    if (gl_affinemodels.value)
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

    glPopMatrix();

    glDisableClientState(GL_VERTEX_ARRAY);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);

    c_alias_polys += aliashdr->numtris;

    if (_debug_models.value)
        DEBUG_DrawModelInfo(entity, lerpdata.origin);
}

void
R_ResetPlayerTextures()
{
    int playernum;

    for (playernum = 0; playernum < MAX_CLIENTS; playernum++) {
	qgltexture_t *texture = &playertextures[playernum].texture;
	if (texture->base) {
	    texture->base = 0;
	    texture->fullbright = 0;
	    R_TranslatePlayerSkin(playernum);
	}
    }
}

//=============================================================================

/*
===============
R_MarkLeaves
===============
*/
void
R_MarkLeaves(void)
{
    const leafbits_t *pvs;
    leafblock_t check;
    int leafnum;
    mnode_t *node;
    mleaf_t *leaf;

    if (r_oldviewleaf == r_viewleaf && !r_novis.value)
	return;

    if (mirror)
	return;

    r_visframecount++;
    r_oldviewleaf = r_viewleaf;

    /* Pass the zero leaf to get the all visible set */
    leaf = r_novis.value ? cl.worldmodel->leafs : r_viewleaf;

    pvs = Mod_LeafPVS(cl.worldmodel, leaf);
    foreach_leafbit(pvs, leafnum, check) {
	node = (mnode_t *)&cl.worldmodel->leafs[leafnum + 1];
	do {
	    if (node->visframe == r_visframecount)
		break;
	    node->visframe = r_visframecount;
	    node = node->parent;
	} while (node);
    }
}

typedef struct {
    entity_t *entity;
    GLuint texture;
} aliasskinchain_t;

/*
====================
R_DrawEntitiesOnList
====================
*/
static void
R_DrawEntitiesOnList()
{
    entity_t *entity;
    float alpha;
    int i, j;

    if (!r_drawentities.value)
	return;

    /* TODO: make this skin list more efficient */
    aliasskinchain_t *aliasskinchains = alloca(cl_numvisedicts * sizeof(aliasskinchain_t));
    memset(aliasskinchains, 0, cl_numvisedicts * sizeof(aliasskinchain_t));
    int numaliasskins = 0;
    aliasskinchain_t *chain;
    const qgltexture_t *skin;
    entity_t *spritechain = NULL;

    /*
     * Iterate through potentially visible entities:
     * - Drawing brush models as they are found
     * - Group alias models by current skin texture for next pass
     * - Save chain of sprite models for final pass
     */
    for (i = 0; i < cl_numvisedicts; i++) {
        entity = cl_visedicts[i];

        /* Skip fully transparent entities. */
        alpha = ENTALPHA_DECODE(entity->alpha);
        if (!alpha)
            continue;

        switch (entity->model->type) {
            case mod_brush:
                R_DrawDynamicBrushModel(entity);
                break;
            case mod_sprite:
                /* Translucent entities are added to the depth chain for the final pass */
                if (alpha < 1.0f) {
                    DepthChain_AddEntity(&r_depthchain, entity, depthchain_sprite);
                    continue;
                }
                entity->chain = spritechain;
                spritechain = entity;
                break;
            case mod_alias:
                /* Translucent entities are added to the depth chain for the final pass */
                if (alpha < 1.0f) {
                    DepthChain_AddEntity(&r_depthchain, entity, depthchain_alias);
                    continue;
                }
                skin = R_AliasSetupSkin(entity, Mod_Extradata(entity->model));
                for (j = 0; j < numaliasskins; j++) {
                    chain = &aliasskinchains[j];
                    if (skin->base == chain->texture) {
                        entity->chain = chain->entity;
                        chain->entity = entity;
                        break;
                    }
                }
                if (j < numaliasskins)
                    continue;
                chain = &aliasskinchains[numaliasskins++];
                chain->texture = skin->base;
                entity->chain = chain->entity;
                chain->entity = entity;
                break;
        }
    }

    /* Draw alias models in skin order */
    for (i = 0; i < numaliasskins; i++) {
        chain = &aliasskinchains[i];
        entity = chain->entity;
        while (entity) {
            R_AliasDrawModel(entity);
            entity = entity->chain;
        }
    }

    /* Draw sprites last, because of alpha blending */
    for (entity = spritechain; entity; entity = entity->chain)
        R_DrawSpriteModel(entity);
}

/*
=============
R_DrawViewModel
=============
*/
static void
R_DrawViewModel(void)
{
#ifdef NQ_HACK
    if (!r_drawviewmodel.value)
	return;

    if (chase_active.value)
	return;
#endif
#ifdef QW_HACK
    if (!r_drawviewmodel.value || !Cam_DrawViewModel())
	return;
#endif

    if (envmap)
	return;

    if (!r_drawentities.value)
	return;

    if (cl.stats[STAT_ITEMS] & IT_INVISIBILITY)
	return;

    if (cl.stats[STAT_HEALTH] <= 0)
	return;

    if (!cl.viewent.model)
	return;

    // hack the depth range to prevent view model from poking into walls
    glDepthRange(gldepthmin, gldepthmin + 0.3 * (gldepthmax - gldepthmin));
    R_AliasDrawModel(&cl.viewent);
    glDepthRange(gldepthmin, gldepthmax);
}

/*
 * GL_DrawBlendPoly
 * - Render a polygon covering the whole screen
 * - Used for full-screen color blending and approximated gamma correction
 */
static void
GL_DrawBlendPoly(void)
{
    glBegin(GL_QUADS);
    glVertex3f(10, 100, 100);
    glVertex3f(10, -100, 100);
    glVertex3f(10, -100, -100);
    glVertex3f(10, 100, -100);
    glEnd();
}

/*
============
R_PolyBlend
============
*/
static void
R_PolyBlend(void)
{
    float gamma = 1.0;

    if (!VID_IsFullScreen() || (!VID_SetGammaRamp &&
				_gl_allowgammafallback.value)) {
	gamma = v_gamma.value * v_gamma.value;
	if (gamma < 0.25)
	    gamma = 0.25;
	else if (gamma > 1.0)
	    gamma = 1.0;
    }

    if ((gl_polyblend.value && v_blend[3]) || gamma < 1.0) {
	GL_DisableMultitexture();

	glDisable(GL_ALPHA_TEST);
	glEnable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);

	glLoadIdentity();
	glRotatef(-90, 1, 0, 0);	// put Z going up
	glRotatef(90, 0, 0, 1);		// put Z going up

	if (gl_polyblend.value && v_blend[3]) {
	    glColor4fv(v_blend);
	    GL_DrawBlendPoly();
	}
	if (gamma < 1.0) {
	    glBlendFunc(GL_DST_COLOR, GL_ONE_MINUS_SRC_ALPHA);
	    glColor4f(1, 1, 1, gamma);
	    GL_DrawBlendPoly();
	    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}

	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_ALPHA_TEST);
    }
}

static void
R_SetFrustum(void)
{
    int i;

    // FIXME - organise better?
    if (r_lockfrustum.value)
	return;

    if (r_refdef.fov_x == 90) {
	// front side is visible

	VectorAdd(vpn, vright, frustum[0].normal);
	VectorSubtract(vpn, vright, frustum[1].normal);

	VectorAdd(vpn, vup, frustum[2].normal);
	VectorSubtract(vpn, vup, frustum[3].normal);
    } else {
	// rotate VPN right by FOV_X/2 degrees
	RotatePointAroundVector(frustum[0].normal, vup, vpn,
				-(90 - r_refdef.fov_x / 2));
	// rotate VPN left by FOV_X/2 degrees
	RotatePointAroundVector(frustum[1].normal, vup, vpn,
				90 - r_refdef.fov_x / 2);
	// rotate VPN up by FOV_X/2 degrees
	RotatePointAroundVector(frustum[2].normal, vright, vpn,
				90 - r_refdef.fov_y / 2);
	// rotate VPN down by FOV_X/2 degrees
	RotatePointAroundVector(frustum[3].normal, vright, vpn,
				-(90 - r_refdef.fov_y / 2));
    }

    for (i = 0; i < 4; i++) {
	frustum[i].type = PLANE_ANYZ;	// FIXME - true for all angles?
	frustum[i].dist = DotProduct(r_origin, frustum[i].normal);
	frustum[i].signbits = SignbitsForPlane(&frustum[i]);
    }
}

/*
===============
R_SetupFrame
===============
*/
void
R_SetupFrame(void)
{
// don't allow cheats in multiplayer
#ifdef NQ_HACK
    if (cl.maxclients > 1)
	Cvar_Set("r_fullbright", "0");
#endif
#ifdef QW_HACK
    r_fullbright.value = 0;
    r_lightmap.value = 0;
    if (!atoi(Info_ValueForKey(cl.serverinfo, "watervis")))
	r_wateralpha.value = 1;
#endif

    R_AnimateLight();

    r_framecount++;

// build the transformation matrix for the given view angles
    VectorCopy(r_refdef.vieworg, r_origin);

    AngleVectors(r_refdef.viewangles, vpn, vright, vup);

// current viewleaf
    r_oldviewleaf = r_viewleaf;
    if (!r_viewleaf || !r_lockpvs.value)
	r_viewleaf = Mod_PointInLeaf(cl.worldmodel, r_origin);

// color shifting for water, etc.
    V_SetContentsColor(r_viewleaf->contents);
    V_CalcBlend();

// surface cache isn't thrashing (don't have one in GL?)
    r_cache_thrash = false;

// reset count of polys for this frame
    c_brush_polys = 0;
    c_alias_polys = 0;
    c_lightmaps_uploaded = 0;
}


static void
MYgluPerspective(GLdouble fovy, GLdouble aspect,
		 GLdouble zNear, GLdouble zFar)
{
    GLdouble xmin, xmax, ymin, ymax;

    ymax = zNear * tan(fovy * M_PI / 360.0);
    ymin = -ymax;

    xmin = ymin * aspect;
    xmax = ymax * aspect;

    glFrustum(xmin, xmax, ymin, ymax, zNear, zFar);
}


/*
=============
R_SetupGL
=============
*/
static void
R_SetupGL(void)
{
    float screenaspect;
    int x, x2, y2, y, w, h;

    //
    // set up viewpoint
    //
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();

    x = r_refdef.vrect.x * glwidth / vid.width;
    x2 = (r_refdef.vrect.x + r_refdef.vrect.width) * glwidth / vid.width;
    y = (vid.height - r_refdef.vrect.y) * glheight / vid.height;
    y2 = (vid.height - (r_refdef.vrect.y + r_refdef.vrect.height)) * glheight / vid.height;

    // fudge around because of frac screen scale
    // FIXME: well not fix, but figure out why this is done...
    if (x > 0)
	x--;
    if (x2 < glwidth)
	x2++;
    if (y2 < 0)
	y2--;
    if (y < glheight)
	y++;

    w = x2 - x;
    h = y - y2;

    // FIXME: Skybox? Regular Quake sky?
    if (envmap) {
	x = y2 = 0;
	w = h = 256;
    }

    glViewport(glx + x, gly + y2, w, h);
    screenaspect = (float)r_refdef.vrect.width / r_refdef.vrect.height;

    /* TODO: Set depth dynamically based on PVS. */
    MYgluPerspective(r_refdef.fov_y, screenaspect, 4, gl_farclip.value);

    if (mirror) {
	if (mirror_plane->normal[2])
	    glScalef(1, -1, 1);
	else
	    glScalef(-1, 1, 1);
    }

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    glRotatef(-90, 1, 0, 0);	// put Z going up
    glRotatef(90, 0, 0, 1);	// put Z going up
    glRotatef(-r_refdef.viewangles[2], 1, 0, 0);
    glRotatef(-r_refdef.viewangles[0], 0, 1, 0);
    glRotatef(-r_refdef.viewangles[1], 0, 0, 1);
    glTranslatef(-r_refdef.vieworg[0], -r_refdef.vieworg[1],
		 -r_refdef.vieworg[2]);

    glGetFloatv(GL_MODELVIEW_MATRIX, r_world_matrix);

    //
    // set drawing parms
    //
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glEnable(GL_DEPTH_TEST);

    Fog_SetupGL();
}

static void
R_UpdateModelLighting()
{
    int i, dlightnum;
    entity_t *entity;

    if (r_drawflat.value)
        return;

    for (i = 0; i < cl_numvisedicts; i++) {
        entity = cl_visedicts[i];
        if (entity->model->type != mod_brush)
            continue;
        brushmodel_t *brushmodel = BrushModel(entity->model);
        if (!brushmodel->firstmodelsurface)
            continue;
        dlight_t *dlight = cl_dlights;
        for (dlightnum = 0; dlightnum < MAX_DLIGHTS; dlightnum++, dlight++) {
            if (dlight->die < cl.time || !dlight->radius)
                continue;
            mnode_t *node = brushmodel->nodes + brushmodel->hulls[0].firstclipnode;
            R_MarkLights(dlight, 1 << dlightnum, node);
        }
    }
}

/*
 * Walk the depth chain and draw translucenct surfaces/entities from back to front.
 * Intersecting translucent surfaces won't be properly clipped and ordered.
 */
void
R_DrawTranslucency(void)
{
    depthchain_t *entry, *next;
    msurface_t *materialchain, *tail, *surf;
    int material;
    byte alpha;
    entity_t *entity;

    entry = r_depthchain.next;
    while (entry != &r_depthchain) {
        next = entry->next;
        switch (entry->type) {
            case depthchain_alias:
                R_AliasDrawModel(entry->entity);
                break;
            case depthchain_sprite:
                R_DrawSpriteModel(entry->entity);
                break;
            case depthchain_bmodel_instanced:
                R_DrawInstancedTranslucentBmodel(entry->entity);
                break;
            case depthchain_bmodel_static:
                /* Build a chain of consecutive materials with the same alpha */
                materialchain = tail = DepthChain_Surf(entry);
                material = materialchain->material;
                alpha = entry->alpha;
                while (next->type == depthchain_bmodel_static) {
                    surf = DepthChain_Surf(next);
                    if (surf->material != material || next->alpha != alpha)
                        break;
                    tail->chain = surf;
                    tail = surf;
                    next = next->next;
                }
                tail->chain = NULL;

                // DRAW
                glLoadMatrixf(r_world_matrix);
                R_DrawTranslucentChain(entry->entity, materialchain, ENTALPHA_DECODE(alpha));

                break;
            case depthchain_bmodel_transformed:
                /* Build a chain of consecutive materials, from the same entity (transform) */
                materialchain = tail = DepthChain_Surf(entry);
                material = materialchain->material;
                entity = entry->entity;
                while (next->type == depthchain_bmodel_transformed) {
                    surf = DepthChain_Surf(next);
                    if (surf->material != material || next->entity != entity)
                        break;
                    tail->chain = surf;
                    tail = surf;
                    next = next->next;
                }
                tail->chain = NULL;

                // DRAW

                break;
            default:
                break;
        }
        entry = next;
    }
}

/*
================
R_RenderScene

r_refdef must be set before the first call
================
*/
static void
R_RenderScene(void)
{
    gl_draw_calls = 0;
    gl_verts_submitted = 0;
    gl_indices_submitted = 0;
    gl_full_buffers = 0;

    DepthChain_Init(&r_depthchain); // Init the empty depth chain for translucent surfaces/models

    R_SetupFrame();
    R_SetFrustum();
    R_SetupGL();
    R_MarkLeaves();		// done here so we know if we're in water
    R_UpdateModelLighting();    // Update dynamic lightmaps on bmodels

    Fog_EnableGlobalFog();
    R_DrawWorld();		// adds static entities to the list, handles sky surfaces for all brush models
    S_ExtraUpdate();		// don't let sound get messed up if going slow
    R_DrawEntitiesOnList();

    R_DrawTranslucency();       // Draw all the translucent brush/alias/sprite models

    Fog_DisableGlobalFog();

    R_DrawViewModel();
    GL_DisableMultitexture();
    R_DrawParticles();

    if (r_speeds.value == 2.0f) {
        Con_Printf("%4d draw calls, %d tris, %6d verts, %d full buffers\n",
                   gl_draw_calls, gl_indices_submitted / 3, gl_verts_submitted,
                   gl_full_buffers);
    }
}


/*
=============
R_Clear
=============
*/
static void
R_Clear(void)
{
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

    if (r_mirroralpha.value != 1.0) {
	gldepthmin = 0;
	gldepthmax = 0.5;
	glDepthFunc(GL_LEQUAL);
    } else {
	gldepthmin = 0;
	gldepthmax = 1;
	glDepthFunc(GL_LEQUAL);
    }
    glDepthRange(gldepthmin, gldepthmax);

    if (gl_zfix.value) {
	if (gldepthmax > gldepthmin)
	    glPolygonOffset(1, 1);
	else
	    glPolygonOffset(-1, -1);
    }
}

#ifdef NQ_HACK /* Mirrors disabled for now in QW */
/*
=============
R_Mirror
=============
*/
static void
R_Mirror(void)
{
    // TODO: Re-implement this?
#if 0
    float r_base_world_matrix[16];
    float d;
    msurface_t *s;
    entity_t *ent;

    if (!mirror)
	return;

    memcpy(r_base_world_matrix, r_world_matrix, sizeof(r_base_world_matrix));

    d = DotProduct(r_refdef.vieworg,
		   mirror_plane->normal) - mirror_plane->dist;
    VectorMA(r_refdef.vieworg, -2 * d, mirror_plane->normal,
	     r_refdef.vieworg);

    d = DotProduct(vpn, mirror_plane->normal);
    VectorMA(vpn, -2 * d, mirror_plane->normal, vpn);

    r_refdef.viewangles[0] = -asin(vpn[2]) / M_PI * 180;
    r_refdef.viewangles[1] = atan2(vpn[1], vpn[0]) / M_PI * 180;
    r_refdef.viewangles[2] = -r_refdef.viewangles[2];

    /* Add the player to visedicts they can see their reflection */
    ent = &cl_entities[cl.viewentity];
    if (cl_numvisedicts < MAX_VISEDICTS) {
	cl_visedicts[cl_numvisedicts] = ent;
	cl_numvisedicts++;
    }

    gldepthmin = 0.5;
    gldepthmax = 1;
    glDepthRange(gldepthmin, gldepthmax);
    glDepthFunc(GL_LEQUAL);

    R_RenderScene();

    gldepthmin = 0;
    gldepthmax = 0.5;
    glDepthRange(gldepthmin, gldepthmax);
    glDepthFunc(GL_LEQUAL);

    // blend on top
    glEnable(GL_BLEND);
    glMatrixMode(GL_PROJECTION);
    if (mirror_plane->normal[2])
	glScalef(1, -1, 1);
    else
	glScalef(-1, 1, 1);
    glCullFace(GL_FRONT);
    glMatrixMode(GL_MODELVIEW);

    glLoadMatrixf(r_base_world_matrix);

    glColor4f(1, 1, 1, r_mirroralpha.value);
    s = cl.worldmodel->textures[mirrortexturenum]->texturechain;
    for (; s; s = s->texturechain) {
	texture_t *texture = R_TextureAnimation(&r_worldentity, s->texinfo->texture);
	R_RenderBrushPoly(&r_worldentity, s, texture);
    }
    cl.worldmodel->textures[mirrortexturenum]->texturechain = NULL;
    glDisable(GL_BLEND);
    glColor4f(1, 1, 1, 1);
    glCullFace(GL_BACK);
#endif
}
#endif

/*
================
R_RenderView

r_refdef must be set before the first call
================
*/
void
R_RenderView(void)
{
    double time1 = 0, time2;

    if (r_norefresh.value)
	return;

    if (!r_worldentity.model || !cl.worldmodel)
	Sys_Error("%s: NULL worldmodel", __func__);

    if (gl_finish.value || r_speeds.value == 1.0f)
	glFinish();

    if (r_speeds.value == 1.0f) {
	time1 = Sys_DoubleTime();
	c_brush_polys = 0;
	c_alias_polys = 0;
	c_lightmaps_uploaded = 0;
    }

    mirror = false;

    R_Clear();

    // render normal view
    R_RenderScene();

#ifdef NQ_HACK /* Mirrors disabled for now in QW */
    // render mirror view
    R_Mirror();
#endif

    R_PolyBlend();

    if (r_speeds.value == 1.0f) {
//              glFinish ();
	time2 = Sys_DoubleTime();
	Con_Printf("%3i ms  %4i wpoly %4i epoly %4i dlit\n",
		   (int)((time2 - time1) * 1000), c_brush_polys,
		   c_alias_polys, c_lightmaps_uploaded);
    }
}
