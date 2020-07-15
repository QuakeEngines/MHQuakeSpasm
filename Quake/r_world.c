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

extern cvar_t gl_fullbrights; // johnfitz

int vis_changed;


static GLuint r_brush_lightmapped_vp = 0;
static GLuint r_brush_lightmapped_fp[8] = { 0 };

static GLuint r_brush_dynamic_vp = 0;
static GLuint r_brush_dynamic_fp = 0;

static GLuint r_brush_notexture_vp = 0;
static GLuint r_brush_notexture_fp = 0;

static GLuint r_brush_drawflat_vp = 0;
static GLuint r_brush_drawflat_fp = 0;

/*
=============
GLWorld_CreateShaders
=============
*/
void GLWorld_CreateShaders (void)
{
	const GLchar *vp_lightmapped_source = \
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
		"# copy over drawflat colour\n"
		"MOV result.color, vertex.attrib[4];\n"
		"\n"
		"# set up fog coordinate\n"
		"DP4 result.fogcoord.x, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_lightmapped_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff, lmap, luma, fence;\n"
		"TEMP lmr, lmg, lmb;\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"TEX luma, fragment.texcoord[0], texture[1], 2D;\n"
		"\n"
		"# fence texture test\n"
		"SUB fence, diff, 0.666;\n"
		"KIL fence.a;\n"
		"\n"
		"# read the lightmaps\n"
		"TEX lmr, fragment.texcoord[1], texture[2], 2D;\n"
		"TEX lmg, fragment.texcoord[1], texture[3], 2D;\n"
		"TEX lmb, fragment.texcoord[1], texture[4], 2D;\n"
		"\n"
		"# apply the lightstyles\n"
		"DP4 lmap.r, lmr, program.local[0];\n"
		"DP4 lmap.g, lmg, program.local[0];\n"
		"DP4 lmap.b, lmb, program.local[0];\n"
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

	const GLchar *vp_dynamic_source = \
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
		"# copy over the normal in texcoord[1] so that we can do per-pixel lighting\n"
		"MOV result.texcoord[1], vertex.attrib[3];\n"
		"\n"
		"# result.texcoord[2] is light vector\n"
		"SUB result.texcoord[2], program.local[1], vertex.attrib[0];\n"
		"\n"
		"# set up fog coordinate\n"
		"DP4 result.fogcoord.x, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *vp_notexture_source = \
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
		"# set up fog coordinate\n"
		"DP4 result.fogcoord.x, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_brush_lightmapped_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, GL_GetVertexProgram (vp_lightmapped_source, SHADERFLAG_NONE));

	for (int shaderflag = 0; shaderflag < 8; shaderflag++)
		r_brush_lightmapped_fp[shaderflag] = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetFragmentProgram (fp_lightmapped_source, shaderflag));

	r_brush_dynamic_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_dynamic_source);
	r_brush_dynamic_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetDynamicLightFragmentProgramSource ());

	r_brush_notexture_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_notexture_source);
	r_brush_notexture_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetFullbrightFragmentProgramSource ());

	r_brush_drawflat_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, GL_GetVertexProgram (vp_lightmapped_source, SHADERFLAG_DRAWFLAT));
	r_brush_drawflat_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, GL_GetDrawflatFragmentProgramSource ());
}


// ==============================================================================
// SETUP CHAINS
// ==============================================================================

/*
================
R_ClearTextureChains -- ericw

clears texture chains for all textures used by the given model
================
*/
void R_ClearTextureChains (qmodel_t *mod, texchain_t chain)
{
	// set all chains to null
	for (int i = 0; i < mod->numtextures; i++)
	{
		if (!mod->textures[i]) continue;
		mod->textures[i]->texturechains[chain] = NULL;
	}
}


/*
================
R_ChainSurface -- ericw -- adds the given surface to its texture chain
================
*/
void R_ChainSurface (msurface_t *surf, texchain_t chain)
{
	extern int r_dlightframecount;

	// marks this surf as having been seen in this dlight frame
	surf->dlightframe = r_dlightframecount;

	// and chain it normally
	surf->texturechain = surf->texinfo->texture->texturechains[chain];
	surf->texinfo->texture->texturechains[chain] = surf;
}


// ==============================================================================
// DRAW CHAINS
// ==============================================================================

void R_DrawLightmappedChain (msurface_t *s, texture_t *t)
{
	int shaderflag = R_SelectTexturesAndShaders (t->gltexture, t->fullbright, s->flags & SURF_DRAWFENCE);

	// bind the selected programs
	GL_BindPrograms (r_brush_lightmapped_vp, r_brush_lightmapped_fp[shaderflag]);

	// beginning with an empty batch
	R_ClearBatch ();

	// set these up so that they will trigger a set first time it's seen
	for (unsigned oldstyle = ~s->fullstyle, currentlightmap = ~s->lightmaptexturenum; s; s = s->texturechain)
	{
		// check for lightmap change
		if (s->lightmaptexturenum != currentlightmap)
		{
			R_FlushBatch (); // first time through there will be nothing in the batch

			// bind all the lightmaps
			GL_BindLightmaps (s->lightmaptexturenum);

			// store back
			currentlightmap = s->lightmaptexturenum;
		}

		// check for lightstyle change
		if (s->fullstyle != oldstyle)
		{
			R_FlushBatch (); // first time through there will be nothing in the batch

			// build the new style
			GL_SetSurfaceStyles (s);

			// store back
			oldstyle = s->fullstyle;
		}

		// batch this surface
		R_BatchSurface (s);
	}

	// draw the batch
	R_FlushBatch ();
}


void R_DrawSimpleTexturechain (msurface_t *s)
{
	R_ClearBatch ();

	for (; s; s = s->texturechain)
	{
		R_BatchSurface (s);
		rs_brushpolys++;
	}

	R_FlushBatch ();
}


void R_DrawNoTextureChain (msurface_t *s, texture_t *t)
{
	if (r_lightmap_cheatsafe)
		GL_BindTexture (GL_TEXTURE0, whitetexture);
	else GL_BindTexture (GL_TEXTURE0, t->gltexture);

	GL_BindPrograms (r_brush_notexture_vp, r_brush_notexture_fp);

	R_DrawSimpleTexturechain (s);
}


void R_DrawDlightChains (qmodel_t *model, entity_t *ent, dlight_t *dl)
{
	// switch to additive blending
	GL_DepthState (GL_TRUE, GL_EQUAL, GL_FALSE);
	GL_BlendState (GL_TRUE, GL_ONE, GL_ONE);

	// dynamic light programs
	GL_BindPrograms (r_brush_dynamic_vp, r_brush_dynamic_fp);

	// light properties
	GL_SetupDynamicLight (dl);

	// and draw them
	for (int i = 0; i < model->numtextures; i++)
	{
		// fixme - make this never happen!!!
		if (!model->textures[i]) continue;

		texture_t *t = model->textures[i];
		msurface_t *s = t->texturechains[chain_dlight];

		if (!s) continue;

		if (r_lightmap_cheatsafe)
			GL_BindTexture (GL_TEXTURE0, greytexture);
		else
		{
			texture_t *anim = R_TextureAnimation (t, ent != NULL ? ent->frame : 0);
			GL_BindTexture (GL_TEXTURE0, anim->gltexture);
		}

		R_DrawSimpleTexturechain (s);
	}
}


/*
=============
R_DrawWorld -- johnfitz -- rewritten
=============
*/
void R_DrawTextureChains (qmodel_t *model, entity_t *ent, QMATRIX *localMatrix, texchain_t chain)
{
	// entity alpha
	float		alpha = (ent != NULL) ? ENTALPHA_DECODE (ent->alpha) : 1.0f;

	// enable blending / disable depth writes
	R_BeginTransparentDrawing (alpha);

	R_SetupWorldVBOState ();

	for (int i = 0; i < model->numtextures; i++)
	{
		// fixme - make this never happen!!!
		if (!model->textures[i]) continue;

		texture_t *t = model->textures[i];
		msurface_t *s = t->texturechains[chain];

		// select the proper shaders
		if (!s)
		{
			// nothing was chained
			continue;
		}
		else if (r_drawflat_cheatsafe)
		{
			GL_BindPrograms (r_brush_drawflat_vp, r_brush_drawflat_fp);
			R_DrawSimpleTexturechain (s);
		}
		else if (s->flags & SURF_NOTEXTURE)
		{
			// special handling for missing textures
			R_DrawNoTextureChain (s, t);
		}
		else if (s->flags & SURF_DRAWSKY)
		{
			// sky; either layers or cubemap
			R_DrawSkychain_ARB (s);
		}
		else if (!cl.worldmodel->lightdata || r_fullbright_cheatsafe)
		{
			// the no lightdata case just draws the same as the notexture case, but should animate the texture
			R_DrawNoTextureChain (s, R_TextureAnimation (t, ent != NULL ? ent->frame : 0));
		}
		else if (!(s->flags & SURF_DRAWTURB))
		{
			// normal lightmapped surface - turbs are drawn separately because of alpha
			R_DrawLightmappedChain (s, R_TextureAnimation (t, ent != NULL ? ent->frame : 0));
		}
	}

	// set up dynamic lighting correctly for the entity type
	if (alpha < 1)
		;		// translucents don't have dlights (light goes through them!)
	else if (!r_dynamic.value)
		;		// dlights switched off
	else if (!cl.worldmodel->lightdata || r_fullbright_cheatsafe || r_drawflat_cheatsafe)
		;		// no light data
	else if (!ent)
		R_PushDlights_New (NULL, NULL, model, cl.worldmodel->nodes);
	else if (model->firstmodelsurface != 0)
		R_PushDlights_New (ent, localMatrix, model, model->nodes + model->hulls[0].firstclipnode);
	else R_PushDlights_New (ent, localMatrix, model, model->nodes);
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


/*
================
R_RecursiveWorldNode
================
*/
void R_RecursiveWorldNode (mnode_t *node, int clipflags)
{
	int			c, side;
	double		dot;

	if (node->contents == CONTENTS_SOLID) return;		// solid
	if (r_novis.value) node->visframe = r_visframecount;
	if (node->visframe != r_visframecount) return;

	if (clipflags)
	{
		for (c = 0; c < 4; c++)
		{
			// don't need to clip against it
			if (!(clipflags & (1 << c))) continue;

			side = BoxOnPlaneSide (node->minmaxs, &node->minmaxs[3], &frustum[c]);

			if (side == 1) clipflags &= ~(1 << c);	// node is entirely on screen
			if (side == 2) return;	// node is entirely off screen
		}
	}

	// if a leaf node, draw stuff
	if (node->contents < 0)
	{
		mleaf_t *pleaf = (mleaf_t *) node;
		msurface_t **mark = pleaf->firstmarksurface;

		if ((c = pleaf->nummarksurfaces) > 0)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			} while (--c);
		}

		// deal with model fragments in this leaf
		if (pleaf->efrags) R_StoreEfrags (&pleaf->efrags);

		return;
	}

	// node is just a decision point, so go down the apropriate sides
	// find which side of the node we are on
	if ((dot = Mod_PlaneDist (node->plane, r_refdef.vieworg)) >= 0)
		side = 0;
	else side = 1;

	// recurse down the children, front side first
	R_RecursiveWorldNode (node->children[side], clipflags);

	// draw stuff
	if ((c = node->numsurfaces) > 0)
	{
		for (msurface_t *surf = node->surfaces; c; c--, surf++)
		{
			// do fast reject cases first
			if (surf->visframe != r_framecount) continue;
			if ((dot < 0) ^ !!(surf->flags & SURF_PLANEBACK)) continue;
			if (R_CullBox (surf->mins, surf->maxs)) continue;

			// and chain it for drawing
			R_ChainSurface (surf, chain_world);
		}
	}

	// recurse down the back side
	R_RecursiveWorldNode (node->children[!side], clipflags);
}


void R_DrawWorld_Old (void)
{
	QMATRIX localMatrix;

	R_IdentityMatrix (&localMatrix);

	R_ClearTextureChains (cl.worldmodel, chain_world);

	R_RecursiveWorldNode (cl.worldmodel->nodes, 15);

	R_DrawTextureChains (cl.worldmodel, NULL, &localMatrix, chain_world);
}


void R_AddPVSLeaf (mleaf_t *leaf)
{
	byte *vis = Mod_LeafPVS (leaf, cl.worldmodel);

	for (int i = 0; i < cl.worldmodel->numleafs; i++)
	{
		if (vis[i >> 3] & (1 << (i & 7)))
		{
			mnode_t *node = (mnode_t *) &cl.worldmodel->leafs[i + 1];

			do
			{
				if (node->visframe == r_visframecount)
					break;

				node->visframe = r_visframecount;
				node = node->parent;
			} while (node);
		}
	}
}


void R_MarkLeaves (void)
{
	if (r_lockpvs.value) return;
	if (r_oldviewleaf == r_viewleaf && !vis_changed) return;

	r_visframecount++;

	R_AddPVSLeaf (r_viewleaf);

	if (r_oldviewleaf && r_oldviewleaf->contents != r_viewleaf->contents)
		R_AddPVSLeaf (r_oldviewleaf);

	r_oldviewleaf = r_viewleaf;
	vis_changed = 0;
}


