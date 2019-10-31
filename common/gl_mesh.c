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
// gl_mesh.c: triangle model functions

#include "buildinfo.h"
#include "common.h"
#include "console.h"
#include "glquake.h"
#include "model.h"
#include "quakedef.h"

/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION

=================================================================
*/

void
GL_LoadAliasMeshData(const model_t *model, aliashdr_t *hdr,
		     const alias_meshdata_t *meshdata,
		     const alias_posedata_t *posedata)
{
    const mtriangle_t *tris = meshdata->triangles;
    const stvert_t *stverts = meshdata->stverts;
    const mtriangle_t *tri;
    uint16_t *seam_vertex_map, num_seam_verts;
    int trinum, i;

    /*
     * Allocate temporary storage for creating a mapping of on-seam
     * vertexes.  When a vertex is reused on the back of the seam, we
     * have to duplicate the vertex and create the adjusted texture
     * coordinates that correspond to it.
     */
    seam_vertex_map = Hunk_TempAllocExtend(hdr->numverts * sizeof(uint16_t));
    num_seam_verts = 0;

    /* Find all the verticies used with non-translated s/t coords */
    tri = tris;
    for (trinum = 0; trinum < hdr->numtris; trinum++, tri++) {
        for (i = 0; i < 3; i++) {
            const stvert_t *stvert = &stverts[tri->vertindex[i]];
            if (!tri->facesfront && stvert->onseam) {
                seam_vertex_map[tri->vertindex[i]] = 1;
            }
        }
    }

    /* Setup the mapping from front/back vertices in vertex order */
    for (i = 0; i < hdr->numverts; i++) {
        if (seam_vertex_map[i])
            seam_vertex_map[i] = hdr->numverts + num_seam_verts++;
    }

    /*
     * Allocate the indices
     */
    uint16_t *indices = Hunk_AllocName(hdr->numtris * 3 * sizeof(uint16_t), "trimesh");
    GL_Aliashdr(hdr)->indices = (byte *)indices - (byte *)hdr;
    tri = tris;
    for (trinum = 0; trinum < hdr->numtris; trinum++, tri++) {
        for (i = 0; i < 3; i++) {
            const stvert_t *stvert = &stverts[tri->vertindex[i]];
            if (!tri->facesfront && stvert->onseam) {
                *indices++ = seam_vertex_map[tri->vertindex[i]];
            } else {
                *indices++ = tri->vertindex[i];
            }
        }
    }

    /*
     * Allocate the texcoords data
     *
     * We fudge the width/height +2 here to slightly stretch the
     * texture to push the background fill slightly further from the
     * seams.
     */
    float widthscale = (float)hdr->skinwidth / (float)GL_Aliashdr(hdr)->texturewidth;
    float heightscale = (float)hdr->skinheight / (float)GL_Aliashdr(hdr)->textureheight;
    texcoord_t *texcoord = Hunk_AllocName((hdr->numverts + num_seam_verts) * sizeof(texcoord_t), "trimesh");
    GL_Aliashdr(hdr)->texcoords = (byte *)texcoord - (byte *)hdr;
    const stvert_t *stvert = stverts;
    for (i = 0; i < hdr->numverts; i++, stvert++, texcoord++) {
        texcoord->s = (stvert->s + 0.5f) / (hdr->skinwidth + 2) * widthscale;
        texcoord->t = (stvert->t + 0.5f) / (hdr->skinheight + 2) * heightscale;
    }
    stvert = stverts;
    for (i = 0; i < hdr->numverts; i++, stvert++) {
        if (!seam_vertex_map[i])
            continue;

        /* Adjust the s coord to map to the back of the skin */
        int s = stvert->s + (hdr->skinwidth / 2) + 1;
        texcoord->s = (s         + 0.5f) / (hdr->skinwidth + 2) * widthscale;
        texcoord->t = (stvert->t + 0.5f) / (hdr->skinheight + 2) * heightscale;
        texcoord++;
    }

    /*
     * Allocate the pose data
     */
    int numverts = hdr->numverts + num_seam_verts;
    trivertx_t *vertex = Hunk_AllocName(hdr->numposes * numverts * sizeof(trivertx_t), "trimesh");
    hdr->posedata = (byte *)vertex - (byte *)hdr;

    /* Each pose has unique vertices and a normal which is used to generate a color for shading */
    int posenum;
    for (posenum = 0; posenum < hdr->numposes; posenum++) {
        for (i = 0; i < hdr->numverts; i++) {
	    *vertex++ = posedata->verts[posenum][i];
        }
        for (i = 0; i < hdr->numverts; i++) {
            if (!seam_vertex_map[i])
                continue;
            *vertex++ = posedata->verts[posenum][i];
        }
    }

    hdr->numverts += num_seam_verts;
}
