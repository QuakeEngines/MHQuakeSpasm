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

// r_alias.c -- alias model rendering

#include "quakedef.h"

extern cvar_t gl_fullbrights, r_lerpmodels, r_lerpmove; // johnfitz

// up to 16 color translated skins
gltexture_t *playertextures[MAX_SCOREBOARD]; // johnfitz -- changed to an array of pointers

#define NUMVERTEXNORMALS	162

float	r_avertexnormals[NUMVERTEXNORMALS][3] = {
#include "anorms.h"
};

extern float	shadelight[4];

extern	vec3_t			lightspot;

float	shadevector[4]; // padded for shader uniforms

float	entalpha; // johnfitz

qboolean shading = true; // johnfitz -- if false, disable vertex shading for various reasons (fullbright, etc)

// johnfitz -- struct for passing lerp information to drawing functions
typedef struct lerpdata_s {
	short pose1;
	short pose2;
	float blend;
	vec3_t origin;
	vec3_t angles;
} lerpdata_t;
// johnfitz


/*
=============
GLARB_GetXYZOffset

Returns the offset of the first vertex's meshxyz_t.xyz in the vbo for the given
model and pose.
=============
*/
static void *GLARB_GetXYZOffset (aliashdr_t *hdr, int pose)
{
	const int xyzoffs = offsetof (meshxyz_t, xyz);
	return (void *) (currententity->model->vboxyzofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_t)) + xyzoffs);
}

/*
=============
GLARB_GetNormalOffset

Returns the offset of the first vertex's meshxyz_t.normal in the vbo for the
given model and pose.
=============
*/
static void *GLARB_GetNormalOffset (aliashdr_t *hdr, int pose)
{
	const int normaloffs = offsetof (meshxyz_t, normal);
	return (void *) (currententity->model->vboxyzofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_t)) + normaloffs);
}


GLuint r_alias_vp = 0;
GLuint r_alias_fp[2] = { 0, 0 }; // luma, no luma

/*
=============
GLAlias_CreateShaders
=============
*/
void GLAlias_CreateShaders (void)
{
	const GLchar *vp_source = \
		"!!ARBvp1.0\n"
		"\n"
		"TEMP position, normal;\n"
		"PARAM lerpfactor = program.local[0];\n"
		"\n"
		"# interpolate the position\n"
		"SUB position, vertex.attrib[0], vertex.attrib[2];\n"
		"MAD position, lerpfactor, position, vertex.attrib[2];\n"
		"\n"
		"# transform interpolated position to output position\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], position;\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], position;\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], position;\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], position;\n"
		"\n"
		"# copy over texcoord\n"
		"MOV result.texcoord[0], vertex.attrib[4];\n"
		"\n"
		"# interpolate the normal and store in texcoord[1] so that we can do per-fragment lighting for better quality\n"
		"SUB normal, vertex.attrib[1], vertex.attrib[3];\n"
		"MAD result.texcoord[1], lerpfactor, normal, vertex.attrib[3];\n"
		"\n"
		"# set up fog coordinate\n"
		"DP4 result.fogcoord.x, state.matrix.mvp.row[3], position;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_source0 = \
		"!!ARBfp1.0\n"
		"\n"
		"PARAM shadelight = program.local[0];\n"
		"PARAM shadevector = program.local[1];\n"
		"\n"
		"TEMP diff, fence;\n"
		"TEMP normal, shadedot, dothi, dotlo;\n"
		"\n"
		"# normalize incoming normal\n"
		"DP3 normal.w, fragment.texcoord[1], fragment.texcoord[1];\n"
		"RSQ normal.w, normal.w;\n"
		"MUL normal.xyz, normal.w, fragment.texcoord[1];\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"\n"
		"# fence texture test\n"
		"SUB fence, diff, 0.666;\n"
		"KIL fence.a;\n"
		"\n"
		"# perform the lighting\n"
		"DP3 shadedot, normal, shadevector;\n"
		"ADD dothi, shadedot, 1.0;\n"
		"MAD dotlo, shadedot, 0.2954545, 1.0;\n"
		"MAX shadedot, dothi, dotlo;\n"
		"\n"
		"# perform the lightmapping to output\n"
		"MUL diff.rgb, diff, shadedot;\n"
		"MUL diff.rgb, diff, shadelight;\n"
		"MUL diff.rgb, diff, program.env[10].z; # overbright factor\n"
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

	const GLchar *fp_source1 = \
		"!!ARBfp1.0\n"
		"\n"
		"PARAM shadelight = program.local[0];\n"
		"PARAM shadevector = program.local[1];\n"
		"\n"
		"TEMP diff, fence, luma;\n"
		"TEMP normal, shadedot, dothi, dotlo;\n"
		"\n"
		"# normalize incoming normal\n"
		"DP3 normal.w, fragment.texcoord[1], fragment.texcoord[1];\n"
		"RSQ normal.w, normal.w;\n"
		"MUL normal.xyz, normal.w, fragment.texcoord[1];\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"TEX luma, fragment.texcoord[0], texture[1], 2D;\n"
		"\n"
		"# fence texture test\n"
		"SUB fence, diff, 0.666;\n"
		"KIL fence.a;\n"
		"\n"
		"# perform the lighting\n"
		"DP3 shadedot, normal, shadevector;\n"
		"ADD dothi, shadedot, 1.0;\n"
		"MAD dotlo, shadedot, 0.2954545, 1.0;\n"
		"MAX shadedot, dothi, dotlo;\n"
		"\n"
		"# perform the lightmapping to output\n"
		"MUL diff.rgb, diff, shadedot;\n"
		"MUL diff.rgb, diff, shadelight;\n"
		"MUL diff.rgb, diff, program.env[10].z; # overbright factor\n"
		"\n"
		"# perform the luma masking\n"
		"MAX diff, diff, luma;\n"
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

	r_alias_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_source);
	r_alias_fp[0] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_source0);
	r_alias_fp[1] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_source1);
}


void GL_DrawAliasFrame_ARB (aliashdr_t *paliashdr, lerpdata_t lerpdata, gltexture_t *tx, gltexture_t *fb)
{
	float	blend;

	if (lerpdata.pose1 != lerpdata.pose2)
	{
		blend = 1.0f - lerpdata.blend;
	}
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
	{
		blend = 1;
	}

	GL_BindBuffer (GL_ARRAY_BUFFER, currententity->model->meshvbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, currententity->model->meshindexesvbo);

	GL_EnableVertexAttribArrays (VAA0 | VAA1 | VAA2 | VAA3 | VAA4);

	glVertexAttribPointer (0, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_t), GLARB_GetXYZOffset (paliashdr, lerpdata.pose1));
	glVertexAttribPointer (1, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_t), GLARB_GetNormalOffset (paliashdr, lerpdata.pose1));
	glVertexAttribPointer (2, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_t), GLARB_GetXYZOffset (paliashdr, lerpdata.pose2));
	glVertexAttribPointer (3, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_t), GLARB_GetNormalOffset (paliashdr, lerpdata.pose2));
	glVertexAttribPointer (4, 2, GL_FLOAT, GL_FALSE, 0, (void *) (intptr_t) currententity->model->vbostofs);

	// set textures
	GL_BindTexture (GL_TEXTURE0, tx);

	if (fb)
	{
		GL_BindTexture (GL_TEXTURE1, fb);
		GL_BindPrograms (r_alias_vp, r_alias_fp[1]);
	}
	else GL_BindPrograms (r_alias_vp, r_alias_fp[0]);

	// set uniforms
	glProgramLocalParameter4fARB (GL_VERTEX_PROGRAM_ARB, 0, blend, blend, blend, 0);

	glProgramLocalParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 0, shadelight);
	glProgramLocalParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 1, shadevector);

	glProgramEnvParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 0, 1, 1, 1, entalpha);

	// draw
	glDrawElements (GL_TRIANGLES, paliashdr->numindexes, GL_UNSIGNED_SHORT, (void *) (intptr_t) currententity->model->vboindexofs);

	rs_aliaspasses += paliashdr->numtris;
}


/*
=================
R_SetupAliasFrame -- johnfitz -- rewritten to support lerping
=================
*/
void R_SetupAliasFrame (aliashdr_t *paliashdr, int frame, lerpdata_t *lerpdata)
{
	entity_t *e = currententity;
	int				posenum, numposes;

	if ((frame >= paliashdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d for '%s'\n", frame, e->model->name);
		frame = 0;
	}

	posenum = paliashdr->frames[frame].firstpose;
	numposes = paliashdr->frames[frame].numposes;

	if (numposes > 1)
	{
		e->lerptime = paliashdr->frames[frame].interval;
		posenum += (int) (cl.time / e->lerptime) % numposes;
	}
	else
		e->lerptime = 0.1;

	if (e->lerpflags & LERP_RESETANIM) // kill any lerp in progress
	{
		e->lerpstart = 0;
		e->previouspose = posenum;
		e->currentpose = posenum;
		e->lerpflags -= LERP_RESETANIM;
	}
	else if (e->currentpose != posenum) // pose changed, start new lerp
	{
		if (e->lerpflags & LERP_RESETANIM2) // defer lerping one more time
		{
			e->lerpstart = 0;
			e->previouspose = posenum;
			e->currentpose = posenum;
			e->lerpflags -= LERP_RESETANIM2;
		}
		else
		{
			e->lerpstart = cl.time;
			e->previouspose = e->currentpose;
			e->currentpose = posenum;
		}
	}

	// set up values
	if (r_lerpmodels.value && !(e->model->flags & MOD_NOLERP && r_lerpmodels.value != 2))
	{
		if (e->lerpflags & LERP_FINISH && numposes == 1)
			lerpdata->blend = CLAMP (0, (cl.time - e->lerpstart) / (e->lerpfinish - e->lerpstart), 1);
		else
			lerpdata->blend = CLAMP (0, (cl.time - e->lerpstart) / e->lerptime, 1);
		lerpdata->pose1 = e->previouspose;
		lerpdata->pose2 = e->currentpose;
	}
	else // don't lerp
	{
		lerpdata->blend = 1;
		lerpdata->pose1 = posenum;
		lerpdata->pose2 = posenum;
	}
}

/*
=================
R_SetupEntityTransform -- johnfitz -- set up transform part of lerpdata
=================
*/
void R_SetupEntityTransform (entity_t *e, lerpdata_t *lerpdata)
{
	float blend;
	vec3_t d;
	int i;

	// if LERP_RESETMOVE, kill any lerps in progress
	if (e->lerpflags & LERP_RESETMOVE)
	{
		e->movelerpstart = 0;
		VectorCopy (e->origin, e->previousorigin);
		VectorCopy (e->origin, e->currentorigin);
		VectorCopy (e->angles, e->previousangles);
		VectorCopy (e->angles, e->currentangles);
		e->lerpflags -= LERP_RESETMOVE;
	}
	else if (!VectorCompare (e->origin, e->currentorigin) || !VectorCompare (e->angles, e->currentangles)) // origin/angles changed, start new lerp
	{
		e->movelerpstart = cl.time;
		VectorCopy (e->currentorigin, e->previousorigin);
		VectorCopy (e->origin, e->currentorigin);
		VectorCopy (e->currentangles, e->previousangles);
		VectorCopy (e->angles, e->currentangles);
	}

	// set up values
	if (r_lerpmove.value && e != &cl.viewent && e->lerpflags & LERP_MOVESTEP)
	{
		if (e->lerpflags & LERP_FINISH)
			blend = CLAMP (0, (cl.time - e->movelerpstart) / (e->lerpfinish - e->movelerpstart), 1);
		else
			blend = CLAMP (0, (cl.time - e->movelerpstart) / 0.1, 1);

		// translation
		VectorSubtract (e->currentorigin, e->previousorigin, d);
		lerpdata->origin[0] = e->previousorigin[0] + d[0] * blend;
		lerpdata->origin[1] = e->previousorigin[1] + d[1] * blend;
		lerpdata->origin[2] = e->previousorigin[2] + d[2] * blend;

		// rotation
		VectorSubtract (e->currentangles, e->previousangles, d);
		for (i = 0; i < 3; i++)
		{
			if (d[i] > 180)  d[i] -= 360;
			if (d[i] < -180) d[i] += 360;
		}
		lerpdata->angles[0] = e->previousangles[0] + d[0] * blend;
		lerpdata->angles[1] = e->previousangles[1] + d[1] * blend;
		lerpdata->angles[2] = e->previousangles[2] + d[2] * blend;
	}
	else // don't lerp
	{
		VectorCopy (e->origin, lerpdata->origin);
		VectorCopy (e->angles, lerpdata->angles);
	}
}

/*
=================
R_SetupAliasLighting -- johnfitz -- broken out from R_DrawAliasModel and rewritten
=================
*/
void R_SetupAliasLighting (entity_t *e)
{
	R_LightPoint (e->origin);

	// minimum light value on gun (24)
	if (e == &cl.viewent)
	{
		float add = (float) (72 >> (int) gl_overbright.value) - (shadelight[0] + shadelight[1] + shadelight[2]);

		if (add > 0.0f)
		{
			shadelight[0] += add / 3.0f;
			shadelight[1] += add / 3.0f;
			shadelight[2] += add / 3.0f;
		}
	}

	// minimum light value on players (8)
	if (e > cl_entities && e <= cl_entities + cl.maxclients)
	{
		float add = (float) (24 >> (int) gl_overbright.value) - (shadelight[0] + shadelight[1] + shadelight[2]);

		if (add > 0.0f)
		{
			shadelight[0] += add / 3.0f;
			shadelight[1] += add / 3.0f;
			shadelight[2] += add / 3.0f;
		}
	}

	// hack up the brightness when fullbrights but no overbrights (256)
	// MH - the new "max-blending" removes the need for this

	// ericw -- shadevector is passed to the shader to compute shadedots inside the
	// shader, see GLAlias_CreateShaders()
	float an = e->angles[1] / 180 * M_PI;

	shadevector[0] = cos (-an);
	shadevector[1] = sin (-an);
	shadevector[2] = 1;

	VectorNormalize (shadevector);
	// ericw --

	// take the final colour down to 0..1 range
	// note: our DotProducts will potentially scale this up to 2*, so reduce the range a little further to compensate
	VectorScale (shadelight, 1.0f / 320.0f, shadelight);
}

/*
=================
R_DrawAliasModel -- johnfitz -- almost completely rewritten
=================
*/
void R_DrawAliasModel (entity_t *e)
{
	aliashdr_t *paliashdr = (aliashdr_t *) Mod_Extradata (e->model);
	int			i, anim, skinnum;
	gltexture_t *tx, *fb;
	lerpdata_t	lerpdata;

	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	R_SetupAliasFrame (paliashdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	// cull it
	if (R_CullModelForEntity (e))
		return;

	// transform it
	glPushMatrix ();

	glTranslatef (lerpdata.origin[0], lerpdata.origin[1], lerpdata.origin[2]);
	glRotatef (lerpdata.angles[1], 0, 0, 1);
	glRotatef (-lerpdata.angles[0], 0, 1, 0);
	glRotatef (lerpdata.angles[2], 1, 0, 0);

	glTranslatef (paliashdr->scale_origin[0], paliashdr->scale_origin[1], paliashdr->scale_origin[2]);
	glScalef (paliashdr->scale[0], paliashdr->scale[1], paliashdr->scale[2]);

	shading = true;

	// set up for alpha blending
	entalpha = ENTALPHA_DECODE (e->alpha);

	if (entalpha == 0)
		goto cleanup;

	if (entalpha < 1)
	{
		GL_DepthState (GL_TRUE, GL_LEQUAL, GL_FALSE);
		GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	}
	else
	{
		GL_DepthState (GL_TRUE, GL_LEQUAL, GL_TRUE);
		GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
	}

	// set up lighting
	rs_aliaspolys += paliashdr->numtris;
	R_SetupAliasLighting (e);

	// set up textures
	anim = (int) (cl.time * 10) & 3;
	skinnum = e->skinnum;

	if ((skinnum >= paliashdr->numskins) || (skinnum < 0))
	{
		Con_DPrintf ("R_DrawAliasModel: no such skin # %d for '%s'\n", skinnum, e->model->name);
		// ericw -- display skin 0 for winquake compatibility
		skinnum = 0;
	}

	tx = paliashdr->gltextures[skinnum][anim];
	fb = paliashdr->fbtextures[skinnum][anim];

	if (e->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = e - cl_entities;
		if (i >= 1 && i <= cl.maxclients /* && !strcmp (e->model->name, "progs/player.mdl") */)
			tx = playertextures[i - 1];
	}

	if (!gl_fullbrights.value)
		fb = NULL;

	// draw it
	GL_DrawAliasFrame_ARB (paliashdr, lerpdata, tx, fb);

cleanup:
	glShadeModel (GL_SMOOTH);

	glPopMatrix ();
}


