/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
Copyright (C) 2010-2014 QuakeSpasm developers

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

#include <math.h>
#include <float.h>

#include "glquake.h"
#include "cmd.h"
#include "common.h"
#include "console.h"
#include "client.h"
#include "sys.h"

texture_t skytextures[6];
char map_skyboxname[256];

static void
Sky_Skyalpha_f(cvar_t *cvar)
{
    int i;
    texture_t *skytexture;

    if (!cl.worldmodel)
        return;

    // TODO: This can be done more efficiently (i.e. precache sky texture and glTexSubImage())
    skytexture = NULL;
    for (i = 0; i < cl.worldmodel->numtextures; i++)
        if (cl.worldmodel->textures[i] && !strncmp(cl.worldmodel->textures[i]->name, "sky", 3))
            skytexture = cl.worldmodel->textures[i];
    if (skytexture)
        R_InitSky(skytexture);
}

cvar_t r_sky_quality = { "r_sky_quality", "16", true };
cvar_t r_fastsky = { "r_fastsky", "0", true };
cvar_t r_skyalpha = { "r_skyalpha", "1", true, .callback = Sky_Skyalpha_f };

qboolean
Sky_LoadSkyboxTextures(const char *skyboxname)
{
    const char *suffix[] = { "rt", "lf", "bk", "ft", "up", "dn" };
    const char *filename, *texturename;
    qboolean found = false;
    int i, mark;
    qpic32_t *skypic;
    texture_t *texture;

    for (i = 0; i < 6; i++) {
        mark = Hunk_LowMark();
        filename = va("gfx/env/%s%s.tga", skyboxname, suffix[i]);
        skypic = TGA_LoadHunkFile(filename, "SKYBOX");
        if (!skypic) {
            filename = va("gfx/env/%s%s.pcx", skyboxname, suffix[i]);
            skypic = PCX_LoadHunkFile(filename, "SKYBOX");
        }
        texture = &skytextures[i];
        if (skypic) {
            texturename = va("@skybox:%s%s", skyboxname, suffix[i]);
            texture->gl_texturenum = GL_AllocTexture32(texturename, skypic, TEXTURE_TYPE_SKYBOX);
            texture->width = skypic->width;
            texture->height = skypic->height;
            GL_Bind(texture->gl_texturenum);
            GL_Upload32(skypic, TEXTURE_TYPE_SKYBOX);
            found = true;
        } else {
            texture->gl_texturenum = r_notexture_mip->gl_texturenum;
        }
        Hunk_FreeToLowMark(mark);
    }

    return found;
}

/*
=================
Sky_NewMap
=================
*/
void
Sky_NewMap()
{
    int i;

    const char *skykeys[] = { "sky", "skyname", "qlsky" };
    for (i = 0; i < ARRAY_SIZE(skykeys); i++) {
        Entity_ValueForKey(cl.worldmodel->entities, skykeys[i], map_skyboxname, sizeof(map_skyboxname));
        if (map_skyboxname[0]) {
            qboolean succeeded = Sky_LoadSkyboxTextures(map_skyboxname);
            if (!succeeded) {
                // Fallback to regular sky...
                // TODO: Use something a bit nicer than just the name string to flag this
                map_skyboxname[0] = 0;
            }
            break;
        }
    }
}

static void
Sky_SkyCommand_f()
{
    switch (Cmd_Argc()) {
	case 1:
            Con_Printf("\"sky\" is \"%s\"\n", map_skyboxname);
            break;
	case 2:
            qstrncpy(map_skyboxname, Cmd_Argv(1), sizeof(map_skyboxname));
            qboolean succeeded = Sky_LoadSkyboxTextures(map_skyboxname);
            if (!succeeded) {
                // Fallback to regular sky...
                // TODO: Use something a bit nicer than just the name string to flag this
                map_skyboxname[0] = 0;
            }
            break;
	default:
            Con_Printf("usage: sky <skyname>\n");
    }
}

static struct stree_root *
Sky_SkyCommand_Arg_f(const char *arg)
{
    struct stree_root *root;

    root = Z_Malloc(sizeof(struct stree_root));
    if (root) {
	*root = STREE_ROOT;
	STree_AllocInit();
	COM_ScanDir(root, "gfx/env", arg, "rt.tga", true);
	COM_ScanDir(root, "gfx/env", arg, "rt.pcx", true);
    }

    return root;
}

void
Sky_Init()
{
    Cvar_RegisterVariable(&r_sky_quality);
    Cvar_RegisterVariable (&r_fastsky);
    Cvar_RegisterVariable (&r_skyalpha);

    Cmd_AddCommand("sky", Sky_SkyCommand_f);
    Cmd_SetCompletion("sky", Sky_SkyCommand_Arg_f);
}


// ----------------------------------------------------------------------
// Skybox Bounds Checking
//
// Limit the amount of the skybox that we draw (at least in the
// sub-divided scrolling sky case) by calculating what parts of the
// skybox are covered by visible sky brush polys.
//
// The bounds are expressed as a min/max S/T coordinate on the skybox
// face.  Note that the coordinate space is from -1.0 to 1.0 since the
// math is slightly simpler than conversion to a 0 - 1 range.
// ----------------------------------------------------------------------

/*
 * Clip plane normals for clipping sky polys against the sky box.
 */
#define SQRT2_2 0.70710678f
static const vec3_t skyclip_normals[6] = {
    {  SQRT2_2,  SQRT2_2,       0 },
    {  SQRT2_2, -SQRT2_2,       0 },
    {        0, -SQRT2_2, SQRT2_2 },
    {        0,  SQRT2_2, SQRT2_2 },
    {  SQRT2_2,        0, SQRT2_2 },
    { -SQRT2_2,        0, SQRT2_2 },
};

static inline void
Sky_AddToBounds(float s, float t, float mins[2], float maxs[2])
{
    if (s < mins[0])
        mins[0] = s;
    if (s > maxs[0])
        maxs[0] = s;
    if (t < mins[1])
        mins[1] = t;
    if (t > maxs[1])
        maxs[1] = t;
}

typedef struct {
    int numverts;
    vec3_t verts[];
} clippoly_t;

static void
Sky_AddClippedPolyToBounds(clippoly_t *skypoly, float mins[6][2], float maxs[6][2])
{
    int i, facenum;
    const float *vert;
    float s, t, dist;
    vec3_t bias = { 0, 0, 0 };
    vec3_t sizes;

    vert = skypoly->verts[0];
    for (i = 0; i < skypoly->numverts; i++, vert += 3)
        VectorAdd(vert, bias, bias);

    /* Decide which skybox face the poly maps onto */
    sizes[0] = fabsf(bias[0]);
    sizes[1] = fabsf(bias[1]);
    sizes[2] = fabsf(bias[2]);
    if (sizes[0] > sizes[1] && sizes[0] > sizes[2])
        facenum = (bias[0] < 0) ? 1 : 0;
    else if (sizes[1] > sizes[2] && sizes[1] > sizes[0])
        facenum = (bias[1] < 0) ? 3 : 2;
    else
        facenum = (bias[2] < 0) ? 5 : 4;

    vert = skypoly->verts[0];
    switch (facenum) {
        case 0:
            for (i = 0; i < skypoly->numverts; i++, vert += 3) {
                dist = vert[0];
                if (fabsf(dist) > ON_EPSILON) {
                    s = -vert[1] / dist;
                    t = vert[2] / dist;
                    Sky_AddToBounds(s, t, mins[0], maxs[0]);
                }
            }
            break;
        case 1:
            for (i = 0; i < skypoly->numverts; i++, vert += 3) {
                dist = -vert[0];
                if (fabsf(dist) > ON_EPSILON) {
                    s = vert[1] / dist;
                    t = vert[2] / dist;
                    Sky_AddToBounds(s, t, mins[1], maxs[1]);
                }
            }
            break;
        case 2:
            for (i = 0; i < skypoly->numverts; i++, vert += 3) {
                dist = vert[1];
                if (fabsf(dist) > ON_EPSILON) {
                    s = vert[0] / dist;
                    t = vert[2] / dist;
                    Sky_AddToBounds(s, t, mins[2], maxs[2]);
                }
            }
            break;
        case 3:
            for (i = 0; i < skypoly->numverts; i++, vert += 3) {
                dist = -vert[1];
                if (fabsf(dist) > ON_EPSILON) {
                    s = -vert[0] / dist;
                    t = vert[2] / dist;
                    Sky_AddToBounds(s, t, mins[3], maxs[3]);
                }
            }
            break;
        case 4:
            for (i = 0; i < skypoly->numverts; i++, vert += 3) {
                dist = vert[2];
                if (fabsf(dist) > ON_EPSILON) {
                    s = -vert[1] / dist;
                    t = -vert[0] / dist;
                    Sky_AddToBounds(s, t, mins[4], maxs[4]);
                }
            }
            break;
        case 5:
            for (i = 0; i < skypoly->numverts; i++, vert += 3) {
                dist = -vert[2];
                if (fabsf(dist) > ON_EPSILON) {
                    s = -vert[1] / dist;
                    t = vert[0] / dist;
                    Sky_AddToBounds(s, t, mins[5], maxs[5]);
                }
            }
            break;
    }
}

#define CLIP_SIDE_FRONT 0
#define CLIP_SIDE_BACK  1
#define CLIP_SIDE_ON    2

static void
Sky_ClipPolyForBounds(clippoly_t *skypoly, float mins[6][2], float maxs[6][2], int clipnum)
{
    int i, j;
    int counts[3];
    const float *normal;
    float *vert;
    float dist;
    int *sides;
    float *dists;

    sides = alloca((skypoly->numverts + 1) * sizeof(int));
    dists = alloca((skypoly->numverts + 1) * sizeof(float));

 nextClip:
    normal = skyclip_normals[clipnum];
    counts[0] = counts[1] = counts[2] = 0;
    vert = skypoly->verts[0];
    for (i = 0; i < skypoly->numverts; i++, vert += 3) {
        dist = DotProduct(vert, normal);
        if (dist > ON_EPSILON)
            sides[i] = CLIP_SIDE_FRONT;
        else if (dist < -ON_EPSILON)
            sides[i] = CLIP_SIDE_BACK;
        else
            sides[i] = CLIP_SIDE_ON;
        counts[sides[i]]++;
        dists[i] = dist;
    }
    sides[i] = sides[0];
    dists[i] = dists[0];

    /* If not clipped, check against the next plane, unmodified */
    if (!counts[CLIP_SIDE_FRONT] || !counts[CLIP_SIDE_BACK]) {
        if (++clipnum < 6)
            goto nextClip;
        Sky_AddClippedPolyToBounds(skypoly, mins, maxs);
        return;
    }

    /*
     * Create new poly and recursively clip for each side of the split.
     */
    clippoly_t *newpoly = alloca(offsetof(clippoly_t, verts[skypoly->numverts + 4]));
    int side;
    for (side = CLIP_SIDE_FRONT; side <= CLIP_SIDE_BACK; side++) {
        newpoly->numverts = 0;
        vert = skypoly->verts[0];
        for (i = 0; i < skypoly->numverts; i++, vert += 3) {
            if (sides[i] == CLIP_SIDE_ON) {
                VectorCopy(vert, newpoly->verts[newpoly->numverts]);
                newpoly->numverts++;
                continue;
            }
            if (sides[i] == side) {
                VectorCopy(vert, newpoly->verts[newpoly->numverts]);
                newpoly->numverts++;
            }
            if (sides[i + 1] == CLIP_SIDE_ON || sides[i + 1] == sides[i])
                continue;

            /* Generate split point */
            float fraction = dists[i] / (dists[i] - dists[i + 1]);
            float *vert2 = skypoly->verts[(i + 1) % skypoly->numverts];
            for (j = 0; j < 3; j++)
                newpoly->verts[newpoly->numverts][j] = vert[j] + fraction * (vert2[j] - vert[j]);
            newpoly->numverts++;
        }
        if (clipnum == 5)
            Sky_AddClippedPolyToBounds(newpoly, mins, maxs);
        else
            Sky_ClipPolyForBounds(newpoly, mins, maxs, clipnum + 1);
    }
}

/*
 * Update the s/t mins/maxes for each skybox face that this poly would
 * project onto from the current viewpoint.
 */
void
Sky_AddPolyToSkyboxBounds(const glpoly_t *poly, float mins[6][2], float maxs[6][2])
{
    int i;
    clippoly_t *skypoly;

    /* Translate the poly into viewspace */
    skypoly = alloca(offsetof(clippoly_t, verts[poly->numverts]));
    skypoly->numverts = poly->numverts;
    for (i = 0; i < poly->numverts; i++)
        VectorSubtract(poly->verts[i], r_origin, skypoly->verts[i]);

    Sky_ClipPolyForBounds(skypoly, mins, maxs, 0);
}

void
Sky_InitBounds(float mins[6][2], float maxs[6][2])
{
    int facenum;

    for (facenum = 0; facenum < 6; facenum++) {
        mins[facenum][0] = mins[facenum][1] = FLT_MAX;
        maxs[facenum][0] = maxs[facenum][1] = -FLT_MAX;
    }
}

// ----------------------------------------------------------------------

vec3_t skyflatcolor;

/*
=============
R_InitSky

A sky texture is 256*128, with the right side being a masked overlay
==============
*/
void
R_InitSky(texture_t *mt)
{
    byte *src = (byte *)mt + mt->offsets[0];
    qpic8_t pic;

    /* Set up the pic to describe the sky texture */
    pic.width = 128;
    pic.height = 128;
    pic.stride = 256;

    /* Create the solid layer */
    pic.pixels = src + 128;
    mt->gl_texturenum = GL_LoadTexture8(va("@%s:background", mt->name), &pic, TEXTURE_TYPE_SKY_BACKGROUND);

    /* Create the alpha layer */
    pic.pixels = src;
    byte alpha = qclamp(r_skyalpha.value, 0.0f, 1.0f) * 255.0f;
    mt->gl_texturenum_alpha = GL_LoadTexture8_Alpha(va("@%s:foreground", mt->name), &pic, TEXTURE_TYPE_SKY_FOREGROUND, alpha);

    /*
     * Calculate FitzQuake/QuakeSpasm equivalent r_fastsky color as the average
     * of all opaque foreground colors.
     */
    skyflatcolor[0] = skyflatcolor[1] = skyflatcolor[2] = 0;
    int i, j;
    int count = 0;
    for (i = 0; i < pic.height; i++, src += pic.stride - pic.width) {
        for (j = 0; j < pic.width; j++, src++) {
            if (!*src)
                continue;
            qpixel32_t pixel = qpal_alpha_zero.colors[*src];
            skyflatcolor[0] += pixel.c.red;
            skyflatcolor[1] += pixel.c.green;
            skyflatcolor[2] += pixel.c.blue;
            count++;
        }
    }
    VectorScale(skyflatcolor, 1.0f / (255 * count), skyflatcolor);
}
