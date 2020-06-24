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

/*

========================================================================================================================================================================================================

Lightmap Rectangle Updates
--------------------------

GLQuake updates the full width of a dynamic lightmap, which can be a lot more of the lightmap than actually needs to be updated.  We can do better than that by supplying it with a proper subrectangle.

The following replacement structure for glRect_t will define a proper rectangle for use in the rest of this discussion:

typedef struct gl_rect_s
{
	// use a proper rect
	int left, top, right, bottom;
} gl_rect_t;

You'll need one of these for each lightmap (which we'll call the "dirtyrect" and one for each surface (which we'll call the "lightrect"; you may as well store it in the msurface_t struct too).

surf->lightrect.left is equal to smax, surf->lightrect.right is equal to smax + surf->light_s, and I bet you can guess how the rest of them are calculated.

The dirtyrects are initialized similar to the current rectchange, with left set to BLOCK_WIDTH, right to 0, etc.

When a lightmap is modified you can then mark out the changed region with code similar to this:

if (surf->lightrect.left < dirtyrect->left) dirtyrect->left = surf->lightrect.left;
if (surf->lightrect.right > dirtyrect->right) dirtyrect->right = surf->lightrect.right;
if (surf->lightrect.top < dirtyrect->top) dirtyrect->top = surf->lightrect.top;
if (surf->lightrect.bottom > dirtyrect->bottom) dirtyrect->bottom = surf->lightrect.bottom;

Now to update the lightmap.

The first thing we need is to tell OpenGL some information about the texture you're updating by calling glPixelStorei (GL_UNPACK_ROW_LENGTH, BLOCK_WIDTH).
This lets OpenGL know the length of each row in the texture, so that when you do a partial update of a row it will skip to the start of the next one each time.
Otherwise we'll get corrupted lightmap updates as it will most likely append data intended for the start of the next row to the end of the current update region.
Call glPixelStorei (GL_UNPACK_ROW_LENGTH, 0) to set it back to default behaviour when done.

Finally we have our glTexSubImage2D call; an example might look something like this:

glTexSubImage2D (
	GL_TEXTURE_2D,
	0,
	dirtyrect->left,
	dirtyrect->top,
	(dirtyrect->right - dirtyrect->left),
	(dirtyrect->bottom - dirtyrect->top),
	GL_BGRA,
	GL_UNSIGNED_INT_8_8_8_8_REV,
	gl_lightmaps[i].data + (dirtyrect->top * BLOCK_WIDTH + dirtyrect->left) * LIGHTMAP_BYTES
);

And we've just cut down on bandwidth usage for lightmap updating by a potentially significant amount.

Note that this technique is useless on it's own.  You need to stop syncing the GPU with the CPU by following the techniques I've outlined up above first.
Use this in addition to the above to get more speed, not instead of it.

========================================================================================================================================================================================================

*/

#include "quakedef.h"

extern cvar_t gl_fullbrights, gl_overbright; // johnfitz

#define MAX_SANITY_LIGHTMAPS (1u<<20)

struct lightmap_s *lightmap;
int					lightmap_count;
int					last_lightmap_allocated;
int					allocated[LMBLOCK_WIDTH];

unsigned	blocklights[LMBLOCK_WIDTH * LMBLOCK_HEIGHT * 3]; // johnfitz -- was 18*18, added lit support (*3) and loosened surface extents maximum (LMBLOCK_WIDTH*LMBLOCK_HEIGHT)


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
================
DrawGLPoly
================
*/
void DrawGLPoly (glpoly_t *p)
{
	float *v;
	int		i;

	glBegin (GL_POLYGON);
	v = p->verts[0];
	for (i = 0; i < p->numverts; i++, v += VERTEXSIZE)
	{
		glTexCoord2f (v[3], v[4]);
		glVertex3fv (v);
	}
	glEnd ();
}


/*
=============================================================

	BRUSH MODELS

=============================================================
*/

void R_InverseTransform (float *out, float *in, float *origin, float *angles)
{
	VectorSubtract (in, origin, out);

	if (angles[0] || angles[1] || angles[2])
	{
		vec3_t	temp;
		vec3_t	forward, right, up;

		VectorCopy (out, temp);
		AngleVectors (angles, forward, right, up);

		out[0] = DotProduct (temp, forward);
		out[1] = -DotProduct (temp, right);
		out[2] = DotProduct (temp, up);
	}
}


/*
=================
R_DrawBrushModel
=================
*/
void R_DrawBrushModel (entity_t *e)
{
	int			i, k;
	msurface_t *psurf;
	float		dot;
	mplane_t *pplane;
	qmodel_t *clmodel;

	if (R_CullModelForEntity (e))
		return;

	currententity = e;
	clmodel = e->model;

	R_InverseTransform (modelorg, r_refdef.vieworg, e->origin, e->angles);

	psurf = &clmodel->surfaces[clmodel->firstmodelsurface];

	// calculate dynamic lighting for bmodel if it's not an instanced model
	if (clmodel->firstmodelsurface != 0)
	{
		for (k = 0; k < MAX_DLIGHTS; k++)
		{
			if ((cl_dlights[k].die < cl.time) || (!cl_dlights[k].radius))
				continue;

			// MH - dlight transform
			R_InverseTransform (cl_dlights[k].transformed, cl_dlights[k].origin, e->origin, e->angles);

			R_MarkLights (&cl_dlights[k], k, clmodel->nodes + clmodel->hulls[0].firstclipnode);
		}
	}

	glPushMatrix ();

	glTranslatef (e->origin[0], e->origin[1], e->origin[2]);
	glRotatef (e->angles[1], 0, 0, 1);
	glRotatef (e->angles[0], 0, 1, 0);
	glRotatef (e->angles[2], 1, 0, 0);

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

	R_DrawTextureChains (clmodel, e, chain_model);
	R_DrawTextureChains_Water (clmodel, e, chain_model);

	glPopMatrix ();
}


/*
=============================================================

	LIGHTMAPS

=============================================================
*/

/*
================
R_RenderDynamicLightmaps
called during rendering
================
*/
void R_RenderDynamicLightmaps (msurface_t *surf)
{
	byte *base;
	int			maps;
	glRect_t *theRect;
	int smax, tmax;

	if (surf->flags & SURF_DRAWTILED) // johnfitz -- not a lightmapped surface
		return;

	// add to lightmap chain
	surf->polys->chain = lightmap[surf->lightmaptexturenum].polys;
	lightmap[surf->lightmaptexturenum].polys = surf->polys;

	// check for lightmap modification
	for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		if (d_lightstylevalue[surf->styles[maps]] != surf->cached_light[maps])
			goto dynamic;

	if (surf->dlightframe == r_framecount	// dynamic this frame
		|| surf->cached_dlight)			// dynamic previously
	{
dynamic:
		if (r_dynamic.value)
		{
			struct lightmap_s *lm = &lightmap[surf->lightmaptexturenum];
			lm->modified = true;
			theRect = &lm->rectchange;

			if (surf->light_t < theRect->t)
			{
				if (theRect->h)
					theRect->h += theRect->t - surf->light_t;
				theRect->t = surf->light_t;
			}

			if (surf->light_s < theRect->l)
			{
				if (theRect->w)
					theRect->w += theRect->l - surf->light_s;
				theRect->l = surf->light_s;
			}

			smax = (surf->extents[0] >> 4) + 1;
			tmax = (surf->extents[1] >> 4) + 1;

			if ((theRect->w + theRect->l) < (surf->light_s + smax))
				theRect->w = (surf->light_s - theRect->l) + smax;

			if ((theRect->h + theRect->t) < (surf->light_t + tmax))
				theRect->h = (surf->light_t - theRect->t) + tmax;

			base = lm->data;
			base += surf->light_t * LMBLOCK_WIDTH * 4 + surf->light_s * 4;

			R_BuildLightMap (surf, base, LMBLOCK_WIDTH * 4);
		}
	}
}


/*
========================
AllocBlock -- returns a texture number and the position inside it
========================
*/
int AllocBlock (int w, int h, int *x, int *y)
{
	int		i, j;
	int		best, best2;
	int		texnum;

	// ericw -- rather than searching starting at lightmap 0 every time,
	// start at the last lightmap we allocated a surface in.
	// This makes AllocBlock much faster on large levels (can shave off 3+ seconds
	// of load time on a level with 180 lightmaps), at a cost of not quite packing
	// lightmaps as tightly vs. not doing this (uses ~5% more lightmaps)
	for (texnum = last_lightmap_allocated; texnum < MAX_SANITY_LIGHTMAPS; texnum++)
	{
		if (texnum == lightmap_count)
		{
			lightmap_count++;
			lightmap = (struct lightmap_s *) realloc (lightmap, sizeof (*lightmap) * lightmap_count);
			memset (&lightmap[texnum], 0, sizeof (lightmap[texnum]));
			/* FIXME: we leave 'gaps' in malloc()ed data,  CRC_Block() later accesses
			 * that uninitialized data and valgrind complains for it.  use calloc() ? */
			lightmap[texnum].data = (byte *) malloc (4 * LMBLOCK_WIDTH * LMBLOCK_HEIGHT);
			// as we're only tracking one texture, we don't need multiple copies of allocated any more.
			memset (allocated, 0, sizeof (allocated));
		}

		best = LMBLOCK_HEIGHT;

		for (i = 0; i < LMBLOCK_WIDTH - w; i++)
		{
			best2 = 0;

			for (j = 0; j < w; j++)
			{
				if (allocated[i + j] >= best)
					break;
				if (allocated[i + j] > best2)
					best2 = allocated[i + j];
			}

			if (j == w)
			{
				// this is a valid spot
				*x = i;
				*y = best = best2;
			}
		}

		if (best + h > LMBLOCK_HEIGHT)
			continue;

		for (i = 0; i < w; i++)
			allocated[*x + i] = best + h;

		last_lightmap_allocated = texnum;
		return texnum;
	}

	Sys_Error ("AllocBlock: full");
	return 0; // johnfitz -- shut up compiler
}


mvertex_t *r_pcurrentvertbase;
qmodel_t *currentmodel;

int	nColinElim;

/*
========================
GL_CreateSurfaceLightmap
========================
*/
void GL_CreateSurfaceLightmap (msurface_t *surf)
{
	int		smax, tmax;
	byte *base;

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;

	surf->lightmaptexturenum = AllocBlock (smax, tmax, &surf->light_s, &surf->light_t);

	base = lightmap[surf->lightmaptexturenum].data;
	base += (surf->light_t * LMBLOCK_WIDTH + surf->light_s) * 4;

	R_BuildLightMap (surf, base, LMBLOCK_WIDTH * 4);
}


/*
================
BuildSurfaceDisplayList -- called at level load time
================
*/
void BuildSurfaceDisplayList (msurface_t *surf)
{
	int			i, lindex, lnumverts;
	medge_t *pedges, *r_pedge;
	float *vec;
	float		s, t;
	glpoly_t *poly;

	// reconstruct the polygon
	pedges = currentmodel->edges;
	lnumverts = surf->numedges;

	// draw texture
	poly = (glpoly_t *) Hunk_Alloc (sizeof (glpoly_t) + (lnumverts - 4) * VERTEXSIZE * sizeof (float));
	surf->polys = poly;
	poly->numverts = lnumverts;

	for (i = 0; i < lnumverts; i++)
	{
		lindex = currentmodel->surfedges[surf->firstedge + i];

		if (lindex > 0)
		{
			r_pedge = &pedges[lindex];
			vec = r_pcurrentvertbase[r_pedge->v[0]].position;
		}
		else
		{
			r_pedge = &pedges[-lindex];
			vec = r_pcurrentvertbase[r_pedge->v[1]].position;
		}
		s = DotProduct (vec, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
		s /= surf->texinfo->texture->width;

		t = DotProduct (vec, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
		t /= surf->texinfo->texture->height;

		VectorCopy (vec, poly->verts[i]);
		poly->verts[i][3] = s;
		poly->verts[i][4] = t;

		// lightmap texture coordinates
		s = DotProduct (vec, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3];
		s -= surf->texturemins[0];
		s += surf->light_s * 16;
		s += 8;
		s /= LMBLOCK_WIDTH * 16; // surf->texinfo->texture->width;

		t = DotProduct (vec, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3];
		t -= surf->texturemins[1];
		t += surf->light_t * 16;
		t += 8;
		t /= LMBLOCK_HEIGHT * 16; // surf->texinfo->texture->height;

		poly->verts[i][5] = s;
		poly->verts[i][6] = t;
	}

	// johnfitz -- removed gl_keeptjunctions code

	poly->numverts = lnumverts;
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
	char	name[24];
	int		i, j;
	struct lightmap_s *lm;
	qmodel_t *m;

	r_framecount = 1; // no dlightcache

	// Spike -- wipe out all the lightmap data (johnfitz -- the gltexture objects were already freed by Mod_ClearAll)
	for (i = 0; i < lightmap_count; i++)
		free (lightmap[i].data);

	free (lightmap);
	lightmap = NULL;
	last_lightmap_allocated = 0;
	lightmap_count = 0;

	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];

		if (!m) break;
		if (m->name[0] == '*') continue;

		r_pcurrentvertbase = m->vertexes;
		currentmodel = m;

		for (i = 0; i < m->numsurfaces; i++)
		{
			// johnfitz -- rewritten to use SURF_DRAWTILED instead of the sky/water flags
			if (m->surfaces[i].flags & SURF_DRAWTILED)
				continue;
			GL_CreateSurfaceLightmap (m->surfaces + i);
			BuildSurfaceDisplayList (m->surfaces + i);
			// johnfitz
		}
	}

	// upload all lightmaps that were filled
	for (i = 0; i < lightmap_count; i++)
	{
		lm = &lightmap[i];
		lm->modified = false;
		lm->rectchange.l = LMBLOCK_WIDTH;
		lm->rectchange.t = LMBLOCK_HEIGHT;
		lm->rectchange.w = 0;
		lm->rectchange.h = 0;

		// johnfitz -- use texture manager
		sprintf (name, "lightmap%07i", i);
		lm->texture = TexMgr_LoadImage (cl.worldmodel, name, LMBLOCK_WIDTH, LMBLOCK_HEIGHT,
			SRC_LIGHTMAP, lm->data, "", (src_offset_t) lm->data, TEXPREF_LINEAR);
		// johnfitz
	}

	// johnfitz -- warn about exceeding old limits
	// GLQuake limit was 64 textures of 128x128. Estimate how many 128x128 textures we would need
	// given that we are using lightmap_count of LMBLOCK_WIDTH x LMBLOCK_HEIGHT
	i = lightmap_count * ((LMBLOCK_WIDTH / 128) * (LMBLOCK_HEIGHT / 128));
	if (i > 64)
		Con_DWarning ("%i lightmaps exceeds standard limit of 64.\n", i);
	// johnfitz
}


/*
=============================================================

	VBO support

=============================================================
*/

GLuint gl_bmodel_vbo = 0;

void GL_DeleteBModelVertexBuffer (void)
{
	glDeleteBuffers (1, &gl_bmodel_vbo);
	gl_bmodel_vbo = 0;

	GL_ClearBufferBindings ();
}


/*
==================
GL_BuildBModelVertexBuffer

Deletes gl_bmodel_vbo if it already exists, then rebuilds it with all
surfaces from world + all brush models
==================
*/
void GL_BuildBModelVertexBuffer (void)
{
	unsigned int	numverts, varray_bytes, varray_index;
	int		i, j;
	qmodel_t *m;
	float *varray;

	// ask GL for a name for our VBO
	glDeleteBuffers (1, &gl_bmodel_vbo);
	glGenBuffers (1, &gl_bmodel_vbo);

	// count all verts in all models
	numverts = 0;
	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i = 0; i < m->numsurfaces; i++)
		{
			numverts += m->surfaces[i].numedges;
		}
	}

	// build vertex array
	varray_bytes = VERTEXSIZE * sizeof (float) * numverts;
	varray = (float *) malloc (varray_bytes);
	varray_index = 0;

	for (j = 1; j < MAX_MODELS; j++)
	{
		m = cl.model_precache[j];
		if (!m || m->name[0] == '*' || m->type != mod_brush)
			continue;

		for (i = 0; i < m->numsurfaces; i++)
		{
			msurface_t *s = &m->surfaces[i];
			s->vbo_firstvert = varray_index;
			memcpy (&varray[VERTEXSIZE * varray_index], s->polys->verts, VERTEXSIZE * sizeof (float) * s->numedges);
			varray_index += s->numedges;
		}
	}

	// upload to GPU
	glBindBuffer (GL_ARRAY_BUFFER, gl_bmodel_vbo);
	glBufferData (GL_ARRAY_BUFFER, varray_bytes, varray, GL_STATIC_DRAW);
	free (varray);

	// invalidate the cached bindings
	GL_ClearBufferBindings ();
}


/*
===============
R_AddDynamicLights
===============
*/
void R_AddDynamicLights (msurface_t *surf)
{
	int			lnum;
	int			sd, td;
	float		dist, rad, minlight;
	vec3_t		impact, local;
	int			s, t;
	int			i;
	int			smax, tmax;
	mtexinfo_t *tex;
	// johnfitz -- lit support via lordhavoc
	float		cred, cgreen, cblue, brightness;
	unsigned *bl;
	// johnfitz

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	tex = surf->texinfo;

	for (lnum = 0; lnum < MAX_DLIGHTS; lnum++)
	{
		if (!(surf->dlightbits[lnum >> 5] & (1U << (lnum & 31))))
			continue;		// not lit by this light

		rad = cl_dlights[lnum].radius;
		dist = DotProduct (cl_dlights[lnum].transformed, surf->plane->normal) -
			surf->plane->dist;
		rad -= fabs (dist);
		minlight = cl_dlights[lnum].minlight;
		if (rad < minlight)
			continue;
		minlight = rad - minlight;

		for (i = 0; i < 3; i++)
		{
			impact[i] = cl_dlights[lnum].transformed[i] -
				surf->plane->normal[i] * dist;
		}

		local[0] = DotProduct (impact, tex->vecs[0]) + tex->vecs[0][3];
		local[1] = DotProduct (impact, tex->vecs[1]) + tex->vecs[1][3];

		local[0] -= surf->texturemins[0];
		local[1] -= surf->texturemins[1];

		// johnfitz -- lit support via lordhavoc
		bl = blocklights;
		cred = cl_dlights[lnum].color[0] * 256.0f;
		cgreen = cl_dlights[lnum].color[1] * 256.0f;
		cblue = cl_dlights[lnum].color[2] * 256.0f;
		// johnfitz

		for (t = 0; t < tmax; t++)
		{
			td = local[1] - t * 16;
			if (td < 0)
				td = -td;
			for (s = 0; s < smax; s++)
			{
				sd = local[0] - s * 16;
				if (sd < 0)
					sd = -sd;
				if (sd > td)
					dist = sd + (td >> 1);
				else
					dist = td + (sd >> 1);
				if (dist < minlight)
					// johnfitz -- lit support via lordhavoc
				{
					brightness = rad - dist;
					bl[0] += (int) (brightness * cred);
					bl[1] += (int) (brightness * cgreen);
					bl[2] += (int) (brightness * cblue);
				}
				bl += 3;
				// johnfitz
			}
		}
	}
}


/*
===============
R_BuildLightMap -- johnfitz -- revised for lit support via lordhavoc

Combine and scale multiple lightmaps into the 8.8 format in blocklights
===============
*/
void R_BuildLightMap (msurface_t *surf, byte *dest, int stride)
{
	int			smax, tmax;
	int			r, g, b;
	int			i, j, size;
	byte *lightmap;
	unsigned	scale;
	int			maps;
	unsigned *bl;

	surf->cached_dlight = (surf->dlightframe == r_framecount);

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	size = smax * tmax;
	lightmap = surf->samples;

	if (cl.worldmodel->lightdata)
	{
		// clear to no light
		memset (&blocklights[0], 0, size * 3 * sizeof (unsigned int)); // johnfitz -- lit support via lordhavoc

		// add all the lightmaps
		if (lightmap)
		{
			for (maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
			{
				scale = d_lightstylevalue[surf->styles[maps]];
				surf->cached_light[maps] = scale;	// 8.8 fraction
				// johnfitz -- lit support via lordhavoc
				bl = blocklights;

				for (i = 0; i < size; i++)
				{
					*bl++ += *lightmap++ * scale;
					*bl++ += *lightmap++ * scale;
					*bl++ += *lightmap++ * scale;
				}
				// johnfitz
			}
		}

		// add all the dynamic lights
		if (surf->dlightframe == r_framecount)
			R_AddDynamicLights (surf);
	}
	else
	{
		// set to full bright if no light data
		memset (&blocklights[0], 255, size * 3 * sizeof (unsigned int)); // johnfitz -- lit support via lordhavoc
	}

	// bound, invert, and shift
	// store:
	stride -= smax * 4;
	bl = blocklights;

	for (i = 0; i < tmax; i++, dest += stride)
	{
		for (j = 0; j < smax; j++)
		{
			if (gl_overbright.value)
			{
				r = *bl++ >> 8;
				g = *bl++ >> 8;
				b = *bl++ >> 8;
			}
			else
			{
				r = *bl++ >> 7;
				g = *bl++ >> 7;
				b = *bl++ >> 7;
			}

			*dest++ = (b > 255) ? 255 : b;
			*dest++ = (g > 255) ? 255 : g;
			*dest++ = (r > 255) ? 255 : r;
			*dest++ = 255;
		}
	}
}


/*
===============
R_UploadLightmaps -- johnfitz -- uploads the modified lightmap to opengl if necessary

assumes lightmap texture is already bound
===============
*/
void R_UploadLightmaps (void)
{
	int lmap;

	for (lmap = 0; lmap < lightmap_count; lmap++)
	{
		struct lightmap_s *lm = &lightmap[lmap];

		if (!lm->modified)
			continue;

		GL_Bind (lm->texture);

		// fixme - do the proper rect here
		glTexSubImage2D (GL_TEXTURE_2D, 0, 0, lm->rectchange.t, LMBLOCK_WIDTH, lm->rectchange.h, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, lm->data + lm->rectchange.t * LMBLOCK_WIDTH * 4);

		lm->rectchange.l = LMBLOCK_WIDTH;
		lm->rectchange.t = LMBLOCK_HEIGHT;
		lm->rectchange.h = 0;
		lm->rectchange.w = 0;

		rs_dynamiclightmaps++;

		lm->modified = false;
	}
}


/*
================
R_RebuildAllLightmaps -- johnfitz -- called when gl_overbright gets toggled
================
*/
void R_RebuildAllLightmaps (void)
{
	int			i, j;
	qmodel_t *mod;
	msurface_t *surf;
	byte *base;

	if (!cl.worldmodel) // is this the correct test?
		return;

	// for each surface in each model, rebuild lightmap with new scale
	for (i = 1; i < MAX_MODELS; i++)
	{
		if (!(mod = cl.model_precache[i]))
			continue;

		surf = &mod->surfaces[mod->firstmodelsurface];

		for (j = 0; j < mod->nummodelsurfaces; j++, surf++)
		{
			if (surf->flags & SURF_DRAWTILED)
				continue;

			base = lightmap[surf->lightmaptexturenum].data;
			base += surf->light_t * LMBLOCK_WIDTH * 4 + surf->light_s * 4;

			R_BuildLightMap (surf, base, LMBLOCK_WIDTH * 4);
		}
	}

	// for each lightmap, upload it
	for (i = 0; i < lightmap_count; i++)
	{
		GL_Bind (lightmap[i].texture);
		glTexSubImage2D (GL_TEXTURE_2D, 0, 0, 0, LMBLOCK_WIDTH, LMBLOCK_HEIGHT, GL_BGRA, GL_UNSIGNED_INT_8_8_8_8_REV, lightmap[i].data);
	}
}

