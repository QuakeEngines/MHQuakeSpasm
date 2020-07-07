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
// cl_main.c  -- client main loop

#include "quakedef.h"
#include "bgmusic.h"

// we need to declare some mouse variables here, because the menu system
// references them even when on a unix system.

// these two are not intended to be set directly
cvar_t	cl_name = { "_cl_name", "player", CVAR_ARCHIVE };
cvar_t	cl_color = { "_cl_color", "0", CVAR_ARCHIVE };

cvar_t	cl_shownet = { "cl_shownet", "0", CVAR_NONE };	// can be 0, 1, or 2
cvar_t	cl_nolerp = { "cl_nolerp", "0", CVAR_NONE };

cvar_t	cfg_unbindall = { "cfg_unbindall", "1", CVAR_ARCHIVE };

cvar_t	lookspring = { "lookspring", "0", CVAR_ARCHIVE };
cvar_t	lookstrafe = { "lookstrafe", "0", CVAR_ARCHIVE };
cvar_t	sensitivity = { "sensitivity", "3", CVAR_ARCHIVE };
cvar_t	freelook = { "freelook", "1", CVAR_ARCHIVE };

cvar_t	m_pitch = { "m_pitch", "0.022", CVAR_ARCHIVE };
cvar_t	m_yaw = { "m_yaw", "0.022", CVAR_ARCHIVE };
cvar_t	m_forward = { "m_forward", "1", CVAR_ARCHIVE };
cvar_t	m_side = { "m_side", "0.8", CVAR_ARCHIVE };

cvar_t	cl_maxpitch = { "cl_maxpitch", "90", CVAR_ARCHIVE }; // johnfitz -- variable pitch clamping
cvar_t	cl_minpitch = { "cl_minpitch", "-90", CVAR_ARCHIVE }; // johnfitz -- variable pitch clamping

cvar_t	cl_extradlights = { "cl_extradlights", "1", CVAR_ARCHIVE }; // mh - extra dlights on certain trails and temp ent effects


client_static_t	cls;
client_state_t	cl;

// FIXME: put these on hunk?
lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
dlight_t		cl_dlights[MAX_DLIGHTS];

entity_t		*cl_entities[MAX_EDICTS]; // mh - doing this right

int				cl_numvisedicts;
entity_t		*cl_visedicts[MAX_EDICTS];

extern cvar_t	r_lerpmodels, r_lerpmove; // johnfitz

extern	float scr_centertime_off;

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState (void)
{
	if (!sv.active)
		Host_ClearMemory ();

	// wipe the entire cl structure
	memset (&cl, 0, sizeof (cl));

	SZ_Clear (&cls.message);

	// clear other arrays
	memset (cl_dlights, 0, sizeof (cl_dlights));
	memset (cl_lightstyle, 0, sizeof (cl_lightstyle));
	memset (cl_temp_entities, 0, sizeof (cl_temp_entities));
	memset (cl_beams, 0, sizeof (cl_beams));
	memset (cl_entities, 0, sizeof (cl_entities));

	// clear this here as well in case there isn't a server
	scr_centertime_off = 0;
}

/*
=====================
CL_Disconnect

Sends a disconnect message to the server
This is also called on Host_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect (void)
{
	if (key_dest == key_message)
		Key_EndChat ();	// don't get stuck in chat mode

	// stop sounds (especially looping!)
	S_StopAllSounds (true);
	BGM_Stop ();
	CDAudio_Stop ();

	// if running a local server, shut it down
	if (cls.demoplayback)
		CL_StopPlayback ();
	else if (cls.state == ca_connected)
	{
		if (cls.demorecording)
			CL_Stop_f ();

		Con_DPrintf ("Sending clc_disconnect\n");
		SZ_Clear (&cls.message);
		MSG_WriteByte (&cls.message, clc_disconnect);
		NET_SendUnreliableMessage (cls.netcon, &cls.message);
		SZ_Clear (&cls.message);
		NET_Close (cls.netcon);

		cls.state = ca_disconnected;

		if (sv.active)
			Host_ShutdownServer (false);
	}

	cls.demoplayback = cls.timedemo = false;
	cls.demopaused = false;
	cls.signon = 0;
	cl.intermission = 0;
}

void CL_Disconnect_f (void)
{
	CL_Disconnect ();

	if (sv.active)
		Host_ShutdownServer (false);
}


/*
=====================
CL_EstablishConnection

Host should be either "local" or a net address to be passed on
=====================
*/
void CL_EstablishConnection (const char *host)
{
	if (cls.state == ca_dedicated)
		return;

	if (cls.demoplayback)
		return;

	CL_Disconnect ();

	cls.netcon = NET_Connect (host);
	if (!cls.netcon)
		Host_Error ("CL_Connect: connect failed\n");
	Con_DPrintf ("CL_EstablishConnection: connected to %s\n", host);

	cls.demonum = -1;			// not in the demo loop now
	cls.state = ca_connected;
	cls.signon = 0;				// need all the signon messages before playing
	MSG_WriteByte (&cls.message, clc_nop);	// NAT Fix from ProQuake
}

/*
=====================
CL_SignonReply

An svc_signonnum has been received, perform a client side setup
=====================
*/
void CL_SignonReply (void)
{
	char 	str[8192];

	Con_DPrintf ("CL_SignonReply: %i\n", cls.signon);

	switch (cls.signon)
	{
	case 1:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "prespawn");
		break;

	case 2:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va ("name \"%s\"\n", cl_name.string));

		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, va ("color %i %i\n", ((int) cl_color.value) >> 4, ((int) cl_color.value) & 15));

		MSG_WriteByte (&cls.message, clc_stringcmd);
		sprintf (str, "spawn %s", cls.spawnparms);
		MSG_WriteString (&cls.message, str);
		break;

	case 3:
		MSG_WriteByte (&cls.message, clc_stringcmd);
		MSG_WriteString (&cls.message, "begin");
		Cache_Report ();		// print remaining memory
		break;

	case 4:
		SCR_EndLoadingPlaque ();		// allow normal screen updates
		break;
	}
}

/*
=====================
CL_NextDemo

Called to play the next demo in the demo loop
=====================
*/
void CL_NextDemo (void)
{
	char	str[1024];

	if (cls.demonum == -1)
		return;		// don't play demos

	if (!cls.demos[cls.demonum][0] || cls.demonum == MAX_DEMOS)
	{
		cls.demonum = 0;

		if (!cls.demos[cls.demonum][0])
		{
			Con_Printf ("No demos listed with startdemos\n");
			cls.demonum = -1;
			CL_Disconnect ();
			return;
		}
	}

	SCR_BeginLoadingPlaque ();

	sprintf (str, "playdemo %s\n", cls.demos[cls.demonum]);
	Cbuf_InsertText (str);
	cls.demonum++;
}

/*
==============
CL_PrintEntities_f
==============
*/
void CL_PrintEntities_f (void)
{
	if (cls.state != ca_connected)
		return;

	for (int i = 0; i < cl.num_entities; i++)
	{
		entity_t *ent = cl_entities[i];

		Con_Printf ("%3i:", i);

		if (!ent->model)
		{
			Con_Printf ("EMPTY\n");
			continue;
		}

		Con_Printf ("%s:%2i  (%5.1f,%5.1f,%5.1f) [%5.1f %5.1f %5.1f]\n",
			ent->model->name, ent->frame, ent->origin[0], ent->origin[1], ent->origin[2], ent->angles[0], ent->angles[1], ent->angles[2]);
	}
}

/*
===============
CL_AllocDlight

===============
*/
dlight_t *CL_GetDlight (int key)
{
	dlight_t *dl;
	dlight_t *oldest = &cl_dlights[0];

	// first look for an exact key match
	if (key)
	{
		dl = cl_dlights;

		for (int i = 0; i < MAX_DLIGHTS; i++, dl++)
		{
			if (dl->key == key)
			{
				return dl;
			}
		}
	}

	// then look for anything else
	dl = cl_dlights;

	for (int i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		// track the oldest dlight which we'll just replace if we can't find a free light
		if (dl->die < oldest->die) oldest = dl;

		// take the first dead light
		if (dl->die < cl.time || !(dl->radius > dl->minlight))
		{
			return dl;
		}
	}

	// take the oldest dl
	dl = oldest;

	return dl;
}


dlight_t *CL_AllocDlight (int key, float radius, int r, int g, int b)
{
	// get a dl
	dlight_t *dl = CL_GetDlight (key);

	// fill it in
	memset (dl, 0, sizeof (*dl));
	dl->key = key;

	if (cl.worldmodel->colouredlight)
	{
		dl->rgba[0] = (float) r / 255.0f;
		dl->rgba[1] = (float) g / 255.0f;
		dl->rgba[2] = (float) b / 255.0f;
	}
	else dl->rgba[0] = dl->rgba[1] = dl->rgba[2] = 1; // johnfitz -- lit support via lordhavoc

	// start time so that we can drop the radius framerate-independently
	dl->starttime = cl.time;

	// copy off the start radius for framerate-independent decays
	dl->startradius = dl->radius = radius;

	// and return it
	return dl;
}


dlight_t *CL_AllocDLightForModelFlags (entity_t *ent, float radius, int key)
{
	if (ent->model->flags & EF_WIZARDFLASH)
		return CL_AllocDlight (key, radius, DL_COLOR_GREEN);
	else if (ent->model->flags & EF_SHALRATHFLASH)
		return CL_AllocDlight (key, radius, DL_COLOR_PURPLE);
	else if (ent->model->flags & EF_SHAMBLERFLASH)
		return CL_AllocDlight (key, radius, DL_COLOR_BLUE);
	else if (ent->model->flags & EF_ORANGEFLASH)
		return CL_AllocDlight (key, radius, DL_COLOR_ORANGE);
	else if (ent->model->flags & EF_REDFLASH)
		return CL_AllocDlight (key, radius, DL_COLOR_RED);
	else if (ent->model->flags & EF_YELLOWFLASH)
		return CL_AllocDlight (key, radius, DL_COLOR_YELLOW);
	else if (ent->model->flags & EF_GREENFLASH)
		return CL_AllocDlight (key, radius, DL_COLOR_GREEN);
	else if (ent->model->flags & EF_PURPLEFLASH)
		return CL_AllocDlight (key, radius, DL_COLOR_PURPLE);
	else if (ent->model->flags & EF_BLUEFLASH)
		return CL_AllocDlight (key, radius, DL_COLOR_BLUE);
	else return NULL;
}


/*
===============
CL_DecayLights

===============
*/
void CL_DecayLights (void)
{
	for (int i = 0; i < MAX_DLIGHTS; i++)
	{
		dlight_t *dl = &cl_dlights[i];

		if (dl->die < cl.time || !(dl->radius > dl->minlight))
		{
			dl->radius = 0;
			dl->die = -1;
		}
		else if ((dl->radius = dl->startradius - ((cl.time - dl->starttime) * dl->decay)) < dl->minlight)
		{
			dl->radius = 0;
			dl->die = -1;
		}
	}
}


/*
===============
CL_LerpPoint

Determines the fraction between the last two messages that the objects
should be put at.
===============
*/
float	CL_LerpPoint (void)
{
	float	f, frac;

	f = cl.mtime[0] - cl.mtime[1];

	if (!f || cls.timedemo || sv.active)
	{
		cl.time = cl.mtime[0];
		return 1;
	}

	if (f > 0.1) // dropped packet, or start of demo
	{
		cl.mtime[1] = cl.mtime[0] - 0.1;
		f = 0.1;
	}

	frac = (cl.time - cl.mtime[1]) / f;

	if (frac < 0)
	{
		if (frac < -0.01)
			cl.time = cl.mtime[1];
		frac = 0;
	}
	else if (frac > 1)
	{
		if (frac > 1.01)
			cl.time = cl.mtime[0];
		frac = 1;
	}

	// johnfitz -- better nolerp behavior
	if (cl_nolerp.value)
		return 1;
	// johnfitz

	return frac;
}


// update trails at a consistent fixed rate
static const double cl_traildelta = HALF_FRAMEDELTA;

void CL_RocketTrail (entity_t *ent, int type)
{
	if (cl.time > ent->nexttrailtime)
	{
		R_RocketTrail (ent->oldtrailorigin, ent->origin, type);
		VectorCopy (ent->origin, ent->oldtrailorigin);
		ent->nexttrailtime = cl.time + cl_traildelta;
	}
}


void CL_ClearRocketTrail (entity_t *ent)
{
	VectorCopy (ent->origin, ent->oldtrailorigin);
	ent->nexttrailtime = cl.time + cl_traildelta;
}


qboolean CL_AllocExtraDlight (void)
{
	// arcane dimensions overloads/abuses certain trail types for different coloured blood
	if (arcdim) return false;

	// i just don't trust quoth to behave itself at all
	if (quoth) return false;

	// i just don't trust quoth to behave itself at all
	if (nehahra) return false;

	// if this var is set then spawn extra lights
	if (cl_extradlights.value) return true;

	// no extra light
	return false;
}


/*
===============
CL_RelinkEntities
===============
*/
void CL_RelinkEntities (void)
{
	int			j;
	float		frac, f, d;
	vec3_t		delta;
	float		bobjrotate;
	vec3_t		oldorg;
	dlight_t *dl;

	// determine partial update time
	frac = CL_LerpPoint ();

	cl_numvisedicts = 0;

	// interpolate player info
	for (int i = 0; i < 3; i++)
		cl.velocity[i] = cl.mvelocity[1][i] + frac * (cl.mvelocity[0][i] - cl.mvelocity[1][i]);

	if (cls.demoplayback)
	{
		// interpolate the angles
		for (j = 0; j < 3; j++)
		{
			d = cl.mviewangles[0][j] - cl.mviewangles[1][j];

			if (d > 180)
				d -= 360;
			else if (d < -180)
				d += 360;

			cl.viewangles[j] = cl.mviewangles[1][j] + frac * d;
		}
	}

	bobjrotate = anglemod (100 * cl.time);

	// start on the entity after the world
	for (int i = 1; i < cl.num_entities; i++)
	{
		entity_t *ent = cl_entities[i];

		if (!ent->model)
		{
			// empty slot
			// ericw -- efrags are only used for static entities in GLQuake
			// ent can't be static, so this is a no-op.
			// if (ent->forcelink)
			//	R_RemoveEfrags (ent);	// just became empty
			CL_ClearRocketTrail (ent);
			continue;
		}

		// if the object wasn't included in the last packet, remove it
		if (ent->msgtime != cl.mtime[0])
		{
			ent->model = NULL;
			ent->lerpflags |= LERP_RESETMOVE | LERP_RESETANIM; // johnfitz -- next time this entity slot is reused, the lerp will need to be reset
			CL_ClearRocketTrail (ent);
			continue;
		}

		VectorCopy (ent->origin, oldorg);

		if (ent->forcelink)
		{
			// the entity was not updated in the last message
			// so move to the final spot
			VectorCopy (ent->msg_origins[0], ent->origin);
			VectorCopy (ent->msg_angles[0], ent->angles);
		}
		else
		{
			// if the delta is large, assume a teleport and don't lerp
			f = frac;
			for (j = 0; j < 3; j++)
			{
				delta[j] = ent->msg_origins[0][j] - ent->msg_origins[1][j];
				if (delta[j] > 100 || delta[j] < -100)
				{
					f = 1;		// assume a teleportation, not a motion
					ent->lerpflags |= LERP_RESETMOVE; // johnfitz -- don't lerp teleports
				}
			}

			// johnfitz -- don't cl_lerp entities that will be r_lerped
			if (r_lerpmove.value && (ent->lerpflags & LERP_MOVESTEP))
				f = 1;
			// johnfitz

			// interpolate the origin and angles
			for (j = 0; j < 3; j++)
			{
				ent->origin[j] = ent->msg_origins[1][j] + f * delta[j];

				d = ent->msg_angles[0][j] - ent->msg_angles[1][j];
				if (d > 180)
					d -= 360;
				else if (d < -180)
					d += 360;
				ent->angles[j] = ent->msg_angles[1][j] + f * d;
			}
		}

		// rotate binary objects locally
		if (ent->model->flags & EF_ROTATE)
		{
			// there was a mod, can't remember which, messed this up.
			ent->angles[0] = 0;
			ent->angles[1] = bobjrotate;
			ent->angles[2] = 0;
		}

		if (ent->effects & EF_BRIGHTFIELD)
			R_EntityParticles (ent);

		if (ent->effects & EF_MUZZLEFLASH)
		{
			vec3_t		fv, rv, uv;

			if (i == cl.viewentity)
			{
				// player - select the flash colour based on the currently active ammo
				if (rogue)
				{
					if (cl.items & RIT_CELLS)
						dl = CL_AllocDlight (i, 200 + (rand () & 31), DL_COLOR_BLUE);
					else if (cl.items & RIT_LAVA_NAILS)
						dl = CL_AllocDlight (i, 200 + (rand () & 31), DL_COLOR_RED);
					else if (cl.items & RIT_PLASMA_AMMO)
						dl = CL_AllocDlight (i, 200 + (rand () & 31), DL_COLOR_BLUE);
					else dl = CL_AllocDlight (i, 200 + (rand () & 31), DL_COLOR_ORANGE);
				}
				else
				{
					if (cl.items & IT_CELLS)
						dl = CL_AllocDlight (i, 200 + (rand () & 31), DL_COLOR_BLUE);
					else dl = CL_AllocDlight (i, 200 + (rand () & 31), DL_COLOR_ORANGE);
				}
			}
			else
			{
				// monster - to do...
				if ((dl = CL_AllocDLightForModelFlags (ent, i, 200 + (rand () & 31))) == NULL)
					dl = CL_AllocDlight (i, 200 + (rand () & 31), DL_COLOR_ORANGE);
			}

			VectorCopy (ent->origin, dl->origin);
			dl->origin[2] += 16;
			AngleVectors (ent->angles, fv, rv, uv);

			VectorMA (dl->origin, 18, fv, dl->origin);
			dl->minlight = 32;
			dl->die = cl.time + 0.1;

			// johnfitz -- assume muzzle flash accompanied by muzzle flare, which looks bad when lerped
			if (r_lerpmodels.value != 2)
			{
				if (ent == cl_entities[cl.viewentity])
					cl.viewent.lerpflags |= LERP_RESETANIM | LERP_RESETANIM2; // no lerping for two frames
				else
					ent->lerpflags |= LERP_RESETANIM | LERP_RESETANIM2; // no lerping for two frames
			}
			// johnfitz
		}

		if (ent->effects & EF_BRIGHTLIGHT)
		{
			// to do - check where this is used....
			if ((dl = CL_AllocDLightForModelFlags (ent, i, 400 + (rand () & 31))) == NULL)
				dl = CL_AllocDlight (i, 400 + (rand () & 31), DL_COLOR_WHITE);

			VectorCopy (ent->origin, dl->origin);
			dl->origin[2] += 16;
			dl->die = cl.time + 0.001;
		}

		if (ent->effects & EF_DIMLIGHT)
		{
			if (i == cl.viewentity)
			{
				// powerup - select the light colour based on the powerup type
				if ((cl.items & IT_QUAD) && (cl.items & IT_INVULNERABILITY))
					dl = CL_AllocDlight (i, 400 + (rand () & 31), DL_COLOR_PURPLE);
				else if (cl.items & IT_QUAD)
					dl = CL_AllocDlight (i, 400 + (rand () & 31), DL_COLOR_BLUE);
				else if (cl.items & IT_INVULNERABILITY)
					dl = CL_AllocDlight (i, 400 + (rand () & 31), DL_COLOR_RED);
				else dl = CL_AllocDlight (i, 400 + (rand () & 31), DL_COLOR_WHITE);
			}
			else if ((dl = CL_AllocDLightForModelFlags (ent, i, 200 + (rand () & 31))) == NULL)
				dl = CL_AllocDlight (i, 200 + (rand () & 31), DL_COLOR_WHITE);

			VectorCopy (ent->origin, dl->origin);
			dl->die = cl.time + 0.001;
		}

		// i'd like to put dlights on some of the trail types but AD overloads some of them for e.g. simulating green blood, so i can't
		if (ent->model->flags & EF_GIB)
			CL_RocketTrail (ent, RT_BLOOD);
		else if (ent->model->flags & EF_ZOMGIB)
			CL_RocketTrail (ent, RT_SLIGHTBLOOD);
		else if (ent->model->flags & EF_TRACER)
		{
			if (CL_AllocExtraDlight ())
			{
				dl = CL_AllocDlight (i, 200, DL_COLOR_GREEN);
				VectorCopy (ent->origin, dl->origin);
				dl->die = cl.time + 0.01;
			}

			CL_RocketTrail (ent, RT_TRACER3);
		}
		else if (ent->model->flags & EF_TRACER2)
		{
			if (CL_AllocExtraDlight ())
			{
				dl = CL_AllocDlight (i, 200, DL_COLOR_ORANGE);
				VectorCopy (ent->origin, dl->origin);
				dl->die = cl.time + 0.01;
			}

			CL_RocketTrail (ent, RT_TRACER5);
		}
		else if (ent->model->flags & EF_ROCKET)
		{
			CL_RocketTrail (ent, RT_ROCKETTRAIL);
			dl = CL_AllocDlight (i, 200, DL_COLOR_ORANGE);
			VectorCopy (ent->origin, dl->origin);
			dl->die = cl.time + 0.01;
		}
		else if (ent->model->flags & EF_GRENADE)
			CL_RocketTrail (ent, RT_SMOKESMOKE);
		else if (ent->model->flags & EF_TRACER3)
		{
			if (CL_AllocExtraDlight ())
			{
				dl = CL_AllocDlight (i, 200, DL_COLOR_PURPLE);
				VectorCopy (ent->origin, dl->origin);
				dl->die = cl.time + 0.01;
			}

			CL_RocketTrail (ent, RT_VOORTRAIL);
		}

		ent->forcelink = false;

		if (i == cl.viewentity && !chase_active.value)
			continue;

		if (cl_numvisedicts < MAX_EDICTS)
		{
			cl_visedicts[cl_numvisedicts] = ent;
			cl_numvisedicts++;
		}
	}
}


/*
===============
CL_ReadFromServer

Read all incoming data from the server
===============
*/
int CL_ReadFromServer (double frametime)
{
	int			ret;
	extern int	num_temp_entities; // johnfitz
	int			num_beams = 0; // johnfitz
	int			num_dlights = 0; // johnfitz
	beam_t *b; // johnfitz
	dlight_t *dl; // johnfitz
	int			i; // johnfitz

	cl.oldtime = cl.time;
	cl.time += frametime;

	// keep the random time-dependent
	srand ((unsigned int) (cl.time * HALF_STANDARDFPS));

	do
	{
		ret = CL_GetMessage ();
		if (ret == -1)
			Host_Error ("CL_ReadFromServer: lost server connection");
		if (!ret)
			break;

		cl.last_received_message = realtime;
		CL_ParseServerMessage ();
	} while (ret && cls.state == ca_connected);

	if (cl_shownet.value)
		Con_Printf ("\n");

	CL_RelinkEntities ();
	CL_UpdateTEnts ();

	// johnfitz -- devstats

	// visedicts
	if (cl_numvisedicts > 256 && dev_peakstats.visedicts <= 256)
		Con_DWarning ("%i visedicts exceeds standard limit of 256.\n", cl_numvisedicts);
	dev_stats.visedicts = cl_numvisedicts;
	dev_peakstats.visedicts = q_max (cl_numvisedicts, dev_peakstats.visedicts);

	// temp entities
	if (num_temp_entities > 64 && dev_peakstats.tempents <= 64)
		Con_DWarning ("%i tempentities exceeds standard limit of 64 (max = %d).\n", num_temp_entities, MAX_TEMP_ENTITIES);
	dev_stats.tempents = num_temp_entities;
	dev_peakstats.tempents = q_max (num_temp_entities, dev_peakstats.tempents);

	// beams
	for (i = 0, b = cl_beams; i < MAX_BEAMS; i++, b++)
		if (b->model && b->endtime >= cl.time)
			num_beams++;
	if (num_beams > 24 && dev_peakstats.beams <= 24)
		Con_DWarning ("%i beams exceeded standard limit of 24 (max = %d).\n", num_beams, MAX_BEAMS);
	dev_stats.beams = num_beams;
	dev_peakstats.beams = q_max (num_beams, dev_peakstats.beams);

	// dlights
	for (i = 0, dl = cl_dlights; i < MAX_DLIGHTS; i++, dl++)
		if (dl->die >= cl.time && dl->radius > dl->minlight)
			num_dlights++;

	if (num_dlights > 32 && dev_peakstats.dlights <= 32)
		Con_DWarning ("%i dlights exceeded standard limit of 32 (max = %d).\n", num_dlights, MAX_DLIGHTS);

	dev_stats.dlights = num_dlights;
	dev_peakstats.dlights = q_max (num_dlights, dev_peakstats.dlights);

	// johnfitz

	// bring the links up to date
	return 0;
}

/*
=================
CL_SendCmd
=================
*/
void CL_SendCmd (double frametime)
{
	usercmd_t		cmd;

	if (cls.state != ca_connected)
		return;

	if (cls.signon == SIGNONS)
	{
		// get basic movement from keyboard
		CL_BaseMove (&cmd, frametime);

		// allow mice or other external controllers to add to the move
		IN_Move (&cmd, frametime);

		// send the unreliable message
		CL_SendMove (&cmd);
	}

	if (cls.demoplayback)
	{
		SZ_Clear (&cls.message);
		return;
	}

	// send the reliable message
	if (!cls.message.cursize)
		return;		// no message at all

	if (!NET_CanSendMessage (cls.netcon))
	{
		Con_DPrintf ("CL_SendCmd: can't send\n");
		return;
	}

	if (NET_SendMessage (cls.netcon, &cls.message) == -1)
		Host_Error ("CL_SendCmd: lost server connection");

	SZ_Clear (&cls.message);
}

/*
=============
CL_Tracepos_f -- johnfitz

display impact point of trace along VPN
=============
*/
void CL_Tracepos_f (void)
{
	vec3_t	v, w;

	if (cls.state != ca_connected)
		return;

	VectorMA (r_refdef.vieworg, 8192.0, vpn, v);
	TraceLine (r_refdef.vieworg, v, w);

	if (VectorLength (w) == 0)
		Con_Printf ("Tracepos: trace didn't hit anything\n");
	else
		Con_Printf ("Tracepos: (%i %i %i)\n", (int) w[0], (int) w[1], (int) w[2]);
}

/*
=============
CL_Viewpos_f -- johnfitz

display client's position and angles
=============
*/
void CL_Viewpos_f (void)
{
	if (cls.state != ca_connected)
		return;
#if 0
	// camera position
	Con_Printf ("Viewpos: (%i %i %i) %i %i %i\n",
		(int) r_refdef.vieworg[0],
		(int) r_refdef.vieworg[1],
		(int) r_refdef.vieworg[2],
		(int) r_refdef.viewangles[PITCH],
		(int) r_refdef.viewangles[YAW],
		(int) r_refdef.viewangles[ROLL]);
#else
	// player position
	Con_Printf ("Viewpos: (%i %i %i) %i %i %i\n",
		(int) cl_entities[cl.viewentity]->origin[0],
		(int) cl_entities[cl.viewentity]->origin[1],
		(int) cl_entities[cl.viewentity]->origin[2],
		(int) cl.viewangles[PITCH],
		(int) cl.viewangles[YAW],
		(int) cl.viewangles[ROLL]);
#endif
}

/*
=================
CL_Init
=================
*/
void CL_Init (void)
{
	SZ_Alloc (&cls.message, 1024);

	CL_InitInput ();
	CL_InitTEnts ();

	Cvar_RegisterVariable (&cl_name);
	Cvar_RegisterVariable (&cl_color);
	Cvar_RegisterVariable (&cl_upspeed);
	Cvar_RegisterVariable (&cl_forwardspeed);
	Cvar_RegisterVariable (&cl_backspeed);
	Cvar_RegisterVariable (&cl_sidespeed);
	Cvar_RegisterVariable (&cl_movespeedkey);
	Cvar_RegisterVariable (&cl_yawspeed);
	Cvar_RegisterVariable (&cl_pitchspeed);
	Cvar_RegisterVariable (&cl_anglespeedkey);
	Cvar_RegisterVariable (&cl_shownet);
	Cvar_RegisterVariable (&cl_nolerp);
	Cvar_RegisterVariable (&lookspring);
	Cvar_RegisterVariable (&lookstrafe);
	Cvar_RegisterVariable (&sensitivity);
	Cvar_RegisterVariable (&freelook);

	Cvar_RegisterVariable (&cl_alwaysrun);

	Cvar_RegisterVariable (&m_pitch);
	Cvar_RegisterVariable (&m_yaw);
	Cvar_RegisterVariable (&m_forward);
	Cvar_RegisterVariable (&m_side);

	Cvar_RegisterVariable (&cfg_unbindall);

	Cvar_RegisterVariable (&cl_maxpitch); // johnfitz -- variable pitch clamping
	Cvar_RegisterVariable (&cl_minpitch); // johnfitz -- variable pitch clamping

	Cvar_RegisterVariable (&cl_extradlights);

	Cmd_AddCommand ("entities", CL_PrintEntities_f);
	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("stop", CL_Stop_f);
	Cmd_AddCommand ("playdemo", CL_PlayDemo_f);
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f);

	Cmd_AddCommand ("tracepos", CL_Tracepos_f); // johnfitz
	Cmd_AddCommand ("viewpos", CL_Viewpos_f); // johnfitz
}

