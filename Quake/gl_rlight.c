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

int	r_dlightframecount;

extern cvar_t r_flatlightstyles; // johnfitz
extern cvar_t gl_fullbrights; // johnfitz


/*
==================
R_AnimateLight
==================
*/
void R_AnimateLight (double time)
{
	int			i, j, k;

	// light animations
	// 'm' is normal light, 'a' is no light, 'z' is double bright
	i = (int) (time * 10);

	for (j = 0; j < MAX_LIGHTSTYLES; j++)
	{
		if (!cl_lightstyle[j].length)
		{
			d_lightstylevalue[j] = 256;
			continue;
		}

		// johnfitz -- r_flatlightstyles
		if (r_flatlightstyles.value == 2)
			k = cl_lightstyle[j].peak - 'a';
		else if (r_flatlightstyles.value == 1)
			k = cl_lightstyle[j].average - 'a';
		else
		{
			k = i % cl_lightstyle[j].length;
			k = cl_lightstyle[j].map[k] - 'a';
		}

		d_lightstylevalue[j] = k * 22;
		// johnfitz
	}
}


/*
=============================================================================

DYNAMIC LIGHTS

=============================================================================
*/

/*
=============
R_MarkLights -- johnfitz -- rewritten to use LordHavoc's lighting speedup
=============
*/
void R_MarkLights (dlight_t *dl, int num, mnode_t *node)
{
	mplane_t *splitplane;
	msurface_t *surf;
	vec3_t		impact;
	float		dist, l, maxdist;
	int			i, j, s, t;

start:;
	if (node->contents < 0)
		return;

	splitplane = node->plane;

	if (splitplane->type < 3)
		dist = dl->transformed[splitplane->type] - splitplane->dist;
	else
		dist = DotProduct (dl->transformed, splitplane->normal) - splitplane->dist;

	if (dist > dl->radius)
	{
		node = node->children[0];
		goto start;
	}

	if (dist < -dl->radius)
	{
		node = node->children[1];
		goto start;
	}

	maxdist = dl->radius * dl->radius;

	// mark the polygons
	surf = node->surfaces;

	for (i = 0; i < node->numsurfaces; i++, surf++)
	{
		for (j = 0; j < 3; j++)
			impact[j] = dl->transformed[j] - surf->plane->normal[j] * dist;

		// clamp center of light to corner and check brightness
		l = DotProduct (impact, surf->texinfo->vecs[0]) + surf->texinfo->vecs[0][3] - surf->texturemins[0];
		s = l + 0.5; if (s < 0) s = 0; else if (s > surf->extents[0]) s = surf->extents[0];
		s = l - s;

		l = DotProduct (impact, surf->texinfo->vecs[1]) + surf->texinfo->vecs[1][3] - surf->texturemins[1];
		t = l + 0.5; if (t < 0) t = 0; else if (t > surf->extents[1]) t = surf->extents[1];
		t = l - t;

		// compare to minimum light
		if ((s * s + t * t + dist * dist) < maxdist)
		{
			if (surf->dlightframe != r_dlightframecount) // not dynamic until now
			{
				surf->dlightbits[num >> 5] = 1U << (num & 31);
				surf->dlightframe = r_dlightframecount;
			}
			else // already dynamic
				surf->dlightbits[num >> 5] |= 1U << (num & 31);
		}
	}

	if (node->children[0]->contents >= 0) R_MarkLights (dl, num, node->children[0]);
	if (node->children[1]->contents >= 0) R_MarkLights (dl, num, node->children[1]);
}

/*
=============
R_PushDlights
=============
*/
void R_PushDlights (void)
{
	int		i;
	dlight_t *dl;

	r_dlightframecount = r_framecount + 1;	// because the count hasn't
											//  advanced yet for this frame
	dl = cl_dlights;

	for (i = 0; i < MAX_DLIGHTS; i++, dl++)
	{
		if (dl->die < cl.time || !(dl->radius > dl->minlight))
			continue;

		// MH - only the world model goes through this path
		dl->transformed[0] = dl->origin[0];
		dl->transformed[1] = dl->origin[1];
		dl->transformed[2] = dl->origin[2];

		R_MarkLights (dl, i, cl.worldmodel->nodes);
	}
}


/*
=============================================================================

LIGHT SAMPLING

=============================================================================
*/

mplane_t *lightplane;
vec3_t			lightspot;
float			shadelight[4]; // johnfitz -- lit support via lordhavoc

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
	if (node->plane->type < 3)
	{
		front = start[node->plane->type] - node->plane->dist;
		back = end[node->plane->type] - node->plane->dist;
	}
	else
	{
		front = DotProduct (start, node->plane->normal) - node->plane->dist;
		back = DotProduct (end, node->plane->normal) - node->plane->dist;
	}

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
	if (r_fullbright.value || !cl.worldmodel->lightdata)
	{
		shadelight[0] = 255 * 128;
		shadelight[1] = 255 * 128;
		shadelight[2] = 255 * 128;
	}
	else
	{
		vec3_t		end;

		end[0] = p[0];
		end[1] = p[1];
		end[2] = cl.worldmodel->mins[2] - 10.0f;	// MH - trace the full worldmodel

		shadelight[0] = shadelight[1] = shadelight[2] = 0;
		RecursiveLightPoint (shadelight, cl.worldmodel->nodes, p, end);

		// add dlights
		for (int i = 0; i < MAX_DLIGHTS; i++)
		{
			if (cl_dlights[i].die >= cl.time)
			{
				float dist[3], add;

				VectorSubtract (p, cl_dlights[i].origin, dist);
				add = cl_dlights[i].radius - VectorLength (dist);

				if (add > 0)
				{
					// bring the dlight colour up to the range as in R_AddDynamicLights
					shadelight[0] += cl_dlights[i].color[0] * add * 256.0f;
					shadelight[1] += cl_dlights[i].color[1] * add * 256.0f;
					shadelight[2] += cl_dlights[i].color[2] * add * 256.0f;
				}
			}
		}
	}

	// shift down for overbrighting range
	if ((shadelight[0] = (int) shadelight[0] >> (7 + (int) gl_overbright.value)) > 255) shadelight[0] = 255;
	if ((shadelight[1] = (int) shadelight[1] >> (7 + (int) gl_overbright.value)) > 255) shadelight[1] = 255;
	if ((shadelight[2] = (int) shadelight[2] >> (7 + (int) gl_overbright.value)) > 255) shadelight[2] = 255;

	return ((shadelight[0] + shadelight[1] + shadelight[2]) * (1.0f / 3.0f));
}

