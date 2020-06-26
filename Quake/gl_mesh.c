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

=================================================================
*/


#define NUMVERTEXNORMALS	 162
extern	float	r_avertexnormals[NUMVERTEXNORMALS][3];

/*
================
GLMesh_LoadVertexBuffer

Upload the given alias model's mesh to a VBO

Original code by MH from RMQEngine
================
*/
static void GLMesh_LoadVertexBuffer (qmodel_t *m, const aliashdr_t *hdr)
{
	int totalvbosize = 0;
	const aliasmesh_t *desc;
	const short *indexes;
	const trivertx_t *trivertexes;
	byte *vbodata;
	int f;

	int mark = Hunk_LowMark ();

	// count the sizes we need
	// ericw -- RMQEngine stored these vbo*ofs values in aliashdr_t, but we must not
	// mutate Mod_Extradata since it might be reloaded from disk, so I moved them to qmodel_t
	// (test case: roman1.bsp from arwop, 64mb heap)
	m->vboindexofs = 0;
	m->vboxyzofs = 0;
	totalvbosize += (hdr->numposes * hdr->numverts_vbo * sizeof (meshxyz_t)); // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm

	m->vbostofs = totalvbosize;
	totalvbosize += (hdr->numverts_vbo * sizeof (meshst_t));

	if (!hdr->numindexes) return;
	if (!totalvbosize) return;

	// grab the pointers to data in the extradata
	desc = (aliasmesh_t *) ((byte *) hdr + hdr->meshdesc);
	indexes = (short *) ((byte *) hdr + hdr->indexes);
	trivertexes = (trivertx_t *) ((byte *) hdr + hdr->vertexes);

	// upload indices buffer
	glDeleteBuffers (1, &m->meshindexesvbo);
	glGenBuffers (1, &m->meshindexesvbo);
	glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, m->meshindexesvbo);
	glBufferData (GL_ELEMENT_ARRAY_BUFFER, hdr->numindexes * sizeof (unsigned short), indexes, GL_STATIC_DRAW);

	// create the vertex buffer (empty)
	vbodata = (byte *) Hunk_Alloc (totalvbosize);

	// fill in the vertices at the start of the buffer
	for (f = 0; f < hdr->numposes; f++) // ericw -- what RMQEngine called nummeshframes is called numposes in QuakeSpasm
	{
		int v;
		meshxyz_t *xyz = (meshxyz_t *) (vbodata + (f * hdr->numverts_vbo * sizeof (meshxyz_t)));
		const trivertx_t *tv = trivertexes + (hdr->numverts * f);

		for (v = 0; v < hdr->numverts_vbo; v++)
		{
			trivertx_t trivert = tv[desc[v].vertindex];

			xyz[v].xyz[0] = trivert.v[0];
			xyz[v].xyz[1] = trivert.v[1];
			xyz[v].xyz[2] = trivert.v[2];
			xyz[v].xyz[3] = 1;	// need w 1 for 4 byte vertex compression

			// map the normal coordinates in [-1..1] to [-127..127] and store in an unsigned char.
			// this introduces some error (less than 0.004), but the normals were very coarse
			// to begin with
			xyz[v].normal[0] = 127 * r_avertexnormals[trivert.lightnormalindex][0];
			xyz[v].normal[1] = 127 * r_avertexnormals[trivert.lightnormalindex][1];
			xyz[v].normal[2] = 127 * r_avertexnormals[trivert.lightnormalindex][2];
			xyz[v].normal[3] = 0;	// unused; for 4-byte alignment
		}
	}

	// fill in the ST coords at the end of the buffer
	{
		meshst_t *st;
		float hscale, vscale;

		// johnfitz -- padded skins
		hscale = (float) hdr->skinwidth / (float) TexMgr_PadConditional (hdr->skinwidth);
		vscale = (float) hdr->skinheight / (float) TexMgr_PadConditional (hdr->skinheight);
		// johnfitz

		st = (meshst_t *) (vbodata + m->vbostofs);

		for (f = 0; f < hdr->numverts_vbo; f++)
		{
			st[f].st[0] = hscale * ((float) desc[f].st[0] + 0.5f) / (float) hdr->skinwidth;
			st[f].st[1] = vscale * ((float) desc[f].st[1] + 0.5f) / (float) hdr->skinheight;
		}
	}

	// upload vertexes buffer
	glDeleteBuffers (1, &m->meshvbo);
	glGenBuffers (1, &m->meshvbo);
	glBindBuffer (GL_ARRAY_BUFFER, m->meshvbo);
	glBufferData (GL_ARRAY_BUFFER, totalvbosize, vbodata, GL_STATIC_DRAW);

	Hunk_FreeToLowMark (mark);

	// invalidate the cached bindings
	GL_ClearBufferBindings ();
}


/*
================
GL_MakeAliasModelDisplayLists
================
*/
void GL_MakeAliasModelDisplayLists (qmodel_t *m, aliashdr_t *hdr)
{
	int i, j;
	int maxverts_vbo;
	trivertx_t *verts;
	unsigned short *indexes;
	aliasmesh_t *desc;

	// first, copy the verts onto the hunk
	verts = (trivertx_t *) Hunk_Alloc (hdr->numposes * hdr->numverts * sizeof (trivertx_t));
	hdr->vertexes = (byte *) verts - (byte *) hdr;

	for (i = 0; i < hdr->numposes; i++)
		for (j = 0; j < hdr->numverts; j++)
			verts[i * hdr->numverts + j] = poseverts[i][j];

	// there can never be more than this number of verts and we just put them all on the hunk
	maxverts_vbo = hdr->numtris * 3;
	desc = (aliasmesh_t *) Hunk_Alloc (sizeof (aliasmesh_t) * maxverts_vbo);

	// there will always be this number of indexes
	indexes = (unsigned short *) Hunk_Alloc (sizeof (unsigned short) * maxverts_vbo);

	// these are stored out in the alias hdr so that the buffer objects can be regenned following a video restart
	hdr->indexes = (intptr_t) indexes - (intptr_t) hdr;
	hdr->meshdesc = (intptr_t) desc - (intptr_t) hdr;
	hdr->numindexes = 0;
	hdr->numverts_vbo = 0;

	for (i = 0; i < hdr->numtris; i++)
	{
		for (j = 0; j < 3; j++)
		{
			int v;

			// index into hdr->vertexes
			unsigned short vertindex = triangles[i].vertindex[j];

			// basic s/t coords
			int s = stverts[vertindex].s;
			int t = stverts[vertindex].t;

			// check for back side and adjust texcoord s
			if (!triangles[i].facesfront && stverts[vertindex].onseam) s += hdr->skinwidth / 2;

			// see does this vert already exist
			for (v = 0; v < hdr->numverts_vbo; v++)
			{
				// it could use the same xyz but have different s and t
				if (desc[v].vertindex == vertindex && (int) desc[v].st[0] == s && (int) desc[v].st[1] == t)
				{
					// exists; emit an index for it
					indexes[hdr->numindexes++] = v;

					// no need to check any more
					break;
				}
			}

			if (v == hdr->numverts_vbo)
			{
				// doesn't exist; emit a new vert and index
				indexes[hdr->numindexes++] = hdr->numverts_vbo;

				desc[hdr->numverts_vbo].vertindex = vertindex;
				desc[hdr->numverts_vbo].st[0] = s;
				desc[hdr->numverts_vbo++].st[1] = t;
			}
		}
	}

	// upload immediately
	GLMesh_LoadVertexBuffer (m, hdr);
}


/*
================
GLMesh_LoadVertexBuffers

Loop over all precached alias models, and upload each one to a VBO.
================
*/
void GLMesh_LoadVertexBuffers (void)
{
	int j;
	qmodel_t *m;
	const aliashdr_t *hdr;

	for (j = 1; j < MAX_MODELS; j++)
	{
		if (!(m = cl.model_precache[j])) break;
		if (m->type != mod_alias) continue;

		hdr = (const aliashdr_t *) Mod_Extradata (m);

		GLMesh_LoadVertexBuffer (m, hdr);
	}
}

/*
================
GLMesh_DeleteVertexBuffers

Delete VBOs for all loaded alias models
================
*/
void GLMesh_DeleteVertexBuffers (void)
{
	int j;
	qmodel_t *m;

	for (j = 1; j < MAX_MODELS; j++)
	{
		if (!(m = cl.model_precache[j])) break;
		if (m->type != mod_alias) continue;

		glDeleteBuffers (1, &m->meshvbo);
		m->meshvbo = 0;

		glDeleteBuffers (1, &m->meshindexesvbo);
		m->meshindexesvbo = 0;
	}

	GL_ClearBufferBindings ();
}


