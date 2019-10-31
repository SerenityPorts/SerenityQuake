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
// r_misc.c

#include "cmd.h"
#include "console.h"
#include "developer.h"
#include "glquake.h"
#include "protocol.h"
#include "quakedef.h"
#include "sbar.h"
#include "screen.h"
#include "sys.h"

// FIXME - should only be needed in r_part.c or here, not both.
GLuint particletexture;

/*
==================
R_InitTextures
==================
*/
void
R_InitTextures(void)
{
    int x, y, m;
    byte *dest;

// create a simple checkerboard texture for the default
    r_notexture_mip = Hunk_AllocName(sizeof(texture_t) + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2, "@notexture");
    qstrncpy(r_notexture_mip->name, "@notexture", sizeof(r_notexture_mip->name));

    r_notexture_mip->width = r_notexture_mip->height = 16;
    r_notexture_mip->offsets[0] = sizeof(texture_t);
    r_notexture_mip->offsets[1] = r_notexture_mip->offsets[0] + 16 * 16;
    r_notexture_mip->offsets[2] = r_notexture_mip->offsets[1] + 8 * 8;
    r_notexture_mip->offsets[3] = r_notexture_mip->offsets[2] + 4 * 4;

    for (m = 0; m < 4; m++) {
	dest = (byte *)r_notexture_mip + r_notexture_mip->offsets[m];
	for (y = 0; y < (16 >> m); y++) {
	    for (x = 0; x < (16 >> m); x++) {
		if ((y < (8 >> m)) ^ (x < (8 >> m)))
		    *dest++ = 0;
		else
		    *dest++ = 0xff;
	    }
	}
    }
}

void
GL_LoadNoTexture()
{
    qpic8_t pic = {
        .width = r_notexture_mip->width,
        .stride = r_notexture_mip->width,
        .height = r_notexture_mip->height,
        .pixels = (byte *)(r_notexture_mip + 1),
    };

    r_notexture_mip->gl_texturenum = GL_LoadTexture8(r_notexture_mip->name, &pic, TEXTURE_TYPE_NOTEXTURE);
}

static const byte dottexture[8][8] = {
    {0, 1, 1, 0, 0, 0, 0, 0},
    {1, 1, 1, 1, 0, 0, 0, 0},
    {1, 1, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
};

void
R_InitParticleTexture(void)
{
    byte pixels[8][8][4];
    int x, y;

    //
    // particle texture
    //
    for (x = 0; x < 8; x++) {
	for (y = 0; y < 8; y++) {
	    pixels[y][x][0] = 255;
	    pixels[y][x][1] = 255;
	    pixels[y][x][2] = 255;
	    pixels[y][x][3] = dottexture[x][y] * 255;
	}
    }

    const qpic8_t particle = {
        .width = 8,
        .height = 8,
        .pixels = &dottexture[0][0],
    };
    particletexture = GL_AllocTexture8("@particle", &particle, TEXTURE_TYPE_PARTICLE);
    GL_Bind(particletexture);

    glTexImage2D(GL_TEXTURE_2D, 0, gl_alpha_format, 8, 8, 0, GL_RGBA,
		 GL_UNSIGNED_BYTE, pixels);

    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

/*
===============
R_Envmap_f

Grab six views for environment mapping tests
===============
*/
static void
R_Envmap_f(void)
{
    byte buffer[256 * 256 * 4];

    glDrawBuffer(GL_FRONT);
    glReadBuffer(GL_FRONT);
    envmap = true;

    r_refdef.vrect.x = 0;
    r_refdef.vrect.y = 0;
    r_refdef.vrect.width = 256;
    r_refdef.vrect.height = 256;

    r_refdef.viewangles[0] = 0;
    r_refdef.viewangles[1] = 0;
    r_refdef.viewangles[2] = 0;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env0.rgb", buffer, sizeof(buffer));

    r_refdef.viewangles[1] = 90;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env1.rgb", buffer, sizeof(buffer));

    r_refdef.viewangles[1] = 180;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env2.rgb", buffer, sizeof(buffer));

    r_refdef.viewangles[1] = 270;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env3.rgb", buffer, sizeof(buffer));

    r_refdef.viewangles[0] = -90;
    r_refdef.viewangles[1] = 0;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env4.rgb", buffer, sizeof(buffer));

    r_refdef.viewangles[0] = 90;
    r_refdef.viewangles[1] = 0;
    GL_BeginRendering(&glx, &gly, &glwidth, &glheight);
    R_RenderView();
    glReadPixels(0, 0, 256, 256, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
    COM_WriteFile("env5.rgb", buffer, sizeof(buffer));

    envmap = false;
    glDrawBuffer(GL_BACK);
    glReadBuffer(GL_BACK);
    GL_EndRendering();
}

// FIXME - locate somewhere else?
cvar_t r_lockpvs = { "r_lockpvs", "0" };
cvar_t r_lockfrustum = { "r_lockfrustum", "0" };
cvar_t r_drawflat = { "r_drawflat", "0" };

static void
GL_Extensions_f()
{
    const char *extensions = (char *)glGetString(GL_EXTENSIONS);
    while (extensions) {
	const char *next = strchr(extensions, ' ');
	int length = next ? (next - extensions) : strlen(extensions);
	Con_Printf("%.*s\n", length, extensions);
	if (!next)
	    break;
	extensions = next;
	while (extensions[0] == ' ')
	    extensions++;
    }
}

/*
===============
R_Init
===============
*/
void
R_Init(void)
{
    Cmd_AddCommand("gl_extensions", GL_Extensions_f);
    Cmd_AddCommand("envmap", R_Envmap_f);
    Cmd_AddCommand("pointfile", R_ReadPointFile_f);
    Cmd_AddCommand("timerefresh", R_TimeRefresh_f);

    Cvar_RegisterVariable(&r_speeds);
    Cvar_RegisterVariable(&r_fullbright);
    Cvar_RegisterVariable(&r_drawentities);
    Cvar_RegisterVariable(&r_drawviewmodel);
    Cvar_RegisterVariable(&r_drawflat);

    Cvar_RegisterVariable(&r_lerpmodels);
    Cvar_RegisterVariable(&r_lerpmove);
    Cvar_RegisterVariable(&r_lockpvs);
    Cvar_RegisterVariable(&r_lockfrustum);

    Cvar_RegisterVariable(&r_norefresh);
    Cvar_RegisterVariable(&r_lightmap);
    Cvar_RegisterVariable(&r_mirroralpha);

    Cvar_RegisterVariable(&r_wateralpha);
    Cvar_RegisterVariable(&r_slimealpha);
    Cvar_RegisterVariable(&r_lavaalpha);
    Cvar_RegisterVariable(&r_telealpha);

    Cvar_RegisterVariable(&r_dynamic);
    Cvar_RegisterVariable(&r_novis);
    Cvar_RegisterVariable(&r_waterwarp);

    Cvar_RegisterVariable(&gl_finish);
    Cvar_RegisterVariable(&gl_texsort);
    Cvar_RegisterVariable(&gl_fullbrights);
    Cvar_RegisterVariable(&gl_farclip);

    Cvar_RegisterVariable(&_gl_allowgammafallback);
    Cvar_RegisterVariable(&_gl_drawhull);

    Cvar_RegisterVariable(&gl_smoothmodels);
    Cvar_RegisterVariable(&gl_affinemodels);
    Cvar_RegisterVariable(&gl_polyblend);
    Cvar_RegisterVariable(&gl_playermip);
    Cvar_RegisterVariable(&gl_nocolors);
    Cvar_RegisterVariable(&gl_zfix);

    Cvar_RegisterVariable(&gl_keeptjunctions);
    Cvar_RegisterVariable(&gl_reporttjunctions);

#ifdef NQ_HACK
    Cvar_RegisterVariable(&gl_doubleeyes);
#endif
#ifdef QW_HACK
    Cvar_RegisterVariable(&r_netgraph);
#endif

    Cvar_RegisterVariable(&_debug_models);

    R_InitParticles();
    R_InitParticleTexture();
    R_InitTranslationTable();

    Fog_Init();
    Sky_Init();
}


/*
===============
R_SetVrect
===============
*/
void
R_SetVrect(const vrect_t *in, vrect_t *out, int lineadj)
{
    int h;
    float size;
    qboolean full;

    if (scr_scale != 1.0f) {
        lineadj = (int)(lineadj * scr_scale);
    }

#ifdef NQ_HACK
    full = (scr_viewsize.value >= 120.0f);
#endif
#ifdef QW_HACK
    full = (!cl_sbar.value && scr_viewsize.value >= 100.0f);
#endif
    size = qmin(scr_viewsize.value, 100.0f);

    /* Hide the status bar during intermission */
    if (cl.intermission) {
	full = true;
	size = 100.0;
	lineadj = 0;
    }
    size /= 100.0;

    if (full)
	h = in->height;
    else
	h = in->height - lineadj;

    out->width = in->width * size;
    if (out->width < 96) {
	size = 96.0 / in->width;
	out->width = 96;	// min for icons
    }

    out->height = in->height * size;
    if (!full) {
	if (out->height > in->height - lineadj)
	    out->height = in->height - lineadj;
    } else if (out->height > in->height)
	out->height = in->height;

    out->x = (in->width - out->width) / 2;
    if (full)
	out->y = 0;
    else
	out->y = (h - out->height) / 2;
}

/*
===============
R_ViewChanged

Called every time the vid structure or r_refdef changes.
Guaranteed to be called before the first refresh
===============
*/
void
R_ViewChanged(const vrect_t *vrect, int lineadj, float aspect)
{
    R_SetVrect(vrect, &r_refdef.vrect, lineadj);
}

/*
===============
R_TranslatePlayerSkin

Translates a skin texture by the per-player color lookup
===============
*/
void
R_TranslatePlayerSkin(int playernum)
{
    const byte *original;
    const byte *translation;
    player_info_t *player;
    int inwidth, inheight, instride;

    GL_DisableMultitexture();

    /*
     * Determine top and bottom colors
     */
    player = &cl.players[playernum];
#ifdef QW_HACK
    if (!player->name[0])
	return;
#endif

    /*
     * Locate the original skin pixels
     *
     * Because the texture upload/translate process allocates low
     * memory on the hunk and the cached skin pixels are in cache
     * memory, the cache entry can get evicted during the texture
     * upload process.  So we need to allocate stable memory on the
     * low hunk and copy the pixels there.
     *
     * TODO: make this cache vs. texture code less trapulent.
     */
    int mark = Hunk_LowMark();
    byte *pixels;

#ifdef NQ_HACK
    entity_t *entity = &cl_entities[1 + playernum];
    model_t *model = entity->model;
    const aliashdr_t *aliashdr;

    if (!model)
	return;			// player doesn't have a model yet
    if (model->type != mod_alias)
	return;			// only translate skins on alias models

    aliashdr = Mod_Extradata(model);
    original = (const byte *)aliashdr + aliashdr->skindata;
    if (entity->skinnum < 0 || entity->skinnum >= aliashdr->numskins) {
	Con_DPrintf("Player %d has invalid skin #%d\n", playernum, entity->skinnum);
    } else {
	const int skinsize = aliashdr->skinwidth * aliashdr->skinheight;
	if (skinsize & 3)
	    Sys_Error("%s: skinsize & 3", __func__);
	original += entity->skinnum * skinsize;
    }

    inwidth = instride = aliashdr->skinwidth;
    inheight = aliashdr->skinheight;

    /* Allocate memory to copy the pixels, then re-check the cache - UGH */
    pixels = Hunk_AllocName(instride * inheight, "skin");
    aliashdr = Mod_Extradata(model);
    original = (const byte *)aliashdr + aliashdr->skindata;

#endif
#ifdef QW_HACK
    /* Hard coded width from original model */
    inwidth = 296;
    inheight = 194;

    Skin_Find(player);
    original = Skin_Cache(player->skin);
    instride = original ? 320 : inwidth;

    /* Allocate memory to copy the pixels, then re-cache the skin pixels - UGH */
    pixels = Hunk_AllocName(instride * inheight, "skin");
    if (original) {
	original = Skin_Cache(player->skin);
    } else {
	model_t *model = cl.model_precache[cl_playerindex];
	const aliashdr_t *aliashdr = Mod_Extradata(model);
	original = (const byte *)aliashdr + aliashdr->skindata;
    }
#endif

    memcpy(pixels, original, instride * inheight);

    qpic8_t playerpic = {
        .width = inwidth,
        .height = inheight,
        .stride = instride,
        .pixels = pixels,
    };
    playertexture_t *playertexture = &playertextures[playernum];
    playertexture->fullbright = QPic_HasFullbrights(&playerpic, TEXTURE_TYPE_PLAYER_SKIN);
    if (!playertexture->texture.base)
        playertexture->texture.base = GL_AllocTexture8(va("@player%02d", playernum), &playerpic, TEXTURE_TYPE_PLAYER_SKIN);
    if (!playertexture->texture.fullbright)
        playertexture->texture.base = GL_AllocTexture8(va("@player%02d:fullbright", playernum), &playerpic, TEXTURE_TYPE_PLAYER_SKIN_FULLBRIGHT);

    GL_Bind(playertexture->texture.base);
    translation = R_GetTranslationTable((int)player->topcolor, (int)player->bottomcolor);
    GL_Upload8_Translate(&playerpic, TEXTURE_TYPE_PLAYER_SKIN, translation);

    if (playertexture->fullbright) {
	/* Reset the width and height and upload as fullbright mask */
	playerpic.width = inwidth;
	playerpic.height = inheight;
	GL_Bind(playertexture->texture.fullbright);
	GL_Upload8(&playerpic, TEXTURE_TYPE_PLAYER_SKIN_FULLBRIGHT);
    }

    Hunk_FreeToLowMark(mark);
}

/*
===============
R_NewMap
===============
*/
void
R_NewMap(void)
{
    int i;

    for (i = 0; i < 256; i++)
	d_lightstylevalue[i] = 264;	// normal light value

    memset(&r_worldentity, 0, sizeof(r_worldentity));
    r_worldentity.model = &cl.worldmodel->model;

// clear out efrags in case the level hasn't been reloaded
// FIXME: is this one short?
    for (i = 0; i < cl.worldmodel->numleafs; i++)
	cl.worldmodel->leafs[i].efrags = NULL;

    r_viewleaf = NULL;
    R_ClearParticles();

    // BSP Post-processing here: All BSP models at once
    GL_BuildMaterials();
    GL_BuildLightmaps();

    /* identify mirror texture */
    mirrortexturenum = -1;
    for (i = 0; i < cl.worldmodel->numtextures; i++) {
	if (!cl.worldmodel->textures[i])
	    continue;
	if (strncmp(cl.worldmodel->textures[i]->name, "window02_1", 10))
            continue;
        mirrortexturenum = i;
        break;
    }

    Fog_NewMap();   // Read fog parameters from worldspawn
    Alpha_NewMap(); // Read water/slime/lava/tele alpha values from worldspawn
    Sky_NewMap();
}


/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void
R_TimeRefresh_f(void)
{
    int i;
    float start, stop, time;

    glDrawBuffer(GL_FRONT);
    glFinish();

    start = Sys_DoubleTime();
    for (i = 0; i < 128; i++) {
	r_refdef.viewangles[1] = i / 128.0 * 360.0;
	R_RenderView();
    }

    glFinish();
    stop = Sys_DoubleTime();
    time = stop - start;
    Con_Printf("%f seconds (%f fps)\n", time, 128 / time);

    glDrawBuffer(GL_BACK);
    GL_EndRendering();
}

void
D_FlushCaches(void)
{
}
