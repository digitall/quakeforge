/*
	r_view.c

	player eye positioning

	Copyright (C) 1996-1997  Id Software, Inc.

	This program is free software; you can redistribute it and/or
	modify it under the terms of the GNU General Public License
	as published by the Free Software Foundation; either version 2
	of the License, or (at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

	See the GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to:

		Free Software Foundation, Inc.
		59 Temple Place - Suite 330
		Boston, MA  02111-1307, USA

*/
#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

static __attribute__ ((used)) const char rcsid[] = 
	"$Id$";

#include "QF/cmd.h"
#include "QF/console.h"
#include "QF/cvar.h"
#include "QF/draw.h"
#include "QF/msg.h"
#include "QF/screen.h"

#include "QF/plugin/vid_render.h"

#include "chase.h"
#include "client.h"
#include "compat.h"
#include "host.h"
#include "clview.h"

/*
	The view is allowed to move slightly from it's true position for bobbing,
	but if it exceeds 8 pixels linear distance (spherical, not box), the list
	of entities sent from the server may not include everything in the pvs,
	especially when crossing a water boudnary.
*/


cvar_t     *scr_ofsx;
cvar_t     *scr_ofsy;
cvar_t     *scr_ofsz;

cvar_t     *cl_rollspeed;
cvar_t     *cl_rollangle;

cvar_t     *cl_bob;
cvar_t     *cl_bobcycle;
cvar_t     *cl_bobup;

cvar_t     *v_centermove;
cvar_t     *v_centerspeed;

cvar_t     *v_kicktime;
cvar_t     *v_kickroll;
cvar_t     *v_kickpitch;

cvar_t     *v_iyaw_cycle;
cvar_t     *v_iroll_cycle;
cvar_t     *v_ipitch_cycle;
cvar_t     *v_iyaw_level;
cvar_t     *v_iroll_level;
cvar_t     *v_ipitch_level;

cvar_t     *v_idlescale;

float       v_dmg_time, v_dmg_roll, v_dmg_pitch;


cshift_t    cshift_empty = { {130, 80, 50}, 0};
cshift_t    cshift_water = { {130, 80, 50}, 128};
cshift_t    cshift_slime = { {0, 25, 5}, 150};
cshift_t    cshift_lava = { {255, 80, 0}, 150};
cshift_t    cshift_bonus = { {215, 186, 60}, 50};


static float
V_CalcBob (void)
{
	float       bob, cycle;

	cycle = cl.time - (int) (cl.time / cl_bobcycle->value) *
		cl_bobcycle->value;
	cycle /= cl_bobcycle->value;
	if (cycle < cl_bobup->value)
		cycle = M_PI * cycle / cl_bobup->value;
	else
		cycle = M_PI + M_PI * (cycle - cl_bobup->value) /
			(1.0 - cl_bobup->value);

	// bob is proportional to velocity in the xy plane
	// (don't count Z, or jumping messes it up)

	bob = sqrt (cl.velocity[0] * cl.velocity[0] +
				cl.velocity[1] * cl.velocity[1]) * cl_bob->value;
	bob = bob * 0.3 + bob * 0.7 * sin (cycle);
	if (bob > 4)
		bob = 4;
	else if (bob < -7)
		bob = -7;
	return bob;
}

void
V_StartPitchDrift (void)
{
	if (cl.laststop == cl.time) {
		return;					// something else is keeping it from drifting
	}

	if (cl.nodrift || !cl.pitchvel) {
		cl.pitchvel = v_centerspeed->value;
		cl.nodrift = false;
		cl.driftmove = 0;
	}
}

void
V_StopPitchDrift (void)
{
	cl.laststop = cl.time;
	cl.nodrift = true;
	cl.pitchvel = 0;
}

/*
	V_DriftPitch

	Moves the client pitch angle towards cl.idealpitch sent by the server.

	If the user is adjusting pitch manually, either with lookup/lookdown,
	mlook and mouse, or klook and keyboard, pitch drifting is constantly
	stopped.

	Drifting is enabled when the center view key is hit, mlook is released
	and lookspring is non 0, or when 
*/
static void
V_DriftPitch (void)
{
	float       delta, move;

	if (noclip_anglehack || !cl.onground || cls.demoplayback) {
		cl.driftmove = 0;
		cl.pitchvel = 0;
		return;
	}

	// don't count small mouse motion
	if (cl.nodrift) {
		if (fabs (cl.cmd.forwardmove) < cl_forwardspeed->value)
			cl.driftmove = 0;
		else
			cl.driftmove += host_frametime;

		if (cl.driftmove > v_centermove->value) {
			V_StartPitchDrift ();
		}
		return;
	}

	delta = cl.idealpitch - cl.viewangles[PITCH];

	if (!delta) {
		cl.pitchvel = 0;
		return;
	}

	move = host_frametime * cl.pitchvel;
	cl.pitchvel += host_frametime * v_centerspeed->value;

	if (delta > 0) {
		if (move > delta) {
			cl.pitchvel = 0;
			move = delta;
		}
		cl.viewangles[PITCH] += move;
	} else if (delta < 0) {
		if (move > -delta) {
			cl.pitchvel = 0;
			move = -delta;
		}
		cl.viewangles[PITCH] -= move;
	}
}

/* PALETTE FLASHES */

void
V_ParseDamage (void)
{
	entity_t   *ent;
	float       count, side;
	int         armor, blood;
	vec3_t      from, forward, right, up;

	armor = MSG_ReadByte (net_message);
	blood = MSG_ReadByte (net_message);
	MSG_ReadCoordV (net_message, from);

	count = blood * 0.5 + armor * 0.5;
	if (count < 10)
		count = 10;

	cl.faceanimtime = cl.time + 0.2;	// but sbar face into pain frame

	if (cl_cshift_damage->int_val
		|| (cl.sv_cshifts & INFO_CSHIFT_DAMAGE)) {

		cl.cshifts[CSHIFT_DAMAGE].percent += 3 * count;
		cl.cshifts[CSHIFT_DAMAGE].percent =
			bound (0, cl.cshifts[CSHIFT_DAMAGE].percent, 150);

		if (armor > blood) {
			cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 200;
			cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 100;
			cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 100;
		} else if (armor) {
			cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 220;
			cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 50;
			cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 50;
		} else {
			cl.cshifts[CSHIFT_DAMAGE].destcolor[0] = 255;
			cl.cshifts[CSHIFT_DAMAGE].destcolor[1] = 0;
			cl.cshifts[CSHIFT_DAMAGE].destcolor[2] = 0;
		}
	}

	// calculate view angle kicks

	ent = &cl_entities[cl.viewentity];
	VectorSubtract (from, ent->origin, from);
	VectorNormalize (from);

	AngleVectors (ent->angles, forward, right, up);

	side = DotProduct (from, right);
	v_dmg_roll = count * side * v_kickroll->value;

	side = DotProduct (from, forward);
	v_dmg_pitch = count * side * v_kickpitch->value;

	v_dmg_time = v_kicktime->value;
}

static void
V_cshift_f (void)
{
	cshift_empty.destcolor[0] = atoi (Cmd_Argv (1));
	cshift_empty.destcolor[1] = atoi (Cmd_Argv (2));
	cshift_empty.destcolor[2] = atoi (Cmd_Argv (3));
	cshift_empty.percent = atoi (Cmd_Argv (4));
}

/*
	V_BonusFlash_f

	When you run over an item, the server sends this command
*/
static void
V_BonusFlash_f (void)
{
	if (!cl_cshift_bonus->int_val
		&& !(cl.sv_cshifts & INFO_CSHIFT_BONUS))
		return;
	cl.cshifts[CSHIFT_BONUS] = cshift_bonus;
}

/*
	V_SetContentsColor

	Underwater, lava, etc each has a color shift
*/
void
V_SetContentsColor (int contents)
{
	if (!cl_cshift_contents->int_val
		&& !(cl.sv_cshifts & INFO_CSHIFT_CONTENTS)) {
		cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
		return;
	}

	switch (contents) {
		case CONTENTS_EMPTY:
			cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
			break;
		case CONTENTS_LAVA:
			cl.cshifts[CSHIFT_CONTENTS] = cshift_lava;
			break;
		case CONTENTS_SOLID:
		case CONTENTS_SLIME:
			cl.cshifts[CSHIFT_CONTENTS] = cshift_slime;
			break;
		default:
			cl.cshifts[CSHIFT_CONTENTS] = cshift_water;
	}
}

static void
V_CalcPowerupCshift (void)
{
	if (!cl.stats[STAT_ITEMS] & (IT_SUIT || IT_INVISIBILITY || IT_QUAD
								 || IT_INVULNERABILITY))
	{
		cl.cshifts[CSHIFT_POWERUP].percent = 0;
		return;
	}

	if (cl.stats[STAT_ITEMS] & IT_INVULNERABILITY &&
		cl.stats[STAT_ITEMS] & IT_QUAD) {
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 255;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	} else if (cl.stats[STAT_ITEMS] & IT_QUAD) {
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 255;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	} else if (cl.stats[STAT_ITEMS] & IT_INVULNERABILITY) {
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 30;
	} else if (cl.stats[STAT_ITEMS] & IT_SUIT) {
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 0;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 255;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 0;
		cl.cshifts[CSHIFT_POWERUP].percent = 20;
	} else if (cl.stats[STAT_ITEMS] & IT_INVISIBILITY) {
		cl.cshifts[CSHIFT_POWERUP].destcolor[0] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[1] = 100;
		cl.cshifts[CSHIFT_POWERUP].destcolor[2] = 100;
		cl.cshifts[CSHIFT_POWERUP].percent = 100;
	} else {
		cl.cshifts[CSHIFT_POWERUP].percent = 0;
	}
}

/*
  V_CalcBlend

  LordHavoc made this a real, true alpha blend.  Cleaned it up
  a bit, but otherwise this is his code.  --KB
*/
void
V_CalcBlend (void)
{
	float       a2, a3;
	float       r = 0, g = 0, b = 0, a = 0;
	int         i;

	for (i = 0; i < NUM_CSHIFTS; i++) {
		a2 = cl.cshifts[i].percent / 255.0;

		if (!a2)
			continue;

		a2 = min (a2, 1.0);
		r += (cl.cshifts[i].destcolor[0] - r) * a2;
		g += (cl.cshifts[i].destcolor[1] - g) * a2;
		b += (cl.cshifts[i].destcolor[2] - b) * a2;

		a3 = (1.0 - a) * (1.0 - a2);
		a = 1.0 - a3;
	}

	// LordHavoc: saturate color
	if (a) {
		a2 = 1.0 / a;
		r *= a2;
		g *= a2;
		b *= a2;
	}

	r_data->vid->cshift_color[0] = min (r, 255.0) / 255.0;
	r_data->vid->cshift_color[1] = min (g, 255.0) / 255.0;
	r_data->vid->cshift_color[2] = min (b, 255.0) / 255.0;
	r_data->vid->cshift_color[3] = bound (0.0, a, 1.0);
}

void
V_PrepBlend (void)
{ 
	int         i, j;

	if (cl_cshift_powerup->int_val
		|| (cl.sv_cshifts & INFO_CSHIFT_POWERUP))
		V_CalcPowerupCshift ();

	r_data->vid->cshift_changed = false;

	for (i = 0; i < NUM_CSHIFTS; i++) {
		if (cl.cshifts[i].percent != cl.prev_cshifts[i].percent) {
			r_data->vid->cshift_changed = true;
			cl.prev_cshifts[i].percent = cl.cshifts[i].percent;
		}
		for (j = 0; j < 3; j++) {
			if (cl.cshifts[i].destcolor[j] != cl.prev_cshifts[i].destcolor[j])
			{
				r_data->vid->cshift_changed = true;
				cl.prev_cshifts[i].destcolor[j] = cl.cshifts[i].destcolor[j];
			}
		}
	}

	// drop the damage value
	cl.cshifts[CSHIFT_DAMAGE].percent -= host_frametime * 150;
	if (cl.cshifts[CSHIFT_DAMAGE].percent < 0)
		cl.cshifts[CSHIFT_DAMAGE].percent = 0;

	// drop the bonus value
	cl.cshifts[CSHIFT_BONUS].percent -= host_frametime * 100;
	if (cl.cshifts[CSHIFT_BONUS].percent < 0)
		cl.cshifts[CSHIFT_BONUS].percent = 0;

	if (!r_data->vid->cshift_changed && !r_data->vid->recalc_refdef)
		return;

	V_CalcBlend ();
}

/* VIEW RENDERING */

static float
angledelta (float a)
{
	a = anglemod (a);
	if (a > 180)
		a -= 360;
	return a;
}

static void
CalcGunAngle (void)
{
	float       yaw, pitch, move;
	static float oldpitch = 0, oldyaw = 0;

	yaw = r_data->refdef->viewangles[YAW];
	pitch = -r_data->refdef->viewangles[PITCH];

	yaw = angledelta (yaw - r_data->refdef->viewangles[YAW]) * 0.4;
	yaw = bound (-10, yaw, 10);
	pitch = angledelta (-pitch - r_data->refdef->viewangles[PITCH]) * 0.4;
	pitch = bound (-10, pitch, 10);

	move = host_frametime * 20;
	if (yaw > oldyaw) {
		if (oldyaw + move < yaw)
			yaw = oldyaw + move;
	} else {
		if (oldyaw - move > yaw)
			yaw = oldyaw - move;
	}

	if (pitch > oldpitch) {
		if (oldpitch + move < pitch)
			pitch = oldpitch + move;
	} else {
		if (oldpitch - move > pitch)
			pitch = oldpitch - move;
	}

	oldyaw = yaw;
	oldpitch = pitch;

	cl.viewent.angles[YAW] = r_data->refdef->viewangles[YAW] + yaw;
	cl.viewent.angles[PITCH] = -(r_data->refdef->viewangles[PITCH] + pitch);

	cl.viewent.angles[ROLL] -=
		v_idlescale->value * sin (cl.time * v_iroll_cycle->value) *
		v_iroll_level->value;
	cl.viewent.angles[PITCH] -=
		v_idlescale->value * sin (cl.time * v_ipitch_cycle->value) *
		v_ipitch_level->value;
	cl.viewent.angles[YAW] -=
		v_idlescale->value * sin (cl.time * v_iyaw_cycle->value) *
		v_iyaw_level->value;
}

static void
V_BoundOffsets (void)
{
	entity_t   *ent;

	ent = &cl_entities[cl.viewentity];

	// absolutely bound refresh reletive to entity clipping hull
	// so the view can never be inside a solid wall

	if (r_data->refdef->vieworg[0] < ent->origin[0] - 14)
		r_data->refdef->vieworg[0] = ent->origin[0] - 14;
	else if (r_data->refdef->vieworg[0] > ent->origin[0] + 14)
		r_data->refdef->vieworg[0] = ent->origin[0] + 14;
	if (r_data->refdef->vieworg[1] < ent->origin[1] - 14)
		r_data->refdef->vieworg[1] = ent->origin[1] - 14;
	else if (r_data->refdef->vieworg[1] > ent->origin[1] + 14)
		r_data->refdef->vieworg[1] = ent->origin[1] + 14;
	if (r_data->refdef->vieworg[2] < ent->origin[2] - 22)
		r_data->refdef->vieworg[2] = ent->origin[2] - 22;
	else if (r_data->refdef->vieworg[2] > ent->origin[2] + 30)
		r_data->refdef->vieworg[2] = ent->origin[2] + 30;
}

/*
	V_AddIdle

	Idle swaying
*/
static void
V_AddIdle (void)
{
	r_data->refdef->viewangles[ROLL] += v_idlescale->value *
		sin (cl.time * v_iroll_cycle->value) * v_iroll_level->value;
	r_data->refdef->viewangles[PITCH] += v_idlescale->value *
		sin (cl.time * v_ipitch_cycle->value) * v_ipitch_level->value;
	r_data->refdef->viewangles[YAW] += v_idlescale->value *
		sin (cl.time * v_iyaw_cycle->value) * v_iyaw_level->value;
}

/*
	V_CalcViewRoll

	Roll is induced by movement and damage
*/
static void
V_CalcViewRoll (void)
{
	float       side;

	side = V_CalcRoll (cl_entities[cl.viewentity].angles, cl.velocity);
	r_data->refdef->viewangles[ROLL] += side;

	if (v_dmg_time > 0) {
		r_data->refdef->viewangles[ROLL] +=
			v_dmg_time / v_kicktime->value * v_dmg_roll;
		r_data->refdef->viewangles[PITCH] +=
			v_dmg_time / v_kicktime->value * v_dmg_pitch;
		v_dmg_time -= host_frametime;
	}

	if (cl.stats[STAT_HEALTH] <= 0) {
		r_data->refdef->viewangles[ROLL] = 80;	// dead view angle
		return;
	}

}

static void
V_CalcIntermissionRefdef (void)
{
	entity_t   *ent, *view;
	float       old;

	// ent is the player model (visible when out of body)
	ent = &cl_entities[cl.viewentity];

	// view is the weapon model (visible only from inside body)
	view = &cl.viewent;

	VectorCopy (ent->origin, r_data->refdef->vieworg);
	VectorCopy (ent->angles, r_data->refdef->viewangles);
	view->model = NULL;

	// always idle in intermission
	old = v_idlescale->value;
	Cvar_SetValue (v_idlescale, 1);
	V_AddIdle ();
	Cvar_SetValue (v_idlescale, old);
}

static void
V_CalcRefdef (void)
{
	entity_t   *ent, *view;
	float       bob;
	static float oldz = 0;
	int         i;
	vec3_t      angles, forward, right, up;

	V_DriftPitch ();

	// ent is the player model (visible when out of body)
	ent = &cl_entities[cl.viewentity];
	// view is the weapon model (visible only from inside body)
	view = &cl.viewent;

	bob = V_CalcBob ();

	// refresh position
	VectorCopy (ent->origin, r_data->refdef->vieworg);
	r_data->refdef->vieworg[2] += cl.viewheight + bob;

	// never let it sit exactly on a node line, because a water plane can
	// disappear when viewed with the eye exactly on it.
	// server protocol specifies to only 1/16 pixel, so add 1/32 in each axis
	r_data->refdef->vieworg[0] += 1.0 / 32;
	r_data->refdef->vieworg[1] += 1.0 / 32;
	r_data->refdef->vieworg[2] += 1.0 / 32;

	VectorCopy (cl.viewangles, r_data->refdef->viewangles);
	V_CalcViewRoll ();
	V_AddIdle ();

	// offsets
	angles[PITCH] = -ent->angles[PITCH];	// because entity pitches are
											// actually backward
	angles[YAW] = ent->angles[YAW];
	angles[ROLL] = ent->angles[ROLL];

	AngleVectors (angles, forward, right, up);

	for (i = 0; i < 3; i++) {
		r_data->refdef->vieworg[i] += scr_ofsx->value * forward[i] +
			scr_ofsy->value * right[i] +
			scr_ofsz->value * up[i];
	}

	V_BoundOffsets ();

	// set up gun position
	VectorCopy (cl.viewangles, view->angles);

	CalcGunAngle ();

	VectorCopy (ent->origin, view->origin);
	view->origin[2] += cl.viewheight;

	for (i = 0; i < 3; i++) {
		view->origin[i] += forward[i] * bob * 0.4;
//		view->origin[i] += right[i] * bob * 0.4;
//		view->origin[i] += up[i] * bob * 0.8;
	}
	view->origin[2] += bob;

	// fudge position around to keep amount of weapon visible
	// roughly equal with different FOV
	if (hud_sbar->int_val == 0 && r_data->scr_viewsize->int_val >= 100)
		;
	else if (r_data->scr_viewsize->int_val == 110)
		view->origin[2] += 1;
	else if (r_data->scr_viewsize->int_val == 100)
		view->origin[2] += 2;
	else if (r_data->scr_viewsize->int_val == 90)
		view->origin[2] += 1;
	else if (r_data->scr_viewsize->int_val == 80)
		view->origin[2] += 0.5;

	view->model = cl.model_precache[cl.stats[STAT_WEAPON]];
	view->frame = cl.stats[STAT_WEAPONFRAME];
	view->skin = 0;

	// set up the refresh position
	VectorAdd (r_data->refdef->viewangles, cl.punchangle,
			   r_data->refdef->viewangles);

	// smooth out stair step ups
	if (cl.onground && ent->origin[2] - oldz > 0) {
		float       steptime;

		steptime = cl.time - cl.oldtime;
		if (steptime < 0)
			steptime = 0;

		oldz += steptime * 80;
		if (oldz > ent->origin[2])
			oldz = ent->origin[2];
		if (ent->origin[2] - oldz > 12)
			oldz = ent->origin[2] - 12;
		r_data->refdef->vieworg[2] += oldz - ent->origin[2];
		view->origin[2] += oldz - ent->origin[2];
	} else
		oldz = ent->origin[2];

	if (chase_active->int_val)
		Chase_Update ();

	CL_TransformEntity (view, view->angles, true);
}

/*
	V_RenderView

	The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
	the entity origin, so any view position inside that will be valid
*/
void
V_RenderView (void)
{
	if (cls.state != ca_active)
		return;

	// don't allow cheats in multiplayer
	if (cl.maxclients > 1) {
		Cvar_Set (scr_ofsx, "0");
		Cvar_Set (scr_ofsy, "0");
		Cvar_Set (scr_ofsz, "0");
	}

	if (cl.intermission) {				// intermission / finale rendering
		V_CalcIntermissionRefdef ();
	} else {
		if (!cl.paused)
			V_CalcRefdef ();
	}

	r_funcs->R_RenderView ();
}

void
V_Init (void)
{
	Cmd_AddCommand ("bf", V_BonusFlash_f, "Background flash, used when you "
					"pick up an item");
	Cmd_AddCommand ("centerview", V_StartPitchDrift, "Centers the player's "
					"view ahead after +lookup or +lookdown\n"
					"Will not work while mlook is active or freelook is 1.");
	Cmd_AddCommand ("v_cshift", V_cshift_f, "This adjusts all of the colors "
					"currently being displayed.\n"
					"Used when you are underwater, hit, have the Ring of "
					"Shadows, or Quad Damage. (v_cshift r g b intensity)");
}

void
V_Init_Cvars (void)
{
	v_centermove = Cvar_Get ("v_centermove", "0.15", CVAR_NONE, NULL,
							 "How far the player must move forward before the "
							 "view re-centers");
	v_centerspeed = Cvar_Get ("v_centerspeed", "500", CVAR_NONE, NULL,
							  "How quickly you return to a center view after "
							  "a lookup or lookdown");
	v_iyaw_cycle = Cvar_Get ("v_iyaw_cycle", "2", CVAR_NONE, NULL,
							 "How far you tilt right and left when "
							 "v_idlescale is enabled");
	v_iroll_cycle = Cvar_Get ("v_iroll_cycle", "0.5", CVAR_NONE, NULL,
							  "How quickly you tilt right and left when "
							  "v_idlescale is enabled");
	v_ipitch_cycle = Cvar_Get ("v_ipitch_cycle", "1", CVAR_NONE, NULL,
							   "How quickly you lean forwards and backwards "
							   "when v_idlescale is enabled");
	v_iyaw_level = Cvar_Get ("v_iyaw_level", "0.3", CVAR_NONE, NULL,
							 "How far you tilt right and left when "
							 "v_idlescale is enabled");
	v_iroll_level = Cvar_Get ("v_iroll_level", "0.1", CVAR_NONE, NULL,
							  "How far you tilt right and left when "
							  "v_idlescale is enabled");
	v_ipitch_level = Cvar_Get ("v_ipitch_level", "0.3", CVAR_NONE, NULL,
							   "How far you lean forwards and backwards when "
							   "v_idlescale is enabled");
	v_idlescale = Cvar_Get ("v_idlescale", "0", CVAR_NONE, NULL,
							"Toggles whether the view remains idle");

	scr_ofsx = Cvar_Get ("scr_ofsx", "0", CVAR_NONE, NULL, "None");
	scr_ofsy = Cvar_Get ("scr_ofsy", "0", CVAR_NONE, NULL, "None");
	scr_ofsz = Cvar_Get ("scr_ofsz", "0", CVAR_NONE, NULL, "None");
	cl_rollspeed = Cvar_Get ("cl_rollspeed", "200", CVAR_NONE, NULL,
							 "How quickly you straighten out after strafing");
	cl_rollangle = Cvar_Get ("cl_rollangle", "2.0", CVAR_NONE, NULL,
							 "How much your screen tilts when strafing");
	cl_bob = Cvar_Get ("cl_bob", "0.02", CVAR_NONE, NULL,
					   "How much your weapon moves up and down when walking");
	cl_bobcycle = Cvar_Get ("cl_bobcycle", "0.6", CVAR_NONE, NULL,
							"How quickly your weapon moves up and down when "
							"walking");
	cl_bobup = Cvar_Get ("cl_bobup", "0.5", CVAR_NONE, NULL,
						 "How long your weapon stays up before cycling when "
						 "walking");
	v_kicktime = Cvar_Get ("v_kicktime", "0.5", CVAR_NONE, NULL,
						   "How long the kick from an attack lasts");
	v_kickroll = Cvar_Get ("v_kickroll", "0.6", CVAR_NONE, NULL,
						   "How much you lean when hit");
	v_kickpitch = Cvar_Get ("v_kickpitch", "0.6", CVAR_NONE, NULL,
							"How much you look up when hit");
}
