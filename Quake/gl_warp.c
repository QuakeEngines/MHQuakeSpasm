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
		"ADD result.texcoord[1], vertex.attrib[2], program.local[0];\n"
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
		"SIN warpcoord.x, fragment.texcoord[1].x;\n"
		"SIN warpcoord.y, fragment.texcoord[1].y;\n"
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
		"# copy over the result\n"
		"MOV result.color.rgb, diff;\n"
		"\n"
		"# set the alpha channel correctly\n"
		"MOV result.color.a, program.local[0].a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_waterwarp_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_source);
	r_waterwarp_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_source);
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
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);

		// water alpha
		glProgramLocalParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 0, 1, 1, 1, entalpha);
	}
}


/*
=============
R_EndTransparentDrawing -- ericw
=============
*/
static void R_EndTransparentDrawing (float entalpha)
{
	if (entalpha < 1.0f)
	{
		glDepthMask (GL_TRUE);
		glDisable (GL_BLEND);
	}
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

	// water warp
	glProgramLocalParameter4fARB (GL_VERTEX_PROGRAM_ARB,
		0,
		cl.time - ((M_PI * 2) * (int) (cl.time / (M_PI * 2))),
		cl.time - ((M_PI * 2) * (int) (cl.time / (M_PI * 2))),
		0,
		0);

	for (int i = 0; i < model->numtextures; i++)
	{
		texture_t *t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
			continue;

		qboolean bound = false;
		float entalpha = 1.0f;

		for (msurface_t *s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (s->culled) continue;

			if (!bound) // only bind once we are sure we need this texture
			{
				entalpha = GL_WaterAlphaForEntitySurface (ent, s);

				R_BeginTransparentDrawing (entalpha);
				GL_BindTexture (GL_TEXTURE0, t->gltexture);

				R_ClearBatch ();

				bound = true;
			}

			R_BatchSurface (s);

			rs_brushpasses++;
		}

		R_FlushBatch ();
		R_EndTransparentDrawing (entalpha);
	}
}


