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
// r_misc.c


/*

params

fp	env		10		contrast & gamma
fp	env		0		overbright & alpha

*/


#include "quakedef.h"

// johnfitz -- new cvars
extern cvar_t r_clearcolor;
extern cvar_t r_flatlightstyles;
extern cvar_t gl_fullbrights;
extern cvar_t gl_farclip;
extern cvar_t r_waterwarp;
extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;
extern cvar_t r_nolerp_list;
extern cvar_t r_noshadow_list;

extern gltexture_t *playertextures[MAX_SCOREBOARD]; // johnfitz


/*
====================
R_SetClearColor_f -- johnfitz
====================
*/
static void R_SetClearColor_f (cvar_t *var)
{
	byte *rgb = (byte *) &d_8to24table[(int) r_clearcolor.value & 255];
	glClearColor (rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0, 0);
}


/*
====================
R_Novis_f -- johnfitz
====================
*/
static void R_VisChanged (cvar_t *var)
{
	extern int vis_changed;
	vis_changed = 1;
}


/*
===============
R_Model_ExtraFlags_List_f -- johnfitz -- called when r_nolerp_list var changes
===============
*/
static void R_Model_ExtraFlags_List_f (cvar_t *var)
{
	int i;
	for (i = 0; i < MAX_MODELS; i++)
		Mod_SetExtraFlags (cl.model_precache[i]);
}

/*
====================
R_SetWateralpha_f -- ericw
====================
*/
static void R_SetWateralpha_f (cvar_t *var)
{
	map_wateralpha = var->value;
}

/*
====================
R_SetLavaalpha_f -- ericw
====================
*/
static void R_SetLavaalpha_f (cvar_t *var)
{
	map_lavaalpha = var->value;
}

/*
====================
R_SetTelealpha_f -- ericw
====================
*/
static void R_SetTelealpha_f (cvar_t *var)
{
	map_telealpha = var->value;
}

/*
====================
R_SetSlimealpha_f -- ericw
====================
*/
static void R_SetSlimealpha_f (cvar_t *var)
{
	map_slimealpha = var->value;
}

/*
====================
GL_WaterAlphaForSurfface -- ericw
====================
*/
float GL_WaterAlphaForSurface (msurface_t *surf)
{
	if (surf->flags & SURF_DRAWLAVA)
		return map_lavaalpha > 0 ? map_lavaalpha : map_wateralpha;
	else if (surf->flags & SURF_DRAWTELE)
		return map_telealpha > 0 ? map_telealpha : map_wateralpha;
	else if (surf->flags & SURF_DRAWSLIME)
		return map_slimealpha > 0 ? map_slimealpha : map_wateralpha;
	else
		return map_wateralpha;
}


/*
===============
R_Init
===============
*/
void R_Init (void)
{
	extern cvar_t gl_finish;

	Cmd_AddCommand ("timerefresh", R_TimeRefresh_f);
	Cmd_AddCommand ("pointfile", R_ReadPointFile_f);

	Cvar_RegisterVariable (&r_norefresh);
	Cvar_RegisterVariable (&r_drawentities);
	Cvar_RegisterVariable (&r_drawviewmodel);

	Cvar_RegisterVariable (&r_wateralpha);
	Cvar_SetCallback (&r_wateralpha, R_SetWateralpha_f);
	Cvar_RegisterVariable (&r_dynamic);
	Cvar_RegisterVariable (&r_novis);
	Cvar_SetCallback (&r_novis, R_VisChanged);
	Cvar_RegisterVariable (&r_speeds);
	Cvar_RegisterVariable (&r_pos);

	Cvar_RegisterVariable (&gl_finish);
	Cvar_RegisterVariable (&gl_clear);
	Cvar_RegisterVariable (&gl_cull);
	Cvar_RegisterVariable (&gl_polyblend);
	Cvar_RegisterVariable (&gl_nocolors);

	Cvar_RegisterVariable (&r_shadows);

	// johnfitz -- new cvars
	Cvar_RegisterVariable (&r_clearcolor);
	Cvar_SetCallback (&r_clearcolor, R_SetClearColor_f);
	Cvar_RegisterVariable (&r_waterwarp);
	Cvar_RegisterVariable (&r_flatlightstyles);
	Cvar_RegisterVariable (&gl_farclip);
	Cvar_RegisterVariable (&gl_fullbrights);
	Cvar_RegisterVariable (&gl_overbright);
	Cvar_SetCallback (&gl_fullbrights, NULL);
	Cvar_RegisterVariable (&r_lerpmodels);
	Cvar_RegisterVariable (&r_lerpmove);

	Cvar_RegisterVariable (&r_nolerp_list);
	Cvar_SetCallback (&r_nolerp_list, R_Model_ExtraFlags_List_f);

	Cvar_RegisterVariable (&r_noshadow_list);
	Cvar_SetCallback (&r_noshadow_list, R_Model_ExtraFlags_List_f);
	// johnfitz

	Cvar_RegisterVariable (&r_lavaalpha);
	Cvar_RegisterVariable (&r_telealpha);
	Cvar_RegisterVariable (&r_slimealpha);
	Cvar_RegisterVariable (&r_scale);
	Cvar_SetCallback (&r_lavaalpha, R_SetLavaalpha_f);
	Cvar_SetCallback (&r_telealpha, R_SetTelealpha_f);
	Cvar_SetCallback (&r_slimealpha, R_SetSlimealpha_f);

	R_InitParticles ();
	R_SetClearColor_f (&r_clearcolor); // johnfitz

	Sky_Init (); // johnfitz
	Fog_Init (); // johnfitz
}

/*
===============
R_TranslatePlayerSkin -- johnfitz -- rewritten.  also, only handles new colors, not new skins
===============
*/
void R_TranslatePlayerSkin (int playernum)
{
	int			top, bottom;

	top = (cl.scores[playernum].colors & 0xf0) >> 4;
	bottom = cl.scores[playernum].colors & 15;

	// FIXME: if gl_nocolors is on, then turned off, the textures may be out of sync with the scoreboard colors.
	if (!gl_nocolors.value)
		if (playertextures[playernum])
			TexMgr_ReloadImage (playertextures[playernum], top, bottom);
}

/*
===============
R_TranslateNewPlayerSkin -- johnfitz -- split off of TranslatePlayerSkin -- this is called when
the skin or model actually changes, instead of just new colors
added bug fix from bengt jardup
===============
*/
void R_TranslateNewPlayerSkin (int playernum)
{
	char		name[64];
	byte *pixels;
	aliashdr_t *paliashdr;
	int		skinnum;

	// get correct texture pixels
	currententity = &cl_entities[1 + playernum];

	if (!currententity->model || currententity->model->type != mod_alias)
		return;

	paliashdr = (aliashdr_t *) Mod_Extradata (currententity->model);

	skinnum = currententity->skinnum;

	// TODO: move these tests to the place where skinnum gets received from the server
	if (skinnum < 0 || skinnum >= paliashdr->numskins)
	{
		Con_DPrintf ("(%d): Invalid player skin #%d\n", playernum, skinnum);
		skinnum = 0;
	}

	pixels = (byte *) paliashdr + paliashdr->texels[skinnum]; // This is not a persistent place!

// upload new image
	q_snprintf (name, sizeof (name), "player_%i", playernum);
	playertextures[playernum] = TexMgr_LoadImage (currententity->model, name, paliashdr->skinwidth, paliashdr->skinheight,
		SRC_INDEXED, pixels, paliashdr->gltextures[skinnum][0]->source_file, paliashdr->gltextures[skinnum][0]->source_offset, TEXPREF_PAD | TEXPREF_OVERWRITE);

	// now recolor it
	R_TranslatePlayerSkin (playernum);
}

/*
===============
R_NewGame -- johnfitz -- handle a game switch
===============
*/
void R_NewGame (void)
{
	int i;

	// clear playertexture pointers (the textures themselves were freed by texmgr_newgame)
	for (i = 0; i < MAX_SCOREBOARD; i++)
		playertextures[i] = NULL;
}

/*
=============
R_ParseWorldspawn

called at map load
=============
*/
static void R_ParseWorldspawn (void)
{
	char key[128], value[4096];
	const char *data;

	map_wateralpha = r_wateralpha.value;
	map_lavaalpha = r_lavaalpha.value;
	map_telealpha = r_telealpha.value;
	map_slimealpha = r_slimealpha.value;

	data = COM_Parse (cl.worldmodel->entities);
	if (!data)
		return; // error
	if (com_token[0] != '{')
		return; // error
	while (1)
	{
		data = COM_Parse (data);
		if (!data)
			return; // error
		if (com_token[0] == '}')
			break; // end of worldspawn
		if (com_token[0] == '_')
			q_strlcpy (key, com_token + 1, sizeof (key));
		else
			q_strlcpy (key, com_token, sizeof (key));
		while (key[0] && key[strlen (key) - 1] == ' ') // remove trailing spaces
			key[strlen (key) - 1] = 0;
		data = COM_Parse (data);
		if (!data)
			return; // error
		q_strlcpy (value, com_token, sizeof (value));

		if (!strcmp ("wateralpha", key))
			map_wateralpha = atof (value);

		if (!strcmp ("lavaalpha", key))
			map_lavaalpha = atof (value);

		if (!strcmp ("telealpha", key))
			map_telealpha = atof (value);

		if (!strcmp ("slimealpha", key))
			map_slimealpha = atof (value);
	}
}


/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	// clear out efrags in case the level hasn't been reloaded
	// FIXME: is this one short?
	for (int i = 0; i < cl.worldmodel->numleafs; i++)
		cl.worldmodel->leafs[i].efrags = NULL;

	r_viewleaf = r_oldviewleaf = NULL;
	R_ClearParticles ();

	GL_BuildLightmaps ();
	GL_BuildBModelVertexBuffer ();
	// ericw -- no longer load alias models into a VBO here, it's done in Mod_LoadAliasModel

	r_framecount = 0; // johnfitz -- paranoid?
	r_visframecount = 0; // johnfitz -- paranoid?

	Sky_NewMap (); // johnfitz -- skybox in worldspawn
	Fog_NewMap (); // johnfitz -- global fog in worldspawn
	R_ParseWorldspawn (); // ericw -- wateralpha, lavaalpha, telealpha, slimealpha in worldspawn

	// MH - if running a timedemo, remove the console immediately rather than doing a slow scroll, which may corrupt timings
	if (cls.timedemo)
		SCR_RemoveConsole ();
}


/*
====================
R_TimeRefresh_f

For program optimization
====================
*/
void R_TimeRefresh_f (void)
{
	int i;
	float startangle = r_refdef.viewangles[1];
	float start = Sys_DoubleTime ();
	float		stop, time;
	float		timeRefreshTime = 2.75;

	glFinish ();

	scr_timerefresh = true;
	SCR_CalcRefdef ();

	// do a 360 in timeRefreshTime seconds
	for (i = 0;; i++)
	{
		if (GL_BeginRendering (&glx, &gly, &glwidth, &glheight))
		{
			r_refdef.viewangles[1] = startangle + (Sys_DoubleTime () - start) * (360.0 / timeRefreshTime);

			while (r_refdef.viewangles[1] > 360.0)
				r_refdef.viewangles[1] -= 360.0;

			R_RenderView ();
			GL_EndRendering ();
		}

		if ((time = Sys_DoubleTime () - start) >= timeRefreshTime) break;
	}

	glFinish ();

	stop = Sys_DoubleTime ();
	time = stop - start;
	scr_timerefresh = false;
	r_refdef.viewangles[1] = startangle;

	Con_Printf ("%i frames, %f seconds (%f fps)\n", i, time, (float) i / time);

#if 0
	int		i;
	float		start, stop, time;

	if (cls.state != ca_connected)
	{
		Con_Printf ("Not connected to a server\n");
		return;
	}

	start = Sys_DoubleTime ();
	for (i = 0; i < 128; i++)
	{
		GL_BeginRendering (&glx, &gly, &glwidth, &glheight);
		r_refdef.viewangles[1] = i / 128.0 * 360.0;
		R_RenderView ();
		GL_EndRendering ();
	}

	glFinish ();
	stop = Sys_DoubleTime ();
	time = stop - start;
	Con_Printf ("%f seconds (%f fps)\n", time, 128 / time);
#endif
}


void D_FlushCaches (void)
{
}


static GLuint gl_arb_programs[128];
static int gl_num_arb_programs;


/*
====================
R_DeleteShaders

Deletes any GLSL programs that have been created.
====================
*/
void R_DeleteShaders (void)
{
	glDeleteProgramsARB (gl_num_arb_programs, gl_arb_programs);
	gl_num_arb_programs = 0;
}


GLuint GL_CreateARBProgram (GLenum mode, const GLchar *progstr)
{
	GLuint progid = 0;

	glGenProgramsARB (1, &progid);
	glBindProgramARB (mode, progid);
	glProgramStringARB (mode, GL_PROGRAM_FORMAT_ASCII_ARB, strlen (progstr), progstr);

	const GLubyte *err = glGetString (GL_PROGRAM_ERROR_STRING_ARB);

	if (err && err[0])
		Con_SafePrintf ("Program compilation error : %s\n", err);

	glBindProgramARB (mode, 0);

	if (gl_num_arb_programs == (sizeof (gl_arb_programs) / sizeof (gl_arb_programs[0])))
		Host_Error ("gl_arb_programs overflow");

	gl_arb_programs[gl_num_arb_programs] = progid;
	gl_num_arb_programs++;

	return progid;
}


GLuint current_array_buffer, current_element_array_buffer;

/*
====================
GL_BindBuffer

glBindBuffer wrapper
====================
*/
void GL_BindBuffer (GLenum target, GLuint buffer)
{
	GLuint *cache;

	switch (target)
	{
	case GL_ARRAY_BUFFER:
		cache = &current_array_buffer;
		break;

	case GL_ELEMENT_ARRAY_BUFFER:
		cache = &current_element_array_buffer;
		break;

	default:
		Host_Error ("GL_BindBuffer: unsupported target %d", (int) target);
		return;
	}

	if (*cache != buffer)
	{
		*cache = buffer;
		glBindBuffer (target, *cache);
	}
}


/*
====================
GL_ClearBufferBindings

This must be called if you do anything that could make the cached bindings
invalid (e.g. manually binding, destroying the context).
====================
*/
void GL_ClearBufferBindings (void)
{
	current_array_buffer = 0;
	current_element_array_buffer = 0;

	glBindBuffer (GL_ARRAY_BUFFER, 0);
	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0);
}


void GL_EnableVertexAttribArrays (int arrays)
{
	static int oldarrays = 0;

	if (arrays != oldarrays)
	{
		for (int i = 0, j = 16; i < 16; i++, j++)
		{
			// enable/disable as required
			if ((arrays & (1 << i)) && !(oldarrays & (1 << i))) glEnableVertexAttribArray (i);
			if (!(arrays & (1 << i)) && (oldarrays & (1 << i))) glDisableVertexAttribArray (i);
		}

		oldarrays = arrays;
	}
}


void GL_BindPrograms (GLuint vp, GLuint fp)
{
	static GLuint currentvp = 0;
	static GLuint currentfp = 0;

	if (currentvp != vp)
	{
		glBindProgramARB (GL_VERTEX_PROGRAM_ARB, vp);
		currentvp = vp;
	}

	if (currentfp != fp)
	{
		glBindProgramARB (GL_FRAGMENT_PROGRAM_ARB, fp);
		currentfp = fp;
	}
}


void GL_BlendState (GLenum enable, GLenum sfactor, GLenum dfactor)
{
	static GLenum currentenable = GL_INVALID_VALUE;
	static GLenum currentsfactor = GL_INVALID_VALUE;
	static GLenum currentdfactor = GL_INVALID_VALUE;

	if (enable == GL_INVALID_VALUE)
	{
		// this toggle forces the states to explicitly set the next time they're seen
		currentenable = GL_INVALID_VALUE;
		currentsfactor = GL_INVALID_VALUE;
		currentdfactor = GL_INVALID_VALUE;
		return;
	}

	if (enable != currentenable)
	{
		if (enable == GL_TRUE)
			glEnable (GL_BLEND);
		else glDisable (GL_BLEND);

		currentenable = enable;
	}

	if (enable && (currentsfactor != sfactor || currentdfactor != dfactor))
	{
		glBlendFunc (sfactor, dfactor);
		currentsfactor = sfactor;
		currentdfactor = dfactor;
	}
}


void GL_DepthState (GLenum enable, GLenum testmode, GLenum writemode)
{
	static GLenum currentenable = GL_INVALID_VALUE;
	static GLenum currenttestmode = GL_INVALID_VALUE;
	static GLenum currentwritemode = GL_INVALID_VALUE;

	if (enable == GL_INVALID_VALUE)
	{
		// this toggle forces the states to explicitly set the next time they're seen
		currentenable = GL_INVALID_VALUE;
		currenttestmode = GL_INVALID_VALUE;
		currentwritemode = GL_INVALID_VALUE;
		return;
	}

	if (enable != currentenable)
	{
		if (enable == GL_TRUE)
		{
			glEnable (GL_DEPTH_TEST);

			if (currenttestmode != testmode)
			{
				glDepthFunc (testmode);
				currenttestmode = testmode;
			}
		}
		else glDisable (GL_DEPTH_TEST);

		currentenable = enable;
	}

	if (currentwritemode != writemode)
	{
		glDepthMask (writemode);
		currentwritemode = writemode;
	}
}


// returns a texture number and the position inside it
qboolean R_AllocBlock (int w, int h, int *x, int *y, int *allocated, int block_width, int block_height)
{
	int best = block_height;

	for (int i = 0; i < block_width - w; i++)
	{
		int j;
		int best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (allocated[i + j] >= best)
				break;

			if (allocated[i + j] > best2)
				best2 = allocated[i + j];
		}

		if (j == w)
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > block_height)
		return false;

	for (int i = 0; i < w; i++)
		allocated[*x + i] = best + h;

	return true;
}


