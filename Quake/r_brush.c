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

#define MAX_LIGHTMAPS		1024

lightmap_t gl_lightmaps[MAX_LIGHTMAPS];
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


void LM_ClearDirtyRect (gl_rect_t *dirtyrect)
{
	dirtyrect->left = LIGHTMAP_SIZE;
	dirtyrect->right = 0;
	dirtyrect->top = LIGHTMAP_SIZE;
	dirtyrect->bottom = 0;
}


qboolean R_ShouldModifyLightmap (msurface_t *surf)
{
	// johnfitz -- not a lightmapped surface
	if (surf->flags & SURF_DRAWTILED) return false;

	// gl_overbright toggle; always rebuild, even if r_dynamic is false
	if (surf->lightproperty != lm_lightproperty) return true;

	// no updates
	if (!r_dynamic.value) return false;

	// check for lightmap modification
	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		if (d_lightstylevalue[surf->styles[maps]] != surf->cached_light[maps])
			return true;

	// not modified
	return false;
}


/*
================
R_RenderDynamicLightmaps
called during rendering
================
*/
void R_RenderDynamicLightmaps (msurface_t *surf)
{
	if (R_ShouldModifyLightmap (surf))
	{
		struct lightmap_s *lm = &gl_lightmaps[surf->lightmaptexturenum];
		gl_rect_t *dirtyrect = &lm->dirtyrect;

		R_BuildLightMap (surf, lm->lm_data + (surf->light_t * LIGHTMAP_SIZE + surf->light_s) * 4, LIGHTMAP_SIZE * 4);

		if (surf->lightrect.left < dirtyrect->left) dirtyrect->left = surf->lightrect.left;
		if (surf->lightrect.right > dirtyrect->right) dirtyrect->right = surf->lightrect.right;
		if (surf->lightrect.top < dirtyrect->top) dirtyrect->top = surf->lightrect.top;
		if (surf->lightrect.bottom > dirtyrect->bottom) dirtyrect->bottom = surf->lightrect.bottom;

		surf->lightproperty = lm_lightproperty;
		lm->modified = true;
	}
}


/*
=============================================================================

LIGHTMAP ALLOCATION

=============================================================================
*/

static int lm_allocated[LIGHTMAP_SIZE];
int lm_currenttexture = 0;

static void LM_InitBlock (void)
{
	memset (lm_allocated, 0, sizeof (lm_allocated));
}


static void LM_UploadBlock (void)
{
	char	name[24];
	lightmap_t *lm = &gl_lightmaps[lm_currenttexture];

	// unused
	if (!lm->lm_data)
		Sys_Error ("LM_UploadBlock : NULL lm->data");

	LM_ClearDirtyRect (&lm->dirtyrect);
	lm->modified = false;

	sprintf (name, "lightmap%07i", lm_currenttexture);
	lm->texture = TexMgr_LoadImage (cl.worldmodel, name, LIGHTMAP_SIZE, LIGHTMAP_SIZE, SRC_LIGHTMAP, lm->lm_data, "", (src_offset_t) lm->lm_data, TEXPREF_LINEAR);

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
	byte *base;

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

	// fill in the lightrect for this surf
	surf->lightrect.left = surf->light_s;
	surf->lightrect.right = smax + surf->light_s;
	surf->lightrect.top = surf->light_t;
	surf->lightrect.bottom = tmax + surf->light_t;

	// alloc data for it if needed
	if (!gl_lightmaps[surf->lightmaptexturenum].lm_data)
		gl_lightmaps[surf->lightmaptexturenum].lm_data = (byte *) Hunk_Alloc (LIGHTMAP_SIZE * LIGHTMAP_SIZE * 4);

	// and build the lightmap
	base = gl_lightmaps[surf->lightmaptexturenum].lm_data;
	base += (surf->light_t * LIGHTMAP_SIZE + surf->light_s) * 4;

	R_BuildLightMap (surf, base, LIGHTMAP_SIZE * 4);
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
			surf->lightproperty = lm_lightproperty;
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


/*
===============
R_BuildLightMap -- johnfitz -- revised for lit support via lordhavoc

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
	int smax = (surf->extents[0] >> 4) + 1;
	int tmax = (surf->extents[1] >> 4) + 1;
	int size = smax * tmax;
	byte *lightmap = surf->samples;

	if (r_fullbright.value || !cl.worldmodel->lightdata)
	{
		for (int i = 0; i < size; i++)
		{
			// overbrighting
			lm_blocklights[i][0] = 255 * 128;
			lm_blocklights[i][1] = 255 * 128;
			lm_blocklights[i][2] = 255 * 128;
		}
	}
	else
	{
		// clear to no light
		for (int i = 0; i < size; i++)
		{
			lm_blocklights[i][0] = 0;
			lm_blocklights[i][1] = 0;
			lm_blocklights[i][2] = 0;
		}

		// add all the lightmaps
		if (lightmap)
		{
			for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
			{
				unsigned scale = d_lightstylevalue[surf->styles[maps]];

				for (int i = 0; i < size; i++, lightmap += 3)
				{
					lm_blocklights[i][0] += lightmap[0] * scale;
					lm_blocklights[i][1] += lightmap[1] * scale;
					lm_blocklights[i][2] += lightmap[2] * scale;
				}

				surf->cached_light[maps] = scale;	// 8.8 fraction
			}
		}
	}

	// bound, invert, and shift
	stride -= smax * 4;

	int shift = 7 + (int) gl_overbright.value;

	for (int i = 0, k = 0; i < tmax; i++, dest += stride)
	{
		for (int j = 0; j < smax; j++, k++, dest += 4)
		{
			int r = lm_blocklights[k][0] >> shift;
			int g = lm_blocklights[k][1] >> shift;
			int b = lm_blocklights[k][2] >> shift;

			dest[0] = (b > 255) ? 255 : b;
			dest[1] = (g > 255) ? 255 : g;
			dest[2] = (r > 255) ? 255 : r;
			dest[3] = 255;
		}
	}
}


/*
===============
R_UploadLightmaps -- johnfitz -- uploads the modified lightmap to opengl if necessary

===============
*/
void R_UploadLightmaps (void)
{
	// this lets us load subrects properly (presumably it wasn't in GL1.0 or id would have done it)
	glPixelStorei (GL_UNPACK_ROW_LENGTH, LIGHTMAP_SIZE);

	for (int lmap = 0; lmap < lm_currenttexture; lmap++)
	{
		struct lightmap_s *lm = &gl_lightmaps[lmap];
		gl_rect_t *dirtyrect = &lm->dirtyrect;

		if (!lm->modified)
			continue;

		GL_BindTexture (GL_TEXTURE1, lm->texture);

		// doing the proper subrect here
		glTexSubImage2D (
			GL_TEXTURE_2D,
			0,
			dirtyrect->left,
			dirtyrect->top,
			(dirtyrect->right - dirtyrect->left),
			(dirtyrect->bottom - dirtyrect->top),
			GL_BGRA,
			GL_UNSIGNED_INT_8_8_8_8_REV,
			lm->lm_data + (dirtyrect->top * LIGHTMAP_SIZE + dirtyrect->left) * 4
		);

		LM_ClearDirtyRect (dirtyrect);

		rs_dynamiclightmaps++;

		lm->modified = false;
	}

	// back to normal loading
	glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);
}


/*
================
R_RebuildAllLightmaps -- johnfitz -- called when gl_overbright gets toggled
================
*/
void R_RebuildAllLightmaps (void)
{
	// this toggles a rebuild of all lightmaps next time they're seen
	lm_lightproperty++;
}

