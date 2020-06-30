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
// r_brush.c: brush model rendering. renamed from r_surf.c

#include "quakedef.h"

// to do - move all of the lighting stuff to gl_rlight.c
extern cvar_t gl_fullbrights; // johnfitz


gltexture_t *gl_lightmaps[3][MAX_LIGHTMAPS];
unsigned	lm_blocklights[LIGHTMAP_SIZE * LIGHTMAP_SIZE][3]; // johnfitz -- was 18*18, added lit support (*3) and loosened surface extents maximum (LIGHTMAP_SIZE*LIGHTMAP_SIZE)
int			lm_lightproperty = 1;

/*
===============
R_TextureAnimation -- johnfitz -- added "frame" param to eliminate use of "currententity" global

Returns the proper texture for a given time and base texture
===============
*/
texture_t *R_TextureAnimation (texture_t *base, int frame)
{
	int		relative;
	int		count;

	if (frame)
		if (base->alternate_anims)
			base = base->alternate_anims;

	if (!base->anim_total)
		return base;

	relative = (int) (cl.time * 10) % base->anim_total;
	count = 0;

	while (base->anim_min > relative || base->anim_max <= relative)
	{
		base = base->anim_next;

		if (!base) Sys_Error ("R_TextureAnimation: broken cycle");
		if (++count > 100) Sys_Error ("R_TextureAnimation: infinite cycle");
	}

	return base;
}


/*
=============================================================

	BRUSH MODELS

=============================================================
*/

/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e)
{
	int			i;
	msurface_t *psurf;
	float		dot;
	mplane_t *pplane;
	qmodel_t *clmodel;
	QMATRIX		localMatrix;

	if (R_CullModelForEntity (e))
		return;

	currententity = e;
	clmodel = e->model;

	R_IdentityMatrix (&localMatrix);
	R_TranslateMatrix (&localMatrix, e->origin[0], e->origin[1], e->origin[2]);
	R_RotateMatrix (&localMatrix, e->angles[0], e->angles[1], e->angles[2]);

	R_InverseTransform (&localMatrix, modelorg, r_refdef.vieworg);

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	R_ClearTextureChains (clmodel, chain_model);

	for (i = 0; i < clmodel->nummodelsurfaces; i++, psurf++)
	{
		pplane = psurf->plane;
		dot = DotProduct (modelorg, pplane->normal) - pplane->dist;

		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) || (!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			R_ChainSurface (psurf, chain_model);
			rs_brushpolys++;
		}
	}

	glPushMatrix ();
	glMultMatrixf (localMatrix.m16);

	R_DrawTextureChains (clmodel, e, &localMatrix, chain_model);
	R_DrawTextureChains_Water (clmodel, e, chain_model);

	glPopMatrix ();
}


/*
=============================================================

	LIGHTMAPS

=============================================================
*/


/*
=============================================================================

LIGHTMAP ALLOCATION

=============================================================================
*/

typedef struct lighttexel_s {
	byte styles[4];
} lighttexel_t;


static lighttexel_t lm_block[3][LIGHTMAP_SIZE * LIGHTMAP_SIZE];
static int lm_allocated[LIGHTMAP_SIZE];

int lm_currenttexture = 0;


void R_NewBuildLightmap (msurface_t *surf, int ch)
{
	int smax = (surf->extents[0] >> 4) + 1;
	int tmax = (surf->extents[1] >> 4) + 1;

	if (surf->samples)
	{
		// copy over the lightmap beginning at the appropriate colour channel
		byte *lightmap = surf->samples;

		for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		{
			lighttexel_t *dest = lm_block[ch] + (surf->light_t * LIGHTMAP_SIZE) + surf->light_s;

			for (int t = 0; t < tmax; t++)
			{
				for (int s = 0; s < smax; s++)
				{
					dest[s].styles[maps] = lightmap[ch];	// 'ch' is intentional here
					lightmap += 3;
				}

				dest += LIGHTMAP_SIZE;
			}
		}
	}
}


static void LM_InitBlock (void)
{
	memset (lm_allocated, 0, sizeof (lm_allocated));
	memset (lm_block, 0, sizeof (lm_block));
}


static void LM_UploadBlock (void)
{
	char	name[24];

	sprintf (name, "lightmap%07i", lm_currenttexture);

	gl_lightmaps[0][lm_currenttexture] = TexMgr_LoadImage (cl.worldmodel, va ("%s_r", name), LIGHTMAP_SIZE, LIGHTMAP_SIZE, SRC_LIGHTMAP, (byte *) lm_block[0], "", (src_offset_t) lm_block[0], TEXPREF_LINEAR);
	gl_lightmaps[1][lm_currenttexture] = TexMgr_LoadImage (cl.worldmodel, va ("%s_g", name), LIGHTMAP_SIZE, LIGHTMAP_SIZE, SRC_LIGHTMAP, (byte *) lm_block[1], "", (src_offset_t) lm_block[1], TEXPREF_LINEAR);
	gl_lightmaps[2][lm_currenttexture] = TexMgr_LoadImage (cl.worldmodel, va ("%s_b", name), LIGHTMAP_SIZE, LIGHTMAP_SIZE, SRC_LIGHTMAP, (byte *) lm_block[2], "", (src_offset_t) lm_block[2], TEXPREF_LINEAR);

	if (++lm_currenttexture >= MAX_LIGHTMAPS)
		Sys_Error ("LM_UploadBlock : MAX_LIGHTMAPS exceeded");
}


// returns a texture number and the position inside it
static qboolean LM_AllocBlock (int w, int h, int *x, int *y)
{
	int best = LIGHTMAP_SIZE;

	for (int i = 0; i < LIGHTMAP_SIZE - w; i++)
	{
		int j;
		int best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (lm_allocated[i + j] >= best)
				break;

			if (lm_allocated[i + j] > best2)
				best2 = lm_allocated[i + j];
		}

		if (j == w)
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > LIGHTMAP_SIZE)
		return false;

	for (int i = 0; i < w; i++)
		lm_allocated[*x + i] = best + h;

	return true;
}


/*
========================
GL_CreateSurfaceLightmap
========================
*/
void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	if (surf->flags & SURF_DRAWTILED)
		return;

	int smax = (surf->extents[0] >> 4) + 1;
	int tmax = (surf->extents[1] >> 4) + 1;

	if (!R_AllocBlock (smax, tmax, &surf->light_s, &surf->light_t, lm_allocated, LIGHTMAP_SIZE, LIGHTMAP_SIZE))
	{
		LM_UploadBlock ();
		LM_InitBlock ();

		if (!R_AllocBlock (smax, tmax, &surf->light_s, &surf->light_t, lm_allocated, LIGHTMAP_SIZE, LIGHTMAP_SIZE))
		{
			Sys_Error ("GL_CreateSurfaceLightmap : Consecutive calls to R_AllocBlock (%d, %d) failed", smax, tmax);
		}
	}

	surf->lightmaptexturenum = lm_currenttexture;

	R_NewBuildLightmap (surf, 0); // red
	R_NewBuildLightmap (surf, 1); // green
	R_NewBuildLightmap (surf, 2); // blue
}


GLuint r_surfaces_vbo = 0;


/*
================
GL_BuildPolygonForSurface -- called at level load time
================
*/
void GL_BuildPolygonForSurface (qmodel_t *mod, msurface_t *surf, brushpolyvert_t *verts)
{
	// reconstruct the polygon
	for (int i = 0; i < surf->numedges; i++, verts++)
	{
		int lindex = mod->surfedges[surf->firstedge + i];

		// position
		if (lindex > 0)
		{
			medge_t *r_pedge = &mod->edges[lindex];
			VectorCopy (mod->vertexes[r_pedge->v[0]].position, verts->xyz);
		}
		else
		{
			medge_t *r_pedge = &mod->edges[-lindex];
			VectorCopy (mod->vertexes[r_pedge->v[1]].position, verts->xyz);
		}

		if (surf->flags & SURF_DRAWTURB)
		{
			// diffuse texture coordinates
			verts->st[0] = DotProduct (verts->xyz, surf->texinfo->vecs[0]) * 0.015625f;
			verts->st[1] = DotProduct (verts->xyz, surf->texinfo->vecs[1]) * 0.015625f;

			// warp texture coordinates
			verts->lm[0] = DotProduct (verts->xyz, surf->texinfo->vecs[1]) * M_PI / 64.0f;
			verts->lm[1] = DotProduct (verts->xyz, surf->texinfo->vecs[0]) * M_PI / 64.0f;
		}
		else if (!(surf->flags & SURF_DRAWSKY))
		{
			// diffuse texture coordinates
			verts->st[0] = (DotProduct (verts->xyz, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]) / surf->texinfo->texture->width;
			verts->st[1] = (DotProduct (verts->xyz, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]) / surf->texinfo->texture->height;

			// lightmap texture coordinates
			verts->lm[0] = DotProduct (verts->xyz, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
			verts->lm[0] -= surf->texturemins[0];
			verts->lm[0] += surf->light_s * 16;
			verts->lm[0] += 8;
			verts->lm[0] /= LIGHTMAP_SIZE * 16; // surf->texinfo->texture->width;

			verts->lm[1] = DotProduct (verts->xyz, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
			verts->lm[1] -= surf->texturemins[1];
			verts->lm[1] += surf->light_t * 16;
			verts->lm[1] += 8;
			verts->lm[1] /= LIGHTMAP_SIZE * 16; // surf->texinfo->texture->height;
		}

		// copy over the normals for dynamic lighting
		if (surf->flags & SURF_PLANEBACK)
		{
			verts->normal[0] = 127 * -surf->plane->normal[0];
			verts->normal[1] = 127 * -surf->plane->normal[1];
			verts->normal[2] = 127 * -surf->plane->normal[2];
			verts->normal[3] = 0; // unused; for 4-byte alignment
		}
		else
		{
			verts->normal[0] = 127 * surf->plane->normal[0];
			verts->normal[1] = 127 * surf->plane->normal[1];
			verts->normal[2] = 127 * surf->plane->normal[2];
			verts->normal[3] = 0; // unused; for 4-byte alignment
		}
	}
}


/*
==================
GL_BuildLightmaps -- called at level load time

Builds the lightmap texture
with all the surfaces from all brush models
==================
*/
void GL_BuildLightmaps (void)
{
	// run a light animation at time 0 to setup the default lightstyles
	// this must be done before lightmap building so that lightstyle caching for change tracking will work correctly
	// (and the styles won't rebuild first time they're seen)
	R_AnimateLight (0);

	r_framecount = 1; // no dlightcache

	// reset the lightmaps
	memset (gl_lightmaps, 0, sizeof (gl_lightmaps));
	LM_InitBlock ();
	lm_currenttexture = 0;

	for (int j = 1; j < MAX_MODELS; j++)
	{
		qmodel_t *m = cl.model_precache[j];

		if (!m) break;
		if (m->name[0] == '*') continue;

		for (int i = 0; i < m->numsurfaces; i++)
		{
			msurface_t *surf = &m->surfaces[i];
			GL_CreateSurfaceLightmap (surf);
		}
	}

	// upload the final built lightmap
	LM_UploadBlock ();
}


void GL_BuildBModelVertexBuffer (void)
{
	int r_numsurfaceverts = 0;

	// alloc some hunk space to build the verts into
	int mark = Hunk_LowMark ();
	brushpolyvert_t *verts = (brushpolyvert_t *) Hunk_Alloc (sizeof (brushpolyvert_t)); // make an initial allocation as a baseline for these

	for (int j = 1; j < MAX_MODELS; j++)
	{
		qmodel_t *m = cl.model_precache[j];

		if (!m) break;
		if (m->name[0] == '*') continue;

		for (int i = 0; i < m->numsurfaces; i++)
		{
			msurface_t *surf = &m->surfaces[i];

			// set it up
			surf->numindexes = (surf->numedges - 2) * 3;
			surf->firstvertex = r_numsurfaceverts;
			r_numsurfaceverts += surf->numedges;

			// expand the hunk to ensure there is space for this surface
			Hunk_Alloc (surf->numedges * sizeof (brushpolyvert_t));

			// and create it
			GL_BuildPolygonForSurface (m, surf, &verts[surf->firstvertex]);
		}
	}

	// now delete and recreate the buffer
	glDeleteBuffers (1, &r_surfaces_vbo);
	glGenBuffers (1, &r_surfaces_vbo);

	glBindBuffer (GL_ARRAY_BUFFER, r_surfaces_vbo);
	glBufferData (GL_ARRAY_BUFFER, r_numsurfaceverts * sizeof (brushpolyvert_t), verts, GL_STATIC_DRAW);

	// hand back the hunk space used for surface building
	Hunk_FreeToLowMark (mark);

	// clean up/etc
	glBindBuffer (GL_ARRAY_BUFFER, 0);
	GL_ClearBufferBindings ();
}


void GL_DeleteBModelVertexBuffer (void)
{
	glDeleteBuffers (1, &r_surfaces_vbo);
	r_surfaces_vbo = 0;

	GL_ClearBufferBindings ();
}


