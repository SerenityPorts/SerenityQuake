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

#include <string.h>

#include "client.h"
#include "common.h"
#include "console.h"
#include "pcx.h"
#include "qtypes.h"
#include "zone.h"

#ifdef GLQUAKE
#include "qpic.h"
#endif

void
SwapPCX(pcx_t *pcx)
{
    pcx->xmax = LittleShort(pcx->xmax);
    pcx->ymax = LittleShort(pcx->ymax);
    pcx->hres = LittleShort(pcx->hres);
    pcx->vres = LittleShort(pcx->vres);
    pcx->bytes_per_line = LittleShort(pcx->bytes_per_line);
    pcx->palette_type = LittleShort(pcx->palette_type);
}


/*
==============
WritePCXfile
==============
*/
void
WritePCXfile(const char *filename, const byte *data, int width, int height,
	     int rowbytes, const byte *palette, qboolean upload)
{
    int i, j, length;
    pcx_t *pcx;
    byte *pack;

    pcx = Hunk_TempAlloc(width * height * 2 + 1000);

    pcx->identifier = 0x0A;	// PCX id
    pcx->version = 5;		// 256 color
    pcx->encoding = 1;		// uncompressed
    pcx->bits_per_pixel = 8;	// 256 color
    pcx->xmin = 0;
    pcx->ymin = 0;
    pcx->xmax = width - 1;
    pcx->ymax = height - 1;
    pcx->hres = width;
    pcx->vres = height;
    memset(pcx->palette, 0, sizeof(pcx->palette));
    pcx->color_planes = 1;	// chunky image
    pcx->bytes_per_line = width;
    pcx->palette_type = 1;	// not a grey scale
    memset(pcx->reserved2, 0, sizeof(pcx->reserved2));

    SwapPCX(pcx);

    // pack the image
    pack = pcx->data;

#ifdef GLQUAKE
    // The GL buffer addressing is bottom to top?
    data += rowbytes * (height - 1);
    for (i = 0; i < height; i++) {
	for (j = 0; j < width; j++) {
	    if ((*data & 0xC0) != 0xC0) {
		*pack++ = *data++;
	    } else {
		*pack++ = 0xC1;
		*pack++ = *data++;
	    }
	}
	data += rowbytes - width;
	data -= rowbytes * 2;
    }
#else
    for (i = 0; i < height; i++) {
	for (j = 0; j < width; j++) {
	    if ((*data & 0xC0) != 0xC0) {
		*pack++ = *data++;
	    } else {
		*pack++ = 0xC1;
		*pack++ = *data++;
	    }
	}
	data += rowbytes - width;
    }
#endif

    // write the palette
    *pack++ = 0x0C;		// palette ID byte
    for (i = 0; i < 768; i++)
	*pack++ = *palette++;

    // write output file
    length = pack - (byte *)pcx;

#ifdef QW_HACK
    if (upload) {
	CL_StartUpload((byte *)pcx, length);
	return;
    }
#endif

    COM_WriteFile(filename, pcx, length);
}

#ifdef GLQUAKE

typedef struct {
    const byte *in;
    const byte *palette;
    qpixel32_t color;
    int runcount;
    qboolean overrun;
} pcx_readstate_t;

static inline void
PCX_NextPixel(pcx_readstate_t *state, qpixel32_t *pixel)
{
    const byte *palette;
    byte packet;

    if (state->runcount) {
        if (pixel)
            *pixel = state->color;
        state->runcount--;
        return;
    }
 next:
    if (state->in == state->palette) {
        state->overrun = true;
        return;
    }
    packet = *state->in++;
    if ((packet & 0xC0) == 0xC0) {
        if (state->in == state->palette) {
            state->overrun = true;
            return;
        }
        state->runcount = (packet & 0x3F);
        packet = *state->in++;
        if (!state->runcount)
            goto next;
        state->runcount--;
    }
    palette = state->palette + (int)packet * 3;
    state->color.c.red   = palette[0];
    state->color.c.green = palette[1];
    state->color.c.blue  = palette[2];
    state->color.c.alpha = 255;
    if (pixel)
        *pixel = state->color;
}

qpic32_t *
PCX_LoadHunkFile(const char *filename, const char *hunkname)
{
    FILE *f;
    int filelen, width, height, picmark, tempmark, x, y;
    size_t readcount;
    pcx_t header;
    qpic32_t *pic;
    void *pcxdata;
    pcx_readstate_t readstate;
    qpixel32_t *out;

    filelen = COM_FOpenFile(filename, &f);
    if (!f)
        return NULL;
    if (filelen < sizeof(header)) {
        Con_DPrintf("%s: Invalid PCX file\n", filename);
        goto fail_close;
    }

    readcount = fread(&header, 1, sizeof(header), f);
    if (readcount != sizeof(header)) {
        Con_DPrintf("%s: File read error\n", filename);
        goto fail_close;
    }
    SwapPCX(&header);

    if (header.identifier != 0x0A) {
        Con_DPrintf("%s: Invalid PCX file\n", filename);
        goto fail_close;
    }
    if (header.version != 5) {
        Con_DPrintf("%s: PCX version is %d, only version 5 supported\n", filename, (int)header.version);
        goto fail_close;
    }
    if (header.encoding != 1 || header.bits_per_pixel != 8 || header.color_planes != 1) {
        Con_DPrintf("%s: Unsupported PCX encoding (type %d, %dbpp), only 8bpp supported\n",
                    filename, (int)header.encoding, (int)header.bits_per_pixel * header.color_planes);
        goto fail_close;
    }

    /*
     * Ensure width and height are sane and that we have at least
     * enough data to read palette information
     */
    width = header.xmax - header.xmin + 1;
    height = header.ymax - header.ymin + 1;
    if (width < 1 || height < 1 || header.bytes_per_line < width || filelen < sizeof(header) + 768) {
        Con_DPrintf("%s: Invalid PCX file\n", filename);
        goto fail_close;
    }

    /* Allocate space for the image data */
    picmark = Hunk_LowMark();
    pic = QPic32_Alloc(width, height);
    pic->width = width;
    pic->height = height;

    /* Load the rest of the file for processing */
    tempmark = Hunk_LowMark();
    pcxdata = Hunk_AllocName(filelen - sizeof(header), "PCX_DATA");
    readcount = fread(pcxdata, 1, filelen - sizeof(header), f);
    fclose(f);
    if (readcount != filelen - sizeof(header)) {
        Con_DPrintf("%s(%s): File read error\n", __func__, filename);
        goto fail_free;
    }

    readstate.in = pcxdata;
    readstate.palette = readstate.in + filelen - sizeof(header) - 768;
    readstate.color.rgba = 0;
    readstate.runcount = 0;
    readstate.overrun = 0;
    out = pic->pixels;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            PCX_NextPixel(&readstate, out++);
        for ( ; x < header.bytes_per_line; x++)
            PCX_NextPixel(&readstate, NULL);
    }

    if (readstate.overrun)
        Con_DPrintf("WARNING: %s: Corrupt PCX, data truncated\n", filename);

    /* Free the raw data and return the image */
    Hunk_FreeToLowMark(tempmark);
    return pic;

 fail_close:
    fclose(f);
    return NULL;

 fail_free:
    Hunk_FreeToLowMark(picmark);
    return NULL;
}

#endif
