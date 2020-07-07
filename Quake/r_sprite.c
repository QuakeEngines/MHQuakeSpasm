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
// r_sprite.c -- sprite model rendering

#include "quakedef.h"


GLuint r_sprite_vp = 0;
GLuint r_sprite_fp[2] = { 0 };

#define SPRITE_SOLID	0
#define SPRITE_ALPHA	1


void GLSprite_CreateShaders (void)
{
	const GLchar *vp_source = \
		"!!ARBvp1.0\n"
		"\n"
		"PARAM origin = program.local[0];\n"
		"PARAM uvec = program.local[1];\n"
		"PARAM rvec = program.local[2];\n"
		"\n"
		"# set up position\n"
		"TEMP NewPosition;\n"
		"MAD NewPosition, uvec, vertex.attrib[0].x, origin;\n"
		"MAD NewPosition, rvec, vertex.attrib[0].y, NewPosition;\n"
		"MOV NewPosition.w, 1.0;\n"
		"\n"
		"# transform position to output\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], NewPosition;\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], NewPosition;\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], NewPosition;\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], NewPosition;\n"
		"\n"
		"# copy over texcoord\n"
		"MOV result.texcoord[0], vertex.attrib[1];\n"
		"\n"
		"# set up fog coordinate\n"
		"DP4 result.fogcoord.x, state.matrix.mvp.row[3], NewPosition;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff, fence;\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"\n"
		"# fence texture test\n"
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
		"MOV result.color.a, 1.0;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_sprite_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_source);

	r_sprite_fp[SPRITE_SOLID] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetFragmentProgram (fp_source, SHADERFLAG_NONE));
	r_sprite_fp[SPRITE_ALPHA] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetFragmentProgram (fp_source, SHADERFLAG_FENCE));
}


void R_CreateSpriteVertex (spritepolyvert_t *vert, float a, float b, float s, float t)
{
	Vector2Set (vert->framevec, a, b);
	Vector2Set (vert->texcoord, s, t);
}


void R_CreateSpriteFrame (spritepolyvert_t *verts, mspriteframe_t *frame)
{
	R_CreateSpriteVertex (&verts[0], frame->down, frame->left, 0, frame->tmax);
	R_CreateSpriteVertex (&verts[1], frame->up, frame->left, 0, 0);
	R_CreateSpriteVertex (&verts[2], frame->up, frame->right, frame->smax, 0);
	R_CreateSpriteVertex (&verts[3], frame->down, frame->right, frame->smax, frame->tmax);
}


void R_CreateSpriteFrames (qmodel_t *mod, msprite_t *psprite)
{
	// check if the model already has a buffer set
	if ((mod->buffsetset = R_GetBufferSetForName (mod->name)) != -1) return;

	// get a new one
	if ((mod->buffsetset = R_NewBufferSetForName (mod->name)) == -1)
		Sys_Error ("GLMesh_LoadAliasGeometry : unable to allocate a buffer set");

	bufferset_t *set = &r_buffersets[mod->buffsetset];
	int mark = Hunk_LowMark ();

	// these could probably go to static buffers
	spritepolyvert_t *frameverts = (spritepolyvert_t *) Hunk_Alloc (sizeof (spritepolyvert_t) * psprite->numframeverts);

	for (int i = 0; i < psprite->numframes; i++)
	{
		if (psprite->frames[i].type == SPR_SINGLE)
		{
			mspriteframe_t *frame = psprite->frames[i].frameptr;
			R_CreateSpriteFrame (&frameverts[frame->firstvertex], frame);
		}
		else
		{
			mspritegroup_t *group = (mspritegroup_t *) psprite->frames[i].frameptr;

			for (int j = 0; j < group->numframes; j++)
			{
				mspriteframe_t *frame = group->frames[j];
				R_CreateSpriteFrame (&frameverts[frame->firstvertex], frame);
			}
		}
	}

	// upload vertexes buffer
	glGenBuffers (1, &set->vertexbuffer);
	glBindBuffer (GL_ARRAY_BUFFER, set->vertexbuffer);
	glBufferData (GL_ARRAY_BUFFER, sizeof (spritepolyvert_t) * psprite->numframeverts, frameverts, GL_STATIC_DRAW);

	// invalidate the cached bindings
	GL_ClearBufferBindings ();

	// free temp memory
	Hunk_FreeToLowMark (mark);
}


/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame (entity_t *e)
{
	msprite_t *psprite = (msprite_t *) e->model->cache.data;
	int frame = e->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_DrawSprite: no such frame %d for '%s'\n", frame, e->model->name);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
		return psprite->frames[frame].frameptr;
	else
	{
		mspritegroup_t *pspritegroup = (mspritegroup_t *) psprite->frames[frame].frameptr;
		int groupframe = Mod_GetAutoAnimation (pspritegroup->intervals, pspritegroup->numframes, e->syncbase);
		return pspritegroup->frames[groupframe];
	}
}


/*
=================
R_DrawSpriteModel -- johnfitz -- rewritten: now supports all orientations
=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	float		v_forward[4], v_right[4], v_up[4];	// padded for use in shaders
	msprite_t *psprite;
	mspriteframe_t *frame;
	float *s_up, *s_right;
	float			angle, sr, cr;

	// TODO: frustum cull it?
	frame = R_GetSpriteFrame (e);
	psprite = (msprite_t *) e->model->cache.data;

	switch (psprite->type)
	{
	case SPR_VP_PARALLEL_UPRIGHT: // faces view plane, up is towards the heavens
		v_up[0] = 0;
		v_up[1] = 0;
		v_up[2] = 1;

		s_up = v_up;
		s_right = vright;
		break;

	case SPR_FACING_UPRIGHT: // faces camera origin, up is towards the heavens
		VectorSubtract (e->origin, r_origin, v_forward);
		v_forward[2] = 0;
		VectorNormalize (v_forward);

		v_right[0] = v_forward[1];
		v_right[1] = -v_forward[0];
		v_right[2] = 0;

		v_up[0] = 0;
		v_up[1] = 0;
		v_up[2] = 1;

		s_up = v_up;
		s_right = v_right;
		break;

	case SPR_VP_PARALLEL: // faces view plane, up is towards the top of the screen
		s_up = vup;
		s_right = vright;
		break;

	case SPR_ORIENTED: // pitch yaw roll are independent of camera
		AngleVectors (e->angles, v_forward, v_right, v_up);
		s_up = v_up;
		s_right = v_right;
		break;

	case SPR_VP_PARALLEL_ORIENTED: // faces view plane, but obeys roll value
		angle = e->angles[ROLL] * M_PI_DIV_180;

		sr = sin (angle);
		cr = cos (angle);

		v_right[0] = vright[0] * cr + vup[0] * sr;
		v_right[1] = vright[1] * cr + vup[1] * sr;
		v_right[2] = vright[2] * cr + vup[2] * sr;

		v_up[0] = vright[0] * -sr + vup[0] * cr;
		v_up[1] = vright[1] * -sr + vup[1] * cr;
		v_up[2] = vright[2] * -sr + vup[2] * cr;

		s_up = v_up;
		s_right = v_right;
		break;

	default:
		return;
	}

	// johnfitz: offset decals
	if (psprite->type == SPR_ORIENTED)
		GL_PolygonOffset (OFFSET_DECAL);

	GL_BindTexture (GL_TEXTURE0, frame->gltexture);

	if (frame->gltexture->flags & TEXPREF_ALPHA)
		GL_BindPrograms (r_sprite_vp, r_sprite_fp[SPRITE_ALPHA]);
	else GL_BindPrograms (r_sprite_vp, r_sprite_fp[SPRITE_SOLID]);

	glProgramLocalParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 0, e->origin);	// note : this will overflow the read but that's OK because there are members after it in the struct
	glProgramLocalParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 1, s_up);			// this one was padded to 4 floats
	glProgramLocalParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 2, s_right);		// this one was padded to 4 floats

	GL_DepthState (GL_TRUE, GL_LEQUAL, GL_TRUE);
	GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);

	GL_EnableVertexAttribArrays (VAA0 | VAA1);

	bufferset_t *set = &r_buffersets[e->model->buffsetset];
	GL_BindBuffer (GL_ARRAY_BUFFER, set->vertexbuffer);

	// the data was already built at load time so it just needs to be set up for rendering here
	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, sizeof (spritepolyvert_t), (void *) offsetof (spritepolyvert_t, framevec));
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, sizeof (spritepolyvert_t), (void *) offsetof (spritepolyvert_t, texcoord));

	// and draw it
	glDrawArrays (GL_QUADS, frame->firstvertex, 4);

	// johnfitz: offset decals
	if (psprite->type == SPR_ORIENTED)
		GL_PolygonOffset (OFFSET_NONE);
}


