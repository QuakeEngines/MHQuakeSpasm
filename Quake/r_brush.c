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

	SURFACES VBO BUILDING

=============================================================
*/


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
			verts->normal[0] = -surf->plane->normal[0];
			verts->normal[1] = -surf->plane->normal[1];
			verts->normal[2] = -surf->plane->normal[2];
		}
		else
		{
			verts->normal[0] = surf->plane->normal[0];
			verts->normal[1] = surf->plane->normal[1];
			verts->normal[2] = surf->plane->normal[2];
		}
	}
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


