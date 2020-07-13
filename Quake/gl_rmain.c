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
// r_main.c

#include "quakedef.h"

vec3_t		modelorg, r_entorigin;

int			r_visframecount;	// bumped when going to a new PVS
int			r_framecount;		// used for dlight push checking

mplane_t	frustum[4];

// johnfitz -- rendering statistics
int rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys;
int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;
float rs_megatexels;

// view origin (expanded to 4 component for shader uniforms)
float	vup[4];
float	vpn[4];
float	vright[4];
float	r_origin[4];

// screen size info
refdef_t	r_refdef;

mleaf_t *r_viewleaf, *r_oldviewleaf;

float	d_lightstylevalue[256];	// 8.8 fraction of base light value


// MH - reverted a lot of these to Quake defaults
cvar_t	r_norefresh = { "r_norefresh", "0", CVAR_NONE };
cvar_t	r_drawentities = { "r_drawentities", "1", CVAR_NONE };
cvar_t	r_drawviewmodel = { "r_drawviewmodel", "1", CVAR_NONE };
cvar_t	r_speeds = { "r_speeds", "0", CVAR_NONE };
cvar_t	r_pos = { "r_pos", "0", CVAR_NONE };
cvar_t	r_wateralpha = { "r_wateralpha", "1", CVAR_NONE };
cvar_t	r_dynamic = { "r_dynamic", "1", CVAR_NONE };

cvar_t	r_novis = { "r_novis", "0", CVAR_NONE };
cvar_t	r_lockpvs = { "r_lockpvs", "0", CVAR_NONE };

cvar_t	gl_finish = { "gl_finish", "0", CVAR_NONE };
cvar_t	gl_clear = { "gl_clear", "0", CVAR_NONE };
cvar_t	gl_cull = { "gl_cull", "1", CVAR_NONE };
cvar_t	gl_polyblend = { "gl_polyblend", "1", CVAR_NONE };
cvar_t	gl_nocolors = { "gl_nocolors", "0", CVAR_NONE };

cvar_t	r_shadows = { "r_shadows", "0", CVAR_NONE };

// johnfitz -- new cvars
cvar_t	r_clearcolor = { "r_clearcolor", "2", CVAR_NONE };
cvar_t	r_flatlightstyles = { "r_flatlightstyles", "0", CVAR_NONE };
cvar_t	gl_fullbrights = { "gl_fullbrights", "1", CVAR_NONE };
cvar_t	gl_overbright = { "gl_overbright", "1", CVAR_NONE };
cvar_t	r_lerpmodels = { "r_lerpmodels", "1", CVAR_NONE };
cvar_t	r_lerpmove = { "r_lerpmove", "1", CVAR_NONE };
cvar_t	r_nolerp_list = { "r_nolerp_list", "progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/v_saw.mdl,progs/v_xfist.mdl,progs/h2stuff/newfire.mdl", CVAR_NONE };
cvar_t	r_noshadow_list = { "r_noshadow_list", "progs/flame2.mdl,progs/flame.mdl,progs/bolt1.mdl,progs/bolt2.mdl,progs/bolt3.mdl,progs/laser.mdl", CVAR_NONE };
// johnfitz

// mapper crap
cvar_t	r_fullbright = { "r_fullbright", "0", CVAR_NONE };
cvar_t	r_lightmap = { "r_lightmap", "0", CVAR_NONE };
cvar_t	r_drawflat = { "r_drawflat", "0", CVAR_NONE };
cvar_t	r_showtris = { "r_showtris", "0", CVAR_NONE };
cvar_t	r_showbboxes = { "r_showbboxes", "0", CVAR_NONE };
qboolean r_drawflat_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe; //johnfitz

cvar_t	r_lavaalpha = { "r_lavaalpha", "0", CVAR_NONE };
cvar_t	r_telealpha = { "r_telealpha", "0", CVAR_NONE };
cvar_t	r_slimealpha = { "r_slimealpha", "0", CVAR_NONE };

float	map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha;
qboolean r_dowarp = false;

cvar_t	r_scale = { "r_scale", "1", CVAR_ARCHIVE };


GLuint r_polyblend_vp = 0;
GLuint r_polyblend_fp = 0;

GLuint r_scaleview_vp = 0;
GLuint r_scaleview_fp = 0;

GLuint r_wirepoint_vp = 0;
GLuint r_wirebox_vp = 0;
GLuint r_bbox_fp = 0;


/*
=============
GLMain_CreateShaders
=============
*/
void GLMain_CreateShaders (void)
{
	const GLchar *vp_polyblend_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# copy over position\n"
		"MOV result.position, vertex.attrib[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_polyblend_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff;\n"
		"\n"
		"# apply the contrast\n"
		"MUL diff, program.local[0], program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW result.color.r, diff.r, program.env[10].y;\n"
		"POW result.color.g, diff.g, program.env[10].y;\n"
		"POW result.color.b, diff.b, program.env[10].y;\n"
		"MOV result.color.a, diff.a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *vp_scaleview_source = \
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
	const GLchar *fp_scaleview_source = \
		"!!ARBfp1.0\n"
		"\n"
		"# perform the texturing direct to output\n"
		"TEX result.color, fragment.texcoord[0], texture[5], RECT;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *vp_wirepoint_source = \
		"!!ARBvp1.0\n"
		"\n"
		"TEMP position;\n"
		"ADD position, vertex.attrib[0], program.local[0];\n"
		"\n"
		"# transform position to output\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], position;\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], position;\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], position;\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], position;\n"
		"\n"
		"# move colour to output\n"
		"MOV result.color, {1.0, 1.0, 0.0, 1.0};\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *vp_wirebox_source = \
		"!!ARBvp1.0\n"
		"\n"
		"TEMP position;\n"
		"\n"
		"# interpolate the position\n"
		"SUB position, program.local[2], program.local[1];\n"
		"MAD position, vertex.attrib[0], position, program.local[1];\n"
		"ADD position, position, program.local[0];\n"
		"MOV position.w, 1.0; # ensure\n"
		"\n"
		"# transform position to output\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], position;\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], position;\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], position;\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], position;\n"
		"\n"
		"# move colour to output\n"
		"MOV result.color, {1.0, 0.0, 1.0, 1.0};\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	// each channel is full-intensity or zero, so no point in applying gamma or contrast
	const GLchar *fp_bbox_source = \
		"!!ARBfp1.0\n"
		"MOV result.color, fragment.color;\n"
		"END\n"
		"\n";

	r_polyblend_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_polyblend_source);
	r_polyblend_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_polyblend_source);

	r_scaleview_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_scaleview_source);
	r_scaleview_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_scaleview_source);

	r_wirepoint_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_wirepoint_source);
	r_wirebox_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_wirebox_source);
	r_bbox_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_bbox_source);
}


/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	// https://github.com/Novum/vkQuake/blob/master/Quake/gl_rmain.c#L120
	for (int i = 0; i < 4; i++)
	{
		mplane_t *p = frustum + i;
		byte signbits = p->signbits;

		float vec[3] = {
			((signbits % 2) < 1) ? emaxs[0] : emins[0],
			((signbits % 4) < 2) ? emaxs[1] : emins[1],
			((signbits % 8) < 4) ? emaxs[2] : emins[2]
		};

		if (p->normal[0] * vec[0] + p->normal[1] * vec[1] + p->normal[2] * vec[2] < p->dist)
			return true;
	}

	return false;
}


/*
=================
R_RotateBBox

Rotate a bbox into an entities frame of reference and recomputes a new aa bbox from the rotated bbox
=================
*/
void R_RotateBBox (QMATRIX *matrix, const float *inmins, const float *inmaxs, float *outmins, float *outmaxs)
{
	Vector3Set (outmins, 999999, 999999, 999999);
	Vector3Set (outmaxs, -999999, -999999, -999999);

	// compute a full box
	for (int i = 0; i < 8; i++)
	{
		float point[3], transformed[3];

		// get the corner point
		point[0] = (i & 1) ? inmins[0] : inmaxs[0];
		point[1] = (i & 2) ? inmins[1] : inmaxs[1];
		point[2] = (i & 4) ? inmins[2] : inmaxs[2];

		// transform it
		R_Transform (matrix, transformed, point);

		// accumulate to bbox
		for (int j = 0; j < 3; j++)
		{
			if (transformed[j] < outmins[j]) outmins[j] = transformed[j];
			if (transformed[j] > outmaxs[j]) outmaxs[j] = transformed[j];
		}
	}
}


/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity (entity_t *e, QMATRIX *localMatrix, qboolean rotated)
{
	vec3_t bbmins, bbmaxs;

	if (e == &cl.viewent)
	{
		// never cull the view entity
		return false;
	}
	else if (e->model->type == mod_alias)
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
	else if (e->model->type == mod_brush)
	{
		if (rotated)
		{
			// straightforward bbox rotation
			R_RotateBBox (localMatrix, e->model->mins, e->model->maxs, bbmins, bbmaxs);
		}
		else
		{
			// fast case
			Vector3Add (bbmins, e->origin, e->model->mins);
			Vector3Add (bbmaxs, e->origin, e->model->maxs);
		}
	}
	else
	{
		// always cull unknown model types
		return true;
	}

	// now do the cull test correctly on the rotated bbox
	return R_CullBox (bbmins, bbmaxs);
}


/*
=============
GL_PolygonOffset -- johnfitz

negative offset moves polygon closer to camera
=============
*/
void GL_PolygonOffset (int offset)
{
	if (offset > 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glEnable (GL_POLYGON_OFFSET_LINE);
		glPolygonOffset (1, offset);
	}
	else if (offset < 0)
	{
		glEnable (GL_POLYGON_OFFSET_FILL);
		glEnable (GL_POLYGON_OFFSET_LINE);
		glPolygonOffset (-1, offset);
	}
	else
	{
		glDisable (GL_POLYGON_OFFSET_FILL);
		glDisable (GL_POLYGON_OFFSET_LINE);
	}
}


// ==============================================================================
// SETUP FRAME
// ==============================================================================

int SignbitsForPlane (mplane_t *out)
{
	// for fast box on planeside test
	int bits = 0;

	for (int j = 0; j < 3; j++)
		if (out->normal[j] < 0)
			bits |= 1 << j;

	return bits;
}


/*
=============
GL_SetFrustum -- johnfitz -- written to replace MYgluPerspective
=============
*/
void R_ExtractFrustum (QMATRIX *mvp)
{
	// extract the frustum from the MVP matrix
	frustum[0].normal[0] = mvp->m4x4[0][3] - mvp->m4x4[0][0];
	frustum[0].normal[1] = mvp->m4x4[1][3] - mvp->m4x4[1][0];
	frustum[0].normal[2] = mvp->m4x4[2][3] - mvp->m4x4[2][0];

	frustum[1].normal[0] = mvp->m4x4[0][3] + mvp->m4x4[0][0];
	frustum[1].normal[1] = mvp->m4x4[1][3] + mvp->m4x4[1][0];
	frustum[1].normal[2] = mvp->m4x4[2][3] + mvp->m4x4[2][0];

	frustum[2].normal[0] = mvp->m4x4[0][3] + mvp->m4x4[0][1];
	frustum[2].normal[1] = mvp->m4x4[1][3] + mvp->m4x4[1][1];
	frustum[2].normal[2] = mvp->m4x4[2][3] + mvp->m4x4[2][1];

	frustum[3].normal[0] = mvp->m4x4[0][3] - mvp->m4x4[0][1];
	frustum[3].normal[1] = mvp->m4x4[1][3] - mvp->m4x4[1][1];
	frustum[3].normal[2] = mvp->m4x4[2][3] - mvp->m4x4[2][1];

	for (int i = 0; i < 4; i++)
	{
		VectorNormalize (frustum[i].normal);
		frustum[i].dist = Vector3Dot (r_refdef.vieworg, frustum[i].normal);
		frustum[i].type = PLANE_ANYZ;
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}


float R_GetFarClip (void)
{
	// don't go below the standard Quake farclip
	float farclip = 4096.0f;

	if (cl.worldmodel)
	{
		// this provides the maximum far clip per view position and worldmodel bounds
		for (int i = 0; i < 8; i++)
		{
			float dist;
			vec3_t corner;

			// get this corner point
			if (i & 1) corner[0] = cl.worldmodel->mins[0]; else corner[0] = cl.worldmodel->maxs[0];
			if (i & 2) corner[1] = cl.worldmodel->mins[1]; else corner[1] = cl.worldmodel->maxs[1];
			if (i & 4) corner[2] = cl.worldmodel->mins[2]; else corner[2] = cl.worldmodel->maxs[2];

			if ((dist = Vector3Dist (r_refdef.vieworg, corner)) > farclip)
				farclip = dist;
		}
	}

	return farclip;
}


/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	QMATRIX view;
	QMATRIX proj;
	QMATRIX mvp;

	// johnfitz -- rewrote this section
	// mh - allow fractional scales
	float scale = Q_fclamp (r_scale.value, 1, 4); // ericw -- see R_ScaleView

	glViewport (
		glx + r_refdef.vrect.x,
		gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height,
		r_refdef.vrect.width / scale,
		r_refdef.vrect.height / scale
	);
	// johnfitz

	R_IdentityMatrix (&proj);
	R_FrustumMatrix (&proj, r_refdef.fov_x, r_refdef.fov_y, 4.0f, R_GetFarClip ());

	R_IdentityMatrix (&view);
	R_CameraMatrix (&view, r_refdef.vieworg, r_refdef.viewangles);

	// take these from the view matrix
	Vector3Set (vpn, -view.m4x4[0][2], -view.m4x4[1][2], -view.m4x4[2][2]);
	Vector3Set (vup, view.m4x4[0][1], view.m4x4[1][1], view.m4x4[2][1]);
	Vector3Set (vright, view.m4x4[0][0], view.m4x4[1][0], view.m4x4[2][0]);

	glMatrixMode (GL_PROJECTION);
	glLoadMatrixf (proj.m16);

	glMatrixMode (GL_MODELVIEW);
	glLoadMatrixf (view.m16);

	// derive a full MVP
	R_MultMatrix (&mvp, &view, &proj);

	// and extract the frustum from it
	R_ExtractFrustum (&mvp);

	// set drawing parms
	if (gl_cull.value)
		glEnable (GL_CULL_FACE);
	else
		glDisable (GL_CULL_FACE);

	// set up shader constants
	Sky_SetShaderConstants ();
	Warp_SetShaderConstants ();
}


/*
=============
R_Clear -- johnfitz -- rewritten and gutted
=============
*/
void R_Clear (void)
{
	unsigned int clearbits = GL_DEPTH_BUFFER_BIT;

	// from mh -- if we get a stencil buffer, we should clear it, even though we don't use it
	if (gl_stencilbits) clearbits |= GL_STENCIL_BUFFER_BIT;
	if (gl_clear.value) clearbits |= GL_COLOR_BUFFER_BIT;
	if (r_viewleaf->contents == CONTENTS_SOLID) clearbits |= GL_COLOR_BUFFER_BIT; // clear if in solid so we don't HOM when noclipping

	// in GL clears are affected by the current depth write mask, so set up a default depth state including write-enable so that the clear will work
	// the next depth state set will most likely be for world poly drawing, which is the same as this state, so we won't have redundant sets here
	GL_DepthState (GL_TRUE, GL_LEQUAL, GL_TRUE);

	glClear (clearbits);
}


/*
===============
R_SetupView -- johnfitz -- this is the stuff that needs to be done once per frame, even in stereo mode
===============
*/
void R_SetupView (void)
{
	// going to a new frame
	r_framecount++;

	Fog_SetupFrame (); // johnfitz

	// build the transformation matrix for the given view angles
	// note: vpn, vup, vright are now derived from the view matrix rather than calced separately
	VectorCopy (r_refdef.vieworg, r_origin);

	// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	// allow them to scale the overall blend with gl_polyblend as well
	v_blend[3] *= gl_polyblend.value;

	// same test as software Quake
	r_dowarp = r_waterwarp.value && (r_viewleaf->contents <= CONTENTS_WATER);

	// johnfitz -- cheat-protect some draw modes
	// mh - this seems a bit bogus as it's client-side so a determined cheater could just remove them and recompile the source code
	r_drawflat_cheatsafe = r_fullbright_cheatsafe = r_lightmap_cheatsafe = false;

	if (cl.maxclients == 1)
	{
		if (r_drawflat.value) r_drawflat_cheatsafe = true;
		else if (r_fullbright.value) r_fullbright_cheatsafe = true;
		else if (r_lightmap.value) r_lightmap_cheatsafe = true;
	}
	// johnfitz
}


// ==============================================================================
// RENDER VIEW
// ==============================================================================

/*
=============
R_DrawEntitiesOnList
=============
*/
void R_DrawEntitiesOnList (qboolean alphapass) // johnfitz -- added parameter
{
	if (!r_drawentities.value)
		return;

	// johnfitz -- sprites are not a special case
	for (int i = 0; i < cl_numvisedicts; i++)
	{
		entity_t *e = cl_visedicts[i];

		// johnfitz -- if alphapass is true, draw only alpha entites this time
		// if alphapass is false, draw only nonalpha entities this time
		if ((ENTALPHA_DECODE (e->alpha) < 1 && !alphapass) || (ENTALPHA_DECODE (e->alpha) == 1 && alphapass))
			continue;

		// johnfitz -- chasecam
		if (e == cl_entities[cl.viewentity])
			e->angles[0] *= 0.3;
		// johnfitz

		switch (e->model->type)
		{
		case mod_alias:
			R_DrawAliasModel (e);
			break;

		case mod_brush:
			R_DrawBrushModel (e);
			break;

		case mod_sprite:
			R_DrawSpriteModel (e);
			break;
		}
	}
}


/*
=============
R_DrawViewModel -- johnfitz -- gutted
=============
*/
void R_DrawViewModel (void)
{
	void SCR_SetFOV (refdef_t * rd, int fovvar, int width, int height);
	extern cvar_t scr_fov;
	entity_t *e = &cl.viewent;

	if (!r_drawviewmodel.value || !r_drawentities.value || chase_active.value)
		return;

	if (cl.stats[STAT_HEALTH] <= 0)
		return;

	if (scr_timerefresh)
		return;

	if (!e->model)
		return;

	// johnfitz -- this fixes a crash
	if (e->model->type != mod_alias)
		return;
	// johnfitz

	// interacts with SU_WEAPONALPHA bit in CL_ParseClientdata; this is overwritten/reset each frame so we don't need to cache & restore it
	if (e->alpha != ENTALPHA_DEFAULT)
		; // server has sent alpha for the viewmodel so never override it
	else if (cl.items & IT_INVISIBILITY)
		e->alpha = ENTALPHA_ENCODE (0.25);
	else e->alpha = ENTALPHA_DEFAULT;

	// hack the depth range to prevent view model from poking into walls
	glDepthRange (0, 0.3);

	if (scr_fov.value > 90)
	{
		// the gun model with fov > 90 looks like we're playing wipeout, so draw it at fov 90 if it goes above
		QMATRIX gunProjection;
		refdef_t r_gunrefdef;

		// create a new refdef for the gun based on the main refdef width and height but with FOV 90
		SCR_SetFOV (&r_gunrefdef, 90, r_refdef.vrect.width, r_refdef.vrect.height);

		// and it's projection matrix - standard Quake farclip is OK for this
		R_IdentityMatrix (&gunProjection);
		R_FrustumMatrix (&gunProjection, r_gunrefdef.fov_x, r_gunrefdef.fov_y, 4.0f, 4096.0f);

		// load it on - we could probably go without the push/pop if the gun was rendered absolutely last in the 3D view
		// the projection matrix stack is guaranteed by the GL spec to be at least 2 deep so this is OK
		glMatrixMode (GL_PROJECTION);
		glPushMatrix ();
		glLoadMatrixf (gunProjection.m16);
		glMatrixMode (GL_MODELVIEW);

		// now draw it; we don't need to re-eval the frustum because we don't cull the gun model anyway
		R_DrawAliasModel (e);

		// restore the projection matrix
		glMatrixMode (GL_PROJECTION);
		glPopMatrix ();
		glMatrixMode (GL_MODELVIEW);
	}
	else R_DrawAliasModel (e);

	glDepthRange (0, 1);
}


/*
================
R_EmitWirePoint -- johnfitz -- draws a wireframe cross shape for point entities
================
*/
void R_EmitWirePoint (vec3_t origin)
{
	GL_BindPrograms (r_wirepoint_vp, r_bbox_fp);
	glProgramLocalParameter4fARB (GL_VERTEX_PROGRAM_ARB, 0, origin[0], origin[1], origin[2], 0);
	glDrawArrays (GL_LINES, 0, 6);
}


/*
================
R_EmitWireBox -- johnfitz -- draws one axis aligned bounding box
================
*/
void R_EmitWireBox (vec3_t origin, vec3_t mins, vec3_t maxs)
{
	GL_BindPrograms (r_wirebox_vp, r_bbox_fp);
	glProgramLocalParameter4fARB (GL_VERTEX_PROGRAM_ARB, 0, origin[0], origin[1], origin[2], 0);
	glProgramLocalParameter4fARB (GL_VERTEX_PROGRAM_ARB, 1, mins[0], mins[1], mins[2], 0);
	glProgramLocalParameter4fARB (GL_VERTEX_PROGRAM_ARB, 2, maxs[0], maxs[1], maxs[2], 0);
	glDrawArrays (GL_QUAD_STRIP, 6, 10);
}


/*
================
R_ShowBoundingBoxes -- johnfitz

draw bounding boxes -- the server-side boxes, not the renderer cullboxes
================
*/
void R_ShowBoundingBoxes (void)
{
	if (!r_showbboxes.value || cl.maxclients > 1 || !r_drawentities.value || !sv.active)
		return;

	float bbverts[16][3] = {
		{-8, 0, 0}, {8, 0, 0}, {0, -8, 0}, {0, 8, 0}, {0, 0, -8}, {0, 0, 8}, // wire-point verts at origin
		{0, 0, 0}, {0, 0, 1}, {1, 0, 0}, {1, 0, 1}, {1, 1, 0}, {1, 1, 1}, {0, 1, 0}, {0, 1, 1}, {0, 0, 0}, {0, 0, 1} // wire-box, 0 = mins, 1 = maxs
	};

	GL_BindBuffer (GL_ARRAY_BUFFER, 0);
	GL_EnableVertexAttribArrays (VAA0);

	glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 0, bbverts);

	GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);

	// this is only used here so no point in doing a state change filter for it
	// if we're drawing with depth test disabled, what's the point of using polygon offset????
	glPolygonMode (GL_FRONT_AND_BACK, GL_LINE);
	GL_PolygonOffset (OFFSET_SHOWTRIS);
	glDisable (GL_CULL_FACE);

	// start on the edict after the world
	for (int i = 1; i < sv.num_edicts; i++)
	{
		edict_t *ed = EDICT_NUM (i);
		extern edict_t *sv_player;

		if (ed == sv_player)
			continue; // don't draw player's own bbox

		if (ed->v.mins[0] == ed->v.maxs[0] && ed->v.mins[1] == ed->v.maxs[1] && ed->v.mins[2] == ed->v.maxs[2])
		{
			// point entity
			R_EmitWirePoint (ed->v.origin);
		}
		else
		{
			// box entity
			R_EmitWireBox (ed->v.origin, ed->v.mins, ed->v.maxs);
		}
	}

	// this is only used here so no point in doing a state change filter for it
	glEnable (GL_CULL_FACE);
	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	GL_PolygonOffset (OFFSET_NONE);
}


/*
============
R_DrawPolyBlend -- johnfitz -- moved here from gl_rmain.c, and rewritten to use glOrtho

mh - put it back and using no matrices so it doesn't corrupt current matrix state
============
*/
void R_DrawPolyBlend (void)
{
	float verts[] = { -1.0, -1.0, -1.0, 3.0, 3.0, -1.0 };

	if (!gl_polyblend.value || !(v_blend[3] > 0))
		return;

	GL_BindBuffer (GL_ARRAY_BUFFER, 0);
	GL_EnableVertexAttribArrays (VAA0);
	GL_BindPrograms (r_polyblend_vp, r_polyblend_fp);

	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, verts);
	glProgramLocalParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 0, v_blend);

	GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);

	glDrawArrays (GL_TRIANGLES, 0, 3);
}


/*
================
R_ScaleView

The r_scale cvar allows rendering the 3D view at 1/2, 1/3, or 1/4 resolution.
This function scales the reduced resolution 3D view back up to fill
r_refdef.vrect. This is for emulating a low-resolution pixellated look,
or possibly as a perforance boost on slow graphics cards.
================
*/
void R_ScaleView (void)
{
	// copied from R_SetupGL()
	// mh - allow fractional scales
	float scale = Q_fclamp (r_scale.value, 1, 4);
	int srcx = glx + r_refdef.vrect.x;
	int srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	int srcw = r_refdef.vrect.width / scale;
	int srch = r_refdef.vrect.height / scale;

	if (!(scale > 1))
		return;

	// using GL_TEXTURE5 again for binding points that are not GL_TEXTURE_2D
	// bind to default texture object 0
	glActiveTexture (GL_TEXTURE5);
	glBindTexture (GL_TEXTURE_RECTANGLE, 0);

	// we enforced requiring a rectangle texture extension so we don't need to worry abour np2 stuff
	glCopyTexImage2D (GL_TEXTURE_RECTANGLE, 0, GL_RGBA, srcx, srcy, srcw, srch, 0);
	glTexParameteri (GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
	glTexParameteri (GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	glTexParameterf (GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf (GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// clear cached binding
	GL_ClearTextureBindings ();

	// draw the texture back to the framebuffer
	glDisable (GL_CULL_FACE);

	GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);

	GL_BindBuffer (GL_ARRAY_BUFFER, 0);
	GL_EnableVertexAttribArrays (VAA0 | VAA1);
	GL_BindPrograms (r_scaleview_vp, r_scaleview_fp);

	glViewport (srcx, srcy, r_refdef.vrect.width, r_refdef.vrect.height);

	float positions[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
	float texcoords[] = { 0, 0, srcw, 0, srcw, srch, 0, srch };

	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, positions);
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 0, texcoords);

	glDrawArrays (GL_QUADS, 0, 4);
}

/*
================
R_RenderView
================
*/
void R_RenderView (void)
{
	double	time1, time2;

	if (r_norefresh.value)
		return;

	if (!cl.worldmodel)
		Sys_Error ("R_RenderView: NULL worldmodel");

	time1 = 0; /* avoid compiler warning */

	if (scr_timerefresh)
		; // no speed-tracking in timerefresh mode
	else if (r_speeds.value)
	{
		glFinish ();
		time1 = Sys_DoubleTime ();

		// johnfitz -- rendering statistics
		rs_brushpolys = rs_aliaspolys = rs_skypolys = rs_particles = rs_fogpolys = rs_megatexels = rs_dynamiclightmaps = rs_aliaspasses = rs_skypasses = rs_brushpasses = 0;
	}
	else if (gl_finish.value)
		glFinish ();

	R_SetupView ();

	R_Clear ();

	R_AnimateLight (cl.time);

	R_SetupGL ();

	R_MarkLeaves ();	// done here so we know if we're in water

	R_DrawWorld_Old (); // MH - reverting to old-style recursive world node drawing

	S_ExtraUpdate (); // don't let sound get messed up if going slow

	R_DrawEntitiesOnList (false); // johnfitz -- false means this is the pass for nonalpha entities

	R_DrawWorld_Water (); // johnfitz -- drawn here since they might have transparency

	R_DrawEntitiesOnList (true); // johnfitz -- true means this is the pass for alpha entities

	R_DrawParticlesARB ();

	R_DrawViewModel (); // johnfitz -- moved here from R_RenderView

	R_ShowBoundingBoxes ();

	R_ScaleView ();

	if (r_dowarp)
	{
		// mh - run after the scale view as a post-process over the full screen rect
		R_UnderwaterWarp ();
		v_blend[3] = 0; // the blend was merged into the water warp so switch if off here
	}
	else R_DrawPolyBlend (); // MH - put this back here

	// johnfitz -- modified r_speeds output
	time2 = Sys_DoubleTime ();

	if (scr_timerefresh)
		; // no speed-tracking in timerefresh mode
	else if (r_pos.value)
	{
		Con_Printf ("x %i y %i z %i (pitch %i yaw %i roll %i)\n",
			(int) cl_entities[cl.viewentity]->origin[0],
			(int) cl_entities[cl.viewentity]->origin[1],
			(int) cl_entities[cl.viewentity]->origin[2],
			(int) cl.viewangles[PITCH],
			(int) cl.viewangles[YAW],
			(int) cl.viewangles[ROLL]
		);
	}
	else if (r_speeds.value == 2)
	{
		Con_Printf ("%3i ms  %4i/%4i wpoly %4i/%4i epoly %3i lmap %4i/%4i sky %1.1f mtex\n",
			(int) ((time2 - time1) * 1000),
			rs_brushpolys,
			rs_brushpasses,
			rs_aliaspolys,
			rs_aliaspasses,
			rs_dynamiclightmaps,
			rs_skypolys,
			rs_skypasses,
			TexMgr_FrameUsage ()
		);
	}
	else if (r_speeds.value)
	{
		Con_Printf ("%3i ms  %4i wpoly %4i epoly %3i lmap\n",
			(int) ((time2 - time1) * 1000),
			rs_brushpolys,
			rs_aliaspolys,
			rs_dynamiclightmaps
		);
	}
	// johnfitz
}

