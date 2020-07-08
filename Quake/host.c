/*
Copyright (C) 1996-2001 Id Software, Inc.
Copyright (C) 2002-2009 John Fitzgibbons and others
Copyright (C) 2007-2008 Kristian Duske
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
// host.c -- coordinates spawning and killing of local servers

#include "quakedef.h"
#include "bgmusic.h"
#include <setjmp.h>

/*

A server can allways be started, even if the system started out as a client
to a remote system.

A client can NOT be started if the system started as a dedicated server.

Memory is cleared / released when a server or client begins, not when they end.

*/

quakeparms_t *host_parms;

qboolean	host_initialized;		// true if into command execution

double		realtime;				// without any filtering or bounding

int		host_framecount;

int		host_hunklevel;

int		minimum_memory;

client_t *host_client;			// current client

jmp_buf 	host_abortserver;

byte *host_colormap;

cvar_t	host_framerate = { "host_framerate", "0", CVAR_NONE };	// set for slow motion
cvar_t	host_maxfps = { "host_maxfps", "72", CVAR_ARCHIVE }; // johnfitz
cvar_t	host_timescale = { "host_timescale", "0", CVAR_NONE }; // johnfitz

cvar_t	sys_ticrate = { "sys_ticrate", "0.05", CVAR_NONE }; // dedicated server
cvar_t	serverprofile = { "serverprofile", "0", CVAR_NONE };

cvar_t	fraglimit = { "fraglimit", "0", CVAR_NOTIFY | CVAR_SERVERINFO };
cvar_t	timelimit = { "timelimit", "0", CVAR_NOTIFY | CVAR_SERVERINFO };
cvar_t	teamplay = { "teamplay", "0", CVAR_NOTIFY | CVAR_SERVERINFO };
cvar_t	samelevel = { "samelevel", "0", CVAR_NONE };
cvar_t	noexit = { "noexit", "0", CVAR_NOTIFY | CVAR_SERVERINFO };
cvar_t	skill = { "skill", "1", CVAR_NONE };			// 0 - 3
cvar_t	deathmatch = { "deathmatch", "0", CVAR_NONE };	// 0, 1, or 2
cvar_t	coop = { "coop", "0", CVAR_NONE };			// 0 or 1

cvar_t	pausable = { "pausable", "1", CVAR_NONE };

cvar_t	developer = { "developer", "0", CVAR_NONE };

cvar_t	temp1 = { "temp1", "0", CVAR_NONE };

cvar_t devstats = { "devstats", "0", CVAR_NONE }; // johnfitz -- track developer statistics that vary every frame

devstats_t dev_stats, dev_peakstats;
overflowtimes_t dev_overflows; // this stores the last time overflow messages were displayed, not the last time overflows occured


/*
================
Host_EndGame
================
*/
void Host_EndGame (const char *message, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr, message);
	q_vsnprintf (string, sizeof (string), message, argptr);
	va_end (argptr);
	Con_DPrintf ("Host_EndGame: %s\n", string);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_EndGame: %s\n", string);	// dedicated servers exit

	if (cls.demonum != -1)
		CL_NextDemo ();
	else
		CL_Disconnect ();

	longjmp (host_abortserver, 1);
}

/*
================
Host_Error

This shuts down both the client and server
================
*/
void Host_Error (const char *error, ...)
{
	va_list		argptr;
	char		string[1024];
	static	qboolean inerror = false;

	if (inerror)
		Sys_Error ("Host_Error: recursively entered");
	inerror = true;

	SCR_EndLoadingPlaque ();		// reenable screen updates

	va_start (argptr, error);
	q_vsnprintf (string, sizeof (string), error, argptr);
	va_end (argptr);
	Con_Printf ("Host_Error: %s\n", string);

	if (sv.active)
		Host_ShutdownServer (false);

	if (cls.state == ca_dedicated)
		Sys_Error ("Host_Error: %s\n", string);	// dedicated servers exit

	CL_Disconnect ();
	cls.demonum = -1;
	cl.intermission = 0; // johnfitz -- for errors during intermissions (changelevel with no map found, etc.)

	inerror = false;

	longjmp (host_abortserver, 1);
}

/*
================
Host_FindMaxClients
================
*/
void	Host_FindMaxClients (void)
{
	int		i;

	svs.maxclients = 1;

	i = COM_CheckParm ("-dedicated");
	if (i)
	{
		cls.state = ca_dedicated;
		if (i != (com_argc - 1))
		{
			svs.maxclients = Q_atoi (com_argv[i + 1]);
		}
		else
			svs.maxclients = 8;
	}
	else
		cls.state = ca_disconnected;

	i = COM_CheckParm ("-listen");
	if (i)
	{
		if (cls.state == ca_dedicated)
			Sys_Error ("Only one of -dedicated or -listen can be specified");
		if (i != (com_argc - 1))
			svs.maxclients = Q_atoi (com_argv[i + 1]);
		else
			svs.maxclients = 8;
	}
	if (svs.maxclients < 1)
		svs.maxclients = 8;
	else if (svs.maxclients > MAX_SCOREBOARD)
		svs.maxclients = MAX_SCOREBOARD;

	svs.maxclientslimit = svs.maxclients;
	if (svs.maxclientslimit < 4)
		svs.maxclientslimit = 4;
	svs.clients = (struct client_s *) Hunk_AllocName (svs.maxclientslimit * sizeof (client_t), "clients");

	if (svs.maxclients > 1)
		Cvar_SetQuick (&deathmatch, "1");
	else
		Cvar_SetQuick (&deathmatch, "0");
}

void Host_Version_f (void)
{
	Con_Printf ("Quake Version %1.2f\n", VERSION);
	Con_Printf ("QuakeSpasm Version " QUAKESPASM_VER_STRING "\n");
	Con_Printf ("Exe: " __TIME__ " " __DATE__ "\n");
}

/* cvar callback functions : */
void Host_Callback_Notify (cvar_t *var)
{
	if (sv.active)
		SV_BroadcastPrintf ("\"%s\" changed to \"%s\"\n", var->name, var->string);
}

/*
===============
Host_WriteConfiguration

Writes key bindings and archived cvars to config.cfg
===============
*/
void Host_WriteConfiguration (void)
{
	// dedicated servers initialize the host but don't parse and set the
	// config.cfg cvars
	if (host_initialized && !isDedicated && !host_parms->errstate)
	{
		// play nice with other engine's configs - we load and exec config.cfg but we only save to our own .cfg file
		FILE *f = fopen (va ("%s/mhquakespasm.cfg", com_gamedir), "w");

		if (!f)
		{
			Con_Printf ("Couldn't write mhquakespasm.cfg.\n");
			return;
		}

		// VID_SyncCvars (); // johnfitz -- write actual current mode to config file, in case cvars were messed with

		Key_WriteBindings (f);
		Cvar_WriteVariables (f);

		// johnfitz -- extra commands to preserve state
		fprintf (f, "vid_restart\n");
		// johnfitz

		fclose (f);
	}
}


void Host_TimerCvarCallback_f (cvar_t *var)
{
	// always rearm the timers if one of the speed-control cvars is changed
	Host_RearmTimers ();
}


/*
=======================
Host_InitLocal
======================
*/
void Host_InitLocal (void)
{
	Cmd_AddCommand ("version", Host_Version_f);
	Cmd_AddCommand ("host_writeconfiguration", Host_WriteConfiguration); // MH - this should have been in Quake from the get-go.

	Host_InitCommands ();

	Cvar_RegisterVariable (&host_framerate);
	Cvar_RegisterVariable (&host_maxfps); // johnfitz
	Cvar_RegisterVariable (&host_timescale); // johnfitz

	Cvar_SetCallback (&host_framerate, Host_TimerCvarCallback_f);
	Cvar_SetCallback (&host_maxfps, Host_TimerCvarCallback_f);
	Cvar_SetCallback (&host_timescale, Host_TimerCvarCallback_f);

	Cvar_RegisterVariable (&devstats); // johnfitz

	Cvar_RegisterVariable (&sys_ticrate);
	Cvar_RegisterVariable (&serverprofile);

	Cvar_RegisterVariable (&fraglimit);
	Cvar_RegisterVariable (&timelimit);
	Cvar_RegisterVariable (&teamplay);
	Cvar_SetCallback (&fraglimit, Host_Callback_Notify);
	Cvar_SetCallback (&timelimit, Host_Callback_Notify);
	Cvar_SetCallback (&teamplay, Host_Callback_Notify);
	Cvar_RegisterVariable (&samelevel);
	Cvar_RegisterVariable (&noexit);
	Cvar_SetCallback (&noexit, Host_Callback_Notify);
	Cvar_RegisterVariable (&skill);
	Cvar_RegisterVariable (&developer);
	Cvar_RegisterVariable (&coop);
	Cvar_RegisterVariable (&deathmatch);

	Cvar_RegisterVariable (&pausable);

	Cvar_RegisterVariable (&temp1);

	Host_FindMaxClients ();
}


/*
=================
SV_ClientPrintf

Sends text across to be displayed
FIXME: make this just a stuffed echo?
=================
*/
void SV_ClientPrintf (const char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr, fmt);
	q_vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_print);
	MSG_WriteString (&host_client->message, string);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients
=================
*/
void SV_BroadcastPrintf (const char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];
	int			i;

	va_start (argptr, fmt);
	q_vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active && svs.clients[i].spawned)
		{
			MSG_WriteByte (&svs.clients[i].message, svc_print);
			MSG_WriteString (&svs.clients[i].message, string);
		}
	}
}

/*
=================
Host_ClientCommands

Send text over to the client to be executed
=================
*/
void Host_ClientCommands (const char *fmt, ...)
{
	va_list		argptr;
	char		string[1024];

	va_start (argptr, fmt);
	q_vsnprintf (string, sizeof (string), fmt, argptr);
	va_end (argptr);

	MSG_WriteByte (&host_client->message, svc_stufftext);
	MSG_WriteString (&host_client->message, string);
}

/*
=====================
SV_DropClient

Called when the player is getting totally kicked off the host
if (crash = true), don't bother sending signofs
=====================
*/
void SV_DropClient (qboolean crash)
{
	int		saveSelf;
	int		i;
	client_t *client;

	if (!crash)
	{
		// send any final messages (don't check for errors)
		if (NET_CanSendMessage (host_client->netconnection))
		{
			MSG_WriteByte (&host_client->message, svc_disconnect);
			NET_SendMessage (host_client->netconnection, &host_client->message);
		}

		if (host_client->edict && host_client->spawned)
		{
			// call the prog function for removing a client
			// this will set the body to a dead frame, among other things
			saveSelf = pr_global_struct->self;
			pr_global_struct->self = EDICT_TO_PROG (host_client->edict);
			PR_ExecuteProgram (pr_global_struct->ClientDisconnect);
			pr_global_struct->self = saveSelf;
		}

		Sys_Printf ("Client %s removed\n", host_client->name);
	}

	// break the net connection
	NET_Close (host_client->netconnection);
	host_client->netconnection = NULL;

	// free the client (the body stays around)
	host_client->active = false;
	host_client->name[0] = 0;
	host_client->old_frags = -999999;
	net_activeconnections--;

	// send notification to all clients
	for (i = 0, client = svs.clients; i < svs.maxclients; i++, client++)
	{
		if (!client->active)
			continue;
		MSG_WriteByte (&client->message, svc_updatename);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteString (&client->message, "");
		MSG_WriteByte (&client->message, svc_updatefrags);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteShort (&client->message, 0);
		MSG_WriteByte (&client->message, svc_updatecolors);
		MSG_WriteByte (&client->message, host_client - svs.clients);
		MSG_WriteByte (&client->message, 0);
	}
}

/*
==================
Host_ShutdownServer

This only happens at the end of a game, not between levels
==================
*/
void Host_ShutdownServer (qboolean crash)
{
	int		i;
	int		count;
	sizebuf_t	buf;
	byte		message[4];
	double	start;

	if (!sv.active)
		return;

	sv.active = false;

	// stop all client sounds immediately
	if (cls.state == ca_connected)
		CL_Disconnect ();

	// flush any pending messages - like the score!!!
	start = Sys_DoubleTime ();
	do
	{
		count = 0;
		for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		{
			if (host_client->active && host_client->message.cursize)
			{
				if (NET_CanSendMessage (host_client->netconnection))
				{
					NET_SendMessage (host_client->netconnection, &host_client->message);
					SZ_Clear (&host_client->message);
				}
				else
				{
					NET_GetMessage (host_client->netconnection);
					count++;
				}
			}
		}
		if ((Sys_DoubleTime () - start) > 3.0)
			break;
	} while (count);

	// make sure all the clients know we're disconnecting
	buf.data = message;
	buf.maxsize = 4;
	buf.cursize = 0;
	MSG_WriteByte (&buf, svc_disconnect);
	count = NET_SendToAll (&buf, 5.0);
	if (count)
		Con_Printf ("Host_ShutdownServer: NET_SendToAll failed for %u clients\n", count);

	for (i = 0, host_client = svs.clients; i < svs.maxclients; i++, host_client++)
		if (host_client->active)
			SV_DropClient (crash);

	// clear structures
	//	memset (&sv, 0, sizeof(sv)); // ServerSpawn already do this by Host_ClearMemory
	memset (svs.clients, 0, svs.maxclientslimit * sizeof (client_t));
}


/*
================
Host_ClearMemory

This clears all the memory used by both the client and server, but does
not reinitialize anything.
================
*/
void Host_ClearMemory (void)
{
	Con_DPrintf ("Clearing memory\n");
	Mod_ClearAll ();

	/* host_hunklevel MUST be set at this point */
	Hunk_FreeToLowMark (host_hunklevel);

	cls.signon = 0;

	memset (&sv, 0, sizeof (sv));
	memset (&cl, 0, sizeof (cl));
}


// ==============================================================================
// Host Frame
// ==============================================================================

typedef struct hosttimer_s {
	double		lastframe;
	double		nextframe;
} hosttimer_t;


void HostTimer_Rearm (hosttimer_t *timer)
{
	timer->lastframe = realtime;
	timer->nextframe = realtime + HOST_FRAMEDELTA;
}


qboolean HostTimer_RunFrame (hosttimer_t *timer)
{
	// always run a frame if in a timedemo, otherwise only run one if the time for the next frame has arrived
	if (cls.timedemo)
		return true;
	else return (realtime >= timer->nextframe);
}


double HostTimer_Adjust (hosttimer_t *timer, double maxfps)
{
	double delta = 0.0;

	// get current frametime
	double frametime = realtime - timer->lastframe;

	// evaluate delta - timedemos always run but must accumulate a delta otherwise the times will be off when they finish
	if (cls.timedemo)
		delta = HOST_FRAMEDELTA;
	else if (key_dest != key_game || cls.signon != SIGNONS)
		delta = HOST_FRAMEDELTA;
	else
	{
		if (maxfps > 0.0)
			delta = (1.0 / maxfps);
		else delta = 0;

		// adjust if desired
		if (host_framerate.value > 0) frametime = host_framerate.value;
		if (host_timescale.value > 0) frametime *= host_timescale.value;
	}

	// don't allow really long frames or really long deltas
	if (frametime > 0.1) frametime = 0.1;
	if (delta > 0.1) delta = 0.1;

	// bring on the times for the next frame
	timer->lastframe = realtime;
	timer->nextframe += delta;

	return frametime;
}


static hosttimer_t sv_timer = { 0, 0 };
static hosttimer_t cl_timer = { 0, 0 };


double Host_GetRealtime (void)
{
	// ensure we get a view of realtime that is always > the prior frame
	double oldrealtime = realtime;
	while ((realtime = Sys_DoubleTime ()) <= oldrealtime);
	return realtime;
}


void Host_RearmTimers (void)
{
	// get a new realtime because the action which caused the rearm may have taken some time
	realtime = Host_GetRealtime ();

	// if host_maxfps changes then reset the timers and deltas so that we don't get bad accumulation
	HostTimer_Rearm (&cl_timer);
	HostTimer_Rearm (&sv_timer);
}


/*
===================
Host_GetConsoleCommands

Add them exactly as if they had been typed at the console
===================
*/
void Host_GetConsoleCommands (void)
{
	if (!isDedicated)
		return;	// no stdin necessary in graphical mode

	while (1)
	{
		const char *cmd = Sys_ConsoleInput ();

		if (!cmd)
			break;

		Cbuf_AddText (cmd);
	}
}


double Host_ServerTime (void)
{
	// bring the server timer up to date (never exceed HOST_STANDARDFPS but we are allowed go below it)
	// this is slightly complex because host_maxfps 0 signifies unbounded FPS but is yet less than HOST_STANDARDFPS,
	// so we need a bunch of checks to test for all of that too
	if (host_maxfps.value > 0)
	{
		if (host_maxfps.value < HOST_STANDARDFPS)
			return HostTimer_Adjust (&sv_timer, host_maxfps.value);
		else return HostTimer_Adjust (&sv_timer, HOST_STANDARDFPS);
	}

	return HostTimer_Adjust (&sv_timer, HOST_STANDARDFPS);
}


double Host_ClientTime (void)
{
	// to do - if vsync is on then we always run a client frame and let vsync control the timing
	return HostTimer_Adjust (&cl_timer, host_maxfps.value);
}


/*
==================
Host_ServerFrame
==================
*/
void Host_ServerFrame (void)
{
	double frametime = Host_ServerTime ();

	// these are really client operations but they must run synchronized with the server tick rate so run them here
	Cbuf_Execute ();
	NET_Poll ();
	CL_SendCmd (frametime);

	// only if a server is active
	if (sv.active)
	{
		PR_RunClear ();

		// run the world state
		pr_global_struct->frametime = frametime;

		// set the time and clear the general datagram
		SV_ClearDatagram ();

		// check for new clients
		SV_CheckForNewClients ();

		// read client messages
		SV_RunClients (frametime);

		// move things around and think
		// always pause in single player if in console or menus
		if (!sv.paused && (svs.maxclients > 1 || key_dest == key_game))
			SV_Physics (frametime);

		// johnfitz -- devstats
		if (cls.signon == SIGNONS)
		{
			int active = 0;

			for (int i = 0; i < sv.num_edicts; i++)
			{
				edict_t *ent = EDICT_NUM (i);
				if (!ent->free)
					active++;
			}

			if (active > 600 && dev_peakstats.edicts <= 600)
				Con_DWarning ("%i edicts exceeds standard limit of 600.\n", active);

			dev_stats.edicts = active;
			dev_peakstats.edicts = q_max (active, dev_peakstats.edicts);
		}
		// johnfitz

		// send all messages to the clients
		SV_SendClientMessages ();
	}
}


void Host_ClientFrame (void)
{
	double frametime = Host_ClientTime ();

	// fetch results from server
	if (cls.state == ca_connected)
		CL_ReadFromServer (frametime);

	SCR_UpdateScreen ();

	// update audio
	BGM_Update ();	// adds music raw samples and/or advances midi driver

	if (cls.signon == SIGNONS)
	{
		S_Update (r_origin, vpn, vright, vup);
		CL_DecayLights ();
	}
	else S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);

	CDAudio_Update ();
}


/*
==================
Host_Frame

Runs all active servers
==================
*/
void _Host_Frame (void)
{
	static qboolean host_skipnextclientframe = false;

	// something bad happened, or the server disconnected
	if (setjmp (host_abortserver))
	{
		// also reset the timers and deltas here
		Host_RearmTimers ();
		return;
	}

	// advance real time
	realtime = Host_GetRealtime ();

	// keep the random time-dependent (note: this is also done on the server and client with their view of time, so that randomized
	// server or client effects are (1) consistent across different runs, and (2) correctly paused when the server or client is paused).
	srand ((unsigned int) (realtime * HALF_STANDARDFPS));

	// get new key events
	Key_UpdateForDest ();
	IN_UpdateInputMode ();
	Sys_SendKeyEvents ();

	// allow mice or other external controllers to add commands
	IN_Commands ();

	// check for commands typed to the host
	Host_GetConsoleCommands ();

	// if a server frame runs we must also run a client frame to get the results immediately; otherwise
	// we only run a client frame if the accumulated time exceeds the delta and sleep if not
	// if both a server frame and a client frame run we skip the next client frame so that we don't run an extra frame
	if (HostTimer_RunFrame (&sv_timer))
	{
		Host_ServerFrame ();
		Host_ClientFrame ();
		host_skipnextclientframe = true;
	}
	else if (HostTimer_RunFrame (&cl_timer))
	{
		if (host_skipnextclientframe)
			host_skipnextclientframe = false;
		else Host_ClientFrame ();
	}
	else if (!cls.timedemo && host_maxfps.value && host_maxfps.value < 500)
		Sys_Sleep (1);

	// count frames for timedemos
	host_framecount++;

}

void Host_Frame (void)
{
	double	time1, time2;
	static double	timetotal;
	static int		timecount;
	int		i, c, m;

	if (!serverprofile.value)
	{
		_Host_Frame ();
		return;
	}

	time1 = Sys_DoubleTime ();
	_Host_Frame ();
	time2 = Sys_DoubleTime ();

	timetotal += time2 - time1;
	timecount++;

	if (timecount < 1000)
		return;

	m = timetotal * 1000 / timecount;
	timecount = 0;
	timetotal = 0;
	c = 0;
	for (i = 0; i < svs.maxclients; i++)
	{
		if (svs.clients[i].active)
			c++;
	}

	Con_Printf ("serverprofile: %2i clients %2i msec\n", c, m);
}

/*
====================
Host_Init
====================
*/
void Host_Init (void)
{
	if (standard_quake)
		minimum_memory = MINIMUM_MEMORY;
	else	minimum_memory = MINIMUM_MEMORY_LEVELPAK;

	if (COM_CheckParm ("-minmemory"))
		host_parms->memsize = minimum_memory;

	if (host_parms->memsize < minimum_memory)
		Sys_Error ("Only %4.1f megs of memory available, can't execute game", host_parms->memsize / (float) 0x100000);

	com_argc = host_parms->argc;
	com_argv = host_parms->argv;

	Memory_Init (host_parms->membase, host_parms->memsize);
	Cbuf_Init ();
	Cmd_Init ();
	LOG_Init (host_parms);
	Cvar_Init (); // johnfitz
	COM_Init ();
	COM_InitFilesystem ();
	Host_InitLocal ();
	W_LoadWadFile (); // johnfitz -- filename is now hard-coded for honesty
	if (cls.state != ca_dedicated)
	{
		Key_Init ();
		Con_Init ();
	}
	PR_Init ();
	Mod_Init ();
	NET_Init ();
	SV_Init ();

	Con_Printf ("Exe: " __TIME__ " " __DATE__ "\n");
	Con_Printf ("%4.1f megabyte heap\n", host_parms->memsize / (1024 * 1024.0));

	if (cls.state != ca_dedicated)
	{
		host_colormap = (byte *) COM_LoadHunkFile ("gfx/colormap.lmp", NULL);
		if (!host_colormap)
			Sys_Error ("Couldn't load gfx/colormap.lmp");

		V_Init ();
		Chase_Init ();
		M_Init ();
		ExtraMaps_Init (); // johnfitz
		Modlist_Init (); // johnfitz
		DemoList_Init (); // ericw
		VID_Init ();
		IN_Init ();
		TexMgr_Init (); // johnfitz
		Draw_Init ();
		SCR_Init ();
		R_Init ();
		S_Init ();
		CDAudio_Init ();
		BGM_Init ();
		Sbar_Init ();
		CL_Init ();
	}

	Hunk_AllocName (0, "-HOST_HUNKLEVEL-");
	host_hunklevel = Hunk_LowMark ();

	host_initialized = true;
	Con_Printf ("\n========= Quake Initialized =========\n\n");

	if (cls.state != ca_dedicated)
	{
		Cbuf_InsertText ("exec quake.rc\n");
		// johnfitz -- in case the vid mode was locked during vid_init, we can unlock it now.
			// note: two leading newlines because the command buffer swallows one of them.
		Cbuf_AddText ("\n\nvid_unlock\n");
	}

	if (cls.state == ca_dedicated)
	{
		Cbuf_AddText ("exec autoexec.cfg\n");
		Cbuf_AddText ("stuffcmds");
		Cbuf_Execute ();
		if (!sv.active)
			Cbuf_AddText ("map start\n");
	}
}


/*
===============
Host_Shutdown

FIXME: this is a callback from Sys_Quit and Sys_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void Host_Shutdown (void)
{
	static qboolean isdown = false;

	if (isdown)
	{
		printf ("recursive shutdown\n");
		return;
	}
	isdown = true;

	// keep Con_Printf from trying to update the screen
	scr_disabled_for_loading = true;

	Host_WriteConfiguration ();

	NET_Shutdown ();

	if (cls.state != ca_dedicated)
	{
		if (con_initialized)
			History_Shutdown ();
		BGM_Shutdown ();
		CDAudio_Shutdown ();
		S_Shutdown ();
		IN_Shutdown ();
		VID_Shutdown ();
	}

	LOG_Close ();
}

