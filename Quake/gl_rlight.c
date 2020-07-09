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


const GLchar *GL_GetFullbrightFragmentProgramSource (void)
{
	static const GLchar *src = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff, fence;\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"\n"
		"# fence texture test (this mode is not intended for robust general-case use so it's ok for it to be non-optimized)\n"
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
		"MOV result.color.a, program.env[0].a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	return src;
}


const GLchar *GL_GetDynamicLightFragmentProgramSource (void)
{
	// this program is common to BSP and MDL so let's only define it once
	static const GLchar *src = \
		"!!ARBfp1.0\n"
		"\n"
		"# fragment.texcoord[0] is texture coord\n"
		"# fragment.texcoord[1] is normal\n"
		"# fragment.texcoord[2] is light vector\n"
		"\n"
		"# program.local[0] is light radius\n"
		"# program.local[1] is light colour\n"
		"\n"
		"TEMP diff;\n"
		"TEMP normal;\n"
		"TEMP incoming;\n"
		"TEMP dist;\n"
		"TEMP light;\n"
		"TEMP angle;\n"
		"TEMP fence;\n"
		"\n"
		"# brings lights back to a 0..1 range (light.exe used 0..255)\n"
		"PARAM rescale = { 0.0078125, 0.0078125, 0.0078125, 0 };\n"
		"\n"
		"# perform the texturing; doing this early so it can interleave with ALU ops\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"\n"
		"# fence texture test\n"
		"# dynamic lights are additive so they will automagically fence if there is nothing to add\n"
		"# this makes the fence case slightly slower as a tradeoff vs a much faster general case\n"
		"MUL diff.rgb, diff, diff.a;\n"
		"\n"
		"# normalize incoming normal\n"
		"DP3 normal.w, fragment.texcoord[1], fragment.texcoord[1];\n"
		"RSQ normal.w, normal.w;\n"
		"MUL normal.xyz, normal.w, fragment.texcoord[1];\n"
		"\n"
		"# normalize incoming light vector\n"
		"DP3 incoming.w, fragment.texcoord[2], fragment.texcoord[2];\n"
		"RSQ incoming.w, incoming.w;\n"
		"RCP dist, incoming.w;	# get the vector length while we're at it\n"
		"MUL incoming.xyz, incoming.w, fragment.texcoord[2];\n"
		"\n"
		"# adjust for normal; this is the same calculation used by light.exe\n"
		"DP3 angle, incoming, normal;\n"
		"MAD angle, angle, 0.5, 0.5;\n"
		"\n"
		"# get the light attenuation\n"
		"SUB light, program.local[0], dist;\n"
		"MUL light, light, rescale;\n"
		"MUL light, light, program.local[1];\n"
		"MUL light, light, angle;\n"
		"\n"
		"# clamp any fragments with negative light contribution otherwise the POW in the gamma calc will bring them to positive - this is faster than KIL\n"
		"MAX light, light, 0.0;\n"
		"\n"
		"# modulate light by texture\n"
		"MUL diff, diff, light;\n"
		"\n"
		"# perform the fogging\n"
		"TEMP fogFactor;\n"
		"MUL fogFactor.x, state.fog.params.x, fragment.fogcoord.x;\n"
		"MUL fogFactor.x, fogFactor.x, fogFactor.x;\n"
		"EX2_SAT fogFactor.x, -fogFactor.x;\n"
		"LRP diff.rgb, fogFactor.x, diff, { 0.0, 0.0, 0.0, 0.0 };\n"
		"\n"
		"# apply the contrast\n"
		"MUL diff.rgb, diff, program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW result.color.r, diff.r, program.env[10].y;\n"
		"POW result.color.g, diff.g, program.env[10].y;\n"
		"POW result.color.b, diff.b, program.env[10].y;\n"
		"MOV result.color.a, 0.0;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	return src;
}


// mh - must init to non-zero so that it doesn't catch surfs after a memset-0
// advances for each entity (including world) drawn, never resets
int	r_dlightframecount = 1;

extern cvar_t r_flatlightstyles; // johnfitz
extern cvar_t gl_fullbrights; // johnfitz


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

		d_lightstylevalue[i] = (int) (((256.0f * (light - 'a')) / ('m' - 'a')) + 0.5f);
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
	glProgramLocalParameter4fvARB (GL_VERTEX_PROGRAM_ARB, 1, dl->transformed);

	// fragment program params
	glProgramLocalParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 0, dl->radius, dl->radius, dl->radius, 0);
	glProgramLocalParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 1, dl->rgba);
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

mplane_t *lightplane;
vec3_t	lightspot;
float	shadelight[4]; // johnfitz -- lit support via lordhavoc / MH - padded for shader params

/*
=============
RecursiveLightPoint -- johnfitz -- replaced entire function for lit support via lordhavoc
=============
*/
int RecursiveLightPoint (vec3_t color, mnode_t *node, vec3_t start, vec3_t end)
{
	float		front, back, frac;
	vec3_t		mid;

loc0:
	if (node->contents < 0)
		return false;		// didn't hit anything

	// calculate mid point
	front = Mod_PlaneDist (node->plane, start);
	back = Mod_PlaneDist (node->plane, end);

	// LordHavoc: optimized recursion
	if ((back < 0) == (front < 0))
	{
		node = node->children[front < 0];
		goto loc0;
	}

	frac = front / (front - back);
	mid[0] = start[0] + (end[0] - start[0]) * frac;
	mid[1] = start[1] + (end[1] - start[1]) * frac;
	mid[2] = start[2] + (end[2] - start[2]) * frac;

	// go down front side
	if (RecursiveLightPoint (color, node->children[front < 0], start, mid))
		return true;	// hit something
	else
	{
		int i, ds, dt;
		msurface_t *surf;

		// check for impact on this node
		VectorCopy (mid, lightspot);
		lightplane = node->plane;

		surf = node->surfaces;

		for (i = 0; i < node->numsurfaces; i++, surf++)
		{
			if (surf->flags & SURF_DRAWTILED)
				continue;	// no lightmaps

			// ericw -- added double casts to force 64-bit precision.
			// Without them the zombie at the start of jam3_ericw.bsp was
			// incorrectly being lit up in SSE builds.
			ds = (int) ((double) DoublePrecisionDotProduct (mid, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3]);
			dt = (int) ((double) DoublePrecisionDotProduct (mid, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3]);

			if (ds < surf->texturemins[0] || dt < surf->texturemins[1])
				continue;

			ds -= surf->texturemins[0];
			dt -= surf->texturemins[1];

			if (ds > surf->extents[0] || dt > surf->extents[1])
				continue;

			// clear to no light
			color[0] = 0;
			color[1] = 0;
			color[2] = 0;

			if (surf->samples)
			{
				// MH - changed this over to use the same lighting calc as R_BuildLightmap for consistency
				byte *lightmap = surf->samples + ((dt >> 4) * ((surf->extents[0] >> 4) + 1) + (ds >> 4)) * 3; // LordHavoc: *3 for color

				for (int maps = 0; maps < MAXLIGHTMAPS && surf->styles[maps] != 255; maps++)
				{
					unsigned scale = d_lightstylevalue[surf->styles[maps]];

					color[0] += lightmap[0] * scale;
					color[1] += lightmap[1] * scale;
					color[2] += lightmap[2] * scale;

					lightmap += ((surf->extents[0] >> 4) + 1) * ((surf->extents[1] >> 4) + 1) * 3; // LordHavoc: *3 for colored lighting
				}
			}

			return true; // success
		}

		// go down back side
		return RecursiveLightPoint (color, node->children[front >= 0], mid, end);
	}
}


/*
=============
R_LightPoint -- johnfitz -- replaced entire function for lit support via lordhavoc
=============
*/
int R_LightPoint (vec3_t p)
{
	if (!cl.worldmodel->lightdata)
	{
		shadelight[0] = 255 * 128;
		shadelight[1] = 255 * 128;
		shadelight[2] = 255 * 128;
	}
	else
	{
		vec3_t	end;

		end[0] = p[0];
		end[1] = p[1];
		end[2] = cl.worldmodel->mins[2] - 10.0f;	// MH - trace the full worldmodel

		shadelight[0] = shadelight[1] = shadelight[2] = 0;
		RecursiveLightPoint (shadelight, cl.worldmodel->nodes, p, end);
	}

	// shift down for overbrighting range
	if ((shadelight[0] = (int) shadelight[0] >> (7 + (int) gl_overbright.value)) > 255) shadelight[0] = 255;
	if ((shadelight[1] = (int) shadelight[1] >> (7 + (int) gl_overbright.value)) > 255) shadelight[1] = 255;
	if ((shadelight[2] = (int) shadelight[2] >> (7 + (int) gl_overbright.value)) > 255) shadelight[2] = 255;

	return ((shadelight[0] + shadelight[1] + shadelight[2]) * (1.0f / 3.0f));
}


/*
=============================================================================

LIGHTMAP ALLOCATION

=============================================================================
*/

// to do - move all of the lighting stuff to gl_rlight.c
extern cvar_t gl_fullbrights; // johnfitz

gltexture_t *gl_lightmaps[3][MAX_LIGHTMAPS];

typedef struct lighttexel_s {
	byte styles[4];
} lighttexel_t;

static lighttexel_t lm_block[3][LIGHTMAP_SIZE * LIGHTMAP_SIZE];
static int lm_allocated[LIGHTMAP_SIZE];

int lm_currenttexture = 0;


static void R_NewBuildLightmap (msurface_t *surf, int ch)
{
	if (surf->samples)
	{
		// copy over the lightmap beginning at the appropriate colour channel
		byte *lightmap = surf->samples;

		int smax = (surf->extents[0] >> 4) + 1;
		int tmax = (surf->extents[1] >> 4) + 1;

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

	if (!LM_AllocBlock (smax, tmax, &surf->light_s, &surf->light_t))
	{
		LM_UploadBlock ();
		LM_InitBlock ();

		if (!LM_AllocBlock (smax, tmax, &surf->light_s, &surf->light_t))
		{
			Sys_Error ("GL_CreateSurfaceLightmap : Consecutive calls to R_AllocBlock (%d, %d) failed", smax, tmax);
		}
	}

	surf->lightmaptexturenum = lm_currenttexture;

	R_NewBuildLightmap (surf, 0); // red
	R_NewBuildLightmap (surf, 1); // green
	R_NewBuildLightmap (surf, 2); // blue
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


