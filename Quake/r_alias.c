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

static float	shadelight[4]; // johnfitz -- lit support via lordhavoc / MH - padded for shader params
static float	shadevector[4]; // padded for shader uniforms

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

GLuint r_alias_drawflat_vp = 0;
GLuint r_alias_drawflat_fp = 0;

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
		"SUB result.texcoord[2], program.env[7], position;\n"
		"\n"
		"# copy over drawflat colour\n"
		"MOV result.color, vertex.attrib[5];\n"
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
		"PARAM shadelight = program.env[3];\n"
		"PARAM shadevector = program.env[4];\n"
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
		"LRP result.color.rgb, fogFactor.x, program.env[8], state.fog.color;\n"
		"MOV result.color.a, program.env[8].a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_alias_lightmapped_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, GL_GetVertexProgram (vp_aliascommon_source, SHADERFLAG_NONE));

	for (int shaderflag = 0; shaderflag < 8; shaderflag++)
		r_alias_lightmapped_fp[shaderflag] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetFragmentProgram (fp_lightmapped_source, shaderflag));

	r_alias_dynamic_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, GL_GetVertexProgram (vp_aliascommon_source, SHADERFLAG_DYNAMIC));
	r_alias_dynamic_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetDynamicLightFragmentProgramSource ());

	r_alias_drawflat_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, GL_GetVertexProgram (vp_aliascommon_source, SHADERFLAG_DRAWFLAT));
	r_alias_drawflat_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetDrawflatFragmentProgramSource ());

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
	glVertexAttribPointer (4, 2, GL_FLOAT, GL_FALSE, sizeof (meshst_t), (void *) (intptr_t) set->vbostofs);

	// select shaders
	if (r_drawflat_cheatsafe)
		GL_BindPrograms (r_alias_drawflat_vp, r_alias_drawflat_fp);
	else if (!cl.worldmodel->lightdata || r_fullbright_cheatsafe)
	{
		GL_BindTexture (GL_TEXTURE0, tx);
		GL_BindPrograms (r_alias_lightmapped_vp, r_alias_fullbright_fp);
	}
	else
	{
		int shaderflag = R_SelectTexturesAndShaders (tx, fb, e->model->flags & MF_HOLEY);
		GL_BindPrograms (r_alias_lightmapped_vp, r_alias_lightmapped_fp[shaderflag]);
	}

	// set uniforms
	glProgramEnvParameter4fARB (GL_VERTEX_PROGRAM_ARB, 10, blend, blend, blend, 0);
	glProgramEnvParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 11, hdr->scale);
	glProgramEnvParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 12, hdr->scale_origin);

	glProgramEnvParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 3, shadelight);
	glProgramEnvParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 4, shadevector);

	// draw
	if (r_drawflat_cheatsafe)
	{
		// r_drawflat isn't meant to be a robust performant mode anyway....
		for (int i = 0; i < set->numindexes; i += 3)
		{
			// r_drawflat isn't meant to be a robust performant mode anyway....
			extern byte r_flatcolor[1024][4];
			glVertexAttrib4Nubv (5, r_flatcolor[(i / 3) & 1023]);
			glDrawElements (GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (void *) (intptr_t) (i * sizeof (unsigned short)));
		}
	}
	else glDrawElements (GL_TRIANGLES, set->numindexes, GL_UNSIGNED_SHORT, (void *) (intptr_t) 0);

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
		glDrawElements (GL_TRIANGLES, set->numindexes, GL_UNSIGNED_SHORT, (void *) (intptr_t) 0);
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

	// move it
	glPushMatrix ();
	glMultMatrixf (localMatrix.m16);

	// draw it - the vertex array state is already set
	bufferset_t *set = R_GetBufferSetForModel (e->model);
	glDrawElements (GL_TRIANGLES, set->numindexes, GL_UNSIGNED_SHORT, (void *) (intptr_t) 0);
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

	int posenum = hdr->frames[frame].firstpose;
	int numposes = hdr->frames[frame].numposes;

	if (numposes > 1)
	{
		// get the correct group frame
		int groupframe = Mod_GetAutoAnimation (hdr->frames[frame].intervals, numposes, e->syncbase);

		// get the correct interval
		if (groupframe == 0)
			e->animlerp.interval = hdr->frames[frame].intervals[groupframe];
		else e->animlerp.interval = hdr->frames[frame].intervals[groupframe] - hdr->frames[frame].intervals[groupframe - 1];

		// advance to this frame
		posenum += groupframe;
	}
	else e->animlerp.interval = 0.1;

	if (e->lerpflags & LERP_RESETANIM) // kill any lerp in progress
	{
		e->animlerp.starttime = 0;
		e->animlerp.previouspose = posenum;
		e->animlerp.currentpose = posenum;
		e->lerpflags &= ~LERP_RESETANIM;
	}
	else if (e->animlerp.currentpose != posenum) // pose changed, start new lerp
	{
		if (e->lerpflags & LERP_RESETANIM2) // defer lerping one more time
		{
			e->animlerp.starttime = 0;
			e->animlerp.previouspose = posenum;
			e->animlerp.currentpose = posenum;
			e->lerpflags &= ~LERP_RESETANIM2;
		}
		else
		{
			e->animlerp.starttime = cl.time;
			e->animlerp.previouspose = e->animlerp.currentpose;
			e->animlerp.currentpose = posenum;
		}
	}

	// set up values
	if (r_lerpmodels.value && !((e->model->flags & MOD_NOLERP) && r_lerpmodels.value != 2))
	{
		if ((e->lerpflags & LERP_FINISH) && numposes == 1)
			lerpdata->blend = Q_fclamp ((cl.time - e->animlerp.starttime) / (e->lerpfinishtime - e->animlerp.starttime), 0, 1);
		else lerpdata->blend = Q_fclamp ((cl.time - e->animlerp.starttime) / e->animlerp.interval, 0, 1);

		lerpdata->pose1 = e->animlerp.previouspose;
		lerpdata->pose2 = e->animlerp.currentpose;
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
		e->movelerp.starttime = 0;
		VectorCopy (e->origin, e->movelerp.previousorigin);
		VectorCopy (e->origin, e->movelerp.currentorigin);
		VectorCopy (e->angles, e->movelerp.previousangles);
		VectorCopy (e->angles, e->movelerp.currentangles);
		e->lerpflags &= ~LERP_RESETMOVE;
	}
	else if (!VectorCompare (e->origin, e->movelerp.currentorigin) || !VectorCompare (e->angles, e->movelerp.currentangles)) // origin/angles changed, start new lerp
	{
		e->movelerp.starttime = cl.time;
		VectorCopy (e->movelerp.currentorigin, e->movelerp.previousorigin);
		VectorCopy (e->origin, e->movelerp.currentorigin);
		VectorCopy (e->movelerp.currentangles, e->movelerp.previousangles);
		VectorCopy (e->angles, e->movelerp.currentangles);
	}

	// set up values
	if (r_lerpmove.value && e != &cl.viewent && (e->lerpflags & LERP_MOVESTEP))
	{
		if (e->lerpflags & LERP_FINISH)
			blend = Q_fclamp ((cl.time - e->movelerp.starttime) / (e->lerpfinishtime - e->movelerp.starttime), 0, 1);
		else blend = Q_fclamp ((cl.time - e->movelerp.starttime) / 0.1, 0, 1);

		// translation
		VectorSubtract (e->movelerp.currentorigin, e->movelerp.previousorigin, d);
		lerpdata->origin[0] = e->movelerp.previousorigin[0] + d[0] * blend;
		lerpdata->origin[1] = e->movelerp.previousorigin[1] + d[1] * blend;
		lerpdata->origin[2] = e->movelerp.previousorigin[2] + d[2] * blend;

		// rotation
		VectorSubtract (e->movelerp.currentangles, e->movelerp.previousangles, d);

		for (i = 0; i < 3; i++)
		{
			if (d[i] > 180)  d[i] -= 360;
			if (d[i] < -180) d[i] += 360;
		}

		lerpdata->angles[0] = e->movelerp.previousangles[0] + d[0] * blend;
		lerpdata->angles[1] = e->movelerp.previousangles[1] + d[1] * blend;
		lerpdata->angles[2] = e->movelerp.previousangles[2] + d[2] * blend;
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
	aliasskingroup_t *group = hdr->skingroups + (e->skinnum < 0 ? 0 : (e->skinnum >= hdr->numskingroups ? 0 : e->skinnum));
	return &group->skins[Mod_GetAutoAnimation (group->intervals, group->numskins, e->syncbase)];
}


void R_AliasModelBBox (entity_t *e, QMATRIX *localMatrix, qboolean rotated, float *bbmins, float *bbmaxs)
{
	// per modelgen.c, alias bounds are 0...255 which are then scaled and offset by header->scale and header->scale_origin
	aliashdr_t *hdr = (aliashdr_t *) Mod_Extradata (e->model);
	vec3_t amins, amaxs;

	// reconstruct the bbox
	for (int i = 0; i < 3; i++)
	{
		amins[i] = hdr->scale_origin[i];
		amaxs[i] = amins[i] + hdr->scale[i] * 255;
	}

	if (rotated)
	{
		// and rotate it
		R_RotateBBox (localMatrix, amins, amaxs, bbmins, bbmaxs);
	}
	else
	{
		// fast case - we can't use e->origin because it might be lerped, so the actual origin used for the transform is in m4x4[3]
		Vector3Add (bbmins, localMatrix->m4x4[3], amins);
		Vector3Add (bbmaxs, localMatrix->m4x4[3], amaxs);
	}
}


gltexture_t *R_GetPlayerTexture (entity_t *e, aliashdr_t *hdr, aliasskin_t *baseskin)
{
	// implement gl_nocolors - just return the base skin, force the translation to rebuild next time it's needed
	if (gl_nocolors.value)
	{
		e->translation.baseskin = NULL;
		return baseskin->gltexture;
	}

	// new texture, skin change or model change (skins are specific to models so if the model changes the skin always will too)
	if (!e->translation.translated || baseskin != e->translation.baseskin)
	{
		// load or reload it - locate the source pixels
		// this will handle general-case stuff like animating player skins, although that would require constantly reloading the skin and would be quite slow; it would work, though...
		byte *pixels = baseskin->texels;
		int loadflags = TEXPREF_MIPMAP | TEXPREF_FLOODFILL | TEXPREF_PAD | TEXPREF_OVERWRITE;
		gltexture_t *source = baseskin->gltexture;
		char name[64];

		// upload new image
		q_snprintf (name, sizeof (name), "translation_%i", e->entitynum); // giving it a more unique name...
		e->translation.translated = TexMgr_LoadImage (e->model, name, hdr->skinwidth, hdr->skinheight, SRC_INDEXED, pixels, source->source_file, source->source_offset, loadflags);

		// store back skin
		e->translation.baseskin = baseskin;

		// set these to the values that would give a translated skin the same as the original
		e->translation.shirt = 1;
		e->translation.pants = 6;
	}

	// get the requested colours
	int shirt = (cl.scores[e->playernum].colors & 0xf0) >> 4;
	int pants = cl.scores[e->playernum].colors & 15;

	// now check for colour change on either a new player texture or an existing one
	if (shirt != e->translation.shirt || pants != e->translation.pants)
	{
		// run the colour change
		TexMgr_ReloadImage (e->translation.translated, shirt, pants);

		// store back colour
		e->translation.shirt = shirt;
		e->translation.pants = pants;
	}

	// return the texture
	return e->translation.translated;
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
	R_TransformEntityToLocalMatrix (&localMatrix, lerpdata.origin, lerpdata.angles, mod_alias);

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

	if (e->colormapped)
		tx = R_GetPlayerTexture (e, hdr, skin);

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
	else if (!cl.worldmodel->lightdata || r_fullbright_cheatsafe || r_drawflat_cheatsafe)
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


