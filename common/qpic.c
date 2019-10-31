/*
Copyright (C) 1996-1997 Id Software, Inc.  Copyright (C) 2013 Kevin
Shanahan

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
#include <stdint.h>
#include <string.h>

#include "qpic.h"
#include "qtypes.h"
#include "vid.h"
#include "zone.h"

/* Indexed by enum texture_type */
const texture_properties_t texture_properties[] = {
    // Palette                Alpha Operation              mipmap picmip plyrmp repeat
    { &qpal_alpha_zero,       QPIC_ALPHA_OP_EDGE_FIX,      false, false, false, false }, // CHARSET
    { &qpal_alpha,            QPIC_ALPHA_OP_EDGE_FIX,      false, false, false, true  }, // HUD
    { &qpal_standard,         QPIC_ALPHA_OP_NONE,          true,  true,  false, true  }, // WORLD
    { &qpal_fullbright,       QPIC_ALPHA_OP_NONE,          true,  true,  false, true  }, // WORLD_FULLBRIGHT
    { &qpal_alpha,            QPIC_ALPHA_OP_EDGE_FIX,      true,  true,  false, true  }, // FENCE
    { &qpal_alpha_fullbright, QPIC_ALPHA_OP_EDGE_FIX,      true,  true,  false, true  }, // FENCE_FULLBRIGHT
    { &qpal_standard,         QPIC_ALPHA_OP_NONE,          false, false, false, true  }, // SKY_BACKGROUND
    { &qpal_alpha_zero,       QPIC_ALPHA_OP_EDGE_FIX,      false, false, false, true  }, // SKY_FOREGROUND
    { &qpal_alpha_zero,       QPIC_ALPHA_OP_EDGE_FIX,      false, false, false, false }, // SKYBOX
    { &qpal_standard,         QPIC_ALPHA_OP_NONE,          true,  true,  false, false }, // ALIAS_SKIN
    { &qpal_fullbright,       QPIC_ALPHA_OP_CLAMP_TO_ZERO, true,  true,  false, false }, // ALIAS_SKIN_FULLBRIGHT
    { &qpal_standard,         QPIC_ALPHA_OP_NONE,          true,  false, true,  false }, // PLAYER_SKIN
    { &qpal_fullbright,       QPIC_ALPHA_OP_CLAMP_TO_ZERO, true,  false, true,  false }, // PLAYER_SKIN_FULLBRIGHT
    { NULL,                   QPIC_ALPHA_OP_NONE,          false, false, false, false }, // LIGHTMAP
    { &qpal_alpha,            QPIC_ALPHA_OP_EDGE_FIX,      false, false, false, false }, // PARTICLE
    { &qpal_alpha,            QPIC_ALPHA_OP_EDGE_FIX,      true,  true,  false, false }, // SPRITE
    { &qpal_standard,         QPIC_ALPHA_OP_NONE,          false, false, false, true  }, // NOTEXTURE
};

/*
 * Detect 8-bit textures containing fullbrights
 */
qboolean
QPic_HasFullbrights(const qpic8_t *pic, enum texture_type type)
{
    int i, j;
    const byte *pixel;
    const qpalette32_t *palette;

    palette = texture_properties[type].palette;
    pixel = pic->pixels;
    for (i = 0; i < pic->height; i++) {
        for (j = 0; j < pic->width; j++) {
            byte index = *pixel++;
            if (index > 223 && palette->colors[index].c.alpha)
                return true;
        }
        pixel += pic->stride - pic->width;
    }

    return false;
}

/* --------------------------------------------------------------------------*/
/* Pic Format Transformations                                                */
/* --------------------------------------------------------------------------*/

qpic32_t *
QPic32_Alloc(int width, int height)
{
    const int memsize = offsetof(qpic32_t, pixels[width * height]);
    qpic32_t *pic = Hunk_AllocName(memsize, "qp32tmp");

    if (pic) {
	pic->width = width;
	pic->height = height;
    }

    return pic;
}

/*
================
QPic32_AlphaEdgeFix

Operates in-place on an RGBA pic assumed to have all alpha values
either fully opaque or transparent.  Fully transparent pixels get
their color components set to the average color of their
non-transparent neighbours to avoid artifacts from blending.

TODO: add an edge clamp mode?
================
*/
static void
QPic32_AlphaEdgeFix(qpic32_t *pic)
{
    const int width = pic->width;
    const int height = pic->height;
    qpixel32_t *pixels = pic->pixels;

    int x, y, n, red, green, blue, count;
    int neighbours[8];

    for (y = 0; y < height; y++) {
	for (x = 0; x < width; x++) {
	    const int current = y * width + x;

	    /* only modify completely transparent pixels */
	    if (pixels[current].c.alpha)
		continue;

	    /*
	     * Neighbour pixel indexes are left to right:
	     *   1 2 3
	     *   4 * 5
	     *   6 7 8
	     */
	    neighbours[0] = current - width - 1;
	    neighbours[1] = current - width;
	    neighbours[2] = current - width + 1;
	    neighbours[3] = current - 1;
	    neighbours[4] = current + 1;
	    neighbours[5] = current + width - 1;
	    neighbours[6] = current + width;
	    neighbours[7] = current + width + 1;

	    /* handle edge cases (wrap around) */
	    if (!x) {
		neighbours[0] += width;
		neighbours[3] += width;
		neighbours[5] += width;
	    } else if (x == width - 1) {
		neighbours[2] -= width;
		neighbours[4] -= width;
		neighbours[7] -= width;
	    }
	    if (!y) {
		neighbours[0] += width * height;
		neighbours[1] += width * height;
		neighbours[2] += width * height;
	    } else if (y == height - 1) {
		neighbours[5] -= width * height;
		neighbours[6] -= width * height;
		neighbours[7] -= width * height;
	    }

	    /* find the average color of non-transparent neighbours */
	    red = green = blue = count = 0;
	    for (n = 0; n < 8; n++) {
		if (!pixels[neighbours[n]].c.alpha)
		    continue;
		red += pixels[neighbours[n]].c.red;
		green += pixels[neighbours[n]].c.green;
		blue += pixels[neighbours[n]].c.blue;
		count++;
	    }

	    /* skip if no non-transparent neighbours */
	    if (!count)
		continue;

	    pixels[current].c.red = red / count;
	    pixels[current].c.green = green / count;
	    pixels[current].c.blue = blue / count;
	}
    }
}

static void
QPic32_AlphaClampToZero(qpic32_t *pic)
{
    const int size = pic->width * pic->height;
    qpixel32_t *pixel;
    int i;

    for (i = 0, pixel = pic->pixels; i < size; i++, pixel++) {
        if (pixel->c.alpha < 255)
            pixel->c.alpha = 0;
    }
}

static void
QPic32_AlphaFix(qpic32_t *pic, enum qpic_alpha_operation alpha_op)
{
    switch (alpha_op) {
        case QPIC_ALPHA_OP_EDGE_FIX:
            QPic32_AlphaEdgeFix(pic);
            break;
        case QPIC_ALPHA_OP_CLAMP_TO_ZERO:
            QPic32_AlphaClampToZero(pic);
            break;
        case QPIC_ALPHA_OP_NONE:
            break;
    }
}

void
QPic_8to32(const qpic8_t *in, qpic32_t *out, const qpalette32_t *palette, enum qpic_alpha_operation alpha_op)
{
    const int width = in->width;
    const int height = in->height;
    const int stride = in->stride ? in->stride : in->width;
    const byte *in_p = in->pixels;
    qpixel32_t *out_p = out->pixels;
    int x, y;

    for (y = 0; y < height; y++) {
	for (x = 0; x < width; x++)
	    *out_p++ = palette->colors[*in_p++];
	in_p += stride - width;
    }

    QPic32_AlphaFix(out, alpha_op);
}

void
QPic32_ScaleAlpha(qpic32_t *pic, byte alpha)
{
    const int width = pic->width;
    const int height = pic->height;
    qpixel32_t *pixel = pic->pixels;
    int x, y;

    for (y = 0; y < height; y++) {
	for (x = 0; x < width; x++) {
	    pixel->c.alpha = ((int)pixel->c.alpha * alpha / 255);
            pixel++;
        }
    }
}

/*
================
QPic32_Stretch
TODO - should probably be doing bilinear filtering or something
================
*/
void
QPic32_Stretch(const qpic32_t *in, qpic32_t *out)
{
    int i, j;
    const qpixel32_t *inrow;
    qpixel32_t *outrow;
    unsigned frac, fracstep;

    assert(!(out->width & 3));

    fracstep = in->width * 0x10000 / out->width;
    outrow = out->pixels;
    for (i = 0; i < out->height; i++, outrow += out->width) {
	inrow = in->pixels + in->width * (i * in->height / out->height);
	frac = fracstep >> 1;
	for (j = 0; j < out->width; j += 4) {
	    outrow[j] = inrow[frac >> 16];
	    frac += fracstep;
	    outrow[j + 1] = inrow[frac >> 16];
	    frac += fracstep;
	    outrow[j + 2] = inrow[frac >> 16];
	    frac += fracstep;
	    outrow[j + 3] = inrow[frac >> 16];
	    frac += fracstep;
	}
    }
}

/*
============
Expand the texture size, copying the source texture into the top left corner.
============
*/
void
QPic32_Expand(const qpic32_t *in, qpic32_t *out)
{
    int i, j;
    const qpixel32_t *src;
    qpixel32_t *dst;
    qpixel32_t fill;

    assert(in->width <= out->width);
    assert(in->height <= out->height);

    src = in->pixels;
    dst = out->pixels;
    i = 0;
    while (i < in->height) {
	/* Copy source pixels in */
	j = 0;
	while (j < in->width) {
	    *dst++ = *src++;
	    j++;
	}
	/* Fill space with color matching last source pixel */
	fill = *(src - 1);
	while (j < out->width) {
	    *dst++ = fill;
	    j++;
	}
	i++;
    }
    /* Fill remaining rows with color matching the pixel above */
    src = dst - out->width;
    while (i < out->height) {
	*dst++ = *src++;
	i++;
    }
}


/* --------------------------------------------------------------------------*/
/* Mipmaps - Handle all variations of even/odd dimensions                    */
/* --------------------------------------------------------------------------*/

static void
QPic32_MipMap_1D_Even(qpixel32_t *pixels, int length)
{
    const byte *in;
    byte *out;
    int i;

    in = out = (byte *)pixels;

    length >>= 1;
    for (i = 0; i < length; i++, out += 4, in += 8) {
	out[0] = ((int)in[0] + in[4]) >> 1;
	out[1] = ((int)in[1] + in[5]) >> 1;
	out[2] = ((int)in[2] + in[6]) >> 1;
	out[3] = ((int)in[3] + in[7]) >> 1;
    }
}

static void
QPic32_MipMap_1D_Odd(qpixel32_t *pixels, int length)
{
    const int inlength = length;
    const byte *in;
    byte *out;
    int i;

    in = out = (byte *)pixels;

    length >>= 1;

    const float w1 = (float)inlength / length;
    for (i = 0; i < length; i++, out += 4, in += 8) {
	const float w0 = (float)(i - length) / inlength;
	const float w2 = (float)(i + 1) / inlength;

	out[0] = w0 * in[0] + w1 * in[4] + w2 * in[8];
	out[1] = w0 * in[1] + w1 * in[5] + w2 * in[9];
	out[2] = w0 * in[2] + w1 * in[6] + w2 * in[10];
	out[3] = w0 * in[3] + w1 * in[7] + w2 * in[11];
    }
}

/*
================
QPic32_MipMap_EvenEven

Simple 2x2 box filter for pics with even width/height
================
*/
static void
QPic32_MipMap_EvenEven(qpixel32_t *pixels, int width, int height)
{
    int i, j;
    byte *in, *out;

    in = out = (byte *)pixels;

    width <<= 2;
    height >>= 1;
    for (i = 0; i < height; i++, in += width) {
	for (j = 0; j < width; j += 8, out += 4, in += 8) {
	    out[0] = ((int)in[0] + in[4] + in[width + 0] + in[width + 4]) >> 2;
	    out[1] = ((int)in[1] + in[5] + in[width + 1] + in[width + 5]) >> 2;
	    out[2] = ((int)in[2] + in[6] + in[width + 2] + in[width + 6]) >> 2;
	    out[3] = ((int)in[3] + in[7] + in[width + 3] + in[width + 7]) >> 2;
	}
    }
}


/*
================
QPic32_MipMap_OddOdd

With two odd dimensions we have a polyphase box filter in two
dimensions, taking weighted samples from a 3x3 square in the original
pic.
================
*/
static void
QPic32_MipMap_OddOdd(qpixel32_t *pixels, int width, int height)
{
    const int inwidth = width;
    const int inheight = height;
    const byte *in;
    byte *out;
    int x, y;

    in = out = (byte *)pixels;

    width >>= 1;
    height >>= 1;

    /*
     * Take weighted samples from a 3x3 square on the original pic.
     * Weights for the centre pixel work out to be constant.
     */
    const float wy1 = (float)height / inheight;
    const float wx1 = (float)width / inwidth;

    for (y = 0; y < height; y++, in += inwidth << 2) {
	const float wy0 = (float)(height - y) / inheight;
	const float wy2 = (float)(1 + y) / inheight;

	for (x = 0; x < width; x ++, in += 8, out += 4) {
	    const float wx0 = (float)(width - x) / inwidth;
	    const float wx2 = (float)(1 + x) / inwidth;

	    /* Set up input row pointers to make things read easier below */
	    const byte *r0 = in;
	    const byte *r1 = in + (inwidth << 2);
	    const byte *r2 = in + (inwidth << 3);

	    out[0] =
		wx0 * wy0 * r0[0] + wx1 * wy0 * r0[4] + wx2 * wy0 * r0[8] +
		wx0 * wy1 * r1[0] + wx1 * wy1 * r1[4] + wx2 * wy1 * r1[8] +
		wx0 * wy2 * r2[0] + wx1 * wy2 * r2[4] + wx2 * wy2 * r2[8];
	    out[1] =
		wx0 * wy0 * r0[1] + wx1 * wy0 * r0[5] + wx2 * wy0 * r0[9] +
		wx0 * wy1 * r1[1] + wx1 * wy1 * r1[5] + wx2 * wy1 * r1[9] +
		wx0 * wy2 * r2[1] + wx1 * wy2 * r2[5] + wx2 * wy2 * r2[9];
	    out[2] =
		wx0 * wy0 * r0[2] + wx1 * wy0 * r0[6] + wx2 * wy0 * r0[10] +
		wx0 * wy1 * r1[2] + wx1 * wy1 * r1[6] + wx2 * wy1 * r1[10] +
		wx0 * wy2 * r2[2] + wx1 * wy2 * r2[6] + wx2 * wy2 * r2[10];
	    out[3] =
		wx0 * wy0 * r0[3] + wx1 * wy0 * r0[7] + wx2 * wy0 * r0[11] +
		wx0 * wy1 * r1[3] + wx1 * wy1 * r1[7] + wx2 * wy1 * r1[11] +
		wx0 * wy2 * r2[3] + wx1 * wy2 * r2[7] + wx2 * wy2 * r2[11];
	}
    }
}

/*
================
QPic32_MipMap_OddEven

Handle odd width, even height
================
*/
static void
QPic32_MipMap_OddEven(qpixel32_t *pixels, int width, int height)
{
    const int inwidth = width;
    const byte *in;
    byte *out;
    int x, y;

    in = out = (byte *)pixels;

    width >>= 1;
    height >>= 1;

    /*
     * Take weighted samples from a 3x2 square on the original pic.
     * Weights for the centre pixels are constant.
     */
    const float wx1 = (float)width / inwidth;
    for (y = 0; y < height; y++, in += inwidth << 2) {
	for (x = 0; x < width; x ++, in += 8, out += 4) {
	    const float wx0 = (float)(width - x) / inwidth;
	    const float wx2 = (float)(1 + x) / inwidth;

	    /* Set up input row pointers to make things read easier below */
	    const byte *r0 = in;
	    const byte *r1 = in + (inwidth << 2);

	    out[0] = 0.5 * (wx0 * r0[0] + wx1 * r0[4] + wx2 * r0[8] +
			    wx0 * r1[0] + wx1 * r1[4] + wx2 * r1[8]);
	    out[1] = 0.5 * (wx0 * r0[1] + wx1 * r0[5] + wx2 * r0[9] +
			    wx0 * r1[1] + wx1 * r1[5] + wx2 * r1[9]);
	    out[2] = 0.5 * (wx0 * r0[2] + wx1 * r0[6] + wx2 * r0[10] +
			    wx0 * r1[2] + wx1 * r1[6] + wx2 * r1[10]);
	    out[3] = 0.5 * (wx0 * r0[3] + wx1 * r0[7] + wx2 * r0[11] +
			    wx0 * r1[3] + wx1 * r1[7] + wx2 * r1[11]);
	}
    }
}

/*
================
QPic32_MipMap_EvenOdd

Handle even width, odd height
================
*/
static void
QPic32_MipMap_EvenOdd(qpixel32_t *pixels, int width, int height)
{
    const int inwidth = width;
    const int inheight = height;
    const byte *in;
    byte *out;
    int x, y;

    in = out = (byte *)pixels;

    width >>= 1;
    height >>= 1;

    /*
     * Take weighted samples from a 2x3 square on the original pic.
     * Weights for the centre pixels are constant.
     */
    const float wy1 = (float)height / inheight;
    for (y = 0; y < height; y++, in += inwidth << 2) {
	const float wy0 = (float)(height - y) / inheight;
	const float wy2 = (float)(1 + y) / inheight;

	for (x = 0; x < width; x++, in += 8, out += 4) {

	    /* Set up input row pointers to make things read easier below */
	    const byte *r0 = in;
	    const byte *r1 = in + (inwidth << 2);
	    const byte *r2 = in + (inwidth << 3);

	    out[0] = 0.5 * (wy0 * ((int)r0[0] + r0[4]) +
			    wy1 * ((int)r1[0] + r1[4]) +
			    wy2 * ((int)r2[0] + r2[4]));
	    out[1] = 0.5 * (wy0 * ((int)r0[1] + r0[5]) +
			    wy1 * ((int)r1[1] + r1[5]) +
			    wy2 * ((int)r2[1] + r2[5]));
	    out[2] = 0.5 * (wy0 * ((int)r0[2] + r0[6]) +
			    wy1 * ((int)r1[2] + r1[6]) +
			    wy2 * ((int)r2[2] + r2[6]));
	    out[3] = 0.5 * (wy0 * ((int)r0[3] + r0[7]) +
			    wy1 * ((int)r1[3] + r1[7]) +
			    wy2 * ((int)r2[3] + r2[7]));
	}
    }
}

/*
================
QPic32_MipMap

Check pic dimensions and call the approriate specialized mipmap function
================
*/
void
QPic32_MipMap(qpic32_t *in, enum qpic_alpha_operation alpha_op)
{
    assert(in->width > 1 || in->height > 1);

    if (in->width == 1) {
	if (in->height & 1)
	    QPic32_MipMap_1D_Odd(in->pixels, in->height);
	else
	    QPic32_MipMap_1D_Even(in->pixels, in->height);

	in->height >>= 1;
	return;
    }

    if (in->height == 1) {
	if (in->width & 1)
	    QPic32_MipMap_1D_Odd(in->pixels, in->width);
	else
	    QPic32_MipMap_1D_Even(in->pixels, in->width);

	in->width >>= 1;
	return;
    }

    if (in->width & 1) {
	if (in->height & 1)
	    QPic32_MipMap_OddOdd(in->pixels, in->width, in->height);
	else
	    QPic32_MipMap_OddEven(in->pixels, in->width, in->height);
    } else if (in->height & 1) {
	QPic32_MipMap_EvenOdd(in->pixels, in->width, in->height);
    } else {
	QPic32_MipMap_EvenEven(in->pixels, in->width, in->height);
    }

    in->width >>= 1;
    in->height >>= 1;

    QPic32_AlphaFix(in, alpha_op);
}

qpalette32_t qpal_standard;
qpalette32_t qpal_fullbright;
qpalette32_t qpal_alpha_zero;
qpalette32_t qpal_alpha;
qpalette32_t qpal_alpha_fullbright;

void
QPic32_InitPalettes(const byte *palette)
{
    int i;
    const byte *src;
    qpixel32_t *dst;

    /* Standard palette - no transparency */
    src = palette;
    dst = qpal_standard.colors;
    for (i = 0; i < 256; i++, dst++) {
        dst->c.red = *src++;
        dst->c.green = *src++;
        dst->c.blue = *src++;
        dst->c.alpha = 255;
    }
    qpal_standard.alpha = false;

    /* Full-bright pallette - 0-223 are transparent but keep their colors (for mipmapping) */
    memcpy(&qpal_fullbright, &qpal_standard, sizeof(qpalette32_t));
    dst = qpal_fullbright.colors;
    for (i = 0; i < 224; i++, dst++)
        dst->c.alpha = 0;
    qpal_fullbright.alpha = true;

    /* Charset/sky palette - 0 is transparent */
    memcpy(&qpal_alpha_zero, &qpal_standard, sizeof(qpalette32_t));
    qpal_alpha_zero.colors[0].c.alpha = 0;
    qpal_alpha_zero.alpha = true;

    /* HUD/sprite/fence palette - 255 is transparent */
    memcpy(&qpal_alpha, &qpal_standard, sizeof(qpalette32_t));
    qpal_alpha.colors[255].c.alpha = 0;
    qpal_alpha.alpha = true;

    /* Fullbright mask for alpha (fence) textures */
    memcpy(&qpal_alpha_fullbright, &qpal_fullbright, sizeof(qpalette32_t));
    qpal_alpha_fullbright.colors[255].c.alpha = 0;
    qpal_alpha_fullbright.alpha = true;
}
