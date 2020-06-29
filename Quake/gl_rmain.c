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

qboolean	r_cache_thrash;		// compatability

vec3_t		modelorg, r_entorigin;
entity_t *currententity;

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

float r_fovx, r_fovy; // johnfitz -- rendering fov may be different becuase of r_waterwarp

// screen size info
refdef_t	r_refdef;

mleaf_t *r_viewleaf, *r_oldviewleaf;

int		d_lightstylevalue[256];	// 8.8 fraction of base light value


cvar_t	r_norefresh = { "r_norefresh", "0", CVAR_NONE };
cvar_t	r_drawentities = { "r_drawentities", "1", CVAR_NONE };
cvar_t	r_drawviewmodel = { "r_drawviewmodel", "1", CVAR_NONE };
cvar_t	r_speeds = { "r_speeds", "0", CVAR_NONE };
cvar_t	r_pos = { "r_pos", "0", CVAR_NONE };
cvar_t	r_wateralpha = { "r_wateralpha", "1", CVAR_ARCHIVE };
cvar_t	r_dynamic = { "r_dynamic", "1", CVAR_ARCHIVE };
cvar_t	r_fullbright = { "r_fullbright", "0", CVAR_NONE };
cvar_t	r_novis = { "r_novis", "0", CVAR_ARCHIVE };

cvar_t	gl_finish = { "gl_finish", "0", CVAR_NONE };
cvar_t	gl_clear = { "gl_clear", "0", CVAR_NONE }; // MH - reverted Quake default
cvar_t	gl_cull = { "gl_cull", "1", CVAR_NONE };
cvar_t	gl_polyblend = { "gl_polyblend", "1", CVAR_NONE };
cvar_t	gl_nocolors = { "gl_nocolors", "0", CVAR_NONE };

// johnfitz -- new cvars
cvar_t	r_clearcolor = { "r_clearcolor", "2", CVAR_ARCHIVE };
cvar_t	r_flatlightstyles = { "r_flatlightstyles", "0", CVAR_NONE };
cvar_t	gl_fullbrights = { "gl_fullbrights", "1", CVAR_ARCHIVE };
cvar_t	gl_farclip = { "gl_farclip", "16384", CVAR_ARCHIVE };
cvar_t	gl_overbright = { "gl_overbright", "1", CVAR_ARCHIVE };
cvar_t	r_lerpmodels = { "r_lerpmodels", "1", CVAR_NONE };
cvar_t	r_lerpmove = { "r_lerpmove", "1", CVAR_NONE };
cvar_t	r_nolerp_list = { "r_nolerp_list", "progs/flame.mdl,progs/flame2.mdl,progs/braztall.mdl,progs/brazshrt.mdl,progs/longtrch.mdl,progs/flame_pyre.mdl,progs/v_saw.mdl,progs/v_xfist.mdl,progs/h2stuff/newfire.mdl", CVAR_NONE };

extern cvar_t	r_vfog;
// johnfitz

cvar_t	r_lavaalpha = { "r_lavaalpha", "0", CVAR_NONE };
cvar_t	r_telealpha = { "r_telealpha", "0", CVAR_NONE };
cvar_t	r_slimealpha = { "r_slimealpha", "0", CVAR_NONE };

float	map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha;

cvar_t	r_scale = { "r_scale", "1", CVAR_ARCHIVE };


GLuint r_polyblend_vp = 0;
GLuint r_polyblend_fp = 0;

GLuint r_scaleview_vp = 0;
GLuint r_scaleview_fp = 0;


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
		"TEX result.color, fragment.texcoord[0], texture[0], 2D;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_polyblend_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_polyblend_source);
	r_polyblend_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_polyblend_source);

	r_scaleview_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_scaleview_source);
	r_scaleview_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_scaleview_source);
}


/*
=================
R_CullBox -- johnfitz -- replaced with new function from lordhavoc

Returns true if the box is completely outside the frustum
=================
*/
qboolean R_CullBox (vec3_t emins, vec3_t emaxs)
{
	int i;
	mplane_t *p;
	for (i = 0; i < 4; i++)
	{
		p = frustum + i;
		switch (p->signbits)
		{
		default:
		case 0:
			if (p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2] < p->dist)
				return true;
			break;
		case 1:
			if (p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2] < p->dist)
				return true;
			break;
		case 2:
			if (p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2] < p->dist)
				return true;
			break;
		case 3:
			if (p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2] < p->dist)
				return true;
			break;
		case 4:
			if (p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2] < p->dist)
				return true;
			break;
		case 5:
			if (p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2] < p->dist)
				return true;
			break;
		case 6:
			if (p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2] < p->dist)
				return true;
			break;
		case 7:
			if (p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2] < p->dist)
				return true;
			break;
		}
	}
	return false;
}
/*
===============
R_CullModelForEntity -- johnfitz -- uses correct bounds based on rotation
===============
*/
qboolean R_CullModelForEntity (entity_t *e)
{
	vec3_t mins, maxs;

	if (e->angles[0] || e->angles[2]) // pitch or roll
	{
		VectorAdd (e->origin, e->model->rmins, mins);
		VectorAdd (e->origin, e->model->rmaxs, maxs);
	}
	else if (e->angles[1]) // yaw
	{
		VectorAdd (e->origin, e->model->ymins, mins);
		VectorAdd (e->origin, e->model->ymaxs, maxs);
	}
	else // no rotation
	{
		VectorAdd (e->origin, e->model->mins, mins);
		VectorAdd (e->origin, e->model->maxs, maxs);
	}

	return R_CullBox (mins, maxs);
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
	int	bits, j;

	// for fast box on planeside test

	bits = 0;
	for (j = 0; j < 3; j++)
	{
		if (out->normal[j] < 0)
			bits |= 1 << j;
	}
	return bits;
}

/*
===============
TurnVector -- johnfitz

turn forward towards side on the plane defined by forward and side
if angle = 90, the result will be equal to side
assumes side and forward are perpendicular, and normalized
to turn away from side, use a negative angle
===============
*/
#define DEG2RAD( a ) ( (a) * M_PI_DIV_180 )
void TurnVector (vec3_t out, const vec3_t forward, const vec3_t side, float angle)
{
	float scale_forward, scale_side;

	scale_forward = cos (DEG2RAD (angle));
	scale_side = sin (DEG2RAD (angle));

	out[0] = scale_forward * forward[0] + scale_side * side[0];
	out[1] = scale_forward * forward[1] + scale_side * side[1];
	out[2] = scale_forward * forward[2] + scale_side * side[2];
}

/*
===============
R_SetFrustum -- johnfitz -- rewritten
===============
*/
void R_SetFrustum (float fovx, float fovy)
{
	TurnVector (frustum[0].normal, vpn, vright, fovx / 2 - 90); // left plane
	TurnVector (frustum[1].normal, vpn, vright, 90 - fovx / 2); // right plane
	TurnVector (frustum[2].normal, vpn, vup, 90 - fovy / 2); // bottom plane
	TurnVector (frustum[3].normal, vpn, vup, fovy / 2 - 90); // top plane

	for (int i = 0; i < 4; i++)
	{
		frustum[i].type = PLANE_ANYZ;
		frustum[i].dist = DotProduct (r_origin, frustum[i].normal); // FIXME: shouldn't this always be zero?
		frustum[i].signbits = SignbitsForPlane (&frustum[i]);
	}
}

/*
=============
GL_SetFrustum -- johnfitz -- written to replace MYgluPerspective
=============
*/
#define NEARCLIP 4

void GL_SetFrustum (float fovx, float fovy)
{
	float xmax = NEARCLIP * tan (fovx * M_PI / 360.0);
	float ymax = NEARCLIP * tan (fovy * M_PI / 360.0);
	glFrustum (-xmax, xmax, -ymax, ymax, NEARCLIP, gl_farclip.value);
}


/*
=============
R_SetupGL
=============
*/
void R_SetupGL (void)
{
	int scale;

	// johnfitz -- rewrote this section
	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();
	scale = CLAMP (1, (int) r_scale.value, 4); // ericw -- see R_ScaleView
	glViewport (glx + r_refdef.vrect.x,
		gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height,
		r_refdef.vrect.width / scale,
		r_refdef.vrect.height / scale);
	// johnfitz

	GL_SetFrustum (r_fovx, r_fovy); // johnfitz -- use r_fov* vars

//	glCullFace(GL_BACK); // johnfitz -- glquake used CCW with backwards culling -- let's do it right

	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();

	glRotatef (-90, 1, 0, 0);	    // put Z going up
	glRotatef (90, 0, 0, 1);	    // put Z going up
	glRotatef (-r_refdef.viewangles[2], 1, 0, 0);
	glRotatef (-r_refdef.viewangles[0], 0, 1, 0);
	glRotatef (-r_refdef.viewangles[1], 0, 0, 1);
	glTranslatef (-r_refdef.vieworg[0], -r_refdef.vieworg[1], -r_refdef.vieworg[2]);

	// set drawing parms
	if (gl_cull.value)
		glEnable (GL_CULL_FACE);
	else
		glDisable (GL_CULL_FACE);
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

	// in GL clears are affected by the current depth write mask, so set up a default depth state including write-enable so that the clear will work
	// the next depth state set will most likely be for world poly drawing, which is the same as this state, so we won't have redundant sets here
	GL_DepthState (GL_TRUE, GL_LEQUAL, GL_TRUE);

	glClear (clearbits);
}


/*
===============
R_SetupScene -- johnfitz -- this is the stuff that needs to be done once per eye in stereo mode
===============
*/
void R_SetupScene (void)
{
//	R_PushDlights ();
	R_AnimateLight (cl.time);
	r_framecount++;
	R_SetupGL ();
}


/*
===============
R_SetupView -- johnfitz -- this is the stuff that needs to be done once per frame, even in stereo mode
===============
*/
void R_SetupView (void)
{
	Fog_SetupFrame (); // johnfitz

	// build the transformation matrix for the given view angles
	VectorCopy (r_refdef.vieworg, r_origin);
	AngleVectors (r_refdef.viewangles, vpn, vright, vup);

	// current viewleaf
	r_oldviewleaf = r_viewleaf;
	r_viewleaf = Mod_PointInLeaf (r_origin, cl.worldmodel);

	V_SetContentsColor (r_viewleaf->contents);
	V_CalcBlend ();

	r_cache_thrash = false;

	// johnfitz -- calculate r_fovx and r_fovy here
	r_fovx = r_refdef.fov_x;
	r_fovy = r_refdef.fov_y;

	if (r_waterwarp.value)
	{
		int contents = Mod_PointInLeaf (r_origin, cl.worldmodel)->contents;
		if (contents == CONTENTS_WATER || contents == CONTENTS_SLIME || contents == CONTENTS_LAVA)
		{
			// variance is a percentage of width, where width = 2 * tan(fov / 2) otherwise the effect is too dramatic at high FOV and too subtle at low FOV.  what a mess!
			r_fovx = atan (tan (DEG2RAD (r_refdef.fov_x) / 2) * (0.97 + sin (cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
			r_fovy = atan (tan (DEG2RAD (r_refdef.fov_y) / 2) * (1.03 - sin (cl.time * 1.5) * 0.03)) * 2 / M_PI_DIV_180;
		}
	}
	// johnfitz

	R_SetFrustum (r_fovx, r_fovy); // johnfitz -- use r_fov* vars

	R_Clear ();
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
	int		i;

	if (!r_drawentities.value)
		return;

	// johnfitz -- sprites are not a special case
	for (i = 0; i < cl_numvisedicts; i++)
	{
		currententity = cl_visedicts[i];

		// johnfitz -- if alphapass is true, draw only alpha entites this time
		// if alphapass is false, draw only nonalpha entities this time
		if ((ENTALPHA_DECODE (currententity->alpha) < 1 && !alphapass) ||
			(ENTALPHA_DECODE (currententity->alpha) == 1 && alphapass))
			continue;

		// johnfitz -- chasecam
		if (currententity == &cl_entities[cl.viewentity])
			currententity->angles[0] *= 0.3;
		// johnfitz

		switch (currententity->model->type)
		{
		case mod_alias:
			R_DrawAliasModel (currententity);
			break;
		case mod_brush:
			R_DrawBrushModel (currententity);
			break;
		case mod_sprite:
			R_DrawSpriteModel (currententity);
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
	if (!r_drawviewmodel.value || !r_drawentities.value || chase_active.value)
		return;

	if (cl.items & IT_INVISIBILITY || cl.stats[STAT_HEALTH] <= 0)
		return;

	currententity = &cl.viewent;
	if (!currententity->model)
		return;

	// johnfitz -- this fixes a crash
	if (currententity->model->type != mod_alias)
		return;
	// johnfitz

	// hack the depth range to prevent view model from poking into walls
	glDepthRange (0, 0.3);
	R_DrawAliasModel (currententity);
	glDepthRange (0, 1);
}


/*
============
R_DrawPolyBlend -- johnfitz -- moved here from gl_rmain.c, and rewritten to use glOrtho
============
*/
void R_DrawPolyBlend (void)
{
	float verts[] = { -1.0, -1.0, -1.0, 3.0, 3.0, -1.0 };

	if (!gl_polyblend.value || !v_blend[3])
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
R_RenderScene
================
*/
void R_RenderScene (void)
{
	R_SetupScene (); // johnfitz -- this does everything that should be done once per call to RenderScene

	R_MarkLeaves ();	// done here so we know if we're in water

	R_DrawWorld_Old (); // MH - reverting to old-style recursive world node drawing

	S_ExtraUpdate (); // don't let sound get messed up if going slow

	R_DrawEntitiesOnList (false); // johnfitz -- false means this is the pass for nonalpha entities

	R_DrawWorld_Water (); // johnfitz -- drawn here since they might have transparency

	R_DrawEntitiesOnList (true); // johnfitz -- true means this is the pass for alpha entities

	R_DrawParticlesARB ();

	R_DrawViewModel (); // johnfitz -- moved here from R_RenderView
}


static GLuint r_scaleview_texture;
static int r_scaleview_texture_width, r_scaleview_texture_height;

/*
=============
R_ScaleView_DeleteTexture
=============
*/
void R_ScaleView_DeleteTexture (void)
{
	glDeleteTextures (1, &r_scaleview_texture);
	r_scaleview_texture = 0;
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
	int scale = CLAMP (1, (int) r_scale.value, 4);
	int srcx = glx + r_refdef.vrect.x;
	int srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	int srcw = r_refdef.vrect.width / scale;
	int srch = r_refdef.vrect.height / scale;

	if (scale == 1)
		return;

	// make sure texture unit 0 is selected
	glActiveTexture (GL_TEXTURE0);

	// create (if needed) and bind the render-to-texture texture
	if (!r_scaleview_texture)
	{
		glGenTextures (1, &r_scaleview_texture);

		r_scaleview_texture_width = 0;
		r_scaleview_texture_height = 0;
	}

	glBindTexture (GL_TEXTURE_2D, r_scaleview_texture);

	// resize render-to-texture texture if needed
	if (r_scaleview_texture_width < srcw || r_scaleview_texture_height < srch)
	{
		r_scaleview_texture_width = srcw;
		r_scaleview_texture_height = srch;

		if (!GLEW_ARB_texture_non_power_of_two)
		{
			r_scaleview_texture_width = TexMgr_Pad (r_scaleview_texture_width);
			r_scaleview_texture_height = TexMgr_Pad (r_scaleview_texture_height);
		}

		glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, r_scaleview_texture_width, r_scaleview_texture_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}

	// copy the framebuffer to the texture
	glCopyTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, srcx, srcy, srcw, srch);

	// draw the texture back to the framebuffer
	glDisable (GL_CULL_FACE);

	GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);

	glViewport (srcx, srcy, r_refdef.vrect.width, r_refdef.vrect.height);

	// correction factor if we lack NPOT textures, normally these are 1.0f
	float smax = srcw / (float) r_scaleview_texture_width;
	float tmax = srch / (float) r_scaleview_texture_height;

	float positions[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
	float texcoords[] = { 0, 0, smax, 0, smax, tmax, 0, tmax };

	GL_BindBuffer (GL_ARRAY_BUFFER, 0);
	GL_EnableVertexAttribArrays (VAA0 | VAA1);
	GL_BindPrograms (r_scaleview_vp, r_scaleview_fp);

	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, positions);
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 0, texcoords);

	glDrawArrays (GL_QUADS, 0, 4);

	// clear cached binding
	GL_ClearTextureBindings ();
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
	if (r_speeds.value)
	{
		glFinish ();
		time1 = Sys_DoubleTime ();

		// johnfitz -- rendering statistics
		rs_brushpolys = rs_aliaspolys = rs_skypolys = rs_particles = rs_fogpolys = rs_megatexels =
			rs_dynamiclightmaps = rs_aliaspasses = rs_skypasses = rs_brushpasses = 0;
	}
	else if (gl_finish.value)
		glFinish ();

	R_SetupView ();

	R_RenderScene ();

	R_ScaleView ();

	R_DrawPolyBlend (); // MH - put this back

	// johnfitz -- modified r_speeds output
	time2 = Sys_DoubleTime ();
	if (r_pos.value)
		Con_Printf ("x %i y %i z %i (pitch %i yaw %i roll %i)\n",
			(int) cl_entities[cl.viewentity].origin[0],
			(int) cl_entities[cl.viewentity].origin[1],
			(int) cl_entities[cl.viewentity].origin[2],
			(int) cl.viewangles[PITCH],
			(int) cl.viewangles[YAW],
			(int) cl.viewangles[ROLL]);
	else if (r_speeds.value == 2)
		Con_Printf ("%3i ms  %4i/%4i wpoly %4i/%4i epoly %3i lmap %4i/%4i sky %1.1f mtex\n",
			(int) ((time2 - time1) * 1000),
			rs_brushpolys,
			rs_brushpasses,
			rs_aliaspolys,
			rs_aliaspasses,
			rs_dynamiclightmaps,
			rs_skypolys,
			rs_skypasses,
			TexMgr_FrameUsage ());
	else if (r_speeds.value)
		Con_Printf ("%3i ms  %4i wpoly %4i epoly %3i lmap\n",
			(int) ((time2 - time1) * 1000),
			rs_brushpolys,
			rs_aliaspolys,
			rs_dynamiclightmaps);
	// johnfitz
}

