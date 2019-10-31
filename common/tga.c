/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers
Copyright (C) 2019 Kevin Shanahan

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

#include <assert.h>

#include "common.h"
#include "console.h"
#include "qpic.h"
#include "zone.h"

#include "sys.h"

// ----------------------------------------------------------------------

typedef struct {
   char  idlength;
   char  colormaptype;
   char  datatypecode;
   short colormaporigin;
   short colormaplength;
   char  colormapdepth;
   short x_origin;
   short y_origin;
   short width;
   short height;
   char  bitsperpixel;
   char  imagedescriptor;
} __attribute__((packed)) tga_header_t;

#define TGA_SCREEN_ORIGIN_MASK (1 << 5)
#define TGA_DATA_INTERLEAVING_MASK ((1 << 6) & (1 << 7))
#define TGA_PACKET_TYPE_MASK (1 << 7)
#define TGA_PACKET_LENGTH_MASK 0x7f

// ----------------------------------------------------------------------

void
TGA_SwapHeader(tga_header_t *header)
{
    header->colormaporigin = LittleShort(header->colormaporigin);
    header->colormaplength = LittleShort(header->colormaplength);
    header->x_origin = LittleShort(header->x_origin);
    header->y_origin = LittleShort(header->y_origin);
    header->width = LittleShort(header->width);
    header->height = LittleShort(header->height);
}

static inline const byte *
TGA_ReadPixels_RGB(const byte *in, qpixel32_t *out, int pixelcount)
{
    while (pixelcount--) {
        out->c.blue  = *in++;
        out->c.green = *in++;
        out->c.red   = *in++;
        out->c.alpha = 255;
        out++;
    }

    return in;
}

static inline const byte *
TGA_ReadPixels_RGBA(const byte *in, qpixel32_t *out, int pixelcount)
{
    while (pixelcount--) {
        out->c.blue  = *in++;
        out->c.green = *in++;
        out->c.red   = *in++;
        out->c.alpha = *in++;
        out++;
    }

    return in;
}

enum tga_packet_type { TGA_PACKET_RLE, TGA_PACKET_RAW };
typedef struct {
    enum tga_packet_type type;
    int count;
} tga_packet_t;

static inline tga_packet_t
TGA_ReadPacketHeader(const byte header)
{
    tga_packet_t packet;

    packet.type  = (header & TGA_PACKET_TYPE_MASK) ? TGA_PACKET_RLE : TGA_PACKET_RAW;
    packet.count = (header & TGA_PACKET_LENGTH_MASK) + 1;

    return packet;
}

typedef struct {
    const byte *in;
    qpixel32_t *out;
    qpixel32_t *rowstart;
    int rowwidth;
    int rowstride;
    int rowcount;
} tga_readstate_t;

static inline void
TGA_ReadPacketRLE(tga_readstate_t *state, int runlength, qpixel32_t pixel)
{
    while (runlength) {
        int outcount = qmin(runlength, state->rowwidth - (int)(state->out - state->rowstart));
        runlength -= outcount;
        while (outcount--)
            *state->out++ = pixel;
        if (state->out - state->rowstart == state->rowwidth) {
            if (!--state->rowcount)
                return;
            state->rowstart += state->rowstride;
            state->out = state->rowstart;
        }
    }
}

static inline void
TGA_ReadPacketRAW_RGB(tga_readstate_t *state, int runlength)
{
    while (runlength) {
        int outcount = qmin(runlength, state->rowwidth - (int)(state->out - state->rowstart));
        runlength -= outcount;
        state->in = TGA_ReadPixels_RGB(state->in, state->out, outcount);
        state->out += outcount;
        if (state->out - state->rowstart == state->rowwidth) {
            if (!--state->rowcount)
                return;
            state->rowstart += state->rowstride;
            state->out = state->rowstart;
        }
    }
}

static inline void
TGA_ReadPacketRAW_RGBA(tga_readstate_t *state, int runlength)
{
    while (runlength) {
        int outcount = qmin(runlength, state->rowwidth - (int)(state->out - state->rowstart));
        runlength -= outcount;
        state->in = TGA_ReadPixels_RGBA(state->in, state->out, outcount);
        state->out += outcount;
        if (state->out - state->rowstart == state->rowwidth) {
            if (!--state->rowcount)
                return;
            state->rowstart += state->rowstride;
            state->out = state->rowstart;
        }
    }
}

/*
 * Load a TGA file from the filesystem.  If the file is obviously
 * corrupted we try to flag it.  We still load truncated files and
 * leave the unfilled pixels black.
 */
qpic32_t *
TGA_LoadHunkFile(const char *filename, const char *hunkname)
{
    FILE *f;
    int filelen, picmark, tempmark;
    size_t readcount;
    tga_header_t header;
    qpic32_t *pic;
    void *tgadata;

    /* First read in the header and do some sanity checks */
    filelen = COM_FOpenFile(filename, &f);
    if (!f) {
        Con_DPrintf("%s: File not found\n", filename);
        return NULL;
    }
    if (filelen < sizeof(header)) {
        Con_DPrintf("%s: Corrupt TGA header\n", filename);
        goto fail_close;
    }

    readcount = fread(&header, 1, sizeof(header), f);
    if (readcount != sizeof(header)) {
        Con_DPrintf("%s: File read error\n", filename);
        goto fail_close;
    }

    TGA_SwapHeader(&header);

    /* Only support a limited set of formats */
    if (header.datatypecode != 2 && header.datatypecode != 10) {
        Con_DPrintf("%s: TGA type %d not supported (only types 2 and 10 supported)\n", filename, (int)header.datatypecode);
        goto fail_close;
    }
    if (header.colormaptype != 0) {
        Con_DPrintf("%s: Color mapped TGAs not supported\n", filename);
        goto fail_close;
    }
    if (header.bitsperpixel != 24 && header.bitsperpixel != 32) {
        Con_DPrintf("%s: %d bpp images not supported (24 or 32 bpp only)\n", filename, (int)header.bitsperpixel);
        goto fail_close;
    }
    if (header.imagedescriptor & TGA_DATA_INTERLEAVING_MASK) {
        Con_DPrintf("%s: TGA interleaved data format not supported\n", filename);
        goto fail_close;
    }

    /* Allocate space for the image data */
    picmark = Hunk_LowMark();
    pic = QPic32_Alloc(header.width, header.height);
    pic->width = header.width;
    pic->height = header.height;

    /* Load the rest of the file for processing */
    tempmark = Hunk_LowMark();
    tgadata = Hunk_AllocName(filelen - sizeof(header), "TGA_DATA");
    readcount = fread(tgadata, 1, filelen - sizeof(header), f);
    fclose(f);
    if (readcount != filelen - sizeof(header)) {
        Con_DPrintf("%s: File read error\n", filename);
        goto fail_free;
    }

    /*
     * Setup parameters/state for reading in the TGA file.  Lump some
     * parts into a structure just to make it easier to break out
     * parts of the RLE expansion logic.
     */
    int bytes_per_pixel = header.bitsperpixel / 8;
    qboolean flipped = !(header.imagedescriptor & TGA_SCREEN_ORIGIN_MASK);
    qpixel32_t *outstart = flipped ? (pic->pixels + (pic->height - 1) * pic->width) : pic->pixels;
    tga_readstate_t state = {
        .in = tgadata,
        .out = outstart,
        .rowstart = outstart,
        .rowwidth = pic->width,
        .rowstride = flipped ? -pic->width : pic->width,
        .rowcount = pic->height,
    };

    /* Handle simple uncompressed case first */
    if (header.datatypecode == 2) {
        if (filelen < sizeof(header) + pic->width * pic->height * bytes_per_pixel)
            goto done_short;

        if (bytes_per_pixel == 3) {
            while (state.rowcount--) {
                state.in = TGA_ReadPixels_RGB(state.in, state.out, state.rowwidth);
                state.out += state.rowstride;
            }
        } else {
            while (state.rowcount--) {
                state.in = TGA_ReadPixels_RGBA(state.in, state.out, state.rowwidth);
                state.out += state.rowstride;
            }
        }
        goto done;
    }

    /* Handle RLE encoded images */
    const byte *in_end = state.in + (filelen - sizeof(header));
    qpixel32_t pixel;

    if (bytes_per_pixel == 3) {
        while (state.in < in_end - 1 - bytes_per_pixel) {
            tga_packet_t packet = TGA_ReadPacketHeader(*state.in++);
            switch (packet.type) {
                case TGA_PACKET_RLE:
                    state.in = TGA_ReadPixels_RGB(state.in, &pixel, 1);
                    TGA_ReadPacketRLE(&state, packet.count, pixel);
                    break;
                case TGA_PACKET_RAW:
                    TGA_ReadPacketRAW_RGB(&state, packet.count);
                    break;
            }
            if (!state.rowcount)
                goto done;
        }
    } else {
        while (state.in < in_end - 1 - bytes_per_pixel) {
            tga_packet_t packet = TGA_ReadPacketHeader(*state.in++);
            switch (packet.type) {
                case TGA_PACKET_RLE:
                    state.in = TGA_ReadPixels_RGBA(state.in, &pixel, 1);
                    TGA_ReadPacketRLE(&state, packet.count, pixel);
                    break;
                case TGA_PACKET_RAW:
                    TGA_ReadPacketRAW_RGBA(&state, packet.count);
                    break;
            }
            if (!state.rowcount)
                goto done;
        }
    }

 done_short:
    Con_DPrintf("WARNING: %s: Corrupt TGA, data truncated\n", filename);
 done:
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
