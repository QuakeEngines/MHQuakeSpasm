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

static float	shadelight[4]; // johnfitz -- lit support via lordhavoc / MH - padded for shader params

extern	vec3_t			lightspot;

float	shadevector[4]; // padded for shader uniforms

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
static void *GLARB_GetXYZOffset (bufferset_t *set, aliashdr_t *hdr, int pose)
{
	const int xyzoffs = offsetof (meshxyz_t, position);
	return (void *) (set->vboxyzofs + (set->numverts * pose * sizeof (meshxyz_t)) + xyzoffs);
}


/*
=============
GLARB_GetNormalOffset

Returns the offset of the first vertex's meshxyz_t.normal in the vbo for the
given model and pose.
=============
*/
static void *GLARB_GetNormalOffset (bufferset_t *set, aliashdr_t *hdr, int pose)
{
	const int normaloffs = offsetof (meshxyz_t, normal);
	return (void *) (set->vboxyzofs + (set->numverts * pose * sizeof (meshxyz_t)) + normaloffs);
}


GLuint r_alias_lightmapped_vp = 0;
GLuint r_alias_lightmapped_fp[8] = { 0 };

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
	const GLchar *vp_aliascommon_source = \
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

	const GLchar *fp_lightmapped_source = \
		"!!ARBfp1.0\n"
		"\n"
		"PARAM shadelight = program.local[0];\n"
		"PARAM shadevector = program.local[1];\n"
		"\n"
		"TEMP diff, lmap, luma, fence;\n"
		"TEMP normal, shadedot, dothi, dotlo;\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"TEX luma, fragment.texcoord[0], texture[1], 2D;\n"
		"\n"
		"# fence texture test\n"
		"SUB fence, diff, 0.666;\n"
		"KIL fence.a;\n"
		"\n"
		"# normalize incoming normal\n"
		"DP3 normal.w, fragment.texcoord[1], fragment.texcoord[1];\n"
		"RSQ normal.w, normal.w;\n"
		"MUL normal.xyz, normal.w, fragment.texcoord[1];\n"
		"\n"
		"# perform the lighting\n"
		"DP3 shadedot, normal, shadevector;\n"
		"ADD dothi, shadedot, 1.0;\n"
		"MAD dotlo, shadedot, 0.2954545, 1.0;\n"
		"MAX shadedot, dothi, dotlo;\n"
		"MUL lmap, shadedot, shadelight;\n"
		"\n"
		"# perform the overbrighting\n"
		"MUL_SAT lmap.rgb, lmap, program.env[10].w; # inverse overbright factor\n"
		"MUL lmap.rgb, lmap, program.env[10].z; # overbright factor\n"
		"\n"
		"# perform the lightmapping\n"
		"MUL diff.rgb, diff, lmap;\n"
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

	r_alias_lightmapped_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, GL_GetVertexProgram (vp_aliascommon_source, SHADERFLAG_NONE));

	for (int shaderflag = 0; shaderflag < 8; shaderflag++)
		r_alias_lightmapped_fp[shaderflag] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetFragmentProgram (fp_lightmapped_source, shaderflag));

	r_alias_dynamic_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, GL_GetVertexProgram (vp_aliascommon_source, SHADERFLAG_DYNAMIC));
	r_alias_dynamic_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetDynamicLightFragmentProgramSource ());

	r_alias_fullbright_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetFullbrightFragmentProgramSource ());
	r_alias_shadow_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_shadow_source);
}


void GL_DrawAliasFrame_ARB (entity_t *e, QMATRIX *localMatrix, aliashdr_t *hdr, lerpdata_t *lerpdata, gltexture_t *tx, gltexture_t *fb)
{
	float	blend;
	bufferset_t *set = R_GetBufferSetForModel (e->model);

	if (lerpdata->pose1 != lerpdata->pose2)
	{
		blend = 1.0f - lerpdata->blend;
	}
	else // poses the same means either 1. the entity has paused its animation, or 2. r_lerpmodels is disabled
	{
		blend = 0; // because of "1.0f -" above
	}

	GL_BindBuffer (GL_ARRAY_BUFFER, set->vertexbuffer);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, set->indexbuffer);

	GL_EnableVertexAttribArrays (VAA0 | VAA1 | VAA2 | VAA3 | VAA4);

	glVertexAttribPointer (0, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_t), GLARB_GetXYZOffset (set, hdr, lerpdata->pose1));
	glVertexAttribPointer (1, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_t), GLARB_GetNormalOffset (set, hdr, lerpdata->pose1));
	glVertexAttribPointer (2, 4, GL_UNSIGNED_BYTE, GL_FALSE, sizeof (meshxyz_t), GLARB_GetXYZOffset (set, hdr, lerpdata->pose2));
	glVertexAttribPointer (3, 4, GL_BYTE, GL_TRUE, sizeof (meshxyz_t), GLARB_GetNormalOffset (set, hdr, lerpdata->pose2));
	glVertexAttribPointer (4, 2, GL_FLOAT, GL_FALSE, 0, (void *) (intptr_t) set->vbostofs);

	// set textures
	GL_BindTexture (GL_TEXTURE0, tx);

	if (!cl.worldmodel->lightdata)
		GL_BindPrograms (r_alias_lightmapped_vp, r_alias_fullbright_fp);
	else
	{
		// select the shader to use
		int shaderflag = SHADERFLAG_NONE;

		if (fb)
		{
			GL_BindTexture (GL_TEXTURE1, fb);
			shaderflag |= SHADERFLAG_LUMA;
		}

		// fence texture test
		if (e->model->flags & MF_HOLEY) shaderflag |= SHADERFLAG_FENCE;

		// fog on/off
		if (Fog_GetDensity () > 0) shaderflag |= SHADERFLAG_FOG;

		// bind the selected programs
		GL_BindPrograms (r_alias_lightmapped_vp, r_alias_lightmapped_fp[shaderflag]);
	}

	// set uniforms - these need to be env params so that the dynamic and shadow programs can also access them
	glProgramEnvParameter4fARB (GL_VERTEX_PROGRAM_ARB, 10, blend, blend, blend, 0);
	glProgramEnvParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 11, hdr->scale);
	glProgramEnvParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 12, hdr->scale_origin);

	glProgramLocalParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 0, shadelight);
	glProgramLocalParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 1, shadevector);

	// draw
	glDrawElements (GL_TRIANGLES, set->numindexes, GL_UNSIGNED_SHORT, (void *) (intptr_t) set->vboindexofs);
	rs_aliaspasses += hdr->numtris;
}


void GL_DrawAliasDynamicLights (entity_t *e, QMATRIX *localMatrix, aliashdr_t *hdr, lerpdata_t *lerpdata)
{
	// this depends on state from GL_DrawAliasFrame_ARB so don't call it from anywhere else!
	bufferset_t *set = R_GetBufferSetForModel (e->model);

	// add dynamic lights
	for (int i = 0; i < MAX_DLIGHTS; i++)
	{
		dlight_t *dl = &cl_dlights[i];

		if (cl.time > dl->die || !(dl->radius > dl->minlight))
			continue;

		if (Vector3Dist (lerpdata->origin, dl->origin) > (dl->radius + hdr->boundingradius))
			continue;

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
		glDrawElements (GL_TRIANGLES, set->numindexes, GL_UNSIGNED_SHORT, (void *) (intptr_t) set->vboindexofs);
		rs_aliaspasses += hdr->numtris;
	}
}


void GL_DrawAliasShadow (entity_t *e, aliashdr_t *hdr, lerpdata_t *lerpdata)
{
	// this depends on state from GL_DrawAliasFrame_ARB so don't call it from anywhere else!
	//johnfitz -- values for shadow matrix
#define SHADOW_SKEW_X -0.7 //skew along x axis. -0.7 to mimic glquake shadows
#define SHADOW_SKEW_Y 0 //skew along y axis. 0 to mimic glquake shadows
#define SHADOW_VSCALE 0 //0=completely flat
#define SHADOW_HEIGHT 0.1 //how far above the floor to render the shadow
//johnfitz

	QMATRIX	shadowmatrix = {
		1,				0,				0,				0,
		0,				1,				0,				0,
		SHADOW_SKEW_X,	SHADOW_SKEW_Y,	SHADOW_VSCALE,	0,
		0,				0,				SHADOW_HEIGHT,	1
	};

	/*
P = normalize(Plane);
L = Light;
d = -dot(P, L)

P.a * L.x + d  P.a * L.y      P.a * L.z      P.a * L.w
P.b * L.x      P.b * L.y + d  P.b * L.z      P.b * L.w
P.c * L.x      P.c * L.y      P.c * L.z + d  P.c * L.w
P.d * L.x      P.d * L.y      P.d * L.z      P.d * L.w + d
	*/
	float	lheight = lerpdata->origin[2] - lightspot[2];
	QMATRIX	localMatrix;

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
	glProgramLocalParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 0, 0, 0, 0, 0.5f * r_shadows.value);

	// move it
	glPushMatrix ();
	glMultMatrixf (localMatrix.m16);

	// draw it - the vertex array state is already set
	bufferset_t *set = R_GetBufferSetForModel (e->model);
	glDrawElements (GL_TRIANGLES, set->numindexes, GL_UNSIGNED_SHORT, (void *) (intptr_t) set->vboindexofs);
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
	if ((frame >= hdr->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_AliasSetupFrame: no such frame %d for '%s'\n", frame, e->model->name);
		frame = 0;
	}

	int posenum = hdr->pframes[frame].firstpose;
	int numposes = hdr->pframes[frame].numposes;

	if (numposes > 1)
	{
		// get the correct group frame
		int groupframe = Mod_GetAutoAnimation (hdr->pframes[frame].pintervals, numposes, e->syncbase);

		// get the correct interval
		if (groupframe == 0)
			e->lerptime = hdr->pframes[frame].pintervals[groupframe];
		else e->lerptime = hdr->pframes[frame].pintervals[groupframe] - hdr->pframes[frame].pintervals[groupframe - 1];

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
			lerpdata->blend = Q_fclamp ((cl.time - e->lerpstart) / (e->lerpfinish - e->lerpstart), 0, 1);
		else lerpdata->blend = Q_fclamp ((cl.time - e->lerpstart) / e->lerptime, 0, 1);

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
			blend = Q_fclamp ((cl.time - e->movelerpstart) / (e->lerpfinish - e->movelerpstart), 0, 1);
		else blend = Q_fclamp ((cl.time - e->movelerpstart) / 0.1, 0, 1);

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
void R_MinimumLight (float *light, float minlight)
{
	light[0] = ((light[0] + minlight) * 255.0f) / (255.0f + minlight);
	light[1] = ((light[1] + minlight) * 255.0f) / (255.0f + minlight);
	light[2] = ((light[2] + minlight) * 255.0f) / (255.0f + minlight);
}


void R_SetupAliasLighting (entity_t *e, lerpdata_t *lerpdata)
{
	// get base lighting - this should use the lerped origin
	R_LightPoint (lerpdata->origin, shadelight);

	// minimum light value on gun (24)
	if (e == &cl.viewent)
	{
		if (r_viewleaf->contents == CONTENTS_SOLID)
		{
			// if we're in solid our trace down will just hit solid so we set to a constant lighting factor
			shadelight[0] = 192;
			shadelight[1] = 192;
			shadelight[2] = 192;
		}
		else R_MinimumLight (shadelight, 24);
	}

	// minimum light value on players (8)
	if (e->entitynum >= 1 && e->entitynum <= cl.maxclients)
		R_MinimumLight (shadelight, 8);

	// minimum light value on pickups (64)
	if (e->model->flags & EF_ROTATE)
		R_MinimumLight (shadelight, 64);

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


aliasskin_t *R_GetAliasSkin (entity_t *e, aliashdr_t *hdr)
{
	aliasskingroup_t *group = hdr->pskingroups + (e->skinnum < 0 ? 0 : (e->skinnum >= hdr->numskingroups ? 0 : e->skinnum));
	return &group->pskins[Mod_GetAutoAnimation (group->pintervals, group->numskins, e->syncbase)];
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
	lerpdata_t	lerpdata;

	// setup pose/lerp data -- do it first so we don't miss updates due to culling
	R_SetupAliasFrame (e, hdr, e->frame, &lerpdata);
	R_SetupEntityTransform (e, &lerpdata);

	// this needs to be calced early so we can cull it properly
	R_IdentityMatrix (&localMatrix);
	if (lerpdata.origin[0] || lerpdata.origin[1] || lerpdata.origin[2]) R_TranslateMatrix (&localMatrix, lerpdata.origin[0], lerpdata.origin[1], lerpdata.origin[2]);
	if (lerpdata.angles[0] || lerpdata.angles[1] || lerpdata.angles[2]) R_RotateMatrix (&localMatrix, -lerpdata.angles[0], lerpdata.angles[1], lerpdata.angles[2]);

	// cull it
	if (R_CullModelForEntity (e, &localMatrix, (lerpdata.angles[0] || lerpdata.angles[1] || lerpdata.angles[2])))
		return;

	// set up for alpha blending
	float alpha = ENTALPHA_DECODE (e->alpha);

	if (!(alpha > 0))
		return;

	R_BeginTransparentDrawing (alpha);

	// set up lighting
	R_SetupAliasLighting (e, &lerpdata);

	// set up textures
	aliasskin_t *skin = R_GetAliasSkin (e, hdr);
	gltexture_t *tx = skin->gltexture;
	gltexture_t *fb = skin->fbtexture;

	if (e->colormap != vid.colormap && !gl_nocolors.value)
		if (e->entitynum >= 1 && e->entitynum <= cl.maxclients)
			tx = playertextures[e->entitynum - 1];

	if (!gl_fullbrights.value)
		fb = NULL;

	// transform it
	glPushMatrix ();
	glMultMatrixf (localMatrix.m16);

	// draw it
	GL_DrawAliasFrame_ARB (e, &localMatrix, hdr, &lerpdata, tx, fb);

	// set up dynamic lighting (this depends on state from GL_DrawAliasFrame_ARB so don't call it from anywhere else!)
	if (alpha < 1)
		;		// translucents don't have dlights (light goes through them!)
	else if (!r_dynamic.value)
		;		// dlights switched off
	else if (!cl.worldmodel->lightdata)
		;		// no light data
	else GL_DrawAliasDynamicLights (e, &localMatrix, hdr, &lerpdata);

	// revert the transform
	glPopMatrix ();

	// add shadow (this depends on state from GL_DrawAliasFrame_ARB so don't call it from anywhere else!)
	if (alpha < 1)
		;		// translucents don't have shadows (light goes through them!)
	else if (!r_shadows.value)
		;		// shadows switched off
	else if (e == &cl.viewent || e->model->flags & MOD_NOSHADOW)
		;		// no shadows on these model types
	else GL_DrawAliasShadow (e, hdr, &lerpdata);

	rs_aliaspolys += hdr->numtris;
}


