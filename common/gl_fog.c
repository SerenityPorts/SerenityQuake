/*
  Copyright (C) 1996-2001 Id Software, Inc.
  Copyright (C) 2002-2009 John Fitzgibbons and others

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

//==============================================================================
//
//  GLOBAL FOG
//
//==============================================================================

#include <math.h>

#include "client.h"
#include "cmd.h"
#include "console.h"

typedef struct {
    float density;
    float red;
    float green;
    float blue;
} fog_params_t;

static struct {
    fog_params_t current;
    fog_params_t previous;
    float fade_time; //duration of fade
    float fade_done; //time when fade will be done
} fog;

float map_skyfog;

static void
Sky_Skyfog_f(cvar_t *cvar)
{
    map_skyfog = cvar->value;
}

cvar_t r_skyfog = { "r_skyfog", "0.5", true, .callback = Sky_Skyfog_f };

/*
  =============
  Fog_Update

  update internal variables
  =============
*/
void
Fog_Update(float density, float red, float green, float blue, float time)
{
    // Save previous settings for fade
    if (time > 0) {
        //check for a fade in progress
        if (fog.fade_done > cl.time) {
            float lerp = (fog.fade_done - cl.time) / fog.fade_time;
            fog.previous.density = lerp * fog.previous.density + (1.0f - lerp) * fog.current.density;
            fog.previous.red     = lerp * fog.previous.red     + (1.0f - lerp) * fog.current.red;
            fog.previous.green   = lerp * fog.previous.green   + (1.0f - lerp) * fog.current.green;
            fog.previous.blue    = lerp * fog.previous.blue    + (1.0f - lerp) * fog.current.blue;
        } else {
            fog.previous.density = fog.current.density;
            fog.previous.red = fog.current.red;
            fog.previous.green = fog.current.green;
            fog.previous.blue = fog.current.blue;
        }
    }

    fog.current.density = density;
    fog.current.red = red;
    fog.current.green = green;
    fog.current.blue = blue;
    fog.fade_time = time;
    fog.fade_done = cl.time + time;
}

/*
  =============
  Fog_ParseServerMessage

  handle an SVC_FOG message from server
  =============
*/
void
Fog_ParseServerMessage()
{
    float density, red, green, blue, time;

    density = MSG_ReadByte() / 255.0f;
    red = MSG_ReadByte() / 255.0f;
    green = MSG_ReadByte() / 255.0f;
    blue = MSG_ReadByte() / 255.0f;
    time = qmax(0.0f, MSG_ReadShort() / 100.0f);

    Fog_Update(density, red, green, blue, time);
}

/*
  =============
  Fog_Command_f

  handle the 'fog' console command
  =============
*/
void
Fog_Command_f()
{
    switch (Cmd_Argc())	{
        default:
        case 1:
            Con_Printf("usage:\n");
            Con_Printf("   fog <density>\n");
            Con_Printf("   fog <red> <green> <blue>\n");
            Con_Printf("   fog <density> <red> <green> <blue>\n");
            Con_Printf("current values:\n");
            Con_Printf("   \"density\" is \"%f\"\n", fog.current.density);
            Con_Printf("   \"red\" is \"%f\"\n", fog.current.red);
            Con_Printf("   \"green\" is \"%f\"\n", fog.current.green);
            Con_Printf("   \"blue\" is \"%f\"\n", fog.current.blue);
            break;
        case 2:
            Fog_Update(qmax(0.0f, (float)atof(Cmd_Argv(1))),
                       fog.current.red,
                       fog.current.green,
                       fog.current.blue,
                       0.0f);
            break;
        case 3: //TEST
            Fog_Update(qmax(0.0f, (float)atof(Cmd_Argv(1))),
                       fog.current.red,
                       fog.current.green,
                       fog.current.blue,
                       atof(Cmd_Argv(2)));
            break;
        case 4:
            Fog_Update(fog.current.density,
                       qclamp((float)atof(Cmd_Argv(1)), 0.0f, 1.0f),
                       qclamp((float)atof(Cmd_Argv(2)), 0.0f, 1.0f),
                       qclamp((float)atof(Cmd_Argv(3)), 0.0f, 1.0f),
                       0.0f);
            break;
        case 5:
            Fog_Update(qmax(0.0f, (float)atof(Cmd_Argv(1))),
                       qclamp((float)atof(Cmd_Argv(2)), 0.0f, 1.0f),
                       qclamp((float)atof(Cmd_Argv(3)), 0.0f, 1.0f),
                       qclamp((float)atof(Cmd_Argv(4)), 0.0f, 1.0f),
                       0.0f);
            break;
        case 6: //TEST
            Fog_Update(qmax(0.0f, (float)atof(Cmd_Argv(1))),
                       qclamp((float)atof(Cmd_Argv(2)), 0.0f, 1.0f),
                       qclamp((float)atof(Cmd_Argv(3)), 0.0f, 1.0f),
                       qclamp((float)atof(Cmd_Argv(4)), 0.0f, 1.0f),
                       atof(Cmd_Argv(5)));
            break;
    }
}

/*
  =============
  Fog_GetColor

  calculates fog color for this frame, taking into account fade times
  =============
*/
const float *
Fog_GetColor()
{
    static float color[4];
    int i;

    if (fog.fade_done > cl.time) {
        float lerp = (fog.fade_done - cl.time) / fog.fade_time;
        color[0] = lerp * fog.previous.red   + (1.0f - lerp) * fog.current.red;
        color[1] = lerp * fog.previous.green + (1.0f - lerp) * fog.current.green;
        color[2] = lerp * fog.previous.blue  + (1.0f - lerp) * fog.current.blue;
        color[3] = 1.0f;
    } else {
        color[0] = fog.current.red;
        color[1] = fog.current.green;
        color[2] = fog.current.blue;
        color[3] = 1.0f;
    }

    //find closest 24-bit RGB value, so solid-colored sky can match the fog perfectly
    for (i = 0; i < 3; i++)
        color[i] = roundf(color[i] * 255.0f) / 255.0f;

    return color;
}

/*
  =============
  Fog_GetDensity

  returns current density of fog
  =============
*/
float
Fog_GetDensity()
{
    if (fog.fade_done > cl.time) {
        float lerp = (fog.fade_done - cl.time) / fog.fade_time;
        return lerp * fog.previous.density + (1.0f - lerp) * fog.current.density;
    }

    return fog.current.density;
}

/*
  =============
  Fog_SetupGL

  Called at the beginning of each frame
  =============
*/
void
Fog_SetupGL()
{
    glFogfv(GL_FOG_COLOR, Fog_GetColor());
    glFogf(GL_FOG_DENSITY, Fog_GetDensity() / 64.0);
}

/*
  =============
  Fog_EnableGlobalFog

  called before drawing stuff that should be fogged
  =============
*/
void
Fog_EnableGlobalFog()
{
    if (Fog_GetDensity() > 0)
        glEnable(GL_FOG);
}

/*
  =============
  Fog_DisableGlobalFog

  called after drawing stuff that should be fogged
  =============
*/
void
Fog_DisableGlobalFog()
{
    if (Fog_GetDensity() > 0)
        glDisable(GL_FOG);
}

/*
=============
Fog_StartBlend

called before drawing a blended texture pass -- sets fog color to black
=============
*/
void
Fog_StartBlend()
{
    vec3_t color = {0,0,0};

    if (Fog_GetDensity() > 0)
        glFogfv(GL_FOG_COLOR, color);
}

/*
=============
Fog_StopBlend

called to restore fog color after drawing a blended texture pass
=============
*/
void
Fog_StopBlend()
{
    if (Fog_GetDensity() > 0)
        glFogfv(GL_FOG_COLOR, Fog_GetColor());
}

//==============================================================================
//
//  INIT
//
//==============================================================================

/*
  =============
  Fog_NewMap

  called whenever a map is loaded
  =============
*/
void
Fog_NewMap()
{
    char buffer[64];

    /* Default is no fog */
    fog.current.density = 0.0f;
    fog.previous.density = 0.0f;
    fog.fade_time = 0.0f;
    fog.fade_done = 0.0f;

    char *fogParams = Entity_ValueForKey(cl.worldmodel->entities, "fog", buffer, sizeof(buffer));
    if (fogParams) {
        sscanf(fogParams, "%f %f %f %f",
               &fog.current.density,
               &fog.current.red,
               &fog.current.green,
               &fog.current.blue);
    }

    Entity_ValueForKey(cl.worldmodel->entities, "skyfog", buffer, sizeof(buffer));
    map_skyfog = buffer[0] ? atof(buffer) : r_skyfog.value;
}

/*
  =============
  Fog_Init

  called when quake initializes
  =============
*/
void
Fog_Init()
{
    Cmd_AddCommand("fog", Fog_Command_f);
    Cvar_RegisterVariable(&r_skyfog);

    //set up global fog
    fog.current.density = 0.0f;
    fog.current.red = 0.3f;
    fog.current.green = 0.3f;
    fog.current.blue = 0.3f;

    glFogi(GL_FOG_MODE, GL_EXP2);
}
