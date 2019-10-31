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
// gl_ngraph.c

#include "client.h"
#include "draw.h"
#include "glquake.h"
#include "menu.h"
#include "quakedef.h"
#include "sbar.h"

#define NET_GRAPHHEIGHT 32
static byte netgraph_pixels[NET_GRAPHHEIGHT][NET_TIMINGS];
static GLuint netgraphtexture;

void
R_ResetNetGraphTexture()
{
    netgraphtexture = 0;
}

static void
R_LineGraph(int x, int h)
{
    int i;
    byte color;

    if (h == 10000)
	color = 0x6f;		// yellow
    else if (h == 9999)
	color = 0x4f;		// red
    else if (h == 9998)
	color = 0xd0;		// blue
    else
	color = 0xfe;		// white

    if (h > NET_GRAPHHEIGHT)
	h = NET_GRAPHHEIGHT;

    for (i = 0; i < h; i++) {
        netgraph_pixels[NET_GRAPHHEIGHT - 1 - i][x] = (i & 1) ? 0xff : color;
    }
    for (; i < NET_GRAPHHEIGHT; i++) {
	netgraph_pixels[NET_GRAPHHEIGHT - 1 - i][x] = 0xff;
    }
}

/*
==============
R_NetGraph
==============
*/
void
R_NetGraph(void)
{
    int i, time_index, x, y, loss_percent;
    char message[80];

    loss_percent = CL_CalcNet();
    for (i = 0; i < NET_TIMINGS; i++) {
	time_index = (cls.netchan.outgoing_sequence - i) & NET_TIMINGSMASK;
	R_LineGraph(NET_TIMINGS - 1 - i, packet_latency[time_index]);
    }

    x = -((vid.width - 320) >> 1);
    y = vid.height - sb_lines - 24 - NET_GRAPHHEIGHT - 1;

    M_DrawTextBox(x, y, NET_TIMINGS / 8, NET_GRAPHHEIGHT / 8 + 1);
    y += 8;

    qsnprintf(message, sizeof(message), "%3i%% packet loss", loss_percent);
    Draw_String(8, y, message);
    y += 8;

    // now load the netgraph texture into gl and draw it
    qpic8_t netgraph = {
        .width = NET_TIMINGS,
        .height = NET_GRAPHHEIGHT,
        .pixels = &netgraph_pixels[0][0],
    };
    if (!netgraphtexture) {
        netgraphtexture = GL_AllocTexture8("@netgraph", &netgraph, TEXTURE_TYPE_HUD);
    }
    GL_Bind(netgraphtexture);
    GL_Upload8(&netgraph, TEXTURE_TYPE_HUD);

    x = 8;
    glColor3f(1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2f(x, y);
    glTexCoord2f(1, 0);
    glVertex2f(x + NET_TIMINGS, y);
    glTexCoord2f(1, 1);
    glVertex2f(x + NET_TIMINGS, y + NET_GRAPHHEIGHT);
    glTexCoord2f(0, 1);
    glVertex2f(x, y + NET_GRAPHHEIGHT);
    glEnd();
}
