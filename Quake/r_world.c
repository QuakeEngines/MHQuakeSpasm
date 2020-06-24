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
// r_world.c: world model rendering

#include "quakedef.h"

extern cvar_t gl_fullbrights, gl_overbright, r_oldskyleaf; // johnfitz

byte *SV_FatPVS (vec3_t org, qmodel_t *worldmodel);

int vis_changed; // if true, force pvs to be refreshed

// ==============================================================================
// SETUP CHAINS
// ==============================================================================

/*
================
R_ClearTextureChains -- ericw

clears texture chains for all textures used by the given model, and also
clears the lightmap chains
================
*/
void R_ClearTextureChains (qmodel_t *mod, texchain_t chain)
{
	// set all chains to null
	for (int i = 0; i < mod->numtextures; i++)
		if (mod->textures[i])
			mod->textures[i]->texturechains[chain] = NULL;

	// clear lightmap chains
	for (int i = 0; i < lightmap_count; i++)
		lightmap[i].polys = NULL;
}

/*
================
R_ChainSurface -- ericw -- adds the given surface to its texture chain
================
*/
void R_ChainSurface (msurface_t *surf, texchain_t chain)
{
	surf->texturechain = surf->texinfo->texture->texturechains[chain];
	surf->texinfo->texture->texturechains[chain] = surf;
}

/*
===============
R_MarkSurfaces -- johnfitz -- mark surfaces based on PVS and rebuild texture chains
===============
*/
void R_MarkSurfaces (void)
{
	byte *vis;
	mleaf_t *leaf;
	mnode_t *node;
	msurface_t *surf, **mark;
	int			i, j;
	qboolean	nearwaterportal;

	// clear lightmap chains
	for (i = 0; i < lightmap_count; i++)
		lightmap[i].polys = NULL;

	// check this leaf for water portals
	// TODO: loop through all water surfs and use distance to leaf cullbox
	nearwaterportal = false;
	for (i = 0, mark = r_viewleaf->firstmarksurface; i < r_viewleaf->nummarksurfaces; i++, mark++)
		if ((*mark)->flags & SURF_DRAWTURB)
			nearwaterportal = true;

	// choose vis data
	if (r_novis.value || r_viewleaf->contents == CONTENTS_SOLID || r_viewleaf->contents == CONTENTS_SKY)
		vis = Mod_NoVisPVS (cl.worldmodel);
	else if (nearwaterportal)
		vis = SV_FatPVS (r_origin, cl.worldmodel);
	else
		vis = Mod_LeafPVS (r_viewleaf, cl.worldmodel);

	// if surface chains don't need regenerating, just add static entities and return
	if (r_oldviewleaf == r_viewleaf && !vis_changed && !nearwaterportal)
	{
		leaf = &cl.worldmodel->leafs[1];
		for (i = 0; i < cl.worldmodel->numleafs; i++, leaf++)
			if (vis[i >> 3] & (1 << (i & 7)))
				if (leaf->efrags)
					R_StoreEfrags (&leaf->efrags);
		return;
	}

	vis_changed = false;
	r_visframecount++;
	r_oldviewleaf = r_viewleaf;

	// iterate through leaves, marking surfaces
	leaf = &cl.worldmodel->leafs[1];
	for (i = 0; i < cl.worldmodel->numleafs; i++, leaf++)
	{
		if (vis[i >> 3] & (1 << (i & 7)))
		{
			if (r_oldskyleaf.value || leaf->contents != CONTENTS_SKY)
				for (j = 0, mark = leaf->firstmarksurface; j < leaf->nummarksurfaces; j++, mark++)
					(*mark)->visframe = r_visframecount;

			// add static models
			if (leaf->efrags)
				R_StoreEfrags (&leaf->efrags);
		}
	}

	// set all chains to null
	for (i = 0; i < cl.worldmodel->numtextures; i++)
		if (cl.worldmodel->textures[i])
			cl.worldmodel->textures[i]->texturechains[chain_world] = NULL;

	// rebuild chains

#if 1
	// iterate through surfaces one node at a time to rebuild chains
	// need to do it this way if we want to work with tyrann's skip removal tool
	// becuase his tool doesn't actually remove the surfaces from the bsp surfaces lump
	// nor does it remove references to them in each leaf's marksurfaces list
	for (i = 0, node = cl.worldmodel->nodes; i < cl.worldmodel->numnodes; i++, node++)
		for (j = 0, surf = &cl.worldmodel->surfaces[node->firstsurface]; j < node->numsurfaces; j++, surf++)
			if (surf->visframe == r_visframecount)
			{
				R_ChainSurface (surf, chain_world);
			}
#else
	// the old way
	surf = &cl.worldmodel->surfaces[cl.worldmodel->firstmodelsurface];
	for (i = 0; i < cl.worldmodel->nummodelsurfaces; i++, surf++)
	{
		if (surf->visframe == r_visframecount)
		{
			R_ChainSurface (surf, chain_world);
		}
	}
#endif
}

/*
================
R_BackFaceCull -- johnfitz -- returns true if the surface is facing away from vieworg
================
*/
qboolean R_BackFaceCull (msurface_t *surf)
{
	double dot;

	switch (surf->plane->type)
	{
	case PLANE_X:
		dot = r_refdef.vieworg[0] - surf->plane->dist;
		break;
	case PLANE_Y:
		dot = r_refdef.vieworg[1] - surf->plane->dist;
		break;
	case PLANE_Z:
		dot = r_refdef.vieworg[2] - surf->plane->dist;
		break;
	default:
		dot = DotProduct (r_refdef.vieworg, surf->plane->normal) - surf->plane->dist;
		break;
	}

	if ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK))
		return true;

	return false;
}

/*
================
R_CullSurfaces -- johnfitz
================
*/
void R_CullSurfaces (void)
{
	msurface_t *s;
	int i;
	texture_t *t;

	// ericw -- instead of testing (s->visframe == r_visframecount) on all world
	// surfaces, use the chained surfaces, which is exactly the same set of sufaces
	for (i = 0; i < cl.worldmodel->numtextures; i++)
	{
		t = cl.worldmodel->textures[i];

		if (!t || !t->texturechains[chain_world])
			continue;

		for (s = t->texturechains[chain_world]; s; s = s->texturechain)
		{
			if (R_CullBox (s->mins, s->maxs) || R_BackFaceCull (s))
				s->culled = true;
			else
			{
				s->culled = false;
				rs_brushpolys++; // count wpolys here
			}
		}
	}
}

/*
================
R_BuildLightmapChains

ericw -- now always used at the start of R_DrawTextureChains for the
mh dynamic lighting speedup
================
*/
void R_BuildLightmapChains (qmodel_t *model, texchain_t chain)
{
	texture_t *t;
	msurface_t *s;
	int i;

	// clear lightmap chains (already done in r_marksurfaces, but clearing them here to be safe becuase of r_stereo)
	for (i = 0; i < lightmap_count; i++)
		lightmap[i].polys = NULL;

	// now rebuild them
	for (i = 0; i < model->numtextures; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain])
			continue;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
			if (!s->culled)
				R_RenderDynamicLightmaps (s);
	}
}

// ==============================================================================
// DRAW CHAINS
// ==============================================================================

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
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
		glColor4f (1, 1, 1, entalpha);
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
		glTexEnvf (GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
		glColor3f (1, 1, 1);
	}
}


// ==============================================================================
// VBO SUPPORT
// ==============================================================================

static unsigned int R_NumTriangleIndicesForSurf (msurface_t *s)
{
	return 3 * (s->numedges - 2);
}


/*
================
R_TriangleIndicesForSurf

Writes out the triangle indices needed to draw s as a triangle list.
The number of indices it will write is given by R_NumTriangleIndicesForSurf.
================
*/
static void R_TriangleIndicesForSurf (msurface_t *s, unsigned int *dest)
{
	for (int i = 2; i < s->numedges; i++, dest += 3)
	{
		dest[0] = s->vbo_firstvert;
		dest[1] = s->vbo_firstvert + i - 1;
		dest[2] = s->vbo_firstvert + i;
	}
}


#define MAX_BATCH_SIZE 32768

static unsigned int vbo_indices[MAX_BATCH_SIZE];
static unsigned int num_vbo_indices;

/*
================
R_ClearBatch
================
*/
static void R_ClearBatch ()
{
	num_vbo_indices = 0;
}

/*
================
R_FlushBatch

Draw the current batch if non-empty and clears it, ready for more R_BatchSurface calls.
================
*/
static void R_FlushBatch ()
{
	if (num_vbo_indices > 0)
	{
		glDrawElements (GL_TRIANGLES, num_vbo_indices, GL_UNSIGNED_INT, vbo_indices);
		num_vbo_indices = 0;
	}
}

/*
================
R_BatchSurface

Add the surface to the current batch, or just draw it immediately if we're not
using VBOs.
================
*/
static void R_BatchSurface (msurface_t *s)
{
	int num_surf_indices = R_NumTriangleIndicesForSurf (s);

	if (num_vbo_indices + num_surf_indices >= MAX_BATCH_SIZE)
		R_FlushBatch ();

	R_TriangleIndicesForSurf (s, &vbo_indices[num_vbo_indices]);
	num_vbo_indices += num_surf_indices;
}


/*
================
R_DrawTextureChains_NoTexture -- johnfitz

draws surfs whose textures were missing from the BSP
================
*/
void R_DrawTextureChains_NoTexture (qmodel_t *model, texchain_t chain)
{
	int			i;
	msurface_t *s;
	texture_t *t;
	qboolean	bound;

	for (i = 0; i < model->numtextures; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_NOTEXTURE))
			continue;

		bound = false;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
			if (!s->culled)
			{
				if (!bound) // only bind once we are sure we need this texture
				{
					GL_Bind (t->gltexture);
					bound = true;
				}
				DrawGLPoly (s->polys);
				rs_brushpasses++;
			}
	}
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
================
R_DrawTextureChains_Water -- johnfitz
================
*/
void R_DrawTextureChains_Water (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i;
	msurface_t *s;
	texture_t *t;
	qboolean	bound;
	float entalpha;

	for (i = 0; i < model->numtextures; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
			continue;

		bound = false;
		entalpha = 1.0f;

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!s->culled)
			{
				if (!bound) // only bind once we are sure we need this texture
				{
					entalpha = GL_WaterAlphaForEntitySurface (ent, s);

					R_BeginTransparentDrawing (entalpha);
					GL_Bind (t->gltexture);

					bound = true;
				}

				DrawGLPoly (s->polys);
				rs_brushpasses++;
			}
		}

		R_EndTransparentDrawing (entalpha);
	}
}


static GLuint r_world_vp = 0;
static GLuint r_world_fp[2] = { 0, 0 }; // luma, no luma

/*
=============
GLWorld_CreateShaders
=============
*/
void GLWorld_CreateShaders (void)
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
		"# copy over diffuse texcoord\n"
		"MOV result.texcoord[0], vertex.attrib[1];\n"
		"\n"
		"# copy over lightmap texcoord\n"
		"MOV result.texcoord[1], vertex.attrib[2];\n"
		"\n"
		"# set up fog coordinate\n"
		"DP4 result.fogcoord.x, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_source0 = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff, lmap, fence;\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"TEX lmap, fragment.texcoord[1], texture[1], 2D;\n"
		"\n"
		"# fence texture test\n"
		"SUB fence, diff, 0.666;\n"
		"KIL fence.a;\n"
		"\n"
		"# perform the lightmapping\n"
		"MUL diff.rgb, diff, lmap;\n"
		"MUL diff.rgb, diff, program.env[0]; # overbright factor\n"
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
		"MOV result.color.a, program.env[0].a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_source1 = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff, lmap, luma, fence;\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"TEX lmap, fragment.texcoord[1], texture[1], 2D;\n"
		"TEX luma, fragment.texcoord[0], texture[2], 2D;\n"
		"\n"
		"# fence texture test\n"
		"SUB fence, diff, 0.666;\n"
		"KIL fence.a;\n"
		"\n"
		"# perform the lightmapping\n"
		"MUL diff.rgb, diff, lmap;\n"
		"MUL diff.rgb, diff, program.env[0]; # overbright factor\n"
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
		"# copy over the result\n"
		"MOV result.color.rgb, diff;\n"
		"\n"
		"# set the alpha channel correctly\n"
		"MOV result.color.a, program.env[0].a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_world_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_source);
	r_world_fp[0] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_source0);
	r_world_fp[1] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_source1);
}


extern GLuint gl_bmodel_vbo;

/*
================
R_DrawTextureChains_GLSL -- ericw

Draw lightmapped surfaces with fulbrights in one pass, using VBO.
Requires 3 TMUs, OpenGL 2.0
================
*/
void R_DrawTextureChains_ARB (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	int			i;
	msurface_t *s;
	texture_t *t;
	qboolean	bound;
	int		lastlightmap;
	gltexture_t *fullbright = NULL;
	float		entalpha;

	entalpha = (ent != NULL) ? ENTALPHA_DECODE (ent->alpha) : 1.0f;

	// enable blending / disable depth writes
	if (entalpha < 1)
	{
		glDepthMask (GL_FALSE);
		glEnable (GL_BLEND);
	}

	glEnable (GL_VERTEX_PROGRAM_ARB);
	glEnable (GL_FRAGMENT_PROGRAM_ARB);

	// setting the vertex program here; the fragment program will be set later
	glBindProgramARB (GL_VERTEX_PROGRAM_ARB, r_world_vp);

	// overbright
	glProgramEnvParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 0, gl_overbright.value + 1.0f, gl_overbright.value + 1.0f, gl_overbright.value + 1.0f, entalpha);

	// Bind the buffers
	GL_BindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
	GL_BindBuffer (GL_ELEMENT_ARRAY_BUFFER, 0); // indices come from client memory!

	glEnableVertexAttribArray (0);
	glEnableVertexAttribArray (1);
	glEnableVertexAttribArray (2);

	glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof (float), ((float *) 0));
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof (float), ((float *) 0) + 3);
	glVertexAttribPointer (2, 2, GL_FLOAT, GL_FALSE, VERTEXSIZE * sizeof (float), ((float *) 0) + 5);

	for (i = 0; i < model->numtextures; i++)
	{
		t = model->textures[i];

		if (!t || !t->texturechains[chain] || t->texturechains[chain]->flags & (SURF_DRAWTILED | SURF_NOTEXTURE))
			continue;

		// Enable/disable TMU 2 (fullbrights)
		// FIXME: Move below to where we bind GL_TEXTURE0
		if (gl_fullbrights.value && (fullbright = R_TextureAnimation (t, ent != NULL ? ent->frame : 0)->fullbright))
		{
			GL_SelectTexture (GL_TEXTURE2);
			GL_Bind (fullbright);
			glBindProgramARB (GL_FRAGMENT_PROGRAM_ARB, r_world_fp[1]);
		}
		else glBindProgramARB (GL_FRAGMENT_PROGRAM_ARB, r_world_fp[0]);

		R_ClearBatch ();

		bound = false;
		lastlightmap = 0; // avoid compiler warning

		for (s = t->texturechains[chain]; s; s = s->texturechain)
		{
			if (!s->culled)
			{
				if (!bound) // only bind once we are sure we need this texture
				{
					GL_SelectTexture (GL_TEXTURE0);
					GL_Bind ((R_TextureAnimation (t, ent != NULL ? ent->frame : 0))->gltexture);

					bound = true;
					lastlightmap = s->lightmaptexturenum;
				}

				if (s->lightmaptexturenum != lastlightmap)
					R_FlushBatch ();

				GL_SelectTexture (GL_TEXTURE1);
				GL_Bind (lightmap[s->lightmaptexturenum].texture);
				lastlightmap = s->lightmaptexturenum;
				R_BatchSurface (s);

				rs_brushpasses++;
			}
		}

		R_FlushBatch ();
	}

	// clean up
	glDisableVertexAttribArray (0);
	glDisableVertexAttribArray (1);
	glDisableVertexAttribArray (2);

	glBindProgramARB (GL_VERTEX_PROGRAM_ARB, 0);
	glBindProgramARB (GL_FRAGMENT_PROGRAM_ARB, 0);

	glDisable (GL_VERTEX_PROGRAM_ARB);
	glDisable (GL_FRAGMENT_PROGRAM_ARB);

	GL_SelectTexture (GL_TEXTURE0);

	if (entalpha < 1)
	{
		glDepthMask (GL_TRUE);
		glDisable (GL_BLEND);
	}
}


/*
=============
R_DrawWorld -- johnfitz -- rewritten
=============
*/
void R_DrawTextureChains (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	float entalpha;

	if (ent != NULL)
		entalpha = ENTALPHA_DECODE (ent->alpha);
	else
		entalpha = 1;

	// ericw -- the mh dynamic lightmap speedup: make a first pass through all
	// surfaces we are going to draw, and rebuild any lightmaps that need it.
	// the previous implementation of the speedup uploaded lightmaps one frame
	// late which was visible under some conditions, this method avoids that.
	R_BuildLightmapChains (model, chain);
	R_UploadLightmaps ();

	R_BeginTransparentDrawing (entalpha);

	R_DrawTextureChains_NoTexture (model, chain);

	// OpenGL 2 fast path
	R_EndTransparentDrawing (entalpha);
	R_DrawTextureChains_ARB (model, ent, chain);
}


/*
=============
R_DrawWorld -- ericw -- moved from R_DrawTextureChains, which is no longer specific to the world.
=============
*/
void R_DrawWorld (void)
{
	R_DrawTextureChains (cl.worldmodel, NULL, chain_world);
}

/*
=============
R_DrawWorld_Water -- ericw -- moved from R_DrawTextureChains_Water, which is no longer specific to the world.
=============
*/
void R_DrawWorld_Water (void)
{
	R_DrawTextureChains_Water (cl.worldmodel, NULL, chain_world);
}


