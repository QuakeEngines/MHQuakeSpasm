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
// gl_mesh.c: triangle model functions

#include "quakedef.h"


/*
=================================================================

ALIAS MODEL DISPLAY LIST GENERATION


cache a buffer set similar to textures so if a buffer set is cached it doesn't need to be rebuilt and can be reused across multiple maps

=================================================================
*/


typedef struct vertexnormals_s {
	int numnormals;
	float normal[3];
} vertexnormals_t;


void R_BuildFrameNormals (const aliashdr_t *hdr, const trivertx_t *verts, dtriangle_t *triangles, vertexnormals_t *vnorms)
{
	// this is the modelgen.c normal generation code and we regenerate correct normals for alias models
	for (int i = 0; i < hdr->numverts; i++) {
		// no normals initially
		Vector3Clear (vnorms[i].normal);
		vnorms[i].numnormals = 0;
	}

	for (int i = 0; i < hdr->numtris; i++)
	{
		float triverts[3][3];
		float vtemp1[3], vtemp2[3], normal[3];
		int *vertindexes = triangles[i].vertindex;

		// undo the vertex rotation from modelgen.c here too
		for (int j = 0; j < 3; j++) {
			triverts[j][0] = (float) verts[vertindexes[j]].v[1] * hdr->scale[1] + hdr->scale_origin[1];
			triverts[j][1] = -((float) verts[vertindexes[j]].v[0] * hdr->scale[0] + hdr->scale_origin[0]);
			triverts[j][2] = (float) verts[vertindexes[j]].v[2] * hdr->scale[2] + hdr->scale_origin[2];
		}

		// calc the per-triangle normal
		Vector3Subtract (vtemp1, triverts[0], triverts[1]);
		Vector3Subtract (vtemp2, triverts[2], triverts[1]);
		Vector3Cross (normal, vtemp1, vtemp2);
		Vector3Normalize (normal);

		// rotate the normal so the model faces down the positive x axis
		float newnormal[3] = { -normal[1], normal[0], normal[2] };

		// and accumulate it into the calculated normals array
		for (int j = 0; j < 3; j++) {
			Vector3Add (vnorms[vertindexes[j]].normal, vnorms[vertindexes[j]].normal, newnormal);
			vnorms[vertindexes[j]].numnormals++;
		}
	}

	// copy out normals
	for (int i = 0; i < hdr->numverts; i++) {
		// numnormals was checked for > 0 in modelgen.c so we shouldn't need to do it again 
		// here but we do anyway just in case a rogue modder has used a bad modelling tool
		if (vnorms[i].numnormals > 0) {
			Vector3Scalef (vnorms[i].normal, vnorms[i].normal, (float) vnorms[i].numnormals);
			Vector3Normalize (vnorms[i].normal);
		}
		else Vector3Set (vnorms[i].normal, 0.0f, 0.0f, 1.0f);
	}
}


/*
================
GLMesh_LoadVertexBuffer

Upload the given alias model's mesh to a VBO

Original code by MH from RMQEngine
================
*/
static void GLMesh_LoadVertexBuffer (qmodel_t *m, const aliashdr_t *hdr, dtriangle_t *triangles, unsigned short *indexes, aliasmesh_t *desc, trivertx_t *poseverts[])
{
	bufferset_t *set = R_GetBufferSetForModel (m);

	// count the sizes we need
	set->vboxyzofs = 0;

	int totalvbosize = (hdr->numposes * set->numverts * sizeof (meshxyz_t)); // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm

	set->vbostofs = totalvbosize;
	totalvbosize += (set->numverts * sizeof (meshst_t));

	if (!set->numindexes) return;
	if (!totalvbosize) return;

	// upload indices buffer
	glGenBuffers (1, &set->indexbuffer);
	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, set->indexbuffer);
	glBufferData (GL_ELEMENT_ARRAY_BUFFER, set->numindexes * sizeof (unsigned short), indexes, GL_STATIC_DRAW);

	// create the vertex buffer (empty)
	byte *vbodata = (byte *) Hunk_Alloc (totalvbosize);

	// set up the normals
	vertexnormals_t *vnorms = (vertexnormals_t *) Hunk_Alloc (sizeof (vertexnormals_t) * hdr->numverts);

	// fill in the vertices at the start of the buffer
	for (int f = 0; f < hdr->numposes; f++) // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
	{
		meshxyz_t *xyz = (meshxyz_t *) (vbodata + (f * set->numverts * sizeof (meshxyz_t)));
		trivertx_t *tv = poseverts[f];

		// rebuild the normals for this frame
		R_BuildFrameNormals (hdr, tv, triangles, vnorms);

		for (int v = 0; v < set->numverts; v++) {
			trivertx_t *trivert = &tv[desc[v].vertindex];

			xyz[v].position[0] = trivert->v[0];
			xyz[v].position[1] = trivert->v[1];
			xyz[v].position[2] = trivert->v[2];
			xyz[v].position[3] = 1;

			xyz[v].normal[0] = 127 * vnorms[desc[v].vertindex].normal[0];
			xyz[v].normal[1] = 127 * vnorms[desc[v].vertindex].normal[1];
			xyz[v].normal[2] = 127 * vnorms[desc[v].vertindex].normal[2];
		}
	}

	// fill in the ST coords at the end of the buffer
	// johnfitz -- padded skins
	float hscale = (float) hdr->skinwidth / (float) Image_PadConditional (hdr->skinwidth);
	float vscale = (float) hdr->skinheight / (float) Image_PadConditional (hdr->skinheight);
	// johnfitz

	meshst_t *st = (meshst_t *) (vbodata + set->vbostofs);

	for (int f = 0; f < set->numverts; f++) {
		st[f].st[0] = hscale * ((float) desc[f].st[0] + 0.5f) / (float) hdr->skinwidth;
		st[f].st[1] = vscale * ((float) desc[f].st[1] + 0.5f) / (float) hdr->skinheight;
	}

	// upload vertexes buffer
	glGenBuffers (1, &set->vertexbuffer);
	glBindBuffer (GL_ARRAY_BUFFER, set->vertexbuffer);
	glBufferData (GL_ARRAY_BUFFER, totalvbosize, vbodata, GL_STATIC_DRAW);

	// invalidate the cached bindings
	GL_ClearBufferBindings ();
}


/*
================
GL_MakeAliasModelDisplayLists
================
*/
void GL_MakeAliasModelDisplayLists (qmodel_t *m, aliashdr_t *hdr, trivertx_t *poseverts[], dtriangle_t *triangles, stvert_t *stverts)
{
	bufferset_t *set = R_GetBufferSetForModel (m);
	int mark = Hunk_LowMark ();

	// there can never be more than this number of verts and we just put them all on the hunk
	int maxverts_vbo = hdr->numtris * 3;
	aliasmesh_t *desc = (aliasmesh_t *) Hunk_Alloc (sizeof (aliasmesh_t) * maxverts_vbo);

	// there will always be this number of indexes
	unsigned short *indexes = (unsigned short *) Hunk_Alloc (sizeof (unsigned short) * maxverts_vbo);

	// these are stored out in the alias hdr so that the buffer objects can be regenned following a video restart
	set->numindexes = 0;
	set->numverts = 0;

	for (int i = 0; i < hdr->numtris; i++)
	{
		for (int j = 0; j < 3; j++)
		{
			int v;

			// index into hdr->vertexes
			unsigned short vertindex = triangles[i].vertindex[j];

			// basic s/t coords
			int s = stverts[vertindex].s;
			int t = stverts[vertindex].t;

			// check for back side and adjust texcoord s
			if (!triangles[i].facesfront && stverts[vertindex].onseam) s += hdr->skinwidth / 2;

			// see does this vert already exist (it could use the same xyz but have different s and t)
			for (v = 0; v < set->numverts; v++)
				if (desc[v].vertindex == vertindex && desc[v].st[0] == s && desc[v].st[1] == t)
					break;

			if (v == set->numverts)
			{
				// doesn't exist; emit a new vert
				desc[set->numverts].vertindex = vertindex;
				desc[set->numverts].st[0] = s;
				desc[set->numverts].st[1] = t;

				// go to the next vert
				set->numverts++;
			}

			// emit an index for it
			indexes[set->numindexes++] = v;
		}
	}

	// upload immediately
	GLMesh_LoadVertexBuffer (m, hdr, triangles, indexes, desc, poseverts);
	Hunk_FreeToLowMark (mark);
}


void GLMesh_LoadAliasGeometry (qmodel_t *mod, aliashdr_t *hdr, byte *buf)
{
	// check if the model already has a buffer set
	if ((mod->buffsetset = R_GetBufferSetForName (mod->name)) != -1) return;

	// get a new one
	if ((mod->buffsetset = R_NewBufferSetForName (mod->name)) == -1)
		Sys_Error ("GLMesh_LoadAliasGeometry : unable to allocate a buffer set");

	// common geometry load for both initial load and reload following vid_restart
	stvert_t *pinstverts = (stvert_t *) &buf[hdr->ofs_stverts];
	dtriangle_t *pintriangles = (dtriangle_t *) &buf[hdr->ofs_triangles];
	daliasframetype_t *pframetype = (daliasframetype_t *) &buf[hdr->ofs_frames];

	extern trivertx_t *poseverts[];
	int numposes = 0;

	for (int i = 0; i < hdr->numverts; i++)
	{
		pinstverts[i].onseam = LittleLong (pinstverts[i].onseam);
		pinstverts[i].s = LittleLong (pinstverts[i].s);
		pinstverts[i].t = LittleLong (pinstverts[i].t);
	}

	for (int i = 0; i < hdr->numtris; i++)
	{
		pintriangles[i].facesfront = LittleLong (pintriangles[i].facesfront);

		pintriangles[i].vertindex[0] = LittleLong (pintriangles[i].vertindex[0]);
		pintriangles[i].vertindex[1] = LittleLong (pintriangles[i].vertindex[1]);
		pintriangles[i].vertindex[2] = LittleLong (pintriangles[i].vertindex[2]);
	}

	for (int i = 0; i < hdr->numframes; i++)
	{
		aliasframetype_t frametype = (aliasframetype_t) LittleLong (pframetype->type);

		if (frametype == ALIAS_SINGLE)
		{
			daliasframe_t *pdaliasframe = (daliasframe_t *) (pframetype + 1);
			trivertx_t *pinframe = (trivertx_t *) (pdaliasframe + 1);

			poseverts[numposes] = pinframe;
			numposes++;

			pframetype = (daliasframetype_t *) (pinframe + hdr->numverts);
		}
		else
		{
			daliasgroup_t *pingroup = (daliasgroup_t *) (pframetype + 1);
			int numframes = LittleLong (pingroup->numframes);
			daliasinterval_t *pin_intervals = (daliasinterval_t *) (pingroup + 1);
			void *ptemp = (void *) (pin_intervals + numframes);

			for (int j = 0; j < numframes; j++)
			{
				poseverts[numposes] = (trivertx_t *) ((daliasframe_t *) ptemp + 1);
				numposes++;

				ptemp = (trivertx_t *) ((daliasframe_t *) ptemp + 1) + hdr->numverts;
			}

			pframetype = (daliasframetype_t *) ptemp;
		}
	}

	// and create them
	GL_MakeAliasModelDisplayLists (mod, hdr, poseverts, pintriangles, pinstverts);
}


void GLMesh_ReloadAliasGeometry (qmodel_t *mod)
{
	// rather than caching all of the geometry in the model we'll hit the disk to reload it on a vid_restart
	int mark = Hunk_LowMark ();
	aliashdr_t *hdr = (aliashdr_t *) Mod_Extradata (mod);

	if (mod->type != mod_alias)
		Sys_Error ("GLMesh_ReloadAliasGeometry : not an alias model");

	byte *buf = COM_LoadHunkFile (mod->name, &mod->path_id);

	if (!buf)
		Sys_Error ("GLMesh_ReloadAliasGeometry : model does not exist!");

	int mod_type = (buf[0] | (buf[1] << 8) | (buf[2] << 16) | (buf[3] << 24));

	if (mod_type != IDPOLYHEADER)
		Sys_Error ("GLMesh_ReloadAliasGeometry : not an alias model");

	GLMesh_LoadAliasGeometry (mod, hdr, buf);
	Hunk_FreeToLowMark (mark);
}


/*
================
GLMesh_ReloadVertexBuffers

Loop over all precached alias models, and upload each one to a VBO.
================
*/
void GLMesh_ReloadVertexBuffers (void)
{
	int j;
	qmodel_t *m;
	const aliashdr_t *hdr;

	for (j = 1; j < MAX_MODELS; j++)
	{
		if (!(m = cl.model_precache[j])) break;
		if (m->type != mod_alias) continue;

		hdr = (const aliashdr_t *) Mod_Extradata (m);

		GLMesh_ReloadAliasGeometry (m);
	}
}


