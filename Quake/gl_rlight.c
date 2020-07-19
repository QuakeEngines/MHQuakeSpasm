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
// r_light.c

#include "quakedef.h"


// mh - must init to non-zero so that it doesn't catch surfs after a memset-0
// advances for each entity (including world) drawn, never resets
int	r_dlightframecount = 1;

extern cvar_t r_flatlightstyles; // johnfitz

float	d_lightstylevalue[256];	// 8.8 fraction of base light value


/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight (double time)
{
	// 'm' is normal light, 'a' is no light, 'z' is double bright
	int flight = (int) floor (time * 10.0);
	int clight = (int) ceil (time * 10.0);
	float lerpfrac = (float) ((time * 10.0) - flight);

	// interpolated lightstyle anims
	for (int i = 0; i < MAX_LIGHTSTYLES; i++)
	{
		float light = 'm';
		char *map = cl_lightstyle[i].map;
		int length = cl_lightstyle[i].length;

		if (r_flatlightstyles.value == 2)
			light = cl_lightstyle[i].peak;
		else if (r_flatlightstyles.value == 1)
			light = cl_lightstyle[i].average;
		else if (length > 1)
			light = map[flight % length] + (map[clight % length] - map[flight % length]) * lerpfrac;
		else if (length)
			light = map[0];

		d_lightstylevalue[i] = (float) (((256.0f * (light - 'a')) / ('m' - 'a')) + 0.5f) * 0.0078125f;
	}
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

qboolean R_SurfaceDLImpact (msurface_t *surf, dlight_t *dl, float dist)
{
	int s, t;
	float impact[3], l;

	impact[0] = dl->transformed[0] - surf->plane->normal[0] * dist;
	impact[1] = dl->transformed[1] - surf->plane->normal[1] * dist;
	impact[2] = dl->transformed[2] - surf->plane->normal[2] * dist;

	// clamp center of light to corner and check brightness
	l = DotProduct (impact, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3] - surf->texturemins[0];
	s = l + 0.5; if (s < 0) s = 0; else if (s > surf->extents[0]) s = surf->extents[0];
	s = l - s;

	l = DotProduct (impact, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3] - surf->texturemins[1];
	t = l + 0.5; if (t < 0) t = 0; else if (t > surf->extents[1]) t = surf->extents[1];
	t = l - t;

	// compare to minimum light
	return ((s * s + t * t + dist * dist) < (dl->radius * dl->radius));
}


void R_MarkLights_New (dlight_t *dl, mnode_t *node, int visframe)
{
	float		dist;
	msurface_t *surf;
	int			i;
	float maxdist = dl->radius * dl->radius;

	if (node->contents < 0) return;

	// don't bother tracing nodes that will not be visible; this is *far* more effective than any
	// tricksy recursion "optimization" (which a decent compiler will automatically do for you anyway)
	if (node->visframe != visframe) return;

	dist = Mod_PlaneDist (node->plane, dl->transformed);

	if (dist > dl->radius)
	{
		R_MarkLights_New (dl, node->children[0], visframe);
		return;
	}

	if (dist < -dl->radius)
	{
		R_MarkLights_New (dl, node->children[1], visframe);
		return;
	}

	// mark the polygons
	for (i = 0, surf = node->surfaces; i < node->numsurfaces; i++, surf++)
	{
		// no lightmaps on these surface types
		if (surf->flags & (SURF_DRAWTURB | SURF_DRAWSKY)) continue;

		// if the surf wasn't hit in the standard drawing then don't count it for dlights either
		if (surf->dlightframe != r_dlightframecount) continue;

		if (R_SurfaceDLImpact (surf, dl, dist))
		{
			// add dlight surf to texture chain
			// if sorting by texture, just store it out
			R_ChainSurface (surf, chain_dlight);

			// the DL has some surfaces now
			dl->numsurfaces++;
		}
	}

	R_MarkLights_New (dl, node->children[0], visframe);
	R_MarkLights_New (dl, node->children[1], visframe);
}


void R_PushDlights_New (entity_t *e, QMATRIX *localMatrix, qmodel_t *mod, mnode_t *headnode)
{
	for (int i = 0; i < MAX_DLIGHTS; i++)
	{
		dlight_t *dl = &cl_dlights[i];

		if (cl.time > dl->die || !(dl->radius > dl->minlight))
			continue;

		if (localMatrix)
		{
			// move the light into the same space as the entity
			R_InverseTransform (localMatrix, dl->transformed, dl->origin);
		}
		else
		{
			// light is in the same space as the entiry so just copy it over
			dl->transformed[0] = dl->origin[0];
			dl->transformed[1] = dl->origin[1];
			dl->transformed[2] = dl->origin[2];
		}

		// no lights
		R_ClearTextureChains (mod, chain_dlight);
		dl->numsurfaces = 0;	// no surfaces in the dlight either

		// mark surfaces hit by this light; for the world we only need to mark PVS nodes; for entities we mark all nodes
		if (!e)
			R_MarkLights_New (dl, headnode, r_visframecount);
		else R_MarkLights_New (dl, headnode, 0);

		// no surfaces were hit
		if (!dl->numsurfaces) continue;

		// draw these surfaces
		R_DrawDlightChains (mod, e, dl);
	}

	// go to a new dlight frame for each push so that we don't carry over lights from the previous
	r_dlightframecount++;
}


void GL_SetupDynamicLight (dlight_t *dl)
{
	// the correct vertex and fragment pgograms are already bound so this just sets up some local params containing the light properties
	glProgramEnvParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 7, dl->transformed);

	// fragment program params
	glProgramEnvParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 5, dl->radius, dl->radius, dl->radius, 0);
	glProgramEnvParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 6, dl->rgba);
}


/*
=============================================================================

LIGHT SAMPLING

Light sampling is done in two phases.

Phase 1 traces the world and retrieves information about where the trace hit; it only needs to be done if the entity position changes from it's baseline.

Phase 2 takes the trace information and pulls current light data from it.

This saves a LOT of runtime tracing in cases (such as AD) where there are a lot of unmoving entities.

=============================================================================
*/

int R_TraceLightPoint (lightpoint_t *lightpoint, mnode_t *node, vec3_t start, vec3_t end)
{
	if (node->contents < 0)
		return false;		// didn't hit anything

	// calculate mid point
	float front = Mod_PlaneDist (node->plane, start);
	float back = Mod_PlaneDist (node->plane, end);

	// mh - the compiler will optimize this better than we can
	if ((back < 0) == (front < 0))
		return R_TraceLightPoint (lightpoint, node->children[front < 0], start, end);

	float frac = front / (front - back);

	vec3_t mid = {
		start[0] + (end[0] - start[0]) * frac,
		start[1] + (end[1] - start[1]) * frac,
		start[2] + (end[2] - start[2]) * frac
	};

	// go down front side
	if (R_TraceLightPoint (lightpoint, node->children[front < 0], start, mid))
		return true;	// hit something

	// check for impact on this node
	msurface_t *surf = node->surfaces;

	for (int i = 0; i < node->numsurfaces; i++, surf++)
	{
		if (surf->flags & SURF_DRAWTILED)
			continue;	// no lightmaps

		// ericw -- added double casts to force 64-bit precision.
		// Without them the zombie at the start of jam3_ericw.bsp was
		// incorrectly being lit up in SSE builds.
		int ds = (int) ((double) DoublePrecisionDotProduct (mid, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
		int dt = (int) ((double) DoublePrecisionDotProduct (mid, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

		if (ds < surf->texturemins[0] || dt < surf->texturemins[1])
			continue;

		ds -= surf->texturemins[0];
		dt -= surf->texturemins[1];

		if (ds > surf->extents[0] || dt > surf->extents[1])
			continue;

		// hits on this surf so just store it out; the actual lighting is done separately
		if (surf->samples)
			lightpoint->lightmap = surf->samples + ((dt >> 4) * ((surf->extents[0] >> 4) + 1) + (ds >> 4)) * 3; // LordHavoc: *3 for color
		else lightpoint->lightmap = NULL;

		lightpoint->lightsurf = surf;
		Vector3Copy (lightpoint->lightspot, mid);

		return true; // success
	}

	// go down back side
	return R_TraceLightPoint (lightpoint, node->children[front >= 0], mid, end);
}


void R_LightPointFromPosition (lightpoint_t *lightpoint, float *position)
{
	vec3_t	end = {
		position[0],
		position[1],
		cl.worldmodel->mins[2] - 10.0f	// MH - trace the full worldmodel
	};

	// run an initial lightpoint against the world
	if (!R_TraceLightPoint (lightpoint, cl.worldmodel->nodes, position, end))
	{
		// hit nothing
		lightpoint->lightmap = NULL;
		lightpoint->lightsurf = NULL;
		Vector3Copy (lightpoint->lightspot, end);
	}

	// find bmodels under the lightpoint - move the point to bmodel space, trace down, then check; if r < 0
	// it didn't find a bmodel, otherwise it did (a bmodel under a valid world hit will hit here too)
	// fixme: is it possible for a bmodel to not be in the PVS but yet be a valid candidate for this???
	for (int i = 0; i < cl_numvisedicts; i++)
	{
		entity_t *e = cl_visedicts[i];
		float estart[3], eend[3];
		lightpoint_t entlightpoint;

		// NULL models don't light
		if (!e->model) continue;

		// only bmodel entities give light
		if (e->model->type != mod_brush) continue;
		if (e->model->firstmodelsurface == 0) continue;

		// move start and end points into the entity's frame of reference (we don't store the local matrix in the entity struct so we do it cheap and nasty here)
		Vector3Subtract (estart, position, e->origin);
		Vector3Subtract (eend, end, e->origin);

		// and run the recursive light point on it too
		if (R_TraceLightPoint (&entlightpoint, e->model->nodes + e->model->hulls[0].firstclipnode, estart, eend))
		{
			// a bmodel under a valid world hit will hit here too so take the highest lightspot on all hits
			// move lightspot back to world space
			Vector3Add (entlightpoint.lightspot, entlightpoint.lightspot, e->origin);

			if (entlightpoint.lightspot[2] > lightpoint->lightspot[2])
			{
				// found a bmodel so copy it over
				lightpoint->lightmap = entlightpoint.lightmap;
				lightpoint->lightsurf = entlightpoint.lightsurf;
				Vector3Copy (lightpoint->lightspot, entlightpoint.lightspot);
			}
		}
	}
}


void R_LightFromLightPoint (lightpoint_t *lightpoint, float *lightcolor)
{
	if (!cl.worldmodel->lightdata || r_fullbright_cheatsafe || r_drawflat_cheatsafe)
	{
		// in practice this will go through an alternative shader path that just doesn't have lighting, so these values actually have no effect
		lightcolor[0] = lightcolor[1] = lightcolor[2] = 255;
		return;
	}

	if (!lightpoint->lightsurf)
	{
		// didn't hit anything so give it some light, but not fullbright, so it doesn't look crap
		lightcolor[0] = lightcolor[1] = lightcolor[2] = 128;
		return;
	}

	if (!lightpoint->lightmap)
	{
		// hit a surf but no lightdata for it; this is a Quake optimization to save space where surfs with no light are just not stored, so return full black
		lightcolor[0] = lightcolor[1] = lightcolor[2] = 0;
		return;
	}

	// we have a valid lightpoint now; clear to no light
	lightcolor[0] = lightcolor[1] = lightcolor[2] = 0;

	// and accumulate the lighting normally
	byte *lightmap = lightpoint->lightmap;
	msurface_t *surf = lightpoint->lightsurf;

	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
	{
		float scale = d_lightstylevalue[surf->styles[maps]];

		lightcolor[0] += (float) lightmap[0] * scale;
		lightcolor[1] += (float) lightmap[1] * scale;
		lightcolor[2] += (float) lightmap[2] * scale;

		lightmap += ((surf->extents[0] >> 4) + 1) * ((surf->extents[1] >> 4) + 1) * 3; // LordHavoc: *3 for colored lighting
	}
}


/*
=============================================================================

LIGHTMAP ALLOCATION

=============================================================================
*/

// 512 is sufficient to fit almost every id1 map into a single texture, ad_sepulcher takes 11; 1024 fits ad_sepulcher into 3 textures but is wasteful with id1
#define LIGHTMAP_SIZE	512

// with the higher LIGHTMAP_SIZE we can drop this right down and still be confident of never hitting it, so we don't need to do anything fancy like dynamic allocation.
// this is sufficient to hold ~6x the size of ad_sepulcher, which seems a reasonable upper-bound that we'll never hit.
#define MAX_LIGHTMAPS	64

gltexture_t *gl_lightmaps[3][MAX_LIGHTMAPS];

typedef struct lighttexel_s {
	byte styles[4];
} lighttexel_t;

static lighttexel_t lm_block[3][LIGHTMAP_SIZE * LIGHTMAP_SIZE];
static int lm_allocated[LIGHTMAP_SIZE];

int lm_currenttexture = 0;


static void R_OldBuildLightmap (msurface_t *surf, int smax, int tmax)
{
	// single-style build into lm_block[0]
	if (!surf->samples) return;
	if (surf->styles[0] == 255) return;

	// copy over the full lightmap but for a single style
	byte *lightmap = surf->samples;
	lighttexel_t *dest = lm_block[0] + (surf->light_t * LIGHTMAP_SIZE) + surf->light_s;

	for (int t = 0; t < tmax; t++)
	{
		for (int s = 0; s < smax; s++)
		{
			dest[s].styles[0] = lightmap[0];
			dest[s].styles[1] = lightmap[1];
			dest[s].styles[2] = lightmap[2];
			lightmap += 3;
		}

		dest += LIGHTMAP_SIZE;
	}
}


static void R_NewBuildLightmap (msurface_t *surf, int ch, int smax, int tmax)
{
	// 4-style build into lm_block[ch]
	if (!surf->samples)
		return;

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


static void LM_InitBlock (void)
{
	memset (lm_allocated, 0, sizeof (lm_allocated));
	memset (lm_block, 0, sizeof (lm_block));
}


static void LM_UploadBlock (void)
{
	char	name[24];

	sprintf (name, "lightmap_%02i", lm_currenttexture);

#define LM_UPLOAD_PARAMS(channel) \
	LIGHTMAP_SIZE, LIGHTMAP_SIZE, SRC_LIGHTMAP, (byte *) lm_block[channel], "", (src_offset_t) lm_block[channel], TEXPREF_LINEAR

	gl_lightmaps[0][lm_currenttexture] = TexMgr_LoadImage (cl.worldmodel, va ("%s_r", name), LM_UPLOAD_PARAMS (0));
	gl_lightmaps[1][lm_currenttexture] = TexMgr_LoadImage (cl.worldmodel, va ("%s_g", name), LM_UPLOAD_PARAMS (1));
	gl_lightmaps[2][lm_currenttexture] = TexMgr_LoadImage (cl.worldmodel, va ("%s_b", name), LM_UPLOAD_PARAMS (2));

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

	if (!LM_AllocBlock (smax, tmax, &surf->light_s, &surf->light_t))
	{
		LM_UploadBlock ();
		LM_InitBlock ();

		if (!LM_AllocBlock (smax, tmax, &surf->light_s, &surf->light_t))
			Sys_Error ("GL_CreateSurfaceLightmap : Consecutive calls to LM_AllocBlock (%d, %d) failed", smax, tmax);
	}

	surf->lightmaptexturenum = lm_currenttexture;

	// provide an optimization path - most surfaces in any given scene will just have a single lightstyle on them; if so we can
	// do a faster path that only needs to read a single texture, rather than the more general-case path that reads all three.
	// a possible two-style micro-optimization also exists but (and I'll admit that I haven't measured this) the potential gain
	// seems dubious vs the trade-off of more shader combinations, greater code complexity and more state changes.
	// this is a more useful optimization than checking for white light because (1) if a LIT file is used most surfaces will have
	// some degree of colour, (2) even if not (or if a LIT is not used) the single-style optimization will catch 95% of the
	// same surfaces anyway, and (3) the single-style optimization will also catch most surfaces if there is not white light.
	if (surf->styles[0] == 255)
		; // no light data
	else if (surf->styles[1] == 255)
	{
		// single style - only need to store/read one texture
		R_OldBuildLightmap (surf, smax, tmax);
	}
	else
	{
		// full set of styles
		R_NewBuildLightmap (surf, 0, smax, tmax); // red
		R_NewBuildLightmap (surf, 1, smax, tmax); // green
		R_NewBuildLightmap (surf, 2, smax, tmax); // blue
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
	// Con_Printf ("Using %i lightmaps\n", lm_currenttexture);
}


void GL_BindLightmaps (int lightmaptexturenum)
{
	GL_BindTexture (GL_TEXTURE2, gl_lightmaps[0][lightmaptexturenum]);
	GL_BindTexture (GL_TEXTURE3, gl_lightmaps[1][lightmaptexturenum]);
	GL_BindTexture (GL_TEXTURE4, gl_lightmaps[2][lightmaptexturenum]);
}


float GL_SetLightmapTexCoord (float base)
{
	return base / (float) (LIGHTMAP_SIZE * 16);
}


void GL_SetSurfaceStyles (msurface_t *surf)
{
	// build the new style
	float fstyles[4] = { 0, 0, 0, 0 };

	for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
		fstyles[maps] = d_lightstylevalue[surf->styles[maps]];

	// write them out
	// (to do - benchmark this vs sending them as a glVertexAttrib call - right now it's plenty fast enough)
	glProgramEnvParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 2, fstyles);
}


