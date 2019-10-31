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

#include "client.h"
#include "console.h"
#include "mathlib.h"
#include "render.h"

/*
 * Setup movement interpolation
 */
void
R_AliasSetupTransformLerp(entity_t *entity, lerpdata_t *lerpdata)
{
    float duration, blend;
    vec3_t move, angles;
    int i;

    if (entity->lerp.flags & LERP_RESETMOVE) {
        /* Reset all movement interpolation */
        entity->lerp.move.start = 0;
        VectorCopy(entity->origin, entity->lerp.move.origin.previous);
        VectorCopy(entity->origin, entity->lerp.move.origin.current);
        VectorCopy(entity->angles, entity->lerp.move.angles.previous);
        VectorCopy(entity->angles, entity->lerp.move.angles.current);
        entity->lerp.flags &= ~LERP_RESETMOVE;
    } else if (!VectorCompare(entity->origin, entity->lerp.move.origin.current)
               || !VectorCompare(entity->angles, entity->lerp.move.angles.current)) {
        /* Origin/angle changed, start new interpolation */
        entity->lerp.move.start = cl.time;
        VectorCopy(entity->lerp.move.origin.current, entity->lerp.move.origin.previous);
        VectorCopy(entity->origin, entity->lerp.move.origin.current);
        VectorCopy(entity->lerp.move.angles.current, entity->lerp.move.angles.previous);
        VectorCopy(entity->angles, entity->lerp.move.angles.current);
    }

    /* Handle no-lerp cases */
    if (!r_lerpmove.value || !(entity->lerp.flags & LERP_MOVESTEP) || entity == &cl.viewent) {
        VectorCopy(entity->origin, lerpdata->origin);
        VectorCopy(entity->angles, lerpdata->angles);
        return;
    }

    /* Blend the translation and rotation */
    duration = (entity->lerp.flags & LERP_FINISH) ? entity->lerp.finish - entity->lerp.move.start : 0.1f;
    blend = qclamp((float)(cl.time - entity->lerp.move.start) / duration, 0.0f, 1.0f);

    VectorSubtract(entity->lerp.move.origin.current, entity->lerp.move.origin.previous, move);
    VectorMA(entity->lerp.move.origin.previous, blend, move, lerpdata->origin);

    VectorSubtract(entity->lerp.move.angles.current, entity->lerp.move.angles.previous, angles);
    for (i = 0; i < 3; i++) {
        if (angles[i] > 180.0f)
            angles[i] -= 360.0f;
        else if (angles[i] < -180.0f)
            angles[i] += 360.0f;
    }
    VectorMA(entity->lerp.move.angles.previous, blend, angles, lerpdata->angles);
}

/*
 * Setup animation interpolation
 */
void
R_AliasSetupAnimationLerp(entity_t *entity, aliashdr_t *aliashdr, lerpdata_t *lerpdata)
{
    int frame, pose, numposes;
    const float *intervals;
    float duration;

    frame = entity->frame;
    if ((frame >= aliashdr->numframes) || (frame < 0)) {
	Con_DPrintf("%s: %s has no such frame %d\n", __func__, entity->model->name, frame);
	frame = 0;
    }

    pose = aliashdr->frames[frame].firstpose;
    numposes = aliashdr->frames[frame].numposes;
    entity->lerp.pose.duration = 0.1f;

    if (numposes > 1) {
	const float frametime = cl.time + entity->syncbase;
	intervals = (float *)((byte *)aliashdr + aliashdr->poseintervals);
	pose += Mod_FindInterval(intervals + pose, numposes, frametime);
        if (pose > 0)
            entity->lerp.pose.duration = intervals[pose] - intervals[pose - 1];
        else
            entity->lerp.pose.duration = intervals[0];
    }

    if (entity->lerp.flags & LERP_RESETANIM) {
        /* Reset */
        entity->lerp.pose.start = 0.0f;
        entity->lerp.pose.previous = pose;
        entity->lerp.pose.current = pose;
        entity->lerp.flags &= ~LERP_RESETANIM;
    } else if (entity->lerp.pose.current != pose) {
        /* Pose changed, start new lerp */
        if (entity->lerp.flags & LERP_RESETANIM2) {
            /* Defer lerping one more time */
            entity->lerp.pose.start = 0.0f;
            entity->lerp.pose.previous = pose;
            entity->lerp.pose.current = pose;
            entity->lerp.flags &= ~LERP_RESETANIM2;
	} else if (entity->lerp.flags & LERP_RESETANIM3) {
            /* Defer lerping one more time */
            entity->lerp.pose.start = 0.0f;
            entity->lerp.pose.previous = pose;
            entity->lerp.pose.current = pose;
            entity->lerp.flags &= ~LERP_RESETANIM3;
        } else {
            entity->lerp.pose.start = cl.time;
            entity->lerp.pose.previous = entity->lerp.pose.current;
            entity->lerp.pose.current = pose;
        }
    }

    if (r_lerpmodels.value) {
        if ((entity->lerp.flags & LERP_FINISH) && numposes == 1)
            duration = entity->lerp.finish - entity->lerp.pose.start;
        else
            duration = entity->lerp.pose.duration;
        lerpdata->blend = qclamp((float)(cl.time - entity->lerp.pose.start) / duration, 0.0f, 1.0f);
        lerpdata->pose0 = entity->lerp.pose.previous;
        lerpdata->pose1 = entity->lerp.pose.current;
    } else {
        lerpdata->blend = 1.0f;
        lerpdata->pose0 = pose;
        lerpdata->pose1 = pose;
    }
}
