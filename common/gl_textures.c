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

#include "cmd.h"
#include "console.h"
#include "crc.h"
#include "cvar.h"
#include "glquake.h"
#include "qpic.h"
#include "sys.h"

#ifdef NQ_HACK
#include "host.h"
#endif
#ifdef QW_HACK
#include "client.h"
#endif

// FIXME - should I let this get larger, with view to enhancements?
cvar_t gl_max_size = { "gl_max_size", "1024" };

static cvar_t gl_nobind = { "gl_nobind", "0" };
static cvar_t gl_picmip = { "gl_picmip", "0" };

typedef struct {
    GLuint texnum;
    int width, height;
    enum texture_type type;
    unsigned short crc;		// CRC for texture cache matching
    char name[MAX_QPATH];
} gltexture_t;

#define	MAX_GLTEXTURES	4096
static gltexture_t gltextures[MAX_GLTEXTURES];
static int numgltextures;

void GL_FreeTextures()
{
    int i;

    GL_Bind(0); /* sets currenttexture to zero */
    for (i = 0; i < numgltextures; i++) {
        glDeleteTextures(1, &gltextures[i].texnum);
    }
    numgltextures = 0;
}

void
GL_Bind(int texnum)
{
    if (gl_nobind.value)
	texnum = charset_texture;
    if (currenttexture == texnum)
	return;
    currenttexture = texnum;

    glBindTexture(GL_TEXTURE_2D, texnum);
}


typedef struct {
    const char *name;
    GLenum min_filter;
    GLenum mag_filter;
} glmode_t;

static glmode_t *glmode;

static glmode_t gl_texturemodes[] = {
    { "gl_nearest", GL_NEAREST, GL_NEAREST },
    { "gl_linear", GL_LINEAR, GL_LINEAR },
    { "gl_nearest_mipmap_nearest", GL_NEAREST_MIPMAP_NEAREST, GL_NEAREST },
    { "gl_linear_mipmap_nearest", GL_LINEAR_MIPMAP_NEAREST, GL_LINEAR },
    { "gl_nearest_mipmap_linear", GL_NEAREST_MIPMAP_LINEAR, GL_NEAREST },
    { "gl_linear_mipmap_linear", GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR }
};


/*
===============
Draw_TextureMode_f
===============
*/
static void
GL_TextureMode_f(void)
{
    int i;
    gltexture_t *glt;

    if (Cmd_Argc() == 1) {
	Con_Printf("%s\n", glmode->name);
	return;
    }

    for (i = 0; i < ARRAY_SIZE(gl_texturemodes); i++) {
	if (!strcasecmp(gl_texturemodes[i].name, Cmd_Argv(1))) {
	    glmode = &gl_texturemodes[i];
	    break;
	}
    }
    if (i == ARRAY_SIZE(gl_texturemodes)) {
	Con_Printf("bad filter name\n");
	return;
    }

    /*
     * Change all the existing mipmap texture objects
     */
    for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
        if (glt->type == TEXTURE_TYPE_LIGHTMAP)
            continue; /* Lightmap filter is always GL_LINEAR */
        if (glt->type == TEXTURE_TYPE_NOTEXTURE)
            continue; /* Notexture is always GL_NEAREST */
        GL_Bind(glt->texnum);
        if (texture_properties[glt->type].mipmap) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glmode->min_filter);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glmode->mag_filter);
        } else {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glmode->mag_filter);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glmode->mag_filter);
        }
    }
}

static struct stree_root *
GL_TextureMode_Arg_f(const char *arg)
{
    int i, arg_len;
    struct stree_root *root;

    root = Z_Malloc(sizeof(struct stree_root));
    if (root) {
	*root = STREE_ROOT;
	STree_AllocInit();
	arg_len = arg ? strlen(arg) : 0;
	for (i = 0; i < ARRAY_SIZE(gl_texturemodes); i++) {
	    if (!arg || !strncasecmp(gl_texturemodes[i].name, arg, arg_len))
		STree_InsertAlloc(root, gl_texturemodes[i].name, false);
	}
    }
    return root;
}

/*
================
GL_FindTexture
================
*/
int
GL_FindTexture(const char *name)
{
    int i;
    gltexture_t *glt;

    for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
	if (!strcmp(name, glt->name))
	    return gltextures[i].texnum;
    }

    return -1;
}

/*
 * Uploads a 32-bit RGBA texture.  The original pixels are destroyed during
 * the scaling/mipmap process.  When done, the width and height the source pic
 * are set to the values used to upload the (miplevel 0) texture.
 *
 * This matters in the case of non-power-of-two HUD or skin textures
 * getting expanded, since the texture coordinates need to be adjusted
 * accordingly when drawing.
 */
void
GL_Upload32(qpic32_t *pic, enum texture_type type)
{
    GLint internal_format;
    qpic32_t *scaled;
    int width, height, mark, miplevel, picmip;

    /* This is not written for lightmaps! */
    assert(type != TEXTURE_TYPE_LIGHTMAP);

    if (!gl_npotable || !gl_npot.value) {
	/* find the next power-of-two size up */
	width = 1;
	while (width < pic->width)
	    width <<= 1;
	height = 1;
	while (height < pic->height)
	    height <<= 1;
    } else {
	width = pic->width;
	height = pic->height;
    }

    mark = Hunk_LowMark();

    /* Begin by expanding the texture if needed */
    if (width != pic->width || height != pic->height) {
	scaled = QPic32_Alloc(width, height);
        switch (type) {
            case TEXTURE_TYPE_HUD:
            case TEXTURE_TYPE_ALIAS_SKIN:
            case TEXTURE_TYPE_ALIAS_SKIN_FULLBRIGHT:
            case TEXTURE_TYPE_PLAYER_SKIN:
            case TEXTURE_TYPE_PLAYER_SKIN_FULLBRIGHT:
		QPic32_Expand(pic, scaled);
                break;
            default:
                QPic32_Stretch(pic, scaled);
                break;
	}
    } else {
	scaled = pic;
    }

    /* Allow some textures to be crunched down by player preference */
    if (texture_properties[type].playermip) {
        picmip = qmax(0, (int)gl_playermip.value);
    } else if (texture_properties[type].picmip) {
        picmip = qmax(0, (int)gl_picmip.value);
    } else {
        picmip = 0;
    }
    while (picmip) {
	if (width == 1 && height == 1)
	    break;
	width = qmin(1, width >> 1);
	height = qmin(1, height >> 1);
	QPic32_MipMap(scaled, texture_properties[type].alpha_op);
	picmip--;
    }

    /* Set the internal format */
    if (texture_properties[type].palette->alpha) {
        internal_format = gl_alpha_format;
    } else {
        internal_format = gl_solid_format;
    }

    /* Upload with or without mipmaps, depending on type */
    if (texture_properties[type].mipmap) {
        miplevel = 0;
        while (1) {
            glTexImage2D(GL_TEXTURE_2D, miplevel, internal_format,
                         scaled->width, scaled->height, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, scaled->pixels);
            if (scaled->width == 1 && scaled->height == 1)
                break;

            QPic32_MipMap(scaled, texture_properties[type].alpha_op);
            miplevel++;
        }
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glmode->min_filter);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glmode->mag_filter);
    } else {
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format,
                     scaled->width, scaled->height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, scaled->pixels);
        if (type == TEXTURE_TYPE_NOTEXTURE) {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        } else {
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glmode->mag_filter);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glmode->mag_filter);
        }
    }

    /* Set texture wrap mode */
    GLenum wrap = texture_properties[type].repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE;
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);

    Hunk_FreeToLowMark(mark);

    /* Pass back the width and height used on the scaled texture that was uploaded */
    pic->width = width;
    pic->height = height;
}

/*
===============
GL_Upload8

Sets the actual uploaded width/height in the original pic, which matters for skin
textures that get expanded up to the nearest power-of-two size.

The alpha version will scale the texture alpha channel by a factor of (alpha / 255).
===============
*/
void
GL_Upload8_Alpha(qpic8_t *pic, enum texture_type type, byte alpha)
{
    const qpalette32_t *palette = texture_properties[type].palette;
    enum qpic_alpha_operation alpha_op = texture_properties[type].alpha_op;
    qpic32_t *pic32;
    int mark;

    mark = Hunk_LowMark();

    pic32 = QPic32_Alloc(pic->width, pic->height);
    QPic_8to32(pic, pic32, palette, alpha_op);
    if (alpha != 255)
        QPic32_ScaleAlpha(pic32, alpha);
    GL_Upload32(pic32, type);

    pic->width = pic32->width;
    pic->height = pic32->height;

    Hunk_FreeToLowMark(mark);
}

void
GL_Upload8(qpic8_t *pic, enum texture_type type)
{
    GL_Upload8_Alpha(pic, type, 255);
}

void
GL_Upload8_Translate(qpic8_t *pic, enum texture_type type, const byte *translation)
{
    const byte *source;
    byte *pixels, *dest;
    qpic8_t translated_pic;
    int mark, i, j;

    mark = Hunk_LowMark();

    source = pic->pixels;
    pixels = dest = Hunk_AllocName(pic->width * pic->height, "transpic");
    for (i = 0; i < pic->height; i++) {
        for (j = 0; j < pic->width; j++) {
            *dest++ = translation[*source++];
        }
        source += pic->stride - pic->width;
    }

    translated_pic.width = pic->width;
    translated_pic.height = pic->height;
    translated_pic.stride = pic->width;
    translated_pic.pixels = pixels;

    GL_Upload8(&translated_pic, type);

    Hunk_FreeToLowMark(mark);
}

void
GL_Upload8_GLPic(glpic_t *glpic)
{
    const qpalette32_t *palette = texture_properties[TEXTURE_TYPE_HUD].palette;
    enum qpic_alpha_operation alpha_op = texture_properties[TEXTURE_TYPE_HUD].alpha_op;
    qpic32_t *pic32;
    int mark;

    mark = Hunk_LowMark();

    pic32 = QPic32_Alloc(glpic->pic.width, glpic->pic.height);
    QPic_8to32(&glpic->pic, pic32, palette, alpha_op);
    GL_Upload32(pic32, TEXTURE_TYPE_HUD);

    glpic->sl = 0;
    glpic->sh = qmin(1.0f, (float)glpic->pic.width / (float)pic32->width);
    glpic->tl = 0;
    glpic->th = qmin(1.0f, (float)glpic->pic.height / (float)pic32->height);

    Hunk_FreeToLowMark(mark);
}

/*
================
GL_AllocTexture

Return an existing texture reference if we already have one for this
name (and the CRC matches).  Otherwise allocate and configure a new
one.
================
*/
static int
GL_AllocTexture(const char *name, unsigned short crc, int width, int height, enum texture_type type)
{
    int i;
    gltexture_t *glt;

    /* Check if the texture is already present, if so then return it. */
    for (i = 0, glt = gltextures; i < numgltextures; i++, glt++) {
        if (!strcmp(name, glt->name)) {
            if (crc != glt->crc)
                goto GL_LoadTexture_setup;
            if (width != glt->width || height != glt->height)
                goto GL_LoadTexture_setup;
            return glt->texnum;
        }
    }

    if (numgltextures == MAX_GLTEXTURES)
	Sys_Error("numgltextures == MAX_GLTEXTURES");

    glt = &gltextures[numgltextures++];
    qstrncpy(glt->name, name, sizeof(glt->name));
    glGenTextures(1, &glt->texnum);

  GL_LoadTexture_setup:
    glt->crc = crc;
    glt->width = width;
    glt->height = height;
    glt->type = type;

    return glt->texnum;
}

int
GL_AllocTexture8(const char *name, const qpic8_t *pic, enum texture_type type)
{
    unsigned short crc = CRC_Block(pic->pixels, pic->width * pic->height * sizeof(pic->pixels[0]));
    return GL_AllocTexture(name, crc, pic->width, pic->height, type);
}

int
GL_AllocTexture32(const char *name, const qpic32_t *pic, enum texture_type type)
{
    unsigned short crc = CRC_Block(pic->pixels, pic->width * pic->height * sizeof(pic->pixels[0]));
    return GL_AllocTexture(name, crc, pic->width, pic->height, type);
}

int
GL_LoadTexture8_Alpha(const char *name, qpic8_t *pic, enum texture_type type, byte alpha)
{
    int texnum = GL_AllocTexture8(name, pic, type);

    if (!isDedicated) {
	GL_Bind(texnum);
	GL_Upload8_Alpha(pic, type, alpha);
    }

    return texnum;
}

int
GL_LoadTexture8(const char *name, qpic8_t *pic, enum texture_type type)
{
    return GL_LoadTexture8_Alpha(name, pic, type, 255);
}

int
GL_LoadTexture8_GLPic(const char *name, glpic_t *glpic)
{
    int texnum = GL_AllocTexture8(name, &glpic->pic, TEXTURE_TYPE_HUD);

    if (!isDedicated) {
	GL_Bind(texnum);
	GL_Upload8_GLPic(glpic);
    }

    return texnum;
}


static void
GL_PrintTextures_f(void)
{
    int i;
    gltexture_t *texture;

    for (i = 0, texture = gltextures; i < numgltextures; i++, texture++) {
	Con_Printf(" %s\n", texture->name);
    }
    Con_Printf("%d textures loaded\n", numgltextures);
}

void
GL_InitTextures(void)
{
    GLint max_size;

    glmode = gl_texturemodes;

    Cvar_RegisterVariable(&gl_nobind);
    Cvar_RegisterVariable(&gl_max_size);
    Cvar_RegisterVariable(&gl_picmip);

    // FIXME - could do better to check on each texture upload with
    //         GL_PROXY_TEXTURE_2D
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_size);
    if (gl_max_size.value > max_size) {
	Con_DPrintf("Reducing gl_max_size from %d to %d\n",
		    (int)gl_max_size.value, max_size);
	Cvar_Set("gl_max_size", va("%d", max_size));
    }

    Cmd_AddCommand("gl_texturemode", GL_TextureMode_f);
    Cmd_SetCompletion("gl_texturemode", GL_TextureMode_Arg_f);
    Cmd_AddCommand("gl_printtextures", GL_PrintTextures_f);

    GL_LoadNoTexture();
}
