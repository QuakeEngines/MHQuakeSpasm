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

#include "quakedef.h"

#define MAX_PARTICLES			2048	// default max # of particles at one time

// ramps are made larger to allow for overshoot, using full alpha as the dead particle; anything exceeding ramp[8] is automatically dead
int	ramp1[] = { 0x6f, 0x6d, 0x6b, 0x69, 0x67, 0x65, 0x63, 0x61, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
int	ramp2[] = { 0x6f, 0x6e, 0x6d, 0x6c, 0x6b, 0x6a, 0x68, 0x66, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
int	ramp3[] = { 0x6d, 0x6b, 0x06, 0x05, 0x04, 0x03, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };


typedef struct ptypedef_s {
	vec3_t	dvel;
	vec3_t	grav;
	int *ramp;
	float	ramptime;
	vec3_t	accel;
} ptypedef_t;


typedef enum {
	pt_static, pt_grav, pt_slowgrav, pt_fire, pt_explode, pt_explode2, pt_blob, pt_blob2, pt_entparticles, MAX_PARTICLE_TYPES
} ptype_t;


ptypedef_t p_typedefs[MAX_PARTICLE_TYPES] = {
	{ { 0, 0, 0 }, { 0, 0, 0 }, NULL, 0 },		// pt_static
	{ { 0, 0, 0 }, { 0, 0, -1 }, NULL, 0 },		// pt_grav
	{ { 0, 0, 0 }, { 0, 0, -0.5 }, NULL, 0 },		// pt_slowgrav
	{ { 0, 0, 0 }, { 0, 0, 1 }, ramp3, 5 },		// pt_fire
	{ { 4, 4, 4 }, { 0, 0, -1 }, ramp1, 10 },	// pt_explode
	{ { -1, -1, -1 }, { 0, 0, -1 }, ramp2, 15 },	// pt_explode2
	{ { 4, 4, 4 }, { 0, 0, -1 }, NULL, 0 },		// pt_blob
	{ { -4, -4, 0 }, { 0, 0, -1 }, NULL, 0 },		// pt_blob2
	{ { 4, 4, 4 }, { 0, 0, -1 }, NULL, 0 },	// pt_entparticles replicates pt_explode but with no ramp
};


#define PF_ONEFRAME		(1 << 0)		// particle is removed after one frame irrespective of die times
#define PF_ETERNAL		(1 << 1)		// particle is never removed irrespective of die times

// !!! if this is changed, it must be changed in d_ifacea.h too !!!
typedef struct particle_s {
	// driver-usable fields
	vec3_t		org;
	float		size;
	int			color;

	// drivers never touch the following fields
	vec3_t		vel;
	float		ramp;
	float		time;
	float		die;
	ptype_t		type;

	int			flags;

	struct particle_s *next;
} particle_t;


particle_t *active_particles;
particle_t *free_particles;


cvar_t	r_particles = { "r_particles", "1", CVAR_ARCHIVE }; // johnfitz


GLuint r_particle_vp = 0;
GLuint r_particle_fp_circle = 0;
GLuint r_particle_fp_square = 0;

void GLParticles_CreateShaders (void)
{
	const GLchar *vp_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# pick friendly names for the params\n"
		"PARAM scales = program.env[1];\n"
		"PARAM r_origin = program.env[2];\n"
		"PARAM vpn = program.env[3];\n"
		"PARAM vup = program.env[4];\n"
		"PARAM vright = program.env[5];\n"
		"\n"
		"# pick friendly names for the attribs\n"
		"ATTRIB offsets = vertex.attrib[0];\n"
		"ATTRIB colour = vertex.attrib[1];\n"
		"ATTRIB origin = vertex.attrib[2];\n"
		"ATTRIB vel = vertex.attrib[3];\n"
		"ATTRIB accel = vertex.attrib[4];\n"
		"ATTRIB time = vertex.attrib[5];\n"
		"ATTRIB size = vertex.attrib[6];\n"
		"\n"
		"TEMP pscale, NewPosition;\n"
		"\n"
		"# move the particle in a framerate-independent manner\n"
		"MAD NewPosition, accel, time.x, vel;\n"
		"MAD NewPosition, NewPosition, time.x, origin;\n"
		"\n"
		"# hack a scale up to prevent particles from disappearing\n"
		"SUB pscale, NewPosition, r_origin;\n"
		"DP3 pscale.x, pscale, vpn;\n"
		"MAD pscale.x, pscale.x, scales.y, scales.z;\n"
		"MUL pscale, offsets, pscale.x;\n"
		"\n"
		"# scale by particle size\n"
		"MUL pscale, pscale, size.x;\n"
		"\n"
		"# scale down for large quad size\n"
		"MUL pscale, pscale, scales.x;\n"
		"\n"
		"# compute billboard quad corner\n"
		"MAD NewPosition, vright, pscale.x, NewPosition;\n"
		"MAD NewPosition, vup, pscale.y, NewPosition;\n"
		"\n"
		"# ensure\n"
		"MOV NewPosition.w, 1.0;\n"
		"\n"
		"# transform input position to output position\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], NewPosition;\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], NewPosition;\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], NewPosition;\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], NewPosition;\n"
		"\n"
		"# move colour and texcoords to output\n"
		"MOV result.texcoord[0], offsets;\n"
		"MOV result.color, colour;\n"
		"\n"
		"# set up fog coordinate\n"
		"DP4 result.fogcoord.x, state.matrix.mvp.row[3], NewPosition;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_source_circle = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff;\n"
		"\n"
		"# make a circle\n"
		"DP3 diff.a, fragment.texcoord[0], fragment.texcoord[0];\n"
		"SUB diff.a, 1.0, diff.a;\n"
		"MUL diff.a, 1.5, diff.a;\n"
		"\n"
		"# blend to output\n"
		"MOV diff.rgb, fragment.color;\n"
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
		"MOV result.color.a, diff.a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_source_square = \
		"!!ARBfp1.0\n"
		"\n"
		"# square particles for the authentic crunchy look\n"
		"TEMP diff;\n"
		"\n"
		"# perform the fogging\n"
		"TEMP fogFactor;\n"
		"MUL fogFactor.x, state.fog.params.x, fragment.fogcoord.x;\n"
		"MUL fogFactor.x, fogFactor.x, fogFactor.x;\n"
		"EX2_SAT fogFactor.x, -fogFactor.x;\n"
		"LRP diff.rgb, fogFactor.x, fragment.color, state.fog.color;\n"
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

	r_particle_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_source);
	r_particle_fp_circle = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_source_circle);
	r_particle_fp_square = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_source_square);
}


/*
===============
R_InitParticles
===============
*/
void R_InitParticles (void)
{
	Cvar_RegisterVariable (&r_particles); // johnfitz
}


particle_t *R_AllocParticle (void)
{
	particle_t *p;

	if (!free_particles)
	{
		free_particles = (particle_t *) Hunk_AllocName (MAX_PARTICLES * sizeof (particle_t), "particles");

		for (int i = 1; i < MAX_PARTICLES; i++)
			free_particles[i - 1].next = &free_particles[i];

		free_particles[MAX_PARTICLES - 1].next = NULL;
	}

	// take the first free particle
	p = free_particles;
	free_particles = p->next;

	// and move it to the active list
	p->next = active_particles;
	active_particles = p;

	// save off time the particle was spawned at for time-based effects
	p->time = cl.time;

	// initially no flags
	p->flags = 0;

	// regular particle; may be overridden for some types
	// slightly randomize the size to make them more visually interesting (but not too much, variation of 0.85-1.15 is sufficient)
	p->size = 0.85f + (float) (rand () & 31) / 100.0f;

	// and return what we got
	return p;
}


/*
===============
R_ClearParticles
===============
*/
void R_ClearParticles (void)
{
	free_particles = NULL;
	active_particles = NULL;

	// allocate an initial batch and immediately kill the returned particle so that we don't hit an allocation first time at run time
	R_AllocParticle ()->die = -1;
}


/*
===============
R_EntityParticles
===============
*/
void R_EntityParticles (entity_t *ent, float dist, float beamlength)
{
	// this is the only place these are used nowadays
#define NUMVERTEXNORMALS	162

	static float	avertexnormals[NUMVERTEXNORMALS][3] = {
		{ -0.525731f, 0.000000f, 0.850651f }, { -0.442863f, 0.238856f, 0.864188f }, { -0.295242f, 0.000000f, 0.955423f }, { -0.309017f, 0.500000f, 0.809017f }, { -0.162460f, 0.262866f, 0.951056f }, { 0.000000f, 0.000000f, 1.000000f },
		{ 0.000000f, 0.850651f, 0.525731f }, { -0.147621f, 0.716567f, 0.681718f }, { 0.147621f, 0.716567f, 0.681718f }, { 0.000000f, 0.525731f, 0.850651f }, { 0.309017f, 0.500000f, 0.809017f }, { 0.525731f, 0.000000f, 0.850651f },
		{ 0.295242f, 0.000000f, 0.955423f }, { 0.442863f, 0.238856f, 0.864188f }, { 0.162460f, 0.262866f, 0.951056f }, { -0.681718f, 0.147621f, 0.716567f }, { -0.809017f, 0.309017f, 0.500000f }, { -0.587785f, 0.425325f, 0.688191f },
		{ -0.850651f, 0.525731f, 0.000000f }, { -0.864188f, 0.442863f, 0.238856f }, { -0.716567f, 0.681718f, 0.147621f }, { -0.688191f, 0.587785f, 0.425325f }, { -0.500000f, 0.809017f, 0.309017f }, { -0.238856f, 0.864188f, 0.442863f },
		{ -0.425325f, 0.688191f, 0.587785f }, { -0.716567f, 0.681718f, -0.147621f }, { -0.500000f, 0.809017f, -0.309017f }, { -0.525731f, 0.850651f, 0.000000f }, { 0.000000f, 0.850651f, -0.525731f }, { -0.238856f, 0.864188f, -0.442863f },
		{ 0.000000f, 0.955423f, -0.295242f }, { -0.262866f, 0.951056f, -0.162460f }, { 0.000000f, 1.000000f, 0.000000f }, { 0.000000f, 0.955423f, 0.295242f }, { -0.262866f, 0.951056f, 0.162460f }, { 0.238856f, 0.864188f, 0.442863f },
		{ 0.262866f, 0.951056f, 0.162460f }, { 0.500000f, 0.809017f, 0.309017f }, { 0.238856f, 0.864188f, -0.442863f }, { 0.262866f, 0.951056f, -0.162460f }, { 0.500000f, 0.809017f, -0.309017f }, { 0.850651f, 0.525731f, 0.000000f },
		{ 0.716567f, 0.681718f, 0.147621f }, { 0.716567f, 0.681718f, -0.147621f }, { 0.525731f, 0.850651f, 0.000000f }, { 0.425325f, 0.688191f, 0.587785f }, { 0.864188f, 0.442863f, 0.238856f }, { 0.688191f, 0.587785f, 0.425325f },
		{ 0.809017f, 0.309017f, 0.500000f }, { 0.681718f, 0.147621f, 0.716567f }, { 0.587785f, 0.425325f, 0.688191f }, { 0.955423f, 0.295242f, 0.000000f }, { 1.000000f, 0.000000f, 0.000000f }, { 0.951056f, 0.162460f, 0.262866f },
		{ 0.850651f, -0.525731f, 0.000000f }, { 0.955423f, -0.295242f, 0.000000f }, { 0.864188f, -0.442863f, 0.238856f }, { 0.951056f, -0.162460f, 0.262866f }, { 0.809017f, -0.309017f, 0.500000f }, { 0.681718f, -0.147621f, 0.716567f },
		{ 0.850651f, 0.000000f, 0.525731f }, { 0.864188f, 0.442863f, -0.238856f }, { 0.809017f, 0.309017f, -0.500000f }, { 0.951056f, 0.162460f, -0.262866f }, { 0.525731f, 0.000000f, -0.850651f }, { 0.681718f, 0.147621f, -0.716567f },
		{ 0.681718f, -0.147621f, -0.716567f }, { 0.850651f, 0.000000f, -0.525731f }, { 0.809017f, -0.309017f, -0.500000f }, { 0.864188f, -0.442863f, -0.238856f }, { 0.951056f, -0.162460f, -0.262866f }, { 0.147621f, 0.716567f, -0.681718f },
		{ 0.309017f, 0.500000f, -0.809017f }, { 0.425325f, 0.688191f, -0.587785f }, { 0.442863f, 0.238856f, -0.864188f }, { 0.587785f, 0.425325f, -0.688191f }, { 0.688191f, 0.587785f, -0.425325f }, { -0.147621f, 0.716567f, -0.681718f },
		{ -0.309017f, 0.500000f, -0.809017f }, { 0.000000f, 0.525731f, -0.850651f }, { -0.525731f, 0.000000f, -0.850651f }, { -0.442863f, 0.238856f, -0.864188f }, { -0.295242f, 0.000000f, -0.955423f }, { -0.162460f, 0.262866f, -0.951056f },
		{ 0.000000f, 0.000000f, -1.000000f }, { 0.295242f, 0.000000f, -0.955423f }, { 0.162460f, 0.262866f, -0.951056f }, { -0.442863f, -0.238856f, -0.864188f }, { -0.309017f, -0.500000f, -0.809017f }, { -0.162460f, -0.262866f, -0.951056f },
		{ 0.000000f, -0.850651f, -0.525731f }, { -0.147621f, -0.716567f, -0.681718f }, { 0.147621f, -0.716567f, -0.681718f }, { 0.000000f, -0.525731f, -0.850651f }, { 0.309017f, -0.500000f, -0.809017f }, { 0.442863f, -0.238856f, -0.864188f },
		{ 0.162460f, -0.262866f, -0.951056f }, { 0.238856f, -0.864188f, -0.442863f }, { 0.500000f, -0.809017f, -0.309017f }, { 0.425325f, -0.688191f, -0.587785f }, { 0.716567f, -0.681718f, -0.147621f }, { 0.688191f, -0.587785f, -0.425325f },
		{ 0.587785f, -0.425325f, -0.688191f }, { 0.000000f, -0.955423f, -0.295242f }, { 0.000000f, -1.000000f, 0.000000f }, { 0.262866f, -0.951056f, -0.162460f }, { 0.000000f, -0.850651f, 0.525731f }, { 0.000000f, -0.955423f, 0.295242f },
		{ 0.238856f, -0.864188f, 0.442863f }, { 0.262866f, -0.951056f, 0.162460f }, { 0.500000f, -0.809017f, 0.309017f }, { 0.716567f, -0.681718f, 0.147621f }, { 0.525731f, -0.850651f, 0.000000f }, { -0.238856f, -0.864188f, -0.442863f },
		{ -0.500000f, -0.809017f, -0.309017f }, { -0.262866f, -0.951056f, -0.162460f }, { -0.850651f, -0.525731f, 0.000000f }, { -0.716567f, -0.681718f, -0.147621f }, { -0.716567f, -0.681718f, 0.147621f }, { -0.525731f, -0.850651f, 0.000000f },
		{ -0.500000f, -0.809017f, 0.309017f }, { -0.238856f, -0.864188f, 0.442863f }, { -0.262866f, -0.951056f, 0.162460f }, { -0.864188f, -0.442863f, 0.238856f }, { -0.809017f, -0.309017f, 0.500000f }, { -0.688191f, -0.587785f, 0.425325f },
		{ -0.681718f, -0.147621f, 0.716567f }, { -0.442863f, -0.238856f, 0.864188f }, { -0.587785f, -0.425325f, 0.688191f }, { -0.309017f, -0.500000f, 0.809017f }, { -0.147621f, -0.716567f, 0.681718f }, { -0.425325f, -0.688191f, 0.587785f },
		{ -0.162460f, -0.262866f, 0.951056f }, { 0.442863f, -0.238856f, 0.864188f }, { 0.162460f, -0.262866f, 0.951056f }, { 0.309017f, -0.500000f, 0.809017f }, { 0.147621f, -0.716567f, 0.681718f }, { 0.000000f, -0.525731f, 0.850651f },
		{ 0.425325f, -0.688191f, 0.587785f }, { 0.587785f, -0.425325f, 0.688191f }, { 0.688191f, -0.587785f, 0.425325f }, { -0.955423f, 0.295242f, 0.000000f }, { -0.951056f, 0.162460f, 0.262866f }, { -1.000000f, 0.000000f, 0.000000f },
		{ -0.850651f, 0.000000f, 0.525731f }, { -0.955423f, -0.295242f, 0.000000f }, { -0.951056f, -0.162460f, 0.262866f }, { -0.864188f, 0.442863f, -0.238856f }, { -0.951056f, 0.162460f, -0.262866f }, { -0.809017f, 0.309017f, -0.500000f },
		{ -0.864188f, -0.442863f, -0.238856f }, { -0.951056f, -0.162460f, -0.262866f }, { -0.809017f, -0.309017f, -0.500000f }, { -0.681718f, 0.147621f, -0.716567f }, { -0.681718f, -0.147621f, -0.716567f }, { -0.850651f, 0.000000f, -0.525731f },
		{ -0.688191f, 0.587785f, -0.425325f }, { -0.587785f, 0.425325f, -0.688191f }, { -0.425325f, 0.688191f, -0.587785f }, { -0.425325f, -0.688191f, -0.587785f }, { -0.587785f, -0.425325f, -0.688191f }, { -0.688191f, -0.587785f, -0.425325f }
	};

	static float avelocities[NUMVERTEXNORMALS][3] = {
		{ 1.65f, 2.06f, 1.84f }, { 1.88f, 1.53f, 1.83f }, { 2.33f, 1.60f, 1.93f }, { 2.55f, 0.20f, 0.92f }, { 0.34f, 1.02f, 1.33f }, { 0.81f, 1.60f, 0.12f }, { 1.12f, 0.62f, 0.22f }, { 0.52f, 1.54f, 0.28f }, { 1.66f, 0.71f, 0.86f },
		{ 0.70f, 0.78f, 0.28f }, { 1.79f, 2.21f, 1.88f }, { 1.18f, 2.44f, 1.07f }, { 1.00f, 2.06f, 0.64f }, { 1.98f, 2.07f, 0.83f }, { 1.55f, 0.56f, 0.54f }, { 0.48f, 0.21f, 2.20f }, { 0.79f, 0.13f, 0.65f }, { 0.75f, 1.03f, 2.16f }, 
		{ 2.33f, 1.20f, 1.77f }, { 1.97f, 0.00f, 2.17f }, { 2.22f, 1.47f, 2.16f }, { 2.00f, 2.37f, 0.18f }, { 1.50f, 0.40f, 0.69f }, { 2.26f, 2.26f, 0.75f }, { 0.01f, 1.25f, 2.27f }, { 0.19f, 1.39f, 1.19f }, { 1.06f, 0.88f, 1.08f }, 
		{ 0.05f, 1.09f, 1.38f }, { 0.98f, 1.89f, 1.84f }, { 1.52f, 1.79f, 1.56f }, { 2.23f, 0.16f, 1.94f }, { 0.77f, 1.19f, 1.35f }, { 2.24f, 1.68f, 1.33f }, { 0.59f, 1.00f, 1.22f }, { 0.55f, 2.47f, 2.54f }, { 1.32f, 2.10f, 0.55f }, 
		{ 0.72f, 1.98f, 2.36f }, { 1.41f, 1.58f, 2.53f }, { 2.19f, 0.67f, 0.48f }, { 1.06f, 1.09f, 0.66f }, { 0.85f, 2.13f, 2.18f }, { 0.50f, 0.35f, 2.10f }, { 2.46f, 2.27f, 0.60f }, { 0.67f, 1.71f, 2.36f }, { 2.34f, 0.30f, 1.67f }, 
		{ 1.46f, 1.11f, 1.12f }, { 0.82f, 2.35f, 1.50f }, { 1.62f, 0.03f, 0.67f }, { 1.42f, 2.51f, 1.15f }, { 1.90f, 2.48f, 1.03f }, { 1.14f, 0.63f, 0.63f }, { 1.19f, 2.16f, 1.37f }, { 0.40f, 1.68f, 1.91f }, { 1.64f, 1.70f, 2.32f }, 
		{ 2.39f, 1.31f, 2.55f }, { 0.86f, 1.54f, 2.27f }, { 2.51f, 0.74f, 0.74f }, { 1.18f, 1.57f, 1.49f }, { 0.23f, 0.65f, 2.28f }, { 0.10f, 0.29f, 0.66f }, { 2.27f, 0.61f, 0.45f }, { 0.87f, 0.24f, 1.38f }, { 1.95f, 0.91f, 2.50f }, 
		{ 0.89f, 0.56f, 1.46f }, { 2.09f, 1.61f, 0.57f }, { 0.71f, 2.02f, 1.71f }, { 1.20f, 1.79f, 0.75f }, { 0.25f, 1.61f, 0.33f }, { 1.32f, 0.38f, 1.44f }, { 0.80f, 0.71f, 0.47f }, { 1.93f, 0.25f, 0.25f }, { 1.15f, 0.08f, 0.81f }, 
		{ 1.37f, 0.23f, 1.30f }, { 2.41f, 1.92f, 1.09f }, { 2.03f, 1.16f, 0.90f }, { 1.17f, 1.13f, 1.29f }, { 1.16f, 1.64f, 1.69f }, { 2.02f, 0.06f, 2.29f }, { 0.65f, 0.52f, 1.16f }, { 2.22f, 2.14f, 0.56f }, { 0.95f, 1.16f, 0.71f }, 
		{ 1.37f, 1.93f, 1.45f }, { 1.05f, 2.47f, 0.57f }, { 0.36f, 2.53f, 1.47f }, { 1.63f, 1.06f, 2.23f }, { 0.20f, 2.36f, 1.09f }, { 1.28f, 1.21f, 0.36f }, { 2.02f, 1.49f, 2.40f }, { 2.13f, 1.82f, 0.17f }, { 2.47f, 1.48f, 0.67f }, 
		{ 0.81f, 1.93f, 2.13f }, { 2.24f, 1.51f, 2.19f }, { 0.04f, 0.31f, 1.86f }, { 0.15f, 2.09f, 1.88f }, { 0.28f, 1.37f, 2.03f }, { 2.18f, 2.19f, 0.56f }, { 0.48f, 1.43f, 0.76f }, { 1.19f, 0.03f, 1.63f }, { 1.65f, 2.37f, 0.96f }, 
		{ 1.49f, 2.49f, 1.77f }, { 0.13f, 2.23f, 1.06f }, { 1.78f, 0.81f, 1.23f }, { 1.54f, 0.87f, 0.09f }, { 1.66f, 0.98f, 0.98f }, { 2.48f, 2.28f, 1.99f }, { 0.26f, 0.95f, 0.68f }, { 0.41f, 1.69f, 2.05f }, { 2.29f, 1.90f, 1.95f }, 
		{ 0.57f, 1.85f, 0.63f }, { 1.36f, 0.78f, 1.37f }, { 2.25f, 1.72f, 0.06f }, { 0.58f, 1.35f, 2.13f }, { 1.22f, 0.78f, 0.54f }, { 0.68f, 2.49f, 1.74f }, { 2.01f, 1.84f, 0.87f }, { 1.66f, 2.30f, 1.43f }, { 1.35f, 0.49f, 0.48f }, 
		{ 0.47f, 1.08f, 1.19f }, { 1.90f, 1.64f, 2.37f }, { 1.48f, 2.05f, 1.38f }, { 0.72f, 1.55f, 1.77f }, { 0.46f, 1.68f, 2.18f }, { 0.18f, 2.40f, 2.49f }, { 0.96f, 0.84f, 2.36f }, { 2.51f, 1.89f, 0.87f }, { 2.17f, 1.49f, 0.09f }, 
		{ 0.97f, 0.48f, 1.09f }, { 0.40f, 0.88f, 1.45f }, { 1.81f, 0.75f, 0.53f }, { 1.00f, 0.72f, 1.86f }, { 1.81f, 1.88f, 2.45f }, { 2.10f, 0.81f, 1.28f }, { 0.26f, 0.73f, 1.13f }, { 2.04f, 1.17f, 1.86f }, { 2.21f, 1.49f, 0.56f }, 
		{ 0.93f, 1.16f, 1.40f }, { 0.91f, 0.21f, 0.32f }, { 0.69f, 0.54f, 1.77f }, { 0.09f, 1.53f, 0.79f }, { 1.07f, 0.09f, 0.73f }, { 1.13f, 1.81f, 0.97f }, { 2.27f, 2.54f, 0.54f }, { 1.70f, 2.34f, 1.33f }, { 0.15f, 0.14f, 0.49f }, 
		{ 1.51f, 0.98f, 1.83f }, { 1.36f, 2.15f, 0.21f }, { 1.36f, 1.54f, 0.81f }, { 2.11f, 0.11f, 0.22f }, { 0.13f, 0.84f, 1.71f }, { 0.13f, 2.27f, 2.23f }, { 0.36f, 1.56f, 1.44f }, { 0.13f, 0.55f, 2.02f }, { 0.10f, 2.07f, 1.62f }, 
		{ 1.79f, 0.17f, 1.78f }, { 0.79f, 2.31f, 0.07f }, { 0.98f, 2.04f, 0.13f }, { 0.01f, 1.09f, 2.51f }, { 2.04f, 0.30f, 0.74f }, { 0.11f, 2.35f, 0.24f }, { 1.54f, 1.66f, 1.82f }, { 0.24f, 1.73f, 1.08f }, { 1.64f, 2.07f, 0.72f }
	};

	for (int i = 0; i < NUMVERTEXNORMALS; i++)
	{
		float sy = sin (cl.time * avelocities[i][0]);
		float cy = cos (cl.time * avelocities[i][0]);
		float sp = sin (cl.time * avelocities[i][1]);
		float cp = cos (cl.time * avelocities[i][1]);

		vec3_t forward = { cp * cy, cp * sy, -sp };
		particle_t *p;

		if ((p = R_AllocParticle ()) == NULL)
			return;

		p->die = cl.time + 1; // PF_ONEFRAME will remove it
		p->color = 0x6f;
		p->type = pt_entparticles;

		p->org[0] = ent->origin[0] + avertexnormals[i][0] * dist + forward[0] * beamlength;
		p->org[1] = ent->origin[1] + avertexnormals[i][1] * dist + forward[1] * beamlength;
		p->org[2] = ent->origin[2] + avertexnormals[i][2] * dist + forward[2] * beamlength;

		p->flags |= PF_ONEFRAME;
	}
}


/*
===============
R_ReadPointFile_f
===============
*/
void R_ReadPointFile_f (void)
{
	FILE *f;
	vec3_t	org;
	int		r;
	int		c;
	particle_t *p;
	char	name[MAX_QPATH];

	if (cls.state != ca_connected)
		return;			// need an active map.

	q_snprintf (name, sizeof (name), "maps/%s.pts", cl.mapname);

	COM_FOpenFile (name, &f, NULL);
	if (!f)
	{
		Con_Printf ("couldn't open %s\n", name);
		return;
	}

	Con_Printf ("Reading %s...\n", name);
	c = 0;
	org[0] = org[1] = org[2] = 0; // silence pesky compiler warnings

	for (;; )
	{
		r = fscanf (f, "%f %f %f\n", &org[0], &org[1], &org[2]);
		if (r != 3)
			break;
		c++;

		if ((p = R_AllocParticle ()) == NULL)
		{
			Con_Printf ("Not enough free particles\n");
			break;
		}

		p->die = 99999;
		p->color = (-c) & 15;
		p->type = pt_static;

		VectorCopy (vec3_origin, p->vel);
		VectorCopy (org, p->org);

		p->flags |= PF_ETERNAL;
	}

	fclose (f);
	Con_Printf ("%i points read\n", c);
}

/*
===============
R_ParseParticleEffect

Parse an effect out of the server message
===============
*/
void R_ParseParticleEffect (void)
{
	vec3_t		org, dir;
	int			i, count, color;

	for (i = 0; i < 3; i++)
		org[i] = MSG_ReadCoord (cl.protocolflags);

	for (i = 0; i < 3; i++)
		dir[i] = MSG_ReadChar () * (1.0 / 16);

	count = MSG_ReadByte ();
	color = MSG_ReadByte ();

	if (count == 255)
		R_ParticleExplosion (org);
	else R_RunParticleEffect (org, dir, color, count);
}

/*
===============
R_ParticleExplosion
===============
*/
void R_ParticleExplosion (vec3_t org)
{
	int			i, j;
	particle_t *p;

	for (i = 0; i < 1024; i++)
	{
		if ((p = R_AllocParticle ()) == NULL)
			return;

		p->die = cl.time + 5;
		p->color = ramp1[0];
		p->ramp = rand () & 3;

		if (i & 1)
			p->type = pt_explode;
		else p->type = pt_explode2;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () % 32) - 16);
			p->vel[j] = (rand () % 512) - 256;
		}
	}
}

/*
===============
R_ParticleExplosion2
===============
*/
void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength)
{
	int			i, j;
	particle_t *p;
	int			colorMod = 0;

	for (i = 0; i < 512; i++)
	{
		if ((p = R_AllocParticle ()) == NULL)
			return;

		p->die = cl.time + 0.3;
		p->color = colorStart + (colorMod % colorLength);
		colorMod++;

		p->type = pt_blob;

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () % 32) - 16);
			p->vel[j] = (rand () % 512) - 256;
		}
	}
}

/*
===============
R_BlobExplosion
===============
*/
void R_BlobExplosion (vec3_t org)
{
	int			i, j;
	particle_t *p;

	for (i = 0; i < 1024; i++)
	{
		if ((p = R_AllocParticle ()) == NULL)
			return;

		p->die = cl.time + 1 + (rand () & 8) * 0.05;

		if (i & 1)
		{
			p->type = pt_blob;
			p->color = 66 + rand () % 6;
		}
		else
		{
			p->type = pt_blob2;
			p->color = 150 + rand () % 6;
		}

		for (j = 0; j < 3; j++)
		{
			p->org[j] = org[j] + ((rand () % 32) - 16);
			p->vel[j] = (rand () % 512) - 256;
		}
	}
}

/*
===============
R_RunParticleEffect
===============
*/
void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count)
{
	if (count == 1024)
	{
		R_ParticleExplosion (org);
		return;
	}
	else
	{
		particle_t *p;

		for (int i = 0; i < count; i++)
		{
			if ((p = R_AllocParticle ()) == NULL)
				return;

			p->die = cl.time + 0.1 * (rand () % 5);
			p->color = (color & ~7) + (rand () & 7);
			p->type = pt_slowgrav;

			for (int j = 0; j < 3; j++)
			{
				p->org[j] = org[j] + ((rand () & 15) - 8);
				p->vel[j] = dir[j] * 15;
			}
		}
	}
}


void R_SingleParticle (vec3_t org, int color, float size)
{
	// spawns a single particle at it's position that lasts for 1 frame - this is a gross hack to run AD particles through the particle system
	// without needing to write QC extensions or custom protocol stuff - so the client detects the AD particle sprites and pushes a particle
	// instead of drawing a sprite.
	particle_t *p;

	if ((p = R_AllocParticle ()) == NULL)
		return;

	p->die = cl.time + 1; // PF_ONEFRAME will remove it

	for (int i = 0; i < 3; i++)
	{
		p->org[i] = org[i];
		p->vel[i] = 0;
	}

	p->type = pt_static;
	p->color = color;
	p->size = size;

	p->flags |= PF_ONEFRAME;
}


/*
===============
R_LavaSplash
===============
*/
void R_LavaSplash (vec3_t org)
{
	int			i, j, k;
	particle_t *p;
	float		vel;
	vec3_t		dir;

	for (i = -16; i < 16; i++)
	{
		for (j = -16; j < 16; j++)
		{
			for (k = 0; k < 1; k++)
			{
				if ((p = R_AllocParticle ()) == NULL)
					return;

				p->die = cl.time + 2 + (rand () & 31) * 0.02;
				p->color = 224 + (rand () & 7);
				p->type = pt_slowgrav;

				dir[0] = j * 8 + (rand () & 7);
				dir[1] = i * 8 + (rand () & 7);
				dir[2] = 256;

				p->org[0] = org[0] + dir[0];
				p->org[1] = org[1] + dir[1];
				p->org[2] = org[2] + (rand () & 63);

				VectorNormalize (dir);
				vel = 50 + (rand () & 63);
				VectorScale (dir, vel, p->vel);
			}
		}
	}
}

/*
===============
R_TeleportSplash
===============
*/
void R_TeleportSplash (vec3_t org)
{
	int			i, j, k;
	particle_t *p;
	float		vel;
	vec3_t		dir;

	for (i = -16; i < 16; i += 4)
	{
		for (j = -16; j < 16; j += 4)
		{
			for (k = -24; k < 32; k += 4)
			{
				if ((p = R_AllocParticle ()) == NULL)
					return;

				p->die = cl.time + 0.2 + (rand () & 7) * 0.02;
				p->color = 7 + (rand () & 7);
				p->type = pt_slowgrav;

				dir[0] = j * 8;
				dir[1] = i * 8;
				dir[2] = k * 8;

				p->org[0] = org[0] + i + (rand () & 3);
				p->org[1] = org[1] + j + (rand () & 3);
				p->org[2] = org[2] + k + (rand () & 3);

				VectorNormalize (dir);
				vel = 50 + (rand () & 63);
				VectorScale (dir, vel, p->vel);
			}
		}
	}
}


/*
===============
R_RocketTrail

FIXME -- rename function and use #defined types instead of numbers
===============
*/
void R_RocketTrail (vec3_t start, vec3_t end, int type)
{
	vec3_t		vec;
	float		len;
	int			j;
	particle_t *p;
	int			dec;
	static int	tracercount;

	VectorSubtract (end, start, vec);
	len = VectorNormalize (vec);

	if (type < 128)
		dec = 3;
	else
	{
		dec = 1;
		type -= 128;
	}

	while (len > 0)
	{
		len -= dec;

		if ((p = R_AllocParticle ()) == NULL)
			return;

		VectorCopy (vec3_origin, p->vel);
		p->die = cl.time + 2;

		switch (type)
		{
		case RT_ROCKETTRAIL:	// rocket trail
			p->ramp = (rand () & 3);
			p->color = ramp3[(int) p->ramp];
			p->type = pt_fire;
			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () % 6) - 3);
			break;

		case RT_SMOKESMOKE:	// smoke smoke
			p->ramp = (rand () & 3) + 2;
			p->color = ramp3[(int) p->ramp];
			p->type = pt_fire;
			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () % 6) - 3);
			break;

		case RT_BLOOD:	// blood
			p->type = pt_grav;
			p->color = 67 + (rand () & 3);
			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () % 6) - 3);
			break;

		case RT_TRACER3:
		case RT_TRACER5:	// tracer
			p->die = cl.time + 0.5;
			p->type = pt_static;
			if (type == 3)
				p->color = 52 + ((tracercount & 4) << 1);
			else
				p->color = 230 + ((tracercount & 4) << 1);

			tracercount++;

			VectorCopy (start, p->org);
			if (tracercount & 1)
			{
				p->vel[0] = 30 * vec[1];
				p->vel[1] = 30 * -vec[0];
			}
			else
			{
				p->vel[0] = 30 * -vec[1];
				p->vel[1] = 30 * vec[0];
			}
			break;

		case RT_SLIGHTBLOOD:	// slight blood
			p->type = pt_grav;
			p->color = 67 + (rand () & 3);
			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () % 6) - 3);
			len -= 3;
			break;

		case RT_VOORTRAIL:	// voor trail
			p->color = 9 * 16 + 8 + (rand () & 3);
			p->type = pt_static;
			p->die = cl.time + 0.3;
			for (j = 0; j < 3; j++)
				p->org[j] = start[j] + ((rand () & 15) - 8);
			break;
		}

		VectorAdd (start, vec, start);
	}
}


/*
===============
R_DrawParticles

===============
*/
void R_DrawParticlesARB (void)
{
	if (!r_particles.value)
		return;

	// remove expired particles from the front of the list
	for (;; )
	{
		particle_t *kill = active_particles;

		if (kill && kill->die < cl.time)
		{
			active_particles = kill->next;
			kill->next = free_particles;
			free_particles = kill;
			continue;
		}

		break;
	}

	// ericw -- avoid empty glBegin(),glEnd() pair below; causes issues on AMD
	if (!active_particles)
		return;

	// update particle accelerations
	for (int i = 0; i < MAX_PARTICLE_TYPES; i++)
	{
		extern cvar_t sv_gravity;
		ptypedef_t *pt = &p_typedefs[i];
		float grav = sv_gravity.value * 0.05;

		// in theory this could be calced once and never again, but in practice mods may change sv_gravity from frame-to-frame
		// so we need to recalc it each frame too....
		pt->accel[0] = pt->dvel[0] + (pt->grav[0] * grav);
		pt->accel[1] = pt->dvel[1] + (pt->grav[1] * grav);
		pt->accel[2] = pt->dvel[2] + (pt->grav[2] * grav);
	}

	// set programs and particle size adjusted for type
	switch ((int) r_particles.value)
	{
	case 1:
		GL_BindPrograms (r_particle_vp, r_particle_fp_circle);
		glProgramEnvParameter4fARB (GL_VERTEX_PROGRAM_ARB, 1, 0.666f, 0.002f, 1.0f, 0.0f);
		break;

	default:
		GL_BindPrograms (r_particle_vp, r_particle_fp_square);
		glProgramEnvParameter4fARB (GL_VERTEX_PROGRAM_ARB, 1, 0.5f, 0.002f, 1.0f, 0.0f);
		break;
	}

	// set up states for particle drawing
	GL_DepthState (GL_TRUE, GL_LEQUAL, GL_FALSE);
	GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// ensure that no buffer is bound when drawing particles
	GL_BindBuffer (GL_ARRAY_BUFFER, 0);

	// no arrays in immediate mode
	GL_EnableVertexAttribArrays (0);

	// dynamic unbounded data is more easily handled in immediate mode
	glBegin (GL_QUADS);

	for (particle_t *p = active_particles; p; p = p->next)
	{
		// remove expired particles from the middle of the list
		for (;; )
		{
			particle_t *kill = p->next;

			if (kill && kill->die < cl.time)
			{
				p->next = kill->next;
				kill->next = free_particles;
				free_particles = kill;
				continue;
			}

			break;
		}

		// get the emitter properties for this particle
		ptypedef_t *pt = &p_typedefs[p->type];
		float etime = cl.time - p->time;

		// update colour ramps
		if (pt->ramp)
		{
			int ramp = (int) (p->ramp + (etime * pt->ramptime));

			// set dead particles to full-alpha and the system will remove them on the next frame
			if (ramp > 8)
			{
				p->color = 0xff;
				p->die = -1;
			}
			else if ((p->color = pt->ramp[ramp]) == 0xff)
				p->die = -1;
		}

		// size
		glVertexAttrib1f (6, p->size);

		// movement factors
		glVertexAttrib1f  (5, etime);
		glVertexAttrib3fv (4, pt->accel);
		glVertexAttrib3fv (3, p->vel);
		glVertexAttrib3fv (2, p->org);

		// colour
		glVertexAttrib4Nubv (1, (byte *) &d_8to24table[p->color & 255]);

		// billboard quad corners
		glVertexAttrib2f (0, -1, -1);
		glVertexAttrib2f (0, -1, 1);
		glVertexAttrib2f (0, 1, 1);
		glVertexAttrib2f (0, 1, -1);

		// run particle flags
		if (p->flags & PF_ONEFRAME) p->die = -1;
		if (p->flags & PF_ETERNAL) p->die = cl.time + 1;

		// count for r_speeds
		rs_particles++;
	}

	glEnd ();
}


