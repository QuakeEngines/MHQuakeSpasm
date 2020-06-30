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
// gl_sky.c

#include "quakedef.h"

float Fog_GetDensity (void);
float *Fog_GetColor (void);

extern	qmodel_t *loadmodel;
extern	int	rs_skypolys; // for r_speeds readout
extern	int rs_skypasses; // for r_speeds readout

float	skyflatcolor[3];
char	skybox_name[1024]; // name of current skybox, or "" if no skybox

gltexture_t *solidskytexture, *alphaskytexture;

// because the fitzquake texture manager just assumes GL_TEXTURE_2D everywhere we must bypass it for cubemaps
GLuint r_skybox_cubemap = 0;

cvar_t r_fastsky = { "r_fastsky", "0", CVAR_NONE };
cvar_t r_skyalpha = { "r_skyalpha", "1", CVAR_NONE };
cvar_t r_skyfog = { "r_skyfog", "0.5", CVAR_NONE };

float	skyfog; // ericw


// ==============================================================================
//  INIT
// ==============================================================================

/*
=============
Sky_LoadTexture

A sky texture is 256*128, with the left side being a masked overlay
==============
*/
void Sky_LoadTexture (texture_t *mt, byte *src)
{
	char		texturename[64];
	int			i, j, p, r, g, b, count;
	static byte	front_data[128 * 128]; // FIXME: Hunk_Alloc
	static byte	back_data[128 * 128]; // FIXME: Hunk_Alloc
	unsigned *rgba;

	// extract back layer and upload
	for (i = 0; i < 128; i++)
		for (j = 0; j < 128; j++)
			back_data[(i * 128) + j] = src[i * 256 + j + 128];

	q_snprintf (texturename, sizeof (texturename), "%s:%s_back", loadmodel->name, mt->name);
	solidskytexture = TexMgr_LoadImage (loadmodel, texturename, 128, 128, SRC_INDEXED, back_data, "", (src_offset_t) back_data, TEXPREF_NONE);

	// extract front layer and upload
	for (i = 0; i < 128; i++)
	{
		for (j = 0; j < 128; j++)
		{
			front_data[(i * 128) + j] = src[i * 256 + j];
			if (front_data[(i * 128) + j] == 0)
				front_data[(i * 128) + j] = 255;
		}
	}

	q_snprintf (texturename, sizeof (texturename), "%s:%s_front", loadmodel->name, mt->name);
	alphaskytexture = TexMgr_LoadImage (loadmodel, texturename, 128, 128, SRC_INDEXED, front_data, "", (src_offset_t) front_data, TEXPREF_ALPHA);

	// calculate r_fastsky color based on average of all opaque foreground colors
	r = g = b = count = 0;

	for (i = 0; i < 128; i++)
	{
		for (j = 0; j < 128; j++)
		{
			p = src[i * 256 + j];

			if (p != 0)
			{
				rgba = &d_8to24table[p];
				r += ((byte *) rgba)[0];
				g += ((byte *) rgba)[1];
				b += ((byte *) rgba)[2];
				count++;
			}
		}
	}

	skyflatcolor[0] = (float) r / (count * 255);
	skyflatcolor[1] = (float) g / (count * 255);
	skyflatcolor[2] = (float) b / (count * 255);
}


void Sky_FreeSkybox (void)
{
	glDeleteTextures (1, &r_skybox_cubemap);
	r_skybox_cubemap = 0;
}


/*
==================
Sky_LoadSkyBox
==================
*/
void Sky_LoadSkyBox (const char *name)
{
	if (strcmp (skybox_name, name) == 0)
		return; // no change

	// purge old textures
	Sky_FreeSkybox ();

	// turn off skybox if sky is set to ""
	if (name[0] == 0)
	{
		skybox_name[0] = 0;
		return;
	}

	int mark = Hunk_LowMark ();

	char *suf[6] = { "ft", "bk", "up", "dn", "rt", "lf" };
	byte *data[6] = { NULL, NULL, NULL, NULL, NULL, NULL };
	int width[6] = { 0, 0, 0, 0, 0, 0 };
	int height[6] = { 0, 0, 0, 0, 0, 0 };

	// load textures
	for (int i = 0; i < 6; i++)
	{
		// attempt to load the name with or without an _ for different naming conventions
		if ((data[i] = Image_LoadImage (va ("gfx/env/%s%s", name, suf[i]), &width[i], &height[i])) != NULL) continue;
		if ((data[i] = Image_LoadImage (va ("gfx/env/%s_%s", name, suf[i]), &width[i], &height[i])) != NULL) continue;

		// some maps or mods deliberately have incomplete skyboxes; e.g a bottom face may be missing
		// this should probably be a Con_DPrintf so it's suppressed for the general user
	}

	if ((r_skybox_cubemap = TexMgr_LoadCubemap (data, width, height)) != 0)
	{
		q_strlcpy (skybox_name, name, sizeof (skybox_name));
	}
	else
	{
		glDeleteTextures (1, &r_skybox_cubemap);
		r_skybox_cubemap = 0;
		skybox_name[0] = 0;
	}

	Hunk_FreeToLowMark (mark);
}


void Sky_ReloadSkyBox (void)
{
	char lastname[1024];

	// copy off the skybox name and force it to reload
	strcpy (lastname, skybox_name);
	skybox_name[0] = 0;
	Sky_LoadSkyBox (lastname);
}


/*
=================
Sky_NewMap
=================
*/
void Sky_NewMap (void)
{
	char	key[128], value[4096];
	const char *data;

	// initially no sky
	skybox_name[0] = 0;
	skyfog = r_skyfog.value;

	// read worldspawn (this is so ugly, and shouldn't it be done on the server?)
	data = cl.worldmodel->entities;
	if (!data)
		return; // FIXME: how could this possibly ever happen? -- if there's no
	// worldspawn then the sever wouldn't send the loadmap message to the client

	data = COM_Parse (data);
	if (!data) // should never happen
		return; // error
	if (com_token[0] != '{') // should never happen
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

		if (!strcmp ("sky", key))
			Sky_LoadSkyBox (value);

		if (!strcmp ("skyfog", key))
			skyfog = atof (value);

#if 1 // also accept non-standard keys
		else if (!strcmp ("skyname", key)) // half-life
			Sky_LoadSkyBox (value);
		else if (!strcmp ("qlsky", key)) // quake lives
			Sky_LoadSkyBox (value);
#endif
	}
}

/*
=================
Sky_SkyCommand_f
=================
*/
void Sky_SkyCommand_f (void)
{
	switch (Cmd_Argc ())
	{
	case 1:
		Con_Printf ("\"sky\" is \"%s\"\n", skybox_name);
		break;
	case 2:
		Sky_LoadSkyBox (Cmd_Argv (1));
		break;
	default:
		Con_Printf ("usage: sky <skyname>\n");
	}
}

/*
====================
R_SetSkyfog_f -- ericw
====================
*/
static void R_SetSkyfog_f (cvar_t *var)
{
	// clear any skyfog setting from worldspawn
	skyfog = var->value;
}

/*
=============
Sky_Init
=============
*/
void Sky_Init (void)
{
	Cvar_RegisterVariable (&r_fastsky);
	Cvar_RegisterVariable (&r_skyalpha);
	Cvar_RegisterVariable (&r_skyfog);
	Cvar_SetCallback (&r_skyfog, R_SetSkyfog_f);

	Cmd_AddCommand ("sky", Sky_SkyCommand_f);

	skybox_name[0] = 0;
}


GLuint r_skywarp_vp = 0;
GLuint r_skywarp_fp = 0;

GLuint r_skycube_vp = 0;
GLuint r_skycube_fp = 0;

GLuint r_skyfast_vp = 0;
GLuint r_skyfast_fp = 0;

void GLSky_CreateShaders (void)
{
	const GLchar *vp_warp_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# transform position to output\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], vertex.attrib[0];\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], vertex.attrib[0];\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], vertex.attrib[0];\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# transform input position to texcoord\n"
		"DP4 result.texcoord[0].x, state.matrix.program[0].row[0], vertex.attrib[0];\n"
		"DP4 result.texcoord[0].y, state.matrix.program[0].row[1], vertex.attrib[0];\n"
		"DP4 result.texcoord[0].z, state.matrix.program[0].row[2], vertex.attrib[0];\n"
		"DP4 result.texcoord[0].w, state.matrix.program[0].row[3], vertex.attrib[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_warp_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP alphacolor, solidcolor, texcoord;\n"
		"TEMP alphacoord, solidcoord, diff;\n"
		"\n"
		"# normalize incoming texcoord\n"
		"DP3 texcoord.w, fragment.texcoord[0], fragment.texcoord[0];\n"
		"RSQ texcoord.w, texcoord.w;\n"
		"MUL texcoord.xyz, texcoord.w, fragment.texcoord[0];\n"
		"\n"
		"# scale it down\n"
		"MUL texcoord, texcoord, 2.953125;\n"
		"\n"
		"# scroll the sky\n"
		"ADD solidcoord, texcoord, program.env[1].x;\n"
		"ADD alphacoord, texcoord, program.env[1].y;\n"
		"\n"
		"# perform the texturing\n"
		"TEX solidcolor, solidcoord, texture[0], 2D;\n"
		"TEX alphacolor, alphacoord, texture[1], 2D;\n"
		"\n"
		"# apply sky alpha\n"
		"MUL alphacolor.a, alphacolor.a, program.env[1].z;\n"
		"\n"
		"# blend the layers\n"
		"LRP diff, alphacolor.a, alphacolor, solidcolor;\n"
		"\n"
		"# perform the skyfogging\n"
		"LRP diff.rgb, program.env[1].w, state.fog.color, diff;\n"
		"\n"
		"# apply the contrast\n"
		"MUL diff.rgb, diff, program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW result.color.r, diff.r, program.env[10].y;\n"
		"POW result.color.g, diff.g, program.env[10].y;\n"
		"POW result.color.b, diff.b, program.env[10].y;\n"
		"MOV result.color.a, 1.0;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *vp_cube_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# transform position to output\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], vertex.attrib[0];\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], vertex.attrib[0];\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], vertex.attrib[0];\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# transform input position to texcoord\n"
		"DP4 result.texcoord[0].x, state.matrix.program[0].row[0], vertex.attrib[0];\n"
		"DP4 result.texcoord[0].y, state.matrix.program[0].row[1], vertex.attrib[0];\n"
		"DP4 result.texcoord[0].z, state.matrix.program[0].row[2], vertex.attrib[0];\n"
		"DP4 result.texcoord[0].w, state.matrix.program[0].row[3], vertex.attrib[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_cube_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff;\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[5], CUBE;\n"
		"\n"
		"# perform the skyfogging\n"
		"LRP diff.rgb, program.env[1].w, state.fog.color, diff;\n"
		"\n"
		"# apply the contrast\n"
		"MUL diff.rgb, diff, program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW result.color.r, diff.r, program.env[10].y;\n"
		"POW result.color.g, diff.g, program.env[10].y;\n"
		"POW result.color.b, diff.b, program.env[10].y;\n"
		"MOV result.color.a, 1.0;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *vp_fast_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# transform position to output\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], vertex.attrib[0];\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], vertex.attrib[0];\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], vertex.attrib[0];\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_fast_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff;\n"
		"\n"
		"# perform the texturing\n"
		"MOV diff, program.env[1];\n"
		"\n"
		"# perform the skyfogging\n"
		"LRP diff.rgb, program.env[1].w, state.fog.color, diff;\n"
		"\n"
		"# apply the contrast\n"
		"MUL diff.rgb, diff, program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW result.color.r, diff.r, program.env[10].y;\n"
		"POW result.color.g, diff.g, program.env[10].y;\n"
		"POW result.color.b, diff.b, program.env[10].y;\n"
		"MOV result.color.a, 1.0;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_skywarp_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_warp_source);
	r_skywarp_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_warp_source);

	r_skycube_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_cube_source);
	r_skycube_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_cube_source);

	r_skyfast_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_fast_source);
	r_skyfast_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_fast_source);
}


void Sky_SetShaderConstants (void)
{
	if (r_fastsky.value)
	{
		glProgramEnvParameter4fARB (GL_FRAGMENT_PROGRAM_ARB,
			1,
			skyflatcolor[0],
			skyflatcolor[1],
			skyflatcolor[2],
			Fog_GetDensity () > 0 ? skyfog : 0);
	}
	else if (skybox_name[0])
	{
		float skymatrix[] = { 0, 0, 1, 0, -1, 0, 0, 0, 0, 1, 0, 0, r_origin[1], -r_origin[2], -r_origin[0], 1 };

		glMatrixMode (GL_MATRIX0_ARB);
		glLoadMatrixf (skymatrix);
		glMatrixMode (GL_MODELVIEW);

		// enable seamless cubemapping
		if (GLEW_ARB_seamless_cube_map) glEnable (GL_TEXTURE_CUBE_MAP_SEAMLESS);

		glProgramEnvParameter4fARB (GL_FRAGMENT_PROGRAM_ARB,
			1,
			0,
			0,
			0,
			Fog_GetDensity () > 0 ? skyfog : 0);
	}
	else
	{
		float skymatrix[] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 3, 0, -r_origin[0], -r_origin[1], -r_origin[2] * 3.0f, 1 };

		glMatrixMode (GL_MATRIX0_ARB);
		glLoadMatrixf (skymatrix);
		glMatrixMode (GL_MODELVIEW);

		// sky scroll
		float speedscale[2] = { cl.time * 8.0f, cl.time * 16.0f };

		glProgramEnvParameter4fARB (GL_FRAGMENT_PROGRAM_ARB,
			1,
			(speedscale[0] - ((int) speedscale[0] & ~127)) * 0.0078125f,
			(speedscale[1] - ((int) speedscale[1] & ~127)) * 0.0078125f,
			r_skyalpha.value,
			Fog_GetDensity () > 0 ? skyfog : 0);
	}
}


void R_DrawSkychain_ARB (msurface_t *s)
{
	if (r_fastsky.value)
		GL_BindPrograms (r_skyfast_vp, r_skyfast_fp);
	else if (skybox_name[0])
	{
		// explicitly bind to TMU 3 to bypass the texture manager
		glActiveTexture (GL_TEXTURE5);
		glBindTexture (GL_TEXTURE_CUBE_MAP, r_skybox_cubemap);

		// this is only done once per frame so it's OK to explicitly call each time
		TexMgr_SetCubemapFilterModes ();

		// force a rebind after explicitly calling glActiveTexture
		GL_ClearTextureBindings ();

		GL_BindPrograms (r_skycube_vp, r_skycube_fp);
	}
	else
	{
		GL_BindTexture (GL_TEXTURE0, solidskytexture);
		GL_BindTexture (GL_TEXTURE1, alphaskytexture);

		GL_BindPrograms (r_skywarp_vp, r_skywarp_fp);
	}

	GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
	GL_DepthState (GL_TRUE, GL_LEQUAL, GL_TRUE);

	R_DrawSimpleTexturechain (s);
	rs_skypasses++;
}


