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
// gl_warp.c -- warping animation support

#include "quakedef.h"

cvar_t r_waterwarp = { "r_waterwarp", "1", CVAR_NONE };


GLuint r_waterwarp_vp = 0;
GLuint r_waterwarp_fp = 0;

GLuint r_underwater_vp = 0;
GLuint r_underwater_fp = 0;

GLuint r_underwater_noise = 0;

int r_underwater_width = 0;
int r_underwater_height = 0;


void GLWarp_CreateShaders (void)
{
	const GLchar *vp_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# transform position to output\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], vertex.attrib[0];\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], vertex.attrib[0];\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], vertex.attrib[0];\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# copy over texcoords adjusting[1] for time\n"
		"MOV result.texcoord[0], vertex.attrib[1];\n"
		"MAD result.texcoord[1], vertex.attrib[1], 3.14159265358979323846, program.env[0];\n"
		"\n"
		"# set up fog coordinate\n"
		"DP4 result.fogcoord.x, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff;\n"
		"TEMP warpcoord;\n"
		"\n"
		"# perform the warp\n"
		"SIN warpcoord.x, fragment.texcoord[1].y;\n"
		"SIN warpcoord.y, fragment.texcoord[1].x;\n"
		"\n"
		"# adjust for the warped coord\n"
		"MAD warpcoord, warpcoord, 0.125, fragment.texcoord[0];\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, warpcoord, texture[0], 2D;\n"
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
		"MOV result.color.a, program.local[0].a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *vp_underwater_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# copy over position\n"
		"MOV result.position, vertex.attrib[0];\n"
		"\n"
		"# copy over texcoord\n"
		"MOV result.texcoord[0], vertex.attrib[1];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	// this is a post-process so the input has already had gamma applied
	const GLchar *fp_underwater_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff, v_blend;\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[5], RECT;\n"
		"\n"
		"# apply the contrast\n"
		"MUL v_blend.rgb, program.local[0], program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW v_blend.r, v_blend.r, program.env[10].y;\n"
		"POW v_blend.g, v_blend.g, program.env[10].y;\n"
		"POW v_blend.b, v_blend.b, program.env[10].y;\n"
		"MOV v_blend.a, program.local[0].a;\n"
		"\n"
		"# blend to output\n"
		"LRP result.color, v_blend.a, v_blend, diff;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_waterwarp_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_source);
	r_waterwarp_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_source);

	r_underwater_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_underwater_source);
	r_underwater_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_underwater_source);
}


/*
================
GL_WaterAlphaForEntitySurface -- ericw

Returns the water alpha to use for the entity and surface combination.
================
*/
float GL_WaterAlphaForEntitySurface (entity_t *ent, msurface_t *s)
{
	float entalpha;

	if (ent == NULL || ent->alpha == ENTALPHA_DEFAULT)
		entalpha = GL_WaterAlphaForSurface (s);
	else
		entalpha = ENTALPHA_DECODE (ent->alpha);

	return entalpha;
}


/*
=============
R_BeginTransparentDrawing -- ericw
=============
*/
static void R_BeginTransparentDrawing (float entalpha)
{
	if (entalpha < 1.0f)
	{
		GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GL_DepthState (GL_TRUE, GL_LEQUAL, GL_FALSE);

		// water alpha
		glProgramLocalParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 0, 1, 1, 1, entalpha);
	}
	else
	{
		GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
		GL_DepthState (GL_TRUE, GL_LEQUAL, GL_TRUE);
	}
}


void Warp_SetShaderConstants (void)
{
	// water warp
	glProgramEnvParameter4fARB (GL_VERTEX_PROGRAM_ARB,
		0,
		cl.time - ((M_PI * 2) * (int) (cl.time / (M_PI * 2))),
		cl.time - ((M_PI * 2) * (int) (cl.time / (M_PI * 2))),
		0,
		0
	);
}


/*
================
R_DrawTextureChains_Water -- johnfitz
================
*/
void R_DrawTextureChains_Water (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	R_SetupWorldVBOState ();

	GL_BindPrograms (r_waterwarp_vp, r_waterwarp_fp);

	for (int i = 0; i < model->numtextures; i++)
	{
		texture_t *t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
			continue;

		float entalpha = GL_WaterAlphaForEntitySurface (ent, t->texturechains[chain]);

		R_BeginTransparentDrawing (entalpha);

		GL_BindTexture (GL_TEXTURE0, t->gltexture);

		R_DrawSimpleTexturechain (t->texturechains[chain]);
	}
}


void R_UnderwaterWarp (void)
{
	// set up viewport dims
	int srcx = glx + r_refdef.vrect.x;
	int srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	int srcw = r_refdef.vrect.width;
	int srch = r_refdef.vrect.height;

	// using GL_TEXTURE5 again for binding points that are not GL_TEXTURE_2D
	// bind to default texture object 0
	glActiveTexture (GL_TEXTURE5);
	glBindTexture (GL_TEXTURE_RECTANGLE, 0);

	// we enforced requiring a rectangle texture extension so we don't need to worry about np2 stuff
	glCopyTexImage2D (GL_TEXTURE_RECTANGLE, 0, GL_RGBA, srcx, srcy, srcw, srch, 0);
	glTexParameteri (GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf (GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf (GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// draw the texture back to the framebuffer
	glDisable (GL_CULL_FACE);

	GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);

	glViewport (srcx, srcy, r_refdef.vrect.width, r_refdef.vrect.height);

	float positions[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
	float texcoord1[] = { 0, 0, srcw, 0, srcw, srch, 0, srch };

	GL_BindBuffer (GL_ARRAY_BUFFER, 0);
	GL_EnableVertexAttribArrays (VAA0 | VAA1);
	GL_BindPrograms (r_underwater_vp, r_underwater_fp);

	// merge the polyblend into the water warp for perf
	glProgramLocalParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 0, v_blend);

	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, positions);
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 0, texcoord1);

	glDrawArrays (GL_QUADS, 0, 4);

	// clear cached binding
	GL_ClearTextureBindings ();
}


