/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2010-2014 QuakeSpasm developers

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
// view.c -- player eye positioning

#include "quakedef.h"

/*

The view is allowed to move slightly from it's true position for bobbing,
but if it exceeds 8 pixels linear distance (spherical, not box), the list of
entities sent from the server may not include everything in the pvs, especially
when crossing a water boudnary.

*/

cvar_t	scr_ofsx = { "scr_ofsx", "0", CVAR_NONE };
cvar_t	scr_ofsy = { "scr_ofsy", "0", CVAR_NONE };
cvar_t	scr_ofsz = { "scr_ofsz", "0", CVAR_NONE };

cvar_t	cl_rollspeed = { "cl_rollspeed", "200", CVAR_NONE };
cvar_t	cl_rollangle = { "cl_rollangle", "2.0", CVAR_NONE };

cvar_t	cl_bob = { "cl_bob", "0.02", CVAR_NONE };
cvar_t	cl_bobcycle = { "cl_bobcycle", "0.6", CVAR_NONE };
cvar_t	cl_bobup = { "cl_bobup", "0.5", CVAR_NONE };

cvar_t	v_kicktime = { "v_kicktime", "0.5", CVAR_NONE };
cvar_t	v_kickroll = { "v_kickroll", "0.6", CVAR_NONE };
cvar_t	v_kickpitch = { "v_kickpitch", "0.6", CVAR_NONE };
cvar_t	v_kickyaw = { "v_kickyaw", "0.1", CVAR_NONE };
cvar_t	v_gunkick = { "v_gunkick", "2", CVAR_NONE }; // johnfitz

cvar_t	v_iyaw_cycle = { "v_iyaw_cycle", "2", CVAR_NONE };
cvar_t	v_iroll_cycle = { "v_iroll_cycle", "0.5", CVAR_NONE };
cvar_t	v_ipitch_cycle = { "v_ipitch_cycle", "1", CVAR_NONE };
cvar_t	v_iyaw_level = { "v_iyaw_level", "0.3", CVAR_NONE };
cvar_t	v_iroll_level = { "v_iroll_level", "0.1", CVAR_NONE };
cvar_t	v_ipitch_level = { "v_ipitch_level", "0.3", CVAR_NONE };

cvar_t	v_idlescale = { "v_idlescale", "0", CVAR_NONE };

cvar_t	crosshair = { "crosshair", "0", CVAR_ARCHIVE };
cvar_t	crosshaircolor = { "crosshaircolor", "95", CVAR_ARCHIVE };

cvar_t	gl_cshiftpercent = { "gl_cshiftpercent", "100", CVAR_NONE };
cvar_t	gl_cshiftpercent_contents = { "gl_cshiftpercent_contents", "100", CVAR_NONE }; // QuakeSpasm
cvar_t	gl_cshiftpercent_damage = { "gl_cshiftpercent_damage", "100", CVAR_NONE }; // QuakeSpasm
cvar_t	gl_cshiftpercent_bonus = { "gl_cshiftpercent_bonus", "100", CVAR_NONE }; // QuakeSpasm
cvar_t	gl_cshiftpercent_powerup = { "gl_cshiftpercent_powerup", "100", CVAR_NONE }; // QuakeSpasm

// MH - revert this to a default
cvar_t	r_viewmodel_quake = { "r_viewmodel_quake", "1", CVAR_NONE };

float	v_dmg_time, v_dmg_roll, v_dmg_pitch, v_dmg_yaw;

extern	int			in_forward, in_forward2, in_back;
float   v_oldz, v_stepz;
float   v_steptime;

vec3_t	v_punchangles[2]; // johnfitz -- copied from cl.punchangle.  0 is current, 1 is previous value. never the same unless map just loaded

/*
===============
V_CalcRoll

Used by view and sv_user
===============
*/
float V_CalcRoll (vec3_t angles, vec3_t velocity)
{
	vec3_t	forward, right, up;
	float	sign;
	float	side;
	float	value;

	AngleVectors (angles, forward, right, up);
	side = DotProduct (velocity, right);
	sign = side < 0 ? -1 : 1;
	side = fabs (side);

	value = cl_rollangle.value;
	//	if (cl.inwater)
	//		value *= 6;

	if (side < cl_rollspeed.value)
		side = side * value / cl_rollspeed.value;
	else
		side = value;

	return side * sign;
}


/*
===============
V_CalcBob

===============
*/
float V_CalcBob (void)
{
	/*
	------------------------------------------------------------------------------------------------------
	MORE QUAKE LORE THAT IS WRONG, DANGEROUS AND IGNORANT
	-----------------------------------------------------
	Set "cl_bobcycle 0" to disable bobbing.  Nope.  No.  No way.  You should be setting "cl_bob 0" instead
	because that's what it's there for.  Setting "cl_bobcycle 0" will actually cause division by 0 and a
	NaN cycle; however, it's so embedded in people's consciousness that no matter how many times you tell
	them this, they'll cheerfully ignore it and just keep on doing it anyway.  So we need to add checks.
	------------------------------------------------------------------------------------------------------
	*/
	float	bob;
	float	cycle;

	if (cl_bobcycle.value != 0)
	{
		cycle = cl.time - (int) (cl.time / cl_bobcycle.value) * cl_bobcycle.value;
		cycle /= cl_bobcycle.value;
	}
	else cycle = 0;

	if (cycle < cl_bobup.value)
	{
		if (cl_bobup.value != 0)
			cycle = M_PI * cycle / cl_bobup.value;
		else cycle = 0;
	}
	else if (cl_bobup.value != 1)
		cycle = M_PI + M_PI * (cycle - cl_bobup.value) / (1.0 - cl_bobup.value);
	else cycle = 0;

	// bob is proportional to velocity in the xy plane
	// (don't count Z, or jumping messes it up)
	bob = sqrt (cl.velocity[0] * cl.velocity[0] + cl.velocity[1] * cl.velocity[1]) * cl_bob.value;
	bob = bob * 0.3 + bob * 0.7 * sin (cycle);

	if (bob > 4)
		return 4;
	else if (bob < -7)
		return -7;
	return bob;
}


// =============================================================================


cvar_t	v_centermove = { "v_centermove", "0.15", CVAR_NONE };
cvar_t	v_centerspeed = { "v_centerspeed", "500", CVAR_NONE };


/*
==============================================================================

	VIEW BLENDING

==============================================================================
*/

void V_SetCshift (cshift_t *cs, int r, int g, int b, int pct)
{
	cs->destcolor[0] = r;
	cs->destcolor[1] = g;
	cs->destcolor[2] = b;
	cs->percent = pct;
	cs->initialpercent = pct;
	cs->initialtime = cl.time;
}


cshift_t	cshift_empty = { { 130, 80, 50 }, 0 };
cshift_t	cshift_water = { { 130, 80, 50 }, 128 };
cshift_t	cshift_slime = { { 0, 25, 5 }, 150 };
cshift_t	cshift_lava = { { 255, 80, 0 }, 150 };

float		v_blend[4];		// rgba 0.0 - 1.0

// johnfitz -- deleted BuildGammaTable(), V_CheckGamma(), gammatable[], and ramps[][]

/*
===============
V_ParseDamage
===============
*/
void V_ParseDamage (void)
{
	int		armor, blood;
	vec3_t	from;
	int		i;
	vec3_t	forward, right, up;
	entity_t *ent;
	float	side;
	float	count;

	armor = MSG_ReadByte ();
	blood = MSG_ReadByte ();
	for (i = 0; i < 3; i++)
		from[i] = MSG_ReadCoord (cl.protocolflags);

	count = blood * 0.5 + armor * 0.5;
	if (count < 10)
		count = 10;

	cl.faceanimtime = cl.time + 0.2;		// but sbar face into pain frame

	float pct = cl.cshifts[CSHIFT_DAMAGE].percent + 3 * count;

	if (pct < 0) pct = 0;
	if (pct > 150) pct = 150;

	float r = ((200 * armor) + (255 * blood)) / (armor + blood);
	float g = (100 * armor) / (armor + blood);
	float b = (100 * armor) / (armor + blood);

	V_SetCshift (&cl.cshifts[CSHIFT_DAMAGE], r, g, b, pct);

	// calculate view angle kicks
	ent = cl_entities[cl.viewentity];

	VectorSubtract (from, ent->origin, from);
	VectorNormalize (from);

	AngleVectors (ent->angles, forward, right, up);

	side = DotProduct (from, right);
	v_dmg_roll = count * side * v_kickroll.value;

	side = DotProduct (from, forward);
	v_dmg_pitch = count * side * v_kickpitch.value;

	side = DotProduct (from, up);
	v_dmg_yaw = count * side * v_kickyaw.value;

	v_dmg_time = cl.time + v_kicktime.value;
}


/*
==================
V_cshift_f
==================
*/
void V_cshift_f (void)
{
	V_SetCshift (&cl.cshifts[CSHIFT_VCSHIFT], atoi (Cmd_Argv (1)), atoi (Cmd_Argv (2)), atoi (Cmd_Argv (3)), atoi (Cmd_Argv (4)));
}


/*
==================
V_BonusFlash_f

When you run over an item, the server sends this command
==================
*/
void V_BonusFlash_f (void)
{
	V_SetCshift (&cl.cshifts[CSHIFT_BONUS], 215, 186, 69, 50);
}

/*
=============
V_SetContentsColor

Underwater, lava, etc each has a color shift
=============
*/
void V_SetContentsColor (int contents)
{
	switch (contents)
	{
	case CONTENTS_EMPTY:
	case CONTENTS_SOLID:
	case CONTENTS_SKY: // johnfitz -- no blend in sky
		cl.cshifts[CSHIFT_CONTENTS] = cshift_empty;
		break;
	case CONTENTS_LAVA:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_lava;
		break;
	case CONTENTS_SLIME:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_slime;
		break;
	default:
		cl.cshifts[CSHIFT_CONTENTS] = cshift_water;
	}
}

/*
=============
V_CalcPowerupCshift
=============
*/
void V_CalcPowerupCshift (void)
{
}

/*
=============
V_CalcBlend
=============
*/
void V_CalcBlend (void)
{
	cvar_t *cshiftpercent_cvars[NUM_CSHIFTS] = {
		&gl_cshiftpercent_damage,
		&gl_cshiftpercent_bonus,
		&gl_cshiftpercent_powerup,
		&gl_cshiftpercent_powerup,
		&gl_cshiftpercent_powerup,
		&gl_cshiftpercent_powerup,
		&gl_cshiftpercent_contents,
		&gl_cshiftpercent_contents
	};

	float r = 0;
	float g = 0;
	float b = 0;
	float a = 0;

	for (int j = 0; j < NUM_CSHIFTS; j++)
	{
		if (!gl_cshiftpercent.value)
			continue;

		// johnfitz -- apply only leaf contents color shifts during intermission
		if (cl.intermission && cshiftpercent_cvars[j] != &gl_cshiftpercent_contents)
			continue;
		// johnfitz

		float a2 = ((cl.cshifts[j].percent * gl_cshiftpercent.value) / 100.0) / 255.0;

		// QuakeSpasm -- also scale by the specific gl_cshiftpercent_* cvar
		if (cshiftpercent_cvars[j])
			a2 *= (cshiftpercent_cvars[j]->value / 100.0);
		// QuakeSpasm

		if (!(a2 > 0))
			continue;

		a = a + a2 * (1 - a);
		a2 = a2 / a;

		r = r * (1 - a2) + cl.cshifts[j].destcolor[0] * a2;
		g = g * (1 - a2) + cl.cshifts[j].destcolor[1] * a2;
		b = b * (1 - a2) + cl.cshifts[j].destcolor[2] * a2;
	}

	v_blend[0] = r / 255.0;
	v_blend[1] = g / 255.0;
	v_blend[2] = b / 255.0;
	v_blend[3] = a;

	if (v_blend[3] > 1) v_blend[3] = 1;
	if (v_blend[3] < 0) v_blend[3] = 0;
}


/*
=============
V_UpdateBlend -- johnfitz -- V_UpdatePalette cleaned up and renamed
=============
*/
void V_UpdateBlend (void)
{
	// set the powerup cshifts - this is to allow multiple powerups to be simultaneously active and their colour shifts to blend with each other
	V_SetCshift (&cl.cshifts[CSHIFT_QUAD], 0, 0, 255, (cl.items & IT_QUAD) ? 30 : 0);
	V_SetCshift (&cl.cshifts[CSHIFT_SUIT], 0, 255, 0, (cl.items & IT_SUIT) ? 20 : 0);
	V_SetCshift (&cl.cshifts[CSHIFT_RING], 100, 100, 100, (cl.items & IT_INVISIBILITY) ? 100 : 0);
	V_SetCshift (&cl.cshifts[CSHIFT_PENT], 255, 255, 0, (cl.items & IT_INVULNERABILITY) ? 30 : 0);

	// this should never happen
	if (cl.cshifts[CSHIFT_DAMAGE].initialtime > cl.time) cl.cshifts[CSHIFT_DAMAGE].initialtime = cl.time;
	if (cl.cshifts[CSHIFT_BONUS].initialtime > cl.time) cl.cshifts[CSHIFT_BONUS].initialtime = cl.time;

	// cshift drops are based on absolute time since the shift was initiated; this is to allow SCR_UpdateScreen to potentially be called multiple times between cl.time changes
	cl.cshifts[CSHIFT_DAMAGE].percent = cl.cshifts[CSHIFT_DAMAGE].initialpercent - (cl.time - cl.cshifts[CSHIFT_DAMAGE].initialtime) * 150;
	cl.cshifts[CSHIFT_BONUS].percent = cl.cshifts[CSHIFT_BONUS].initialpercent - (cl.time - cl.cshifts[CSHIFT_BONUS].initialtime) * 100;

	// and lower-bound it so that the blend calc doesn't mess up
	if (cl.cshifts[CSHIFT_DAMAGE].percent < 0) cl.cshifts[CSHIFT_DAMAGE].percent = 0;
	if (cl.cshifts[CSHIFT_BONUS].percent < 0) cl.cshifts[CSHIFT_BONUS].percent = 0;

	// blend it all together
	V_CalcBlend ();
}


/*
==============================================================================

	VIEW RENDERING

==============================================================================
*/

float angledelta (float a)
{
	a = anglemod (a);
	if (a > 180)
		a -= 360;
	return a;
}

/*
==================
CalcGunAngle
==================
*/
void CalcGunAngle (void)
{
	float	yaw, pitch, move;
	static float oldyaw = 0;
	static float oldpitch = 0;

	yaw = r_refdef.viewangles[YAW];
	pitch = -r_refdef.viewangles[PITCH];

	yaw = angledelta (yaw - r_refdef.viewangles[YAW]) * 0.4;
	if (yaw > 10)
		yaw = 10;
	if (yaw < -10)
		yaw = -10;
	pitch = angledelta (-pitch - r_refdef.viewangles[PITCH]) * 0.4;
	if (pitch > 10)
		pitch = 10;
	if (pitch < -10)
		pitch = -10;

	move = (cl.time - cl.oldtime) * 20;

	if (yaw > oldyaw)
	{
		if (oldyaw + move < yaw)
			yaw = oldyaw + move;
	}
	else
	{
		if (oldyaw - move > yaw)
			yaw = oldyaw - move;
	}

	if (pitch > oldpitch)
	{
		if (oldpitch + move < pitch)
			pitch = oldpitch + move;
	}
	else
	{
		if (oldpitch - move > pitch)
			pitch = oldpitch - move;
	}

	oldyaw = yaw;
	oldpitch = pitch;

	cl.viewent.angles[YAW] = r_refdef.viewangles[YAW] + yaw;
	cl.viewent.angles[PITCH] = -(r_refdef.viewangles[PITCH] + pitch);

	cl.viewent.angles[ROLL] -= v_idlescale.value * sin (cl.time * v_iroll_cycle.value) * v_iroll_level.value;
	cl.viewent.angles[PITCH] -= v_idlescale.value * sin (cl.time * v_ipitch_cycle.value) * v_ipitch_level.value;
	cl.viewent.angles[YAW] -= v_idlescale.value * sin (cl.time * v_iyaw_cycle.value) * v_iyaw_level.value;
}

/*
==============
V_BoundOffsets
==============
*/
void V_BoundOffsets (void)
{
	entity_t *ent = cl_entities[cl.viewentity];

	// absolutely bound refresh reletive to entity clipping hull
	// so the view can never be inside a solid wall

	if (r_refdef.vieworg[0] < ent->origin[0] - 14)
		r_refdef.vieworg[0] = ent->origin[0] - 14;
	else if (r_refdef.vieworg[0] > ent->origin[0] + 14)
		r_refdef.vieworg[0] = ent->origin[0] + 14;
	if (r_refdef.vieworg[1] < ent->origin[1] - 14)
		r_refdef.vieworg[1] = ent->origin[1] - 14;
	else if (r_refdef.vieworg[1] > ent->origin[1] + 14)
		r_refdef.vieworg[1] = ent->origin[1] + 14;
	if (r_refdef.vieworg[2] < ent->origin[2] - 22)
		r_refdef.vieworg[2] = ent->origin[2] - 22;
	else if (r_refdef.vieworg[2] > ent->origin[2] + 30)
		r_refdef.vieworg[2] = ent->origin[2] + 30;
}

/*
==============
V_AddIdle

Idle swaying
==============
*/
void V_AddIdle (void)
{
	r_refdef.viewangles[ROLL] += v_idlescale.value * sin (cl.time * v_iroll_cycle.value) * v_iroll_level.value;
	r_refdef.viewangles[PITCH] += v_idlescale.value * sin (cl.time * v_ipitch_cycle.value) * v_ipitch_level.value;
	r_refdef.viewangles[YAW] += v_idlescale.value * sin (cl.time * v_iyaw_cycle.value) * v_iyaw_level.value;
}


/*
==============
V_CalcViewRoll

Roll is induced by movement and damage
==============
*/
void V_CalcViewRoll (void)
{
	r_refdef.viewangles[ROLL] += V_CalcRoll (cl_entities[cl.viewentity]->angles, cl.velocity);

	if (v_dmg_time > cl.time && v_kicktime.value > 0)
	{
		r_refdef.viewangles[ROLL] += (v_dmg_time - cl.time) / v_kicktime.value * v_dmg_roll;
		r_refdef.viewangles[PITCH] += (v_dmg_time - cl.time) / v_kicktime.value * v_dmg_pitch;
		r_refdef.viewangles[YAW] += (v_dmg_time - cl.time) / v_kicktime.value * v_dmg_yaw;
	}

	if (cl.stats[STAT_HEALTH] <= 0)
	{
		r_refdef.viewangles[ROLL] = 80;	// dead view angle
		return;
	}
}

/*
==================
V_CalcIntermissionRefdef

==================
*/
void V_CalcIntermissionRefdef (void)
{
	entity_t *ent, *view;
	float		old;

	// ent is the player model (visible when out of body)
	ent = cl_entities[cl.viewentity];
	// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	VectorCopy (ent->origin, r_refdef.vieworg);
	VectorCopy (ent->angles, r_refdef.viewangles);
	view->model = NULL;

	// allways idle in intermission
	old = v_idlescale.value;
	v_idlescale.value = 1;
	V_AddIdle ();
	v_idlescale.value = old;
}


/*
==================
V_CalcRefdef
==================
*/
void V_CalcRefdef (void)
{
	entity_t *ent, *view;
	int			i;
	vec3_t		forward, right, up;
	vec3_t		angles;
	float		bob;
	static float oldz = 0;
	float punchblend, punch[3]; // lerped gunkick

	// ent is the player model (visible when out of body)
	ent = cl_entities[cl.viewentity];

	// view is the weapon model (only visible from inside body)
	view = &cl.viewent;

	// transform the view offset by the model's matrix to get the offset from
	// model origin for the view
	ent->angles[YAW] = cl.viewangles[YAW];	// the model should face the view dir
	ent->angles[PITCH] = -cl.viewangles[PITCH];	// the model should face the view dir

	bob = V_CalcBob ();

	// refresh position
	VectorCopy (ent->origin, r_refdef.vieworg);
	r_refdef.vieworg[2] += cl.viewheight + bob;

	// never let it sit exactly on a node line, because a water plane can
	// dissapear when viewed with the eye exactly on it.
	// the server protocol only specifies to 1/16 pixel, so add 1/32 in each axis
	r_refdef.vieworg[0] += 1.0 / 32;
	r_refdef.vieworg[1] += 1.0 / 32;
	r_refdef.vieworg[2] += 1.0 / 32;

	VectorCopy (cl.viewangles, r_refdef.viewangles);
	V_CalcViewRoll ();
	V_AddIdle ();

	// offsets
	angles[PITCH] = -ent->angles[PITCH]; // because entity pitches are actually backward
	angles[YAW] = ent->angles[YAW];
	angles[ROLL] = ent->angles[ROLL];

	AngleVectors (angles, forward, right, up);

	if (cl.maxclients <= 1) // johnfitz -- moved cheat-protection here from V_RenderView
		for (i = 0; i < 3; i++)
			r_refdef.vieworg[i] += scr_ofsx.value * forward[i] + scr_ofsy.value * right[i] + scr_ofsz.value * up[i];

	V_BoundOffsets ();

	// set up gun position
	VectorCopy (cl.viewangles, view->angles);

	CalcGunAngle ();

	VectorCopy (ent->origin, view->origin);
	view->origin[2] += cl.viewheight;

	for (i = 0; i < 3; i++)
		view->origin[i] += forward[i] * bob * 0.4;
	view->origin[2] += bob;

	// johnfitz -- removed all gun position fudging code (was used to keep gun from getting covered by sbar)
	// MarkV -- restored this with r_viewmodel_quake cvar
	if (r_viewmodel_quake.value)
	{
		// MH - just using 2
		view->origin[2] += 2;
	}

	view->model = cl.model_precache[cl.stats[STAT_WEAPON]];
	view->frame = cl.stats[STAT_WEAPONFRAME];
	view->colormapped = false;

	// johnfitz -- v_gunkick - mh - rewritten for sanity
	if (v_gunkick.value)
	{
		if (v_gunkick.value == 2)
		{
			punchblend = Q_fclamp ((cl.time - cl.punchtime) / 0.1f, 0, 1);
			Vector3Lerpf (punch, v_punchangles[1], v_punchangles[0], punchblend);
			Vector3Add (r_refdef.viewangles, punch, r_refdef.viewangles);
		}
		else Vector3Add (r_refdef.viewangles, cl.punchangle, r_refdef.viewangles);
	}
	// johnfitz

	// smooth out stair step ups
	if (cl.onground && ent->origin[2] - v_stepz > 0)
	{
		// this resolves herky-jerkies on vertical plats
		v_stepz = v_oldz + (cl.time - v_steptime) * 160; // idQuake used 80 here; BJP bumped it to 160

		if (v_stepz > ent->origin[2])
		{
			v_steptime = cl.time;
			v_stepz = v_oldz = ent->origin[2];
		}

		if (ent->origin[2] - v_stepz > 12)
		{
			v_steptime = cl.time;
			v_stepz = v_oldz = ent->origin[2] - 12;
		}

		r_refdef.vieworg[2] += v_stepz - ent->origin[2];
		view->origin[2] += v_stepz - ent->origin[2];
	}
	else
	{
		v_oldz = v_stepz = ent->origin[2];
		v_steptime = cl.time;
	}

	if (chase_active.value)
		Chase_UpdateForDrawing (); // johnfitz
}

/*
==================
V_RenderView

The player's clipping box goes from (-16 -16 -24) to (16 16 32) from
the entity origin, so any view position inside that will be valid
==================
*/
void V_RenderView (void)
{
	if (con_forcedup)
		return;

	if (cl.intermission)
		V_CalcIntermissionRefdef ();
	else if (!cl.paused /* && (cl.maxclients > 1 || key_dest == key_game) */)
		V_CalcRefdef ();

	// johnfitz -- removed lcd code

	R_RenderView ();
}


/*
==============================================================================

	INIT

==============================================================================
*/

/*
=============
V_Init
=============
*/
void V_Init (void)
{
	Cmd_AddCommand ("v_cshift", V_cshift_f);
	Cmd_AddCommand ("bf", V_BonusFlash_f);

	Cvar_RegisterVariable (&v_centermove);
	Cvar_RegisterVariable (&v_centerspeed);

	Cvar_RegisterVariable (&v_iyaw_cycle);
	Cvar_RegisterVariable (&v_iroll_cycle);
	Cvar_RegisterVariable (&v_ipitch_cycle);
	Cvar_RegisterVariable (&v_iyaw_level);
	Cvar_RegisterVariable (&v_iroll_level);
	Cvar_RegisterVariable (&v_ipitch_level);

	Cvar_RegisterVariable (&v_idlescale);
	Cvar_RegisterVariable (&crosshair);
	Cvar_RegisterVariable (&crosshaircolor);
	Cvar_RegisterVariable (&gl_cshiftpercent);
	Cvar_RegisterVariable (&gl_cshiftpercent_contents); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_damage); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_bonus); // QuakeSpasm
	Cvar_RegisterVariable (&gl_cshiftpercent_powerup); // QuakeSpasm

	Cvar_RegisterVariable (&scr_ofsx);
	Cvar_RegisterVariable (&scr_ofsy);
	Cvar_RegisterVariable (&scr_ofsz);
	Cvar_RegisterVariable (&cl_rollspeed);
	Cvar_RegisterVariable (&cl_rollangle);
	Cvar_RegisterVariable (&cl_bob);
	Cvar_RegisterVariable (&cl_bobcycle);
	Cvar_RegisterVariable (&cl_bobup);

	Cvar_RegisterVariable (&v_kicktime);
	Cvar_RegisterVariable (&v_kickroll);
	Cvar_RegisterVariable (&v_kickpitch);
	Cvar_RegisterVariable (&v_kickyaw);
	Cvar_RegisterVariable (&v_gunkick); // johnfitz

	Cvar_RegisterVariable (&r_viewmodel_quake); // MarkV
}


void V_NewMap (void)
{
	// reset step and damage timers
	v_oldz = v_stepz = 0;
	v_steptime = 0;
	v_dmg_time = 0;

	// clear punch angles
	v_punchangles[0][0] = v_punchangles[0][1] = v_punchangles[0][2] = 0.0f;
	v_punchangles[1][0] = v_punchangles[1][1] = v_punchangles[1][2] = 0.0f;
	cl.punchangle[0] = cl.punchangle[1] = cl.punchangle[2] = 0.0f;
	cl.punchtime = 0;
}



