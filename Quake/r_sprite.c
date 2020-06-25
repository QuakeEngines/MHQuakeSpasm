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
GLuint r_sprite_fp = 0;


typedef struct spritepolyvert_s {
	float point[3];
	float st[2];
} spritepolyvert_t;


void GLSprite_CreateShaders (void)
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
		"# copy over texcoord\n"
		"MOV result.texcoord[0], vertex.attrib[1];\n"
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
		"LRP result.color.rgb, fogFactor.x, diff, state.fog.color;\n"
		"\n"
		"# set the alpha channel\n"
		"MOV result.color.a, 1.0;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_sprite_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_source);
	r_sprite_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_source);
}


/*
================
R_GetSpriteFrame
================
*/
mspriteframe_t *R_GetSpriteFrame (entity_t *currentent)
{
	msprite_t *psprite;
	mspritegroup_t *pspritegroup;
	mspriteframe_t *pspriteframe;
	int				i, numframes, frame;
	float *pintervals, fullinterval, targettime, time;

	psprite = (msprite_t *) currentent->model->cache.data;
	frame = currentent->frame;

	if ((frame >= psprite->numframes) || (frame < 0))
	{
		Con_DPrintf ("R_DrawSprite: no such frame %d for '%s'\n", frame, currentent->model->name);
		frame = 0;
	}

	if (psprite->frames[frame].type == SPR_SINGLE)
	{
		pspriteframe = psprite->frames[frame].frameptr;
	}
	else
	{
		pspritegroup = (mspritegroup_t *) psprite->frames[frame].frameptr;
		pintervals = pspritegroup->intervals;
		numframes = pspritegroup->numframes;
		fullinterval = pintervals[numframes - 1];

		time = cl.time + currentent->syncbase;

		// when loading in Mod_LoadSpriteGroup, we guaranteed all interval values
		// are positive, so we don't have to worry about division by 0
		targettime = time - ((int) (time / fullinterval)) * fullinterval;

		for (i = 0; i < (numframes - 1); i++)
		{
			if (pintervals[i] > targettime)
				break;
		}

		pspriteframe = pspritegroup->frames[i];
	}

	return pspriteframe;
}


void R_EmitSpriteVertex (spritepolyvert_t *vert, float *origin, float a, float b, float *up, float *right, float s, float t)
{
	VectorMA (origin, a, up, vert->point);
	VectorMA (vert->point, b, right, vert->point);

	vert->st[0] = s;
	vert->st[1] = t;
}


/*
=================
R_DrawSpriteModel -- johnfitz -- rewritten: now supports all orientations
=================
*/
void R_DrawSpriteModel (entity_t *e)
{
	vec3_t			v_forward, v_right, v_up;
	msprite_t *psprite;
	mspriteframe_t *frame;
	float *s_up, *s_right;
	float			angle, sr, cr;
	spritepolyvert_t verts[4];

	// TODO: frustum cull it?

	frame = R_GetSpriteFrame (e);
	psprite = (msprite_t *) currententity->model->cache.data;

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
		VectorSubtract (currententity->origin, r_origin, v_forward);
		v_forward[2] = 0;
		VectorNormalizeFast (v_forward);
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
		AngleVectors (currententity->angles, v_forward, v_right, v_up);
		s_up = v_up;
		s_right = v_right;
		break;
	case SPR_VP_PARALLEL_ORIENTED: // faces view plane, but obeys roll value
		angle = currententity->angles[ROLL] * M_PI_DIV_180;
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
	GL_BindPrograms (r_sprite_vp, r_sprite_fp);
	GL_BindBuffer (GL_ARRAY_BUFFER, 0);

	GL_EnableVertexAttribArrays (VAA0 | VAA1);

	glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, sizeof (spritepolyvert_t), verts->point);
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, sizeof (spritepolyvert_t), verts->st);

	R_EmitSpriteVertex (&verts[0], e->origin, frame->down, frame->left, s_up, s_right, 0, frame->tmax);
	R_EmitSpriteVertex (&verts[1], e->origin, frame->up, frame->left, s_up, s_right, 0, 0);
	R_EmitSpriteVertex (&verts[2], e->origin, frame->up, frame->right, s_up, s_right, frame->smax, 0);
	R_EmitSpriteVertex (&verts[3], e->origin, frame->down, frame->right, s_up, s_right, frame->smax, frame->tmax);

	glDrawArrays (GL_QUADS, 0, 4);

	// johnfitz: offset decals
	if (psprite->type == SPR_ORIENTED)
		GL_PolygonOffset (OFFSET_NONE);
}

