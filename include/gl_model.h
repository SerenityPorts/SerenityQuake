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

#ifndef GL_MODEL_H
#define GL_MODEL_H

#include <assert.h>

#include "model.h"

#define BLOCK_WIDTH  256
#define BLOCK_HEIGHT 256

typedef struct {
    unsigned short l, t, w, h;
} glRect_t;

typedef struct lm_block_s {
    glpoly_t *polys;
    qboolean modified;
    glRect_t rectchange;
    int allocated[BLOCK_WIDTH];
    byte data[BLOCK_WIDTH * BLOCK_HEIGHT * 4]; /* lightmaps */
    GLuint texture;
} lm_block_t;

/*
 * Materials will be stored grouped bu the material class and we'll
 * keep an index into where the first material of each class is
 * stored.
 *
 * Order is chosen to minimise state changes necessary when rendering.
 */
enum material_class {
    MATERIAL_START = 0,
    MATERIAL_SKY = 0,          // Sky textures
    MATERIAL_BASE,             // World textures, lightmapped
    MATERIAL_FULLBRIGHT,       // World textures with fullbright mask
    MATERIAL_FENCE,            // World fence textures, lightmapped
    MATERIAL_FENCE_FULLBRIGHT, // World textures with fullbright mask
    MATERIAL_LIQUID,           // Water, Lava, Slime (maybe split later)
    MATERIAL_END,              // Indexes one past the last material
};

// Material - combination of gl textures required to render the surface
typedef struct surface_material {
    int texturenum;
    int lightmapblock;
} surface_material_t;

typedef struct material_animation {
    int material;
    short numframes;
    short numalt;
    int frames[10]; // Index of the material for each frame
    int alt[10];    // Index of the material for each frame
} material_animation_t;

typedef struct {
    // Indicates the maximum number of verticies on any submodel sky poly
    // Used to more efficiently handle sky surfaces on submodels
    int max_submodel_skypoly_verts;

    // Lightmap blocks
    int numblocks;
    lm_block_t *blocks;
} glbrushmodel_resource_t;

typedef struct {
    // Shared resources for all brush models
    glbrushmodel_resource_t *resources;

    // Flag if we need to handle sky textured surfaces on this model
    qboolean drawsky;

    // Indices where each class of material begins in the materials array
    int material_index[MATERIAL_END + 1];

    // Materials (submodels share this with parent)
    int nummaterials;
    surface_material_t *materials;

    // Information for the animation materials in the model
    int numanimations;
    material_animation_t *animations;

    // Each submodel needs it's own material chains (currently) to
    // allow me to keep track of the transparent surface chains having
    // different transforms.  May be able to move this back on to the
    // material later.
    msurface_t **materialchains;

    // Embedded brushmodel struct (must be last member)
    brushmodel_t brushmodel;
} glbrushmodel_t;

static inline glbrushmodel_t *
GLBrushModel(brushmodel_t *brushmodel)
{
    return container_of(brushmodel, glbrushmodel_t, brushmodel);
}

static inline const glbrushmodel_t *
ConstGLBrushModel(const brushmodel_t *brushmodel)
{
    return const_container_of(brushmodel, glbrushmodel_t, brushmodel);
}

/* Allocates lightmap blocks and material lists for all loaded BSP models */
void GL_BuildMaterials();

/* Colored lighting file header */
typedef struct {
    char identifier[4];
    int version;
} litheader_t;

#endif /* GL_MODEL_H */
