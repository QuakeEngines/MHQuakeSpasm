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
static void *GLARB_GetXYZOffset (entity_t *e, aliashdr_t *hdr, int pose)
{
	const int xyzoffs = offsetof (meshxyz_t, position);
	return (void *) (e->model->vboxyzofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_t)) + xyzoffs);
}


/*
=============
GLARB_GetNormalOffset

Returns the offset of the first vertex's meshxyz_t.normal in the vbo for the
given model and pose.
=============
*/
static void *GLARB_GetNormalOffset (entity_t *e, aliashdr_t *hdr, int pose)
{
	const int normaloffs = offsetof (meshxyz_t, normal);
	return (void *) (e->model->vboxyzofs + (hdr->numverts_vbo * pose * sizeof (meshxyz_t)) + normaloffs);
}


GLuint r_alias_lightmapped_vp = 0;
GLuint r_alias_lightmapped_fp[2] = { 0, 0 }; // luma, no luma

GLuint r_alias_dynamic_vp = 0;
GLuint r_alias_dynamic_fp = 0;

GLuint r_alias_fullbright_fp = 0;
GLuint r_alias_shadow_fp = 0;

/*
=============
GLAlias_CreateShaders
=============
*/
void GLAlias_CreateShaders (void)
{
	const GLchar *vp_lightmapped_source = \
		"!!ARBvp1.0\n"
		"\n"
		"TEMP position, normal;\n"
		"PARAM lerpfactor = program.env[10];\n"
		"PARAM scale = program.env[11];\n"
		"PARAM scale_origin = program.env[12];\n"
		"\n"
		"# interpolate the position\n"
		"SUB position, vertex.attrib[0], vertex.attrib[2];\n"
		"MAD position, lerpfactor, position, vertex.attrib[2];\n"
		"\n"
		"# scale and offset\n"
		"MAD position, position, scale, scale_origin;\n"
		"MOV position.w, 1.0; # ensure\n"
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

	const GLchar *fp_lightmapped_source0 = \
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

	const GLchar *fp_lightmapped_source1 = \
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

	const GLchar *vp_dynamic_source = \
		"!!ARBvp1.0\n"
		"\n"
		"TEMP position, normal;\n"
		"PARAM lerpfactor = program.env[10];\n"
		"PARAM scale = program.env[11];\n"
		"PARAM scale_origin = program.env[12];\n"
		"\n"
		"# interpolate the position\n"
		"SUB position, vertex.attrib[0], vertex.attrib[2];\n"
		"MAD position, lerpfactor, position, vertex.attrib[2];\n"
		"\n"
		"# scale and offset\n"
		"MAD position, position, scale, scale_origin;\n"
		"MOV position.w, 1.0; # ensure\n"
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
		"# result.texcoord[2] is light vector\n"
		"SUB result.texcoord[2], program.local[1], position;\n"
		"\n"
		"# set up fog coordinate\n"
		"DP4 result.fogcoord.x, state.matrix.mvp.row[3], position;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	// fogged shadows don't look great, but the shadows aren't particularly robust anyway, so what the hey
	const GLchar *fp_shadow_source = \
		"!!ARBfp1.0\n"
		"\n"
		"# perform the fogging\n"
		"TEMP fogFactor;\n"
		"MUL fogFactor.x, state.fog.params.x, fragment.fogcoord.x;\n"
		"MUL fogFactor.x, fogFactor.x, fogFactor.x;\n"
		"EX2_SAT fogFactor.x, -fogFactor.x;\n"
		"LRP result.color.rgb, fogFactor.x, program.local[0], state.fog.color;\n"
		"MOV result.color.a, program.local[0].a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_alias_lightmapped_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_lightmapped_source);
	r_alias_lightmapped_fp[0] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_lightmapped_source0);
	r_alias_lightmapped_fp[1] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_lightmapped_source1);

	r_alias_dynamic_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_dynamic_source);
	r_alias_dynamic_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetDynamicLightFragmentProgramSource ());

	r_alias_fullbright_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetFullbrightFragmentProgramSource ());
	r_alias_shadow_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_shadow_source);
}


void GL_DrawAliasFrame_ARB (entity_t *e, QMATRIX *localMatrix, aliashdr_t *hdr, lerpdata_t *lerpdata, gltexture_t *tx, gltexture_t *fb)
{
	float	blend;

	if (lerpdata->pose1 != lerpdata->pose2)
	{
		blend = 1.0f - lerpdata->blend;
	}
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
	{
		blend = 0; // because of "1.0f -" above
	}

	GL_BindBuffer (GL_ARRAY_BUFFER, e->model->meshvbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, e->model->meshindexesvbo);

	GL_EnableVertexAttribArrays (VAA0 | VAA1 | VAA2 | VAA3 | VAA4);

	glVertexAttribPointer (0, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_t), GLARB_GetXYZOffset (e, hdr, lerpdata->pose1));
	glVertexAttribPointer (1, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_t), GLARB_GetNormalOffset (e, hdr, lerpdata->pose1));
	glVertexAttribPointer (2, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_t), GLARB_GetXYZOffset (e, hdr, lerpdata->pose2));
	glVertexAttribPointer (3, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_t), GLARB_GetNormalOffset (e, hdr, lerpdata->pose2));
	glVertexAttribPointer (4, 2, GL_FLOAT, GL_FALSE, 0, (void *) (intptr_t) e->model->vbostofs);

	// set textures
	GL_BindTexture (GL_TEXTURE0, tx);

	if (!cl.worldmodel->lightdata)
		GL_BindPrograms (r_alias_lightmapped_vp, r_alias_fullbright_fp);
	else if (fb)
	{
		GL_BindTexture (GL_TEXTURE1, fb);
		GL_BindPrograms (r_alias_lightmapped_vp, r_alias_lightmapped_fp[1]);
	}
	else GL_BindPrograms (r_alias_lightmapped_vp, r_alias_lightmapped_fp[0]);

	// set uniforms - these need to be env params so that the dynamic program can also access them
	glProgramEnvParameter4fARB (GL_VERTEX_PROGRAM_ARB, 10, blend, blend, blend, 0);
	glProgramEnvParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 11, hdr->scale);
	glProgramEnvParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 12, hdr->scale_origin);

	glProgramLocalParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 0, shadelight);
	glProgramLocalParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 1, shadevector);

	glProgramEnvParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 0, 1, 1, 1, entalpha);

	// draw
	glDrawElements (GL_TRIANGLES, hdr->numindexes, GL_UNSIGNED_SHORT, (void *) (intptr_t) e->model->vboindexofs);
	rs_aliaspasses += hdr->numtris;

	// add dynamic lights
	if (!r_dynamic.value) return;
	if (!cl.worldmodel->lightdata) return;

	for (int i = 0; i < MAX_DLIGHTS; i++)
	{
		dlight_t *dl = &cl_dlights[i];

		if (dl->die < cl.time || !(dl->radius > dl->minlight))
			continue;

		if (((dl->radius + hdr->boundingradius) - VectorDist (lerpdata->origin, dl->origin)) > 0)
		{
			// move the light into the same space as the entity
			R_InverseTransform (localMatrix, dl->transformed, dl->origin);

			// switch to additive blending
			GL_DepthState (GL_TRUE, GL_EQUAL, GL_FALSE);
			GL_BlendState (GL_TRUE, GL_ONE, GL_ONE);

			// dynamic light programs
			GL_BindPrograms (r_alias_dynamic_vp, r_alias_dynamic_fp);

			// light properties
			GL_SetupDynamicLight (dl);

			// and draw it
			glDrawElements (GL_TRIANGLES, hdr->numindexes, GL_UNSIGNED_SHORT, (void *) (intptr_t) e->model->vboindexofs);
			rs_aliaspasses += hdr->numtris;
		}
	}
}


void GL_DrawAliasShadow (entity_t *e, aliashdr_t *hdr, lerpdata_t *lerpdata)
{
	//johnfitz -- values for shadow matrix
#define SHADOW_SKEW_X -0.7 //skew along x axis. -0.7 to mimic glquake shadows
#define SHADOW_SKEW_Y 0 //skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE 0 //0=completely flat
#define SHADOW_HEIGHT 0.1 //how far above the floor to render the shadow
//johnfitz

	/*
	orient onto lightplane:

	s1 = sin(ent->angles[1] / 180 * M_PI);
	c1 = cos(ent->angles[1] / 180 * M_PI);

	interpolated[2] += ((interpolated[1] * (s1 * lightplane->normal[0])) -
				(interpolated[0] * (c1 * lightplane->normal[0])) -
				(interpolated[0] * (s1 * lightplane->normal[1])) -
				(interpolated[1] * (c1 * lightplane->normal[1]))) +
				((1 - lightplane->normal[2]) * 20) + 0.2;

	there's probably a friendlier way of doing this via the matrix transform....
	*/

	QMATRIX	shadowmatrix = {
		1,				0,				0,				0,
		0,				1,				0,				0,
		SHADOW_SKEW_X,	SHADOW_SKEW_Y,	SHADOW_VSCALE,	0,
		0,				0,				SHADOW_HEIGHT,	1
	};

	float	lheight;
	QMATRIX	localMatrix;

	if (e == &cl.viewent || e->model->flags & MOD_NOSHADOW)
		return;

	entalpha = ENTALPHA_DECODE (e->alpha);
	if (entalpha == 0) return;

	lheight = lerpdata->origin[2] - lightspot[2];

	// position the shadow
	R_IdentityMatrix (&localMatrix);
	R_TranslateMatrix (&localMatrix, lerpdata->origin[0], lerpdata->origin[1], lerpdata->origin[2]);
	R_TranslateMatrix (&localMatrix, 0, 0, -lheight);
	R_MultMatrix (&localMatrix, &shadowmatrix, &localMatrix);
	R_TranslateMatrix (&localMatrix, 0, 0, lheight);
	R_RotateMatrix (&localMatrix, 0, lerpdata->angles[1], 0);

	// state and shaders
	GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_DepthState (GL_TRUE, GL_LEQUAL, GL_FALSE);

	// this is the only stencil stuff in the engine so we don't need state tracking for it
	// glClearStencil, glStencilFunc and glStencilOp are set in GL_SetupState
	if (gl_stencilbits)
		glEnable (GL_STENCIL_TEST);

	// we can just reuse the lightmapped vp because it does everything we need without much extra overhead
	GL_BindPrograms (r_alias_lightmapped_vp, r_alias_shadow_fp);

	// shadow colour - allow different values of r_shadows to change the colour
	glProgramLocalParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 0, 0, 0, 0, entalpha * 0.5f * r_shadows.value);

	// move it
	glPushMatrix ();
	glMultMatrixf (localMatrix.m16);

	// draw it
	glDrawElements (GL_TRIANGLES, hdr->numindexes, GL_UNSIGNED_SHORT, (void *) (intptr_t) e->model->vboindexofs);
	rs_aliaspasses += hdr->numtris;

	// revert the transform
	glPopMatrix ();

	// this is the only stencil stuff in the engine so we don't need state tracking for it
	if (gl_stencilbits)
		glDisable (GL_STENCIL_TEST);
}


/*
=================
R_SetupAliasFrame -- johnfitz -- rewritten to support lerping
=================
*/
void R_SetupAliasFrame (entity_t *e, aliashdr_t *hdr, int frame, lerpdata_t *lerpdata)
{
	int		posenum, numposes;

	if ((frame >= hdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d for '%s'\n", frame, e->model->name);
		frame = 0;
	}

	posenum = hdr->frames[frame].firstpose;
	numposes = hdr->frames[frame].numposes;

	if (numposes > 1)
	{
		// get the intervals
		float *intervals = (float *) ((byte *) hdr + hdr->frames[frame].intervals);

		// get the correct group frame
		int groupframe = Mod_GetAutoAnimation (intervals, numposes, e->syncbase);

		// get the correct interval
		if (groupframe == 0)
			e->lerptime = intervals[groupframe];
		else e->lerptime = intervals[groupframe] - intervals[groupframe - 1];

		// advance to this frame
		posenum += groupframe;
	}
	else e->lerptime = 0.1;

	if (e->lerpflags & LERP_RESETANIM) // kill any lerp in progress
	{
		e->lerpstart = 0;
		e->previouspose = posenum;
		e->currentpose = posenum;
		e->lerpflags &= ~LERP_RESETANIM;
	}
	else if (e->currentpose != posenum) // pose changed, start new lerp
	{
		if (e->lerpflags & LERP_RESETANIM2) // defer lerping one more time
		{
			e->lerpstart = 0;
			e->previouspose = posenum;
			e->currentpose = posenum;
			e->lerpflags &= ~LERP_RESETANIM2;
		}
		else
		{
			e->lerpstart = cl.time;
			e->previouspose = e->currentpose;
			e->currentpose = posenum;
		}
	}

	// set up values
	if (r_lerpmodels.value && !((e->model->flags & MOD_NOLERP) && r_lerpmodels.value != 2))
	{
		if ((e->lerpflags & LERP_FINISH) && numposes == 1)
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
		e->lerpflags &= ~LERP_RESETMOVE;
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
	if (r_lerpmove.value && e != &cl.viewent && (e->lerpflags & LERP_MOVESTEP))
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
	QMATRIX localMatrix;
	aliashdr_t *hdr = (aliashdr_t *) Mod_Extradata (e->model);
	int			i, anim, skinnum;
	gltexture_t *tx, *fb;
	lerpdata_t	lerpdata;

	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	R_SetupAliasFrame (e, hdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	// this needs to be calced early so we can cull it properly
	R_IdentityMatrix (&localMatrix);
	R_TranslateMatrix (&localMatrix, lerpdata.origin[0], lerpdata.origin[1], lerpdata.origin[2]);
	R_RotateMatrix (&localMatrix, -lerpdata.angles[0], lerpdata.angles[1], lerpdata.angles[2]);

	// cull it
	if (R_CullModelForEntity (e, &localMatrix))
		return;

	// set up for alpha blending
	entalpha = ENTALPHA_DECODE (e->alpha);

	if (entalpha == 0)
		return;

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
	rs_aliaspolys += hdr->numtris;
	R_SetupAliasLighting (e);

	// set up textures
	anim = (int) (cl.time * 10) & 3;
	skinnum = e->skinnum;

	if ((skinnum >= hdr->numskins) || (skinnum < 0))
	{
		Con_DPrintf ("R_DrawAliasModel: no such skin # %d for '%s'\n", skinnum, e->model->name);
		// ericw -- display skin 0 for winquake compatibility
		skinnum = 0;
	}

	tx = hdr->gltextures[skinnum][anim];
	fb = hdr->fbtextures[skinnum][anim];

	if (e->colormap != vid.colormap && !gl_nocolors.value)
	{
		i = e - cl_entities;
		if (i >= 1 && i <= cl.maxclients /* && !strcmp (e->model->name, "progs/player.mdl") */)
			tx = playertextures[i - 1];
	}

	if (!gl_fullbrights.value)
		fb = NULL;

	// transform it
	glPushMatrix ();
	glMultMatrixf (localMatrix.m16);

	// draw it
	GL_DrawAliasFrame_ARB (e, &localMatrix, hdr, &lerpdata, tx, fb);

	// revert the transform
	glPopMatrix ();

	// add shadow
	if (r_shadows.value)
		GL_DrawAliasShadow (e, hdr, &lerpdata);
}


