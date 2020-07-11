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

// screen.c -- master for refresh, status bar, console, chat, notify, etc

#include "quakedef.h"

/*

background clear
rendering
turtle/net/ram icons
sbar
centerprint / slow centerprint
notify lines
intermission / finale overlay
loading plaque
console
menu

required background clears
required update regions


syncronous draw mode or async
One off screen buffer, with updates either copied or xblited
Need to double buffer?


async draw will require the refresh area to be cleared, because it will be
xblited, but sync draw can just ignore it.

sync
draw

CenterPrint ()
SlowPrint ()
Screen_Update ();
Con_Printf ();

net
turn off messages option

the refresh is allways rendered, unless the console is full screen


console is:
	notify lines
	half
	full

*/


int			glx, gly, glwidth, glheight;

float		scr_con_current;
float		scr_conlines;		// lines of console to display

// johnfitz -- new cvars
// MH - reverted a bunch of these to CVAR_NONE for Quake default behaviours
cvar_t		scr_menuscale = { "scr_menuscale", "1", CVAR_NONE };
cvar_t		scr_sbarscale = { "scr_sbarscale", "1", CVAR_NONE };
cvar_t		scr_sbaralpha = { "scr_sbaralpha", "1", CVAR_NONE }; // MH - revert Quake default
cvar_t		scr_conwidth = { "scr_conwidth", "0", CVAR_NONE };
cvar_t		scr_conscale = { "scr_conscale", "1", CVAR_NONE };
cvar_t		scr_crosshairscale = { "scr_crosshairscale", "1", CVAR_NONE };

// MH - differernt behaviour in debug builds
#ifdef _DEBUG
cvar_t		scr_showfps = { "scr_showfps", "1", CVAR_NONE };
#else
cvar_t		scr_showfps = { "scr_showfps", "0", CVAR_NONE };
#endif

cvar_t		scr_clock = { "scr_clock", "0", CVAR_NONE };
// johnfitz

cvar_t		scr_viewsize = { "viewsize", "100", CVAR_ARCHIVE };
cvar_t		scr_fov = { "fov", "90", CVAR_NONE };	// 10 - 170
cvar_t		scr_conspeed = { "scr_conspeed", "300", CVAR_NONE };
cvar_t		scr_centertime = { "scr_centertime", "2", CVAR_NONE };
cvar_t		scr_showram = { "showram", "1", CVAR_NONE };
cvar_t		scr_showturtle = { "showturtle", "0", CVAR_NONE };
cvar_t		scr_showpause = { "showpause", "1", CVAR_NONE };
cvar_t		scr_printspeed = { "scr_printspeed", "8", CVAR_NONE };

qboolean	scr_initialized;		// ready to draw
qboolean	scr_timerefresh;

qpic_t *scr_ram;
qpic_t *scr_net;
qpic_t *scr_turtle;

int			clearconsole;
int			clearnotify;

qboolean	scr_disabled_for_loading;
qboolean	scr_drawloading;
qboolean	scr_remove_console = false;

float		scr_disabled_time;

void SCR_ScreenShot_f (void);

/*
===============================================================================

CENTER PRINTING

===============================================================================
*/

char		scr_centerstring[1024];
float		scr_centertime_start;	// for slow victory printing
float		scr_centertime_off;
int			scr_center_lines;
int			scr_erase_lines;
int			scr_erase_center;

/*
==============
SCR_CenterPrint

Called for important messages that should stay in the center of the screen
for a few moments
==============
*/
void SCR_CenterPrint (const char *str) // update centerprint data
{
	strncpy (scr_centerstring, str, sizeof (scr_centerstring) - 1);
	scr_centertime_off = cl.time + scr_centertime.value;
	scr_centertime_start = cl.time;

	// count the number of lines for centering
	scr_center_lines = 1;
	str = scr_centerstring;
	while (*str)
	{
		if (*str == '\n')
			scr_center_lines++;
		str++;
	}
}

void SCR_DrawCenterString (void) // actually do the drawing
{
	char *start;
	int		l;
	int		j;
	int		x, y;
	int		remaining;

	GL_SetCanvas (CANVAS_MENU); // johnfitz

// the finale prints the characters one at a time
	if (cl.intermission)
		remaining = scr_printspeed.value * (cl.time - scr_centertime_start);
	else
		remaining = 9999;

	scr_erase_center = 0;
	start = scr_centerstring;

#if 0
	if (scr_center_lines <= 4)
		y = 200 * 0.35;	// johnfitz -- 320x200 coordinate system
	else
		y = 48;
	if (crosshair.value)
		y -= 8;
#else
	// https://github.com/ESWAT/john-carmack-plan-archive/blob/master/by_year/johnc_plan_1996.txt#L3436
	// "changed center print position for very long text messages" and "end of e4 text crash" in the same .plan entry.
	// now, the first of these is 100% the old code, but the reason for it is, i'm 99% certain, due to the second.
	// a starting y of 48 positions the center text directly under the banner, so that's exhibit 1.  exhibit 2 is
	// the end of e4 text being 17 lines, so with the original "y = 200*0.35" code that would cause this exact value to 
	// overflow the software vid.buffer for a 320x200 resolution.  Taken together, this constitutes evidence that 
	// "y = 48" for >= 4 lines was intended to catch intermission text and position it under the banner in a non-crashing
	// way, rather than as intended different behaviour for non-intermission centerprints.
	if (cl.intermission)
		y = 48;
	else y = 100 - scr_center_lines * 4;
#endif

	Draw_BeginString ();

	do
	{
		// scan the width of the line
		for (l = 0; l < 40; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (320 - l * 8) / 2;	// johnfitz -- 320x200 coordinate system

		for (j = 0; j < l; j++, x += 8)
		{
			Draw_StringCharacter (x, y, start[j]);	// johnfitz -- stretch overlays

			if (!remaining--)
			{
				Draw_EndString ();
				return;
			}
		}

		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;		// skip the \n
	} while (1);

	Draw_EndString ();
}


void SCR_CheckDrawCenterString (void)
{
	if (scr_center_lines > scr_erase_lines)
		scr_erase_lines = scr_center_lines;

	if ((cl.time > scr_centertime_off) && !cl.intermission)
		return;
	if (key_dest != key_game)
		return;
	if (cl.paused) // johnfitz -- don't show centerprint during a pause
		return;

	SCR_DrawCenterString ();
}

// =============================================================================


float SCR_CalcFovX (float fov_y, float width, float height)
{
	// bound, don't crash
	if (fov_y < 1) fov_y = 1;
	if (fov_y > 179) fov_y = 179;

	return (atan (width / (height / tan ((fov_y * M_PI) / 360.0f))) * 360.0f) / M_PI;
}


float SCR_CalcFovY (float fov_x, float width, float height)
{
	// bound, don't crash
	if (fov_x < 1) fov_x = 1;
	if (fov_x > 179) fov_x = 179;

	return (atan (height / (width / tan ((fov_x * M_PI) / 360.0f))) * 360.0f) / M_PI;
}


void SCR_SetFOV (refdef_t *rd, int fovvar, int width, int height)
{
	float aspect = (float) height / (float) width;

	// set up relative to a baseline aspect of 640x480 with a 48-high sbar
#define BASELINE_W	640.0f
#define BASELINE_H	432.0f

	// http://www.gamedev.net/topic/431111-perspective-math-calculating-horisontal-fov-from-vertical/
	// horizontalFov = atan (tan (verticalFov) * aspectratio)
	// verticalFov = atan (tan (horizontalFov) / aspectratio)
	if (aspect > (BASELINE_H / BASELINE_W))
	{
		// use the same calculation as GLQuake did (horizontal is constant, vertical varies)
		rd->fov_x = fovvar;
		rd->fov_y = SCR_CalcFovY (rd->fov_x, width, height);
	}
	else
	{
		// alternate calculation (vertical is constant, horizontal varies)
		// consistent with http://www.emsai.net/projects/widescreen/fovcalc/
		rd->fov_y = SCR_CalcFovY (fovvar, BASELINE_W, BASELINE_H);
		rd->fov_x = SCR_CalcFovX (rd->fov_y, width, height);
	}
}


/*
=================
SCR_CalcRefdef

Must be called whenever vid changes
Internal use only
=================
*/
void SCR_CalcRefdef (void)
{
	// bound viewsize
	if (scr_viewsize.value < 30) Cvar_SetQuick (&scr_viewsize, "30");
	if (scr_viewsize.value > 120) Cvar_SetQuick (&scr_viewsize, "120");

	// bound fov
	if (scr_fov.value < 10) Cvar_SetQuick (&scr_fov, "10");
	if (scr_fov.value > 170) Cvar_SetQuick (&scr_fov, "170");

	// johnfitz -- rewrote this section
	float size = scr_viewsize.value;
	float scale = CLAMP (1.0, scr_sbarscale.value, (float) glwidth / 320.0);

	if (size >= 120 || cl.intermission || scr_sbaralpha.value < 1) // johnfitz -- scr_sbaralpha.value
		sb_lines = 0;
	else if (size >= 110)
		sb_lines = 24 * scale;
	else sb_lines = 48 * scale;

	size = q_min (scr_viewsize.value, 100) / 100;
	// johnfitz

	// johnfitz -- rewrote this section
	r_refdef.vrect.width = q_max (glwidth * size, 96); // no smaller than 96, for icons
	r_refdef.vrect.height = q_min (glheight * size, glheight - sb_lines); // make room for sbar
	r_refdef.vrect.x = (glwidth - r_refdef.vrect.width) / 2;
	r_refdef.vrect.y = (glheight - sb_lines - r_refdef.vrect.height) / 2;
	// johnfitz

	// MH - alternate FOV calculation
	SCR_SetFOV (&r_refdef, scr_fov.value, r_refdef.vrect.width, r_refdef.vrect.height);
}


/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
void SCR_SizeUp_f (void)
{
	Cvar_SetValueQuick (&scr_viewsize, scr_viewsize.value + 10);
}


/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
void SCR_SizeDown_f (void)
{
	Cvar_SetValueQuick (&scr_viewsize, scr_viewsize.value - 10);
}

static void SCR_Callback_refdef (cvar_t *var)
{
}

/*
==================
SCR_Conwidth_f -- johnfitz -- called when scr_conwidth or scr_conscale changes
==================
*/
void SCR_Conwidth_f (cvar_t *var)
{
	vid.conwidth = (scr_conwidth.value > 0) ? (int) scr_conwidth.value : (scr_conscale.value > 0) ? (int) (vid.width / scr_conscale.value) : vid.width;
	vid.conwidth = CLAMP (320, vid.conwidth, vid.width);
	vid.conwidth &= 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;
}

// ============================================================================

/*
==================
SCR_LoadPics -- johnfitz
==================
*/
void SCR_LoadPics (void)
{
	scr_ram = Draw_PicFromWad ("ram");
	scr_net = Draw_PicFromWad ("net");
	scr_turtle = Draw_PicFromWad ("turtle");
}

/*
==================
SCR_Init
==================
*/
void SCR_Init (void)
{
	// johnfitz -- new cvars
	Cvar_RegisterVariable (&scr_menuscale);
	Cvar_RegisterVariable (&scr_sbarscale);
	Cvar_SetCallback (&scr_sbaralpha, SCR_Callback_refdef);
	Cvar_RegisterVariable (&scr_sbaralpha);
	Cvar_SetCallback (&scr_conwidth, &SCR_Conwidth_f);
	Cvar_SetCallback (&scr_conscale, &SCR_Conwidth_f);
	Cvar_RegisterVariable (&scr_conwidth);
	Cvar_RegisterVariable (&scr_conscale);
	Cvar_RegisterVariable (&scr_crosshairscale);
	Cvar_RegisterVariable (&scr_showfps);
	Cvar_RegisterVariable (&scr_clock);
	// johnfitz

	Cvar_SetCallback (&scr_fov, SCR_Callback_refdef);
	Cvar_SetCallback (&scr_viewsize, SCR_Callback_refdef);
	Cvar_RegisterVariable (&scr_fov);
	Cvar_RegisterVariable (&scr_viewsize);
	Cvar_RegisterVariable (&scr_conspeed);
	Cvar_RegisterVariable (&scr_showram);
	Cvar_RegisterVariable (&scr_showturtle);
	Cvar_RegisterVariable (&scr_showpause);
	Cvar_RegisterVariable (&scr_centertime);
	Cvar_RegisterVariable (&scr_printspeed);

	Cmd_AddCommand ("screenshot", SCR_ScreenShot_f);
	Cmd_AddCommand ("sizeup", SCR_SizeUp_f);
	Cmd_AddCommand ("sizedown", SCR_SizeDown_f);

	SCR_LoadPics (); // johnfitz

	scr_initialized = true;
}

// ============================================================================

/*
==============
SCR_DrawFPS -- johnfitz
==============
*/
void SCR_DrawFPS (void)
{
	static double	oldtime = 0;
	static double	lastfps = 0;
	static int	oldframecount = 0;
	double	elapsed_time;
	int	frames;

	elapsed_time = realtime - oldtime;
	frames = r_framecount - oldframecount;

	if (elapsed_time < 0 || frames < 0)
	{
		oldtime = realtime;
		oldframecount = r_framecount;
		return;
	}

	// update value every 1/4 second
	if (elapsed_time > 0.25)
	{
		lastfps = frames / elapsed_time;
		oldtime = realtime;
		oldframecount = r_framecount;
	}

	if (scr_showfps.value)
	{
		char	st[16];
		int	x, y;
		sprintf (st, "%4.0f fps", lastfps);
		x = 320 - (strlen (st) << 3);
		y = 200 - 8;
		if (scr_clock.value) y -= 8; // make room for clock
		GL_SetCanvas (CANVAS_BOTTOMRIGHT);
		Draw_String (x, y, st);
	}
}

/*
==============
SCR_DrawClock -- johnfitz
==============
*/
void SCR_DrawClock (void)
{
	char	str[12];

	if (scr_clock.value == 1)
	{
		int minutes, seconds;

		minutes = cl.time / 60;
		seconds = ((int) cl.time) % 60;

		sprintf (str, "%i:%i%i", minutes, seconds / 10, seconds % 10);
	}
	else
		return;

	// draw it
	GL_SetCanvas (CANVAS_BOTTOMRIGHT);
	Draw_String (320 - (strlen (str) << 3), 200 - 8, str);
}

/*
==============
SCR_DrawDevStats
==============
*/
void SCR_DrawDevStats (void)
{
	char	str[40];
	int		y = 25 - 9; // 9=number of lines to print
	int		x = 0; // margin

	if (!devstats.value)
		return;

	GL_SetCanvas (CANVAS_BOTTOMLEFT);

	Draw_Fill (x, y * 8, 19 * 8, 9 * 8, 0, 0.5); // dark rectangle

	sprintf (str, "devstats |Curr Peak");
	Draw_String (x, (y++) * 8 - x, str);

	sprintf (str, "---------+---------");
	Draw_String (x, (y++) * 8 - x, str);

	sprintf (str, "Edicts   |%4i %4i", dev_stats.edicts, dev_peakstats.edicts);
	Draw_String (x, (y++) * 8 - x, str);

	sprintf (str, "Packet   |%4i %4i", dev_stats.packetsize, dev_peakstats.packetsize);
	Draw_String (x, (y++) * 8 - x, str);

	sprintf (str, "Visedicts|%4i %4i", dev_stats.visedicts, dev_peakstats.visedicts);
	Draw_String (x, (y++) * 8 - x, str);

	sprintf (str, "Efrags   |%4i %4i", dev_stats.efrags, dev_peakstats.efrags);
	Draw_String (x, (y++) * 8 - x, str);

	sprintf (str, "Dlights  |%4i %4i", dev_stats.dlights, dev_peakstats.dlights);
	Draw_String (x, (y++) * 8 - x, str);

	sprintf (str, "Beams    |%4i %4i", dev_stats.beams, dev_peakstats.beams);
	Draw_String (x, (y++) * 8 - x, str);

	sprintf (str, "Tempents |%4i %4i", dev_stats.tempents, dev_peakstats.tempents);
	Draw_String (x, (y++) * 8 - x, str);
}


/*
==============
DrawPause
==============
*/
void SCR_DrawPause (void)
{
	qpic_t *pic;

	if (!cl.paused)
		return;

	if (!scr_showpause.value)		// turn off for screenshots
		return;

	GL_SetCanvas (CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/pause.lmp");
	Draw_Pic ((320 - pic->width) / 2, (240 - 48 - pic->height) / 2, pic); // johnfitz -- stretched menus
}

/*
==============
SCR_DrawLoading
==============
*/
void SCR_DrawLoading (void)
{
	qpic_t *pic;

	if (!scr_drawloading)
		return;

	GL_SetCanvas (CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/loading.lmp");
	Draw_Pic ((320 - pic->width) / 2, (240 - 48 - pic->height) / 2, pic); // johnfitz -- stretched menus
}


// =============================================================================


/*
==================
SCR_SetUpToDrawConsole

MH - rewritten for improved framerate independence
==================
*/
void SCR_SetUpToDrawConsole (void)
{
	static double con_oldtime = -1;
	double con_frametime;

	// check for first call
	if (con_oldtime < 0) con_oldtime = realtime;

	// get correct frametime
	con_frametime = realtime - con_oldtime;
	con_oldtime = realtime;

	Con_CheckResize ();

	if (scr_drawloading)
		return;		// never a console with loading plaque

	// decide on the height of the console
	con_forcedup = !cl.worldmodel || cls.signon != SIGNONS;

	if (con_forcedup)
	{
		scr_conlines = glheight; // full screen // johnfitz -- glheight instead of vid.height
		scr_con_current = scr_conlines;
	}
	else if (scr_remove_console)
	{
		scr_conlines = 0;
		scr_con_current = 0;
		scr_remove_console = false;
	}
	else if (key_dest == key_console)
		scr_conlines = glheight / 2; // half screen // johnfitz -- glheight instead of vid.height
	else
		scr_conlines = 0; // none visible

	if (scr_conlines < scr_con_current)
	{
		// ericw -- (glheight/600.0) factor makes conspeed resolution independent, using 800x600 as a baseline
		scr_con_current -= scr_conspeed.value * con_frametime * ((float) glheight / 200.0f);
		if (scr_conlines > scr_con_current)
			scr_con_current = scr_conlines;
	}
	else if (scr_conlines > scr_con_current)
	{
		// ericw -- (glheight/600.0)
		scr_con_current += scr_conspeed.value * con_frametime * ((float) glheight / 200.0f);
		if (scr_conlines < scr_con_current)
			scr_con_current = scr_conlines;
	}
}

/*
==================
SCR_DrawConsole
==================
*/
void SCR_DrawConsole (void)
{
	if (scr_con_current)
	{
		Con_DrawConsole (scr_con_current, true);
		clearconsole = 0;
	}
	else
	{
		if (key_dest == key_game || key_dest == key_message)
			Con_DrawNotify ();	// only draw notify in game
	}
}


void SCR_RemoveConsole (void)
{
	scr_remove_console = true;
}


/*
==============================================================================

SCREEN SHOTS

==============================================================================
*/

static void SCR_ScreenShot_Usage (void)
{
	Con_Printf ("usage: screenshot <format> <quality>\n");
	Con_Printf ("   format must be \"png\" or \"tga\" or \"jpg\"\n");
	Con_Printf ("   quality must be 1-100\n");
	return;
}

/*
==================
SCR_ScreenShot_f -- johnfitz -- rewritten to use Image_WriteTGA
==================
*/
void SCR_ScreenShot_f (void)
{
	byte *buffer;
	char	ext[4];
	char	imagename[16];  // johnfitz -- was [80]
	char	checkname[MAX_OSPATH];
	int	i, quality;
	qboolean	ok;

	Q_strncpy (ext, "png", sizeof (ext));

	if (Cmd_Argc () >= 2)
	{
		const char *requested_ext = Cmd_Argv (1);

		if (!q_strcasecmp ("png", requested_ext)
			|| !q_strcasecmp ("tga", requested_ext)
			|| !q_strcasecmp ("jpg", requested_ext))
			Q_strncpy (ext, requested_ext, sizeof (ext));
		else
		{
			SCR_ScreenShot_Usage ();
			return;
		}
	}

	// read quality as the 3rd param (only used for JPG)
	quality = 90;
	if (Cmd_Argc () >= 3)
		quality = Q_atoi (Cmd_Argv (2));
	if (quality < 1 || quality > 100)
	{
		SCR_ScreenShot_Usage ();
		return;
	}

	// find a file name to save it to
	for (i = 0; i < 10000; i++)
	{
		q_snprintf (imagename, sizeof (imagename), "spasm%04i.%s", i, ext);	// "fitz%04i.tga"
		q_snprintf (checkname, sizeof (checkname), "%s/%s", com_gamedir, imagename);
		if (Sys_FileTime (checkname) == -1)
			break;	// file doesn't exist
	}

	if (i == 10000)
	{
		Con_Printf ("SCR_ScreenShot_f: Couldn't find an unused filename\n");
		return;
	}

	// get data
	if (!(buffer = (byte *) Q_zmalloc (glwidth * glheight * 3)))
	{
		Con_Printf ("SCR_ScreenShot_f: Couldn't allocate memory\n");
		return;
	}

	glPixelStorei (GL_PACK_ALIGNMENT, 1);/* for widths that aren't a multiple of 4 */
	glReadPixels (glx, gly, glwidth, glheight, GL_RGB, GL_UNSIGNED_BYTE, buffer);
	glPixelStorei (GL_PACK_ALIGNMENT, 0);

	// now write the file
	if (!q_strncasecmp (ext, "png", sizeof (ext)))
		ok = Image_WritePNG (imagename, buffer, glwidth, glheight, 24, false);
	else if (!q_strncasecmp (ext, "tga", sizeof (ext)))
		ok = Image_WriteTGA (imagename, buffer, glwidth, glheight, 24, false);
	else if (!q_strncasecmp (ext, "jpg", sizeof (ext)))
		ok = Image_WriteJPG (imagename, buffer, glwidth, glheight, 24, quality, false);
	else
		ok = false;

	if (ok)
		Con_Printf ("Wrote %s\n", imagename);
	else
		Con_Printf ("SCR_ScreenShot_f: Couldn't create %s\n", imagename);

	free (buffer);

	Host_RearmTimers (); // this op writes to disk so it may take some time
}


// =============================================================================


/*
===============
SCR_BeginLoadingPlaque

================
*/
void SCR_BeginLoadingPlaque (void)
{
	S_StopAllSounds (true);

	if (cls.state != ca_connected)
		return;
	if (cls.signon != SIGNONS)
		return;

	// redraw with no console and the loading plaque
	Con_ClearNotify ();
	scr_centertime_off = 0;
	scr_con_current = 0;

	scr_drawloading = true;
	SCR_UpdateScreen ();
	scr_drawloading = false;

	scr_disabled_for_loading = true;
	scr_disabled_time = realtime;
}

/*
===============
SCR_EndLoadingPlaque

================
*/
void SCR_EndLoadingPlaque (void)
{
	scr_disabled_for_loading = false;
	Con_ClearNotify ();
}

// =============================================================================

const char *scr_notifystring;
qboolean	scr_drawdialog;

void SCR_DrawNotifyString (void)
{
	const char *start;
	int		l;
	int		j;
	int		x, y;

	GL_SetCanvas (CANVAS_MENU); // johnfitz

	start = scr_notifystring;

	y = 200 * 0.35; // johnfitz -- stretched overlays

	Draw_BeginString ();

	do
	{
		// scan the width of the line
		for (l = 0; l < 40; l++)
			if (start[l] == '\n' || !start[l])
				break;
		x = (320 - l * 8) / 2; // johnfitz -- stretched overlays

		for (j = 0; j < l; j++, x += 8)
			Draw_StringCharacter (x, y, start[j]);

		y += 8;

		while (*start && *start != '\n')
			start++;

		if (!*start)
			break;
		start++;		// skip the \n
	} while (1);

	Draw_EndString ();
}


/*
==================
SCR_ModalMessage

Displays a text string in the center of the screen and waits for a Y or N
keypress.
==================
*/
int SCR_ModalMessage (const char *text, float timeout) // johnfitz -- timeout
{
	double time1, time2; // johnfitz -- timeout
	int lastkey, lastchar;

	if (cls.state == ca_dedicated)
		return true;

	scr_notifystring = text;

	// draw a fresh screen
	scr_drawdialog = true;
	SCR_UpdateScreen ();
	scr_drawdialog = false;

	S_ClearBuffer ();		// so dma doesn't loop current sound

	time1 = Sys_DoubleTime () + timeout; // johnfitz -- timeout
	time2 = 0.0f; // johnfitz -- timeout

	Key_BeginInputGrab ();
	do
	{
		Sys_SendKeyEvents ();
		Key_GetGrabbedInput (&lastkey, &lastchar);
		Sys_Sleep (1);
		if (timeout) time2 = Sys_DoubleTime (); // johnfitz -- zero timeout means wait forever.
	} while (lastchar != 'y' && lastchar != 'Y' &&
		lastchar != 'n' && lastchar != 'N' &&
		lastkey != K_ESCAPE &&
		lastkey != K_ABUTTON &&
		lastkey != K_BBUTTON &&
		time2 <= time1);

	Key_EndInputGrab ();
	Host_RearmTimers ();

	//	SCR_UpdateScreen (); // johnfitz -- commented out

		// johnfitz -- timeout
	if (time2 > time1)
		return false;
	// johnfitz

	return (lastchar == 'y' || lastchar == 'Y' || lastkey == K_ABUTTON);
}


// =============================================================================

// johnfitz -- deleted SCR_BringDownConsole


/*
==================
SCR_TileClear
johnfitz -- modified to use glwidth/glheight instead of vid.width/vid.height
		also fixed the dimentions of right and top panels
==================
*/
void SCR_TileClear (void)
{
	if (r_refdef.vrect.x > 0)
	{
		// left
		Draw_TileClear (0,
			0,
			r_refdef.vrect.x,
			glheight - sb_lines);

		// right
		Draw_TileClear (r_refdef.vrect.x + r_refdef.vrect.width,
			0,
			glwidth - r_refdef.vrect.x - r_refdef.vrect.width,
			glheight - sb_lines);
	}

	if (r_refdef.vrect.y > 0)
	{
		// top
		Draw_TileClear (r_refdef.vrect.x,
			0,
			r_refdef.vrect.width,
			r_refdef.vrect.y);

		// bottom
		Draw_TileClear (r_refdef.vrect.x,
			r_refdef.vrect.y + r_refdef.vrect.height,
			r_refdef.vrect.width,
			glheight - r_refdef.vrect.y - r_refdef.vrect.height - sb_lines);
	}
}


/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.

WARNING: be very careful calling this from elsewhere, because the refresh
needs almost the entire 256k of stack space!
==================
*/
void SCR_UpdateScreen (void)
{
	if (scr_disabled_for_loading)
	{
		if (realtime - scr_disabled_time > 60)
		{
			scr_disabled_for_loading = false;
			Con_Printf ("load failed.\n");
		}
		else
			return;
	}

	if (!scr_initialized || !con_initialized)
		return;				// not initialized yet

	GL_BeginRendering (&glx, &gly, &glwidth, &glheight);

	// determine size of refresh window
	SCR_CalcRefdef ();

	// do 3D refresh drawing, and then update the screen
	SCR_SetUpToDrawConsole ();

	V_RenderView ();

	GL_Set2D ();

	// FIXME: only call this when needed
	SCR_TileClear ();

	if (scr_drawdialog) // new game confirm
	{
		if (con_forcedup)
			Draw_ConsoleBackground ();
		else
			Sbar_Draw ();

		Draw_FadeScreen ();
		SCR_DrawNotifyString ();
	}
	else if (scr_drawloading) // loading
	{
		SCR_DrawLoading ();
		Sbar_Draw ();
	}
	else if (cl.intermission == 1 && key_dest == key_game) // end of level
	{
		Sbar_IntermissionOverlay ();
	}
	else if (cl.intermission == 2 && key_dest == key_game) // end of episode
	{
		Sbar_FinaleOverlay ();
		SCR_CheckDrawCenterString ();
	}
	else
	{
		Draw_Crosshair (); // johnfitz
		SCR_DrawPause ();
		SCR_CheckDrawCenterString ();
		Sbar_Draw ();
		SCR_DrawDevStats (); // johnfitz
		SCR_DrawFPS (); // johnfitz
		SCR_DrawClock (); // johnfitz
		SCR_DrawConsole ();
		M_Draw ();
	}

	V_UpdateBlend (); // johnfitz -- V_UpdatePalette cleaned up and renamed

	GL_End2D ();

	GL_EndRendering ();
}

