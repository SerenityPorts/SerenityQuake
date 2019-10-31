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

#include "console.h"
#include "glquake.h"
#include "gl_model.h"
#include "sys.h"

/*
 * Model Loader Functions
 */
static int GL_AliashdrPadding() { return offsetof(gl_aliashdr_t, ahdr); }
static int GL_BrushModelPadding() { return offsetof(glbrushmodel_t, brushmodel); }

/*
=================
Mod_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct {
    short x, y;
} floodfill_t;

// must be a power of 2
#define FLOODFILL_FIFO_SIZE 0x1000
#define FLOODFILL_FIFO_MASK (FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy ) \
{ \
	if (pos[off] == fillcolor) \
	{ \
		pos[off] = 255; \
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK; \
	} \
	else if (pos[off] != 255) fdc = pos[off]; \
}

static void
GL_FloodFillSkin(byte *skin, int skinwidth, int skinheight)
{
    byte fillcolor = *skin;	// assume this is the pixel to fill
    floodfill_t fifo[FLOODFILL_FIFO_SIZE];
    int inpt = 0, outpt = 0;
    int filledcolor = -1;
    int i;

    if (filledcolor == -1) {
	filledcolor = 0;
	// attempt to find opaque black (FIXME - precompute!)
        const qpixel32_t black = { .c.red = 0, .c.green = 0, .c.blue = 0, .c.alpha = 255 };
	for (i = 0; i < 256; ++i)
	    if (qpal_standard.colors[i].rgba == black.rgba)
	    {
		filledcolor = i;
		break;
	    }
    }
    // can't fill to filled color or to transparent color (used as visited marker)
    if ((fillcolor == filledcolor) || (fillcolor == 255)) {
	//printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
	return;
    }

    fifo[inpt].x = 0, fifo[inpt].y = 0;
    inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

    while (outpt != inpt) {
	int x = fifo[outpt].x, y = fifo[outpt].y;
	int fdc = filledcolor;
	byte *pos = &skin[x + skinwidth * y];

	outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

	if (x > 0)
	    FLOODFILL_STEP(-1, -1, 0);
	if (x < skinwidth - 1)
	    FLOODFILL_STEP(1, 1, 0);
	if (y > 0)
	    FLOODFILL_STEP(-skinwidth, 0, -1);
	if (y < skinheight - 1)
	    FLOODFILL_STEP(skinwidth, 0, 1);
	skin[x + skinwidth * y] = fdc;
    }
}

static void
GL_LoadAliasSkinData(model_t *model, aliashdr_t *aliashdr, const alias_skindata_t *skindata)
{
    int i, skinsize;
    qgltexture_t *textures;
    byte *pixels;

    skinsize = aliashdr->skinwidth * aliashdr->skinheight;
    pixels = Mod_AllocName(skindata->numskins * skinsize, model->name);
    aliashdr->skindata = (byte *)pixels - (byte *)aliashdr;
    textures = Mod_AllocName(skindata->numskins * sizeof(qgltexture_t), model->name);
    GL_Aliashdr(aliashdr)->textures = (byte *)textures - (byte *)aliashdr;

    for (i = 0; i < skindata->numskins; i++) {
	GL_FloodFillSkin(skindata->data[i], aliashdr->skinwidth, aliashdr->skinheight);
	memcpy(pixels, skindata->data[i], skinsize);
        pixels += skinsize;
    }

    GL_LoadAliasSkinTextures(model, aliashdr);
}

static alias_loader_t GL_AliasModelLoader = {
    .Padding = GL_AliashdrPadding,
    .LoadSkinData = GL_LoadAliasSkinData,
    .LoadMeshData = GL_LoadAliasMeshData,
    .CacheDestructor = NULL,
};

const alias_loader_t *
R_AliasModelLoader(void)
{
    return &GL_AliasModelLoader;
}

/*
 * Allocates space in the lightmap blocks for the surface lightmap
 */
static void
GL_AllocLightmapBlock(glbrushmodel_resource_t *resources, msurface_t *surf)
{
    int i, j;
    int best, best2;
    int blocknum;
    int *allocated;
    int width, height;

    assert(!(surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB)));

    width = (surf->extents[0] >> 4) + 1;
    height = (surf->extents[1] >> 4) + 1;

    /*
     * Only scan over the last four blocks. Only negligable effects on the
     * packing efficiency, but much faster for maps with a lot of lightmaps.
     */
    blocknum = qmax(0, resources->numblocks - 4);
    for ( ;; blocknum++) {
	if (blocknum == resources->numblocks) {
	    Hunk_AllocExtend(resources->blocks, sizeof(lm_block_t));
	    resources->numblocks++;
	}
	allocated = resources->blocks[blocknum].allocated;

	best = BLOCK_HEIGHT - height + 1;
	for (i = 0; i < BLOCK_WIDTH - width; i++) {
	    best2 = 0;
	    for (j = 0; j < width; j++) {
		/* If it's not going to fit, don't check again... */
		if (allocated[i + j] + height > BLOCK_HEIGHT) {
		    i += j + 1;
		    break;
		}
		if (allocated[i + j] >= best)
		    break;
		if (allocated[i + j] > best2)
		    best2 = allocated[i + j];
	    }
	    if (j == width) {	// this is a valid spot
		surf->light_s = i;
		surf->light_t = best = best2;
	    }
	}
	if (best + height <= BLOCK_HEIGHT)
	    break;
    }

    /* Mark the allocation as used */
    for (i = 0; i < width; i++) {
	allocated[surf->light_s + i] = best + height;
    }

    surf->lightmapblock = blocknum;
}

/*
 * Given an animating texture, return the texture corresponding to the first
 * frame in the animation sequence.
 */
static texture_t *
GL_GetAnimBaseTexture(brushmodel_t *brushmodel, texture_t *texture)
{
    assert(texture->anim_total);
    assert(texture->name[0] == '+');

    texture_t *frame = texture;
    do {
        char index = frame->name[1];
        if (index == '0' || index == 'a' || index == 'A')
            return frame;
        frame = frame->anim_next;
    } while (frame != texture);

    Sys_Error("%s: Unable to find base texture for %s", __func__, texture->name);
}

static void
GL_BrushModelPostProcess(brushmodel_t *brushmodel)
{
    mtexinfo_t *texinfo;
    int i;

    /*
     * For our material based rendering we want to ensure that for all
     * animating textures, the first animation frame is the one applied to the
     * surface.  This allows some simplification to the process for
     * determining the correct animation frame for each surface.
     */
    texinfo = brushmodel->texinfo;
    for (i = 0; i < brushmodel->numtexinfo; i++, texinfo++) {
        if (texinfo->texture->anim_total) {
            texture_t *basetexture = GL_GetAnimBaseTexture(brushmodel, texinfo->texture);
            if (texinfo->texture != basetexture)
                texinfo->texture = basetexture;
        }
    }

    /*
     * Ensure all sky texinfos point to the active sky texture (there
     * can be more than one sky texture in a brushmodel, but only the
     * last one is used).
     */
    texture_t *skytexture = NULL;
    for (i = 0; i < brushmodel->numtextures; i++) {
        texture_t *texture = brushmodel->textures[i];
        if (texture && !strncmp(texture->name, "sky", 3))
            skytexture = texture;
    }
    if (skytexture) {
        mtexinfo_t *texinfo = brushmodel->texinfo;
        for (i = 0; i < brushmodel->numtexinfo; i++, texinfo++) {
            if (!strncmp(texinfo->texture->name, "sky", 3))
                texinfo->texture = skytexture;
        }
    }
}

static enum material_class
GL_GetTextureMaterialClass(texture_t *texture)
{
    if (texture->name[0] == '*')
        return MATERIAL_LIQUID;
    if (texture->name[0] == '{')
        return (texture->gl_texturenum_fullbright) ? MATERIAL_FENCE_FULLBRIGHT : MATERIAL_FENCE;
    if (!strncmp(texture->name, "sky", 3))
        return MATERIAL_SKY;
    if (texture->gl_texturenum_fullbright)
        return MATERIAL_FULLBRIGHT;

    return MATERIAL_BASE;
}

#ifdef DEBUG
static const char *material_class_names[] = {
    "MATERIAL_SKY",
    "MATERIAL_BASE",
    "MATERIAL_FULLBRIGHT",
    "MATERIAL_FENCE",
    "MATERIAL_FENCE_FULLBRIGHT",
    "MATERIAL_LIQUID",
    "MATERIAL_END",
};

static void
Debug_PrintMaterials(glbrushmodel_t *glbrushmodel)
{
    brushmodel_t *brushmodel = &glbrushmodel->brushmodel;
    surface_material_t *material;
    material_animation_t *animation;
    enum material_class class;
    int i;

    Sys_Printf("====== %s ======\n", glbrushmodel->brushmodel.model.name);

    for (class = MATERIAL_START; class <= MATERIAL_END; class++) {
        Sys_Printf("%25s: %d\n", material_class_names[class], glbrushmodel->material_index[class]);
    }

    class = MATERIAL_START;
    material = &glbrushmodel->materials[0];
    for (i = 0; i < glbrushmodel->nummaterials; i++, material++) {
        texture_t *texture = brushmodel->textures[material->texturenum];
        Sys_Printf("Material %3d: %-16s (%3d) :: lightmap block %3d",
                   i, texture->name, material->texturenum, material->lightmapblock);
        if (i == glbrushmodel->material_index[class]) {
            while (i == glbrushmodel->material_index[class + 1])
                class++;
            Sys_Printf("  <---- %s\n", material_class_names[class]);
            class++;
        } else {
            Sys_Printf("\n");
        }
    }

    animation = glbrushmodel->animations;
    for (i = 0; i < glbrushmodel->numanimations; i++, animation++) {
        material = &glbrushmodel->materials[animation->material];
        Sys_Printf("Animation %d: material %d (%s / %d), %d frames, %d alt frames\n",
                   i,
                   animation->material,
                   brushmodel->textures[material->texturenum]->name,
                   material->lightmapblock,
                   animation->numframes,
                   animation->numalt);
        int j;
        for (j = 0; j < animation->numframes; j++)
            Sys_Printf("   frame %d: material %d\n", j, animation->frames[j]);
        for (j = 0; j < animation->numalt; j++)
            Sys_Printf("   alt   %d: material %d\n", j, animation->alt[j]);
    }
}
#else
static inline void Debug_PrintMaterials(glbrushmodel_t *glbrushmodel) { }
#endif

/*
 * Find a material that we know should exist already
 */
static int
GL_FindMaterial(glbrushmodel_t *glbrushmodel, int texturenum, int lightmapblock)
{
    int materialnum;
    surface_material_t *material;

    material = glbrushmodel->materials;
    for (materialnum = 0; materialnum < glbrushmodel->nummaterials; materialnum++, material++) {
        if (material->texturenum != texturenum)
            continue;
        if (material->lightmapblock != lightmapblock)
            continue;
        break;
    }

    assert(materialnum < glbrushmodel->nummaterials);

    return materialnum;
}

static void
GL_AllocateMaterial(glbrushmodel_t *glbrushmodel, msurface_t *texturechain, int texturenum, int material_start)
{
    msurface_t *surf;
    surface_material_t *material;

    for (surf = texturechain; surf; surf = surf->chain) {
        int lightmapblock = surf->lightmapblock;
        int materialnum = material_start;
        qboolean found = false;
        for ( ; materialnum < glbrushmodel->nummaterials; materialnum++) {
            material = &glbrushmodel->materials[materialnum];
            if (material->texturenum != texturenum)
                continue;
            if (material->lightmapblock != lightmapblock)
                continue;
            found = true;
            break;
        }
        if (found) {
            /*
             * Only assign the material if this is the base texture
             * animation frame for the surface.
             */
            if (surf->texinfo->texture->texturenum == texturenum)
                surf->material = materialnum;
            continue;
        }

        /*
         * TODO: The hunk extension will be rounded up to the nearest
         * 16 bytes, which might waste some space over the entire
         * material list.  Make a better alloc helper for this case?
         */
        if (surf->texinfo->texture->texturenum == texturenum) {
            surf->material = glbrushmodel->nummaterials;
        }
        Hunk_AllocExtend(glbrushmodel->materials, sizeof(surface_material_t));
        material = &glbrushmodel->materials[glbrushmodel->nummaterials++];
        material->texturenum = texturenum;
        material->lightmapblock = surf->lightmapblock;
    }
}

void
GL_BuildMaterials()
{
    glbrushmodel_resource_t *resources;
    glbrushmodel_t *glbrushmodel;
    brushmodel_t *brushmodel;
    msurface_t **texturechains;
    msurface_t *surf;
    enum material_class material_class;
    texture_t *texture;
    int surfnum, texturenum;

    /* Allocate the shared resource structure for the bmodels */
    resources = Hunk_AllocName(sizeof(glbrushmodel_resource_t), "resources");
    resources->blocks = Hunk_AllocName(sizeof(lm_block_t), "lightmaps");
    resources->numblocks = 1;

    /*
     * Allocate lightmaps for all brush models first, so we get contiguous
     * memory for the lightmap block allocations.
     */
    for (brushmodel = loaded_brushmodels; brushmodel; brushmodel = brushmodel->next) {
        if (brushmodel->parent)
            continue;

        /*
         * Setup (temporary) texture chains so we can allocate lightmaps in
         * order of texture.  We borrow the materialchains pointer to store
         * the texturechain until every brushmodel has had it's lightmaps
         * allocated.  We'll also initialise the surface material to -1 here
         * to signify no material yet allocated.
         *
         * It is possible to assign the non-base animating texture to a
         * surface, we'll ensure we treat all textures in the sequence as the
         * sequence base texture when allocating materials.  The alternate
         * animation will generate an extra material.
         */
        texturechains = Hunk_TempAllocExtend(brushmodel->numtextures * sizeof(msurface_t *));
        surf = brushmodel->surfaces;
        for (surfnum = 0; surfnum < brushmodel->numsurfaces; surfnum++, surf++) {
            surf->material = -1;
            texture = surf->texinfo->texture;
            if (!texture)
                continue;
            if (texture->anim_total)
                texture = GL_GetAnimBaseTexture(brushmodel, texture);
            surf->chain = texturechains[texture->texturenum];
            texturechains[texture->texturenum] = surf;
        }

        /* Allocate lightmap blocks in texture order */
        for (texturenum = 0; texturenum < brushmodel->numtextures; texturenum++) {
            surf = texturechains[texturenum];
            if (!surf)
                continue;
            if (surf->flags & (SURF_DRAWSKY | SURF_DRAWTURB | SURF_DRAWTILED))
                continue;
            for ( ; surf; surf = surf->chain) {
                GL_AllocLightmapBlock(resources, surf);
            }
        }

        /* Save the texturechains for material allocation below */
        glbrushmodel = GLBrushModel(brushmodel);
        glbrushmodel->materialchains = texturechains;
    }

    /*
     * Next we allocate materials for each brushmodel
     */
    for (brushmodel = loaded_brushmodels; brushmodel; brushmodel = brushmodel->next) {
        if (brushmodel->parent)
            continue;

        glbrushmodel = GLBrushModel(brushmodel);
        glbrushmodel->nummaterials = 0;
        glbrushmodel->materials = Hunk_AllocName(0, "material");
        texturechains = glbrushmodel->materialchains; /* saved earlier */

        /* To group the materials by class we'll do multiple passes over the texture list */
        material_class = MATERIAL_START;
        for ( ; material_class < MATERIAL_END; material_class++) {
            glbrushmodel->material_index[material_class] = glbrushmodel->nummaterials;

            for (texturenum = 0; texturenum < brushmodel->numtextures; texturenum++) {
                texture = brushmodel->textures[texturenum];
                if (!texture)
                    continue;

                /*
                 * For animating textures, we need to register materials for
                 * all frames and alternates that would match the current
                 * class
                 */
                if (texture->anim_total) {
                    /* Animation frames */
                    texture_t *frame = texture;
                    do {
                        if (GL_GetTextureMaterialClass(frame) == material_class) {
                            int index = glbrushmodel->material_index[material_class];
                            GL_AllocateMaterial(glbrushmodel, texturechains[texturenum], frame->texturenum, index);
                        }
                        frame = frame->anim_next;
                    } while (frame != texture);

                    /* Alt-animation frames */
                    if (!texture->alternate_anims)
                        continue;
                    frame = texture = texture->alternate_anims;
                    do {
                        if (GL_GetTextureMaterialClass(frame) == material_class) {
                            int index = glbrushmodel->material_index[material_class];
                            GL_AllocateMaterial(glbrushmodel, texturechains[texturenum], frame->texturenum, index);
                        }
                        frame = frame->anim_next;
                    } while (frame != texture);

                    /* Done with this texture */
                    continue;
                }

                /* Skip past textures not in the current material class */
                if (GL_GetTextureMaterialClass(texture) != material_class)
                    continue;

                int index = glbrushmodel->material_index[material_class];
                GL_AllocateMaterial(glbrushmodel, texturechains[texturenum], texturenum, index);
            }
        }
        glbrushmodel->material_index[MATERIAL_END] = glbrushmodel->nummaterials;

        /*
         * Collect the animation information for each material and
         * store it in the glbrushmodel header.
         */
        glbrushmodel->animations = Hunk_AllocName(0, "animation");
        int materialnum;
        surface_material_t *material = glbrushmodel->materials;
        for (materialnum = 0; materialnum < glbrushmodel->nummaterials; materialnum++, material++) {
            texture = brushmodel->textures[material->texturenum];
            if (texture->name[0] != '+')
                continue;
            char index = texture->name[1];
            if (index != '0' && index != 'a' && index != 'A')
                continue;

            /*
             * If the texture has alternate animations, we store the
             * info on the base (digit) material side
             */
            if (texture->alternate_anims && index != '0')
                continue;

            /* Allocate and add the base frame */
            Hunk_AllocExtend(glbrushmodel->animations, sizeof(material_animation_t));
            material_animation_t *animation = &glbrushmodel->animations[glbrushmodel->numanimations++];
            animation->material = materialnum;
            animation->frames[animation->numframes++] = materialnum;

            /* Add all the animation frames */
            texture_t *frame = texture->anim_next;
            while (frame != texture) {
                int frameMaterial = GL_FindMaterial(glbrushmodel, frame->texturenum, material->lightmapblock);
                animation->frames[animation->numframes++] = frameMaterial;
                frame = frame->anim_next;
            }

            /* Add all the alternate animation frames */
            if (!texture->alternate_anims)
                continue;
            frame = texture = texture->alternate_anims;
            do {
                int frameMaterial = GL_FindMaterial(glbrushmodel, frame->texturenum, material->lightmapblock);
                animation->alt[animation->numalt++] = frameMaterial;
                frame = frame->anim_next;
            } while (frame != texture);
        }
    }

    /*
     * Finally, we allocate the materialchains for every brushmodel (including
     * submodels).  Submodels share the material list with their parent.  All
     * share the common resources struct.
     */
    void *hunkbase = Hunk_AllocName(0, "material");
    for (brushmodel = loaded_brushmodels; brushmodel; brushmodel = brushmodel->next) {
        glbrushmodel = GLBrushModel(brushmodel);
        if (brushmodel->parent) {
            glbrushmodel_t *parent = GLBrushModel(brushmodel->parent);
            glbrushmodel->nummaterials = parent->nummaterials;
            glbrushmodel->materials = parent->materials;
            memcpy(glbrushmodel->material_index, parent->material_index, sizeof(glbrushmodel->material_index));

            /* TODO: only include animations for textures present in the submodel */
            glbrushmodel->numanimations = parent->numanimations;
            glbrushmodel->animations = parent->animations;
        }
        glbrushmodel->materialchains = Hunk_AllocExtend(hunkbase, glbrushmodel->nummaterials * sizeof(msurface_t *));
        glbrushmodel->resources = resources;

        /* It's a bit verbose to print for all submodels, usually */
        if (!brushmodel->parent)
            Debug_PrintMaterials(glbrushmodel);
    }
}

static void
GL_BrushModelLoadLighting(brushmodel_t *brushmodel, dheader_t *header)
{
    const model_t *model = &brushmodel->model;
    const lump_t *headerlump = &header->lumps[LUMP_LIGHTING];
    litheader_t *litheader;
    size_t filesize;
    char litfilename[MAX_QPATH];

    /* Attempt to load a .lit file for colored lighting */
    int mark = Hunk_LowMark();
    COM_DefaultExtension(model->name, ".lit", litfilename, sizeof(litfilename));
    litheader = COM_LoadHunkFile(litfilename, &filesize);
    if (!litheader)
        goto fallback;
    if (filesize < sizeof(litheader_t) + headerlump->filelen * 3)
        goto fallback_corrupt;
    if (memcmp(litheader->identifier, "QLIT", 4))
        goto fallback_corrupt;

    litheader->version = LittleLong(litheader->version);
    if (litheader->version != 1) {
        Con_Printf("Unknown .lit file version (%d), ignoring\n", litheader->version);
        goto fallback;
    }

    brushmodel->lightdata = (byte *)(litheader + 1);
    return;

 fallback_corrupt:
    Con_Printf("Corrupt .lit file, ignoring\n");
 fallback:
    Hunk_FreeToLowMark(mark);

    /* Fall back to plain lighting, if any */
    if (!headerlump->filelen) {
	brushmodel->lightdata = NULL;
        return;
    }

    /* Expand to RGB format */
    brushmodel->lightdata = Mod_AllocName(headerlump->filelen * 3, model->name);
    const byte *in = (byte *)header + headerlump->fileofs;
    byte *out = brushmodel->lightdata;
    int i;
    for (i = 0; i < headerlump->filelen; i++, in++) {
        *out++ = *in;
        *out++ = *in;
        *out++ = *in;
    }
}

static brush_loader_t GL_BrushModelLoader = {
    .Padding = GL_BrushModelPadding,
    .LoadLighting = GL_BrushModelLoadLighting,
    .PostProcess = GL_BrushModelPostProcess,
    .lightmap_sample_bytes = gl_lightmap_bytes,
};

const brush_loader_t *
R_BrushModelLoader()
{
    return &GL_BrushModelLoader;
}

