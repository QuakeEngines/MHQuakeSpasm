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


#include "quakedef.h"

// johnfitz -- new cvars
extern cvar_t r_clearcolor;
extern cvar_t r_flatlightstyles;
extern cvar_t gl_fullbrights;
extern cvar_t r_waterwarp;
extern cvar_t r_lerpmodels;
extern cvar_t r_lerpmove;
extern cvar_t r_nolerp_list;
extern cvar_t r_noshadow_list;

// must init to non-zero so that a memset-0 doesn't incorrectly cause a match
static int r_registration_sequence = 1;


// generic buffersets that may be used for any model type
static bufferset_t r_buffersets[MAX_MODELS];

// for r_drawflat
byte r_flatcolor[1024][4];


int R_GetBufferSetForName (char *name)
{
	// look for an exact match
	for (int i = 0; i < MAX_MODELS; i++)
	{
		if (!strcmp (name, r_buffersets[i].name))
		{
			// mark as used in this registration sequence
			Con_DPrintf ("Reusing bufferset for %s\n", name);
			r_buffersets[i].registration_sequence = r_registration_sequence;
			return i;
		}
	}

	// didn't find a match
	return -1;
}


int R_NewBufferSetForName (char *name)
{
	// look for a free buffer
	for (int i = 0; i < MAX_MODELS; i++)
	{
		// don't use any buffers that have content
		if (r_buffersets[i].vertexbuffer) continue;
		if (r_buffersets[i].indexbuffer) continue;
		if (r_buffersets[i].name[0]) continue;

		// this is a free spot - set it up (the buffers will be glGen'ed by the caller)
		Con_DPrintf ("Creating bufferset for %s\n", name);
		strcpy (r_buffersets[i].name, name);
		r_buffersets[i].registration_sequence = r_registration_sequence;

		return i;
	}

	// didn't find a match (this will normally Sys_Error)
	return -1;
}


bufferset_t *R_GetBufferSetForModel (qmodel_t *m)
{
	return &r_buffersets[m->buffsetset];
}


void R_FreeUnusedBufferSets (void)
{
	for (int i = 0; i < MAX_MODELS; i++)
	{
		// buffer is used
		if (r_buffersets[i].registration_sequence == r_registration_sequence) continue;

		// unused, so free it
		if (r_buffersets[i].name[0]) Con_DPrintf ("Releasing bufferset for %s\n", r_buffersets[i].name);
		if (r_buffersets[i].vertexbuffer) glDeleteBuffers (1, &r_buffersets[i].vertexbuffer);
		if (r_buffersets[i].indexbuffer) glDeleteBuffers (1, &r_buffersets[i].indexbuffer);

		memset (&r_buffersets[i], 0, sizeof (r_buffersets[i]));
	}
}


void R_FreeAllBufferSets (void)
{
	// advance the registration sequence to age-out all buffers so they will all be unused (this is cutesy but is it robust?)
	r_registration_sequence++;
	R_FreeUnusedBufferSets ();
}


/*
================
R_ReloadBufferSets

Loop over all precached alias models, and upload each one to a VBO.
================
*/
void GLMesh_ReloadAliasGeometry (qmodel_t *mod);
void R_CreateSpriteFrames (qmodel_t *mod);

void R_ReloadBufferSets (void)
{
	for (int j = 1; j < MAX_MODELS; j++)
	{
		qmodel_t *m;

		if ((m = cl.model_precache[j]) == NULL) break;

		// alias and sprites need to be reloaded through here; brush models are reloaded via GL_BuildBModelVertexBuffer
		if (m->type == mod_alias) {
			GLMesh_ReloadAliasGeometry (m);
		}
		else if (m->type == mod_sprite) {
			R_CreateSpriteFrames (m);
		}
	}
}


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


static void R_VisChanged (cvar_t *var)
{
	extern int vis_changed;
	vis_changed = 1;
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
	Cvar_RegisterVariable (&r_lockpvs);
	Cvar_SetCallback (&r_novis, R_VisChanged);
	Cvar_SetCallback (&r_lockpvs, R_VisChanged);
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

	// mapper crap
	Cvar_RegisterVariable (&r_lightmap);
	Cvar_RegisterVariable (&r_fullbright);
	Cvar_RegisterVariable (&r_drawflat);
	Cvar_RegisterVariable (&r_showtris);
	Cvar_RegisterVariable (&r_showbboxes);

	// for r_drawflat
	srand (350125); // GO!

	for (int i = 0; i < 1024; i++)
	{
		r_flatcolor[i][0] = rand () & 255;
		r_flatcolor[i][1] = rand () & 255;
		r_flatcolor[i][2] = rand () & 255;
		r_flatcolor[i][3] = 255;
	}

	R_InitParticles ();
	R_SetClearColor_f (&r_clearcolor); // johnfitz

	Sky_Init (); // johnfitz
	Fog_Init (); // johnfitz
}


/*
===============
R_NewGame -- johnfitz -- handle a game switch
===============
*/
void R_NewGame (void)
{
	// nothing here because playertextures are now handled through the entity system
}


/*
=============
R_ParseWorldspawn

called at map load
=============
*/
static void R_ParseWorldspawn (void)
{
	char *value;

	if ((value = Mod_ValueForKeyFromWorldspawn (cl.worldmodel->entities, "wateralpha")) != NULL)
		map_wateralpha = atof (value);
	else map_wateralpha = r_wateralpha.value;

	if ((value = Mod_ValueForKeyFromWorldspawn (cl.worldmodel->entities, "lavaalpha")) != NULL)
		map_lavaalpha = atof (value);
	else map_lavaalpha = r_lavaalpha.value;

	if ((value = Mod_ValueForKeyFromWorldspawn (cl.worldmodel->entities, "telealpha")) != NULL)
		map_telealpha = atof (value);
	else map_telealpha = r_telealpha.value;

	if ((value = Mod_ValueForKeyFromWorldspawn (cl.worldmodel->entities, "slimealpha")) != NULL)
		map_slimealpha = atof (value);
	else map_slimealpha = r_slimealpha.value;
}


/*
===============
R_NewMap
===============
*/
void R_NewMap (void)
{
	// reset for the new map
	r_framecount = 1;

	// clear out efrags in case the level hasn't been reloaded
	// FIXME: is this one short?
	for (int i = 0; i < cl.worldmodel->numleafs; i++)
		cl.worldmodel->leafs[i].efrags = NULL;

	r_viewleaf = r_oldviewleaf = NULL;
	R_ClearParticles ();

	GL_BuildLightmaps ();
	GL_BuildBModelVertexBuffer ();
	// ericw -- no longer load alias models into a VBO here, it's done in Mod_LoadAliasModel

	// mh - must init these to non-zero so that they don't catch surfs, leafs and nodes after a memset-0 during load
	r_framecount = 1; // johnfitz -- paranoid?
	r_visframecount = 1; // johnfitz -- paranoid?

	Sky_NewMap (); // johnfitz -- skybox in worldspawn
	Fog_NewMap (); // johnfitz -- global fog in worldspawn
	R_ParseWorldspawn (); // ericw -- wateralpha, lavaalpha, telealpha, slimealpha in worldspawn

	// MH - if running a timedemo, remove the console immediately rather than doing a slow scroll, which may corrupt timings
	if (cls.timedemo)
		SCR_RemoveConsole ();

	// free any unused objects
	R_FreeUnusedBufferSets ();

	// go to the next registration sequence
	r_registration_sequence++;

	// because the full load may take some time, we rearm the timers here
	Host_RearmTimers ();
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

	scr_timerefresh = true; // runs without vsync on
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
}


/*
============================================================================================================================================================

SHADERS

============================================================================================================================================================
*/


const GLchar *GL_GetFullbrightFragmentProgramSource (void)
{
	static const GLchar *src = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff, fence;\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"\n"
		"# fence texture test (this mode is not intended for robust general-case use so it's ok for it to be non-optimized)\n"
		"SUB fence, diff, 0.666;\n"
		"KIL fence.a;\n"
		"\n"
		"# perform the fogging\n"
		"TEMP fogFactor;\n"
		"MUL fogFactor.x, state.fog.params.x, fragment.fogcoord.x;\n"
		"MUL fogFactor.x, fogFactor.x, fogFactor.x;\n"
		"EX2_SAT fogFactor.x, -fogFactor.x;\n"
		"LRP diff.rgb, fogFactor.x, diff, state.fog.color;\n"
		"\n"
		"# apply the contrast\n"
		"MUL diff.rgb, diff, program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW result.color.r, diff.r, program.env[10].y;\n"
		"POW result.color.g, diff.g, program.env[10].y;\n"
		"POW result.color.b, diff.b, program.env[10].y;\n"
		"MOV result.color.a, program.env[0].a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	return src;
}


const GLchar *GL_GetDrawflatFragmentProgramSource (void)
{
	static const GLchar *src = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff;\n"
		"\n"
		"# perform the fogging\n"
		"TEMP fogFactor;\n"
		"MUL fogFactor.x, state.fog.params.x, fragment.fogcoord.x;\n"
		"MUL fogFactor.x, fogFactor.x, fogFactor.x;\n"
		"EX2_SAT fogFactor.x, -fogFactor.x;\n"
		"LRP diff.rgb, fogFactor.x, fragment.color, state.fog.color;\n"
		"\n"
		"# apply the contrast\n"
		"MUL diff.rgb, diff, program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW result.color.r, diff.r, program.env[10].y;\n"
		"POW result.color.g, diff.g, program.env[10].y;\n"
		"POW result.color.b, diff.b, program.env[10].y;\n"
		"MOV result.color.a, program.env[0].a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	return src;
}


const GLchar *GL_GetDynamicLightFragmentProgramSource (void)
{
	// this program is common to BSP and MDL so let's only define it once
	static const GLchar *src = \
		"!!ARBfp1.0\n"
		"\n"
		"# fragment.texcoord[0] is texture coord\n"
		"# fragment.texcoord[1] is normal\n"
		"# fragment.texcoord[2] is light vector\n"
		"\n"
		"# program.env[5] is light radius\n"
		"# program.env[6] is light colour\n"
		"\n"
		"TEMP diff;\n"
		"TEMP normal;\n"
		"TEMP incoming;\n"
		"TEMP dist;\n"
		"TEMP light;\n"
		"TEMP angle;\n"
		"TEMP fence;\n"
		"\n"
		"# brings lights back to a 0..1 range (light.exe used 0..255)\n"
		"PARAM rescale = { 0.0078125, 0.0078125, 0.0078125, 0 };\n"
		"\n"
		"# perform the texturing; doing this early so it can interleave with ALU ops\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"\n"
		"# fence texture test\n"
		"# dynamic lights are additive so they will automagically fence if there is nothing to add\n"
		"# this makes the fence case slightly slower as a tradeoff vs a much faster general case\n"
		"MUL diff.rgb, diff, diff.a;\n"
		"\n"
		"# normalize incoming normal\n"
		"DP3 normal.w, fragment.texcoord[1], fragment.texcoord[1];\n"
		"RSQ normal.w, normal.w;\n"
		"MUL normal.xyz, normal.w, fragment.texcoord[1];\n"
		"\n"
		"# normalize incoming light vector\n"
		"DP3 incoming.w, fragment.texcoord[2], fragment.texcoord[2];\n"
		"RSQ incoming.w, incoming.w;\n"
		"RCP dist, incoming.w;	# get the vector length while we're at it\n"
		"MUL incoming.xyz, incoming.w, fragment.texcoord[2];\n"
		"\n"
		"# adjust for normal; this is the same calculation used by light.exe\n"
		"DP3 angle, incoming, normal;\n"
		"MAD angle, angle, 0.5, 0.5;\n"
		"\n"
		"# get the light attenuation\n"
		"SUB light, program.env[5], dist;\n"
		"MUL light, light, rescale;\n"
		"MUL light, light, program.env[6];\n"
		"MUL light, light, angle;\n"
		"\n"
		"# clamp any fragments with negative light contribution otherwise the POW in the gamma calc will bring them to positive - this is faster than KIL\n"
		"MAX light, light, 0.0;\n"
		"\n"
		"# modulate light by texture\n"
		"MUL diff, diff, light;\n"
		"\n"
		"# perform the fogging\n"
		"TEMP fogFactor;\n"
		"MUL fogFactor.x, state.fog.params.x, fragment.fogcoord.x;\n"
		"MUL fogFactor.x, fogFactor.x, fogFactor.x;\n"
		"EX2_SAT fogFactor.x, -fogFactor.x;\n"
		"LRP diff.rgb, fogFactor.x, diff, { 0.0, 0.0, 0.0, 0.0 };\n"
		"\n"
		"# apply the contrast\n"
		"MUL diff.rgb, diff, program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW result.color.r, diff.r, program.env[10].y;\n"
		"POW result.color.g, diff.g, program.env[10].y;\n"
		"POW result.color.b, diff.b, program.env[10].y;\n"
		"MOV result.color.a, 0.0;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	return src;
}


// we're currently on 57 shaders so 128 seems a mite low - we could hit that with just 2 or 3 more combinations of some types
#define MAX_ARB_PROGRAMS	256

static GLuint gl_arb_programs[MAX_ARB_PROGRAMS];
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


// fixme - this is getting a little messy and potentially fragile now
const GLchar *GL_GetVertexProgram (const GLchar *base, int shaderflag)
{
	// shader combinations are dealt with by keeping a single copy of the shader source and selectively commenting out parts of it
	char *test = NULL;

	// putting the temporary copy in hunk memory; this is handed back in GL_Init after all shaders are created
	char *modified = (char *) Hunk_Alloc (strlen (base) + 1);

	// copy off the shader because we're going to modify it
	strcpy (modified, base);

	if (shaderflag & SHADERFLAG_NOTEX)
	{
		// remove light/etc so we're just left with diffuse and fogcoord
		if ((test = strstr (modified, "MOV result.texcoord[1], vertex.attrib[2];")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "MOV result.texcoord[1], vertex.attrib[3];")) != NULL) test[0] = '#'; // brush
		if ((test = strstr (modified, "MOV result.color, vertex.attrib[4];")) != NULL) test[0] = '#';
	}

	if (!(shaderflag & SHADERFLAG_DYNAMIC))
	{
		// remove lightvector computations
		if ((test = strstr (modified, "SUB result.texcoord[2], program.env[7], position;")) != NULL) test[0] = '#'; // alias
		if ((test = strstr (modified, "SUB result.texcoord[2], program.env[7], vertex.attrib[0];")) != NULL) test[0] = '#'; // brush

		// remove normal from brush models
		if ((test = strstr (modified, "MOV result.texcoord[1], vertex.attrib[3];")) != NULL) test[0] = '#'; // brush
	}
	else
	{
		// remove light/etc so we're just left with diffuse and fogcoord
		if ((test = strstr (modified, "MOV result.texcoord[1], vertex.attrib[2];")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "MOV result.color, vertex.attrib[4];")) != NULL) test[0] = '#';
	}

	if (!(shaderflag & SHADERFLAG_DRAWFLAT))
	{
		// remove drawflat computations
		if ((test = strstr (modified, "MOV result.color, vertex.attrib[5];")) != NULL) test[0] = '#'; // alias
		if ((test = strstr (modified, "MOV result.color, vertex.attrib[4];")) != NULL) test[0] = '#'; // brush
	}

	// hand back the modified shader source
	return modified;
}


const GLchar *GL_GetFragmentProgram (const GLchar *base, int shaderflag)
{
	// shader combinations are dealt with by keeping a single copy of the shader source and selectively commenting out parts of it
	char *test = NULL;

	// putting the temporary copy in hunk memory; this is handed back in GL_Init after all shaders are created
	char *modified = (char *) Hunk_Alloc (strlen (base) + 1);

	// copy off the shader because we're going to modify it
	strcpy (modified, base);

	if (!(shaderflag & SHADERFLAG_FENCE))
	{
		// remove fence texture test
		if ((test = strstr (modified, "SUB fence")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "KIL fence")) != NULL) test[0] = '#';
	}

	if (!(shaderflag & SHADERFLAG_LUMA))
	{
		// remove luma mask
		if ((test = strstr (modified, "TEX luma, fragment.t")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "MAX diff, diff, luma")) != NULL) test[0] = '#';
	}

	if (!(shaderflag & SHADERFLAG_FOG))
	{
		// remove fog instructions (this optimization is a bit bogus because ALU at this level is virtually free, but if feels "right" to do, nonetheless)
		if ((test = strstr (modified, "TEMP fogFactor;")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "MUL fogFactor.x, state.fog.params.x, fragment.fogcoord.x;")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "MUL fogFactor.x, fogFactor.x, fogFactor.x;")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "EX2_SAT fogFactor.x, -fogFactor.x;")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "LRP diff.rgb, fogFactor.x, diff, state.fog.color;")) != NULL) test[0] = '#';
	}

	if (shaderflag & SHADERFLAG_4STYLE)
	{
		// remove single lightstyle
		if ((test = strstr (modified, "TEX lmap, fragment.texcoord[1], texture[2], 2D;")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "MUL lmap, lmap, program.env[2].x;")) != NULL) test[0] = '#';
	}
	else
	{
		// remove 4 styles
		if ((test = strstr (modified, "TEX lmr, fragment.texcoord[1], texture[2], 2D;")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "TEX lmg, fragment.texcoord[1], texture[3], 2D;")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "TEX lmb, fragment.texcoord[1], texture[4], 2D;")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "DP4 lmap.r, lmr, program.env[2];")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "DP4 lmap.g, lmg, program.env[2];")) != NULL) test[0] = '#';
		if ((test = strstr (modified, "DP4 lmap.b, lmb, program.env[2];")) != NULL) test[0] = '#';
	}

	// hand back the modified shader source
	return modified;
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

	if (gl_num_arb_programs >= MAX_ARB_PROGRAMS)
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


void R_UpdateFragmentProgramAlpha (float alpha)
{
	static float oldalpha = -1;

	// this case just forces it to be reset next time it's seen, even if it otherwise would not be, but does not actually set it, and should be called at the start of each frame
	if (alpha < 0)
	{
		oldalpha = -1;
		return;
	}

	// this case only sets if if it had changed
	if (alpha != oldalpha)
	{
		glProgramEnvParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 0, 1, 1, 1, alpha);
		oldalpha = alpha;
	}
}


/*
=============
R_BeginTransparentDrawing -- ericw
=============
*/
void R_BeginTransparentDrawing (float alpha)
{
	if (alpha < 1.0f)
	{
		GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GL_DepthState (GL_TRUE, GL_LEQUAL, GL_FALSE);
	}
	else
	{
		GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
		GL_DepthState (GL_TRUE, GL_LEQUAL, GL_TRUE);
	}

	R_UpdateFragmentProgramAlpha (alpha);
}


int R_SelectTexturesAndShaders (gltexture_t *tx, gltexture_t *fb, int alphaflag)
{
	// common for alias and world
	int shaderflag = SHADERFLAG_NONE;

	if (r_lightmap_cheatsafe)
		GL_BindTexture (GL_TEXTURE0, greytexture);
	else
	{
		GL_BindTexture (GL_TEXTURE0, tx);

		// Enable/disable TMU 2 (fullbrights)
		if (gl_fullbrights.value && fb)
		{
			GL_BindTexture (GL_TEXTURE1, fb);
			shaderflag |= SHADERFLAG_LUMA;
		}

		// fence texture test
		if (alphaflag) shaderflag |= SHADERFLAG_FENCE;
	}

	// fog on/off
	if (Fog_GetDensity () > 0) shaderflag |= SHADERFLAG_FOG;

	return shaderflag;
}


