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
// gl_warp.c -- warping animation support

#include "quakedef.h"

cvar_t r_waterwarp = { "r_waterwarp", "1", CVAR_NONE };


GLuint r_waterwarp_vp = 0;
GLuint r_waterwarp_fp = 0;

GLuint r_underwater_vp = 0;
GLuint r_underwater_fp = 0;

gltexture_t *r_underwater_noise = NULL;


void GLWarp_CreateShaders (void)
{
	const GLchar *vp_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# transform position to output\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], vertex.attrib[0];\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], vertex.attrib[0];\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], vertex.attrib[0];\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# copy over texcoords adjusting[1] for time\n"
		"MOV result.texcoord[0], vertex.attrib[1];\n"
		"MAD result.texcoord[1], vertex.attrib[1], 3.14159265358979323846, program.env[0];\n"
		"\n"
		"# set up fog coordinate\n"
		"DP4 result.fogcoord.x, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff;\n"
		"TEMP warpcoord;\n"
		"\n"
		"# perform the warp\n"
		"SIN warpcoord.x, fragment.texcoord[1].y;\n"
		"SIN warpcoord.y, fragment.texcoord[1].x;\n"
		"\n"
		"# adjust for the warped coord\n"
		"MAD warpcoord, warpcoord, 0.125, fragment.texcoord[0];\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, warpcoord, texture[0], 2D;\n"
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
		"MOV result.color.a, program.local[0].a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *vp_underwater_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# copy over position\n"
		"MOV result.position, vertex.attrib[0];\n"
		"\n"
		"# copy over texcoords\n"
		"MOV result.texcoord[0], vertex.attrib[1];\n"
		"ADD result.texcoord[1], vertex.attrib[2], program.local[0];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	// this is a post-process so the input has already had gamma applied
	const GLchar *fp_underwater_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff, noisecoord, v_blend;\n"
		"\n"
		"# read the noise image\n"
		"TEX noisecoord, fragment.texcoord[1], texture[0], 2D;\n"
		"MAD noisecoord, noisecoord, program.local[1], fragment.texcoord[0];\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, noisecoord, texture[5], RECT;\n"
		"\n"
		"# apply the contrast\n"
		"MUL v_blend.rgb, program.local[0], program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW v_blend.r, v_blend.r, program.env[10].y;\n"
		"POW v_blend.g, v_blend.g, program.env[10].y;\n"
		"POW v_blend.b, v_blend.b, program.env[10].y;\n"
		"MOV v_blend.a, program.local[0].a;\n"
		"\n"
		"# blend to output\n"
		"LRP result.color, v_blend.a, v_blend, diff;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	r_waterwarp_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_source);
	r_waterwarp_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_source);

	r_underwater_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_underwater_source);
	r_underwater_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_underwater_source);
}


void GLWarp_CreateTextures (void)
{
#define NOISESIZE	16
	// persistently allocate so that it will reload properly via TexMgr
	static unsigned noise_data[] = {
		0x84be2329, 0xaed66ce1, 0xf1499052, 0xebe9bbf1, 0x3cdba6b3, 0x993e0c87, 0x1c0d5e24, 0xde47b706, 0xc84d12b3, 0xa68bbb43, 0x7d5a031f, 0x1f253809, 0xfccbd45d, 0x3b45f596, 0x0a890d13, 0x32aedb1c,
		0xee509a20, 0xfd367840, 0xf6324912, 0xdc497d9e, 0xf2144fad, 0xd0664044, 0xb730c46b, 0x22a13b32, 0x9d9122f6, 0xda1f8be1, 0x0299cab0, 0x499d72b9, 0xc57e802c, 0x80e9d599, 0xccc9eab2, 0xd667bf53,
		0x7ed614bf, 0x668edc2d, 0x4957ef83, 0x8f69ff61, 0x1ed1cd61, 0x72169c9d, 0xf01de672, 0x774a4f84, 0x39e8d702, 0xc9cb532c, 0x74331e12, 0xd5f40c9e, 0xa4d49fd4, 0xcf357e59, 0xccf42232, 0x2d90d3cf,
		0x758fd348, 0x2a1dd9e6, 0x2bf7c0e5, 0x44878178, 0x00505f0e, 0xbe8d61d4, 0x0715057b, 0x1f82333b, 0xda927018, 0xb1ce5464, 0x15693e85, 0x046a46f8, 0xd90e7396, 0x68672f16, 0x4a4af7d4, 0x766857d0,
		0x11bb16fa, 0x8824aead, 0xdb52fe79, 0x3ce54325, 0xd8d345f4, 0xf50bce28, 0x3d5960c5, 0x598a2797, 0xc2d02d76, 0xd468cdc9, 0x25796a49, 0x14406108, 0xa56a3bb1, 0x8cc12811, 0x870ba9d6, 0xf12f8c97,
		0x959a1d15, 0xc0e19bc1, 0x9aa8e97e, 0xb5c286a7, 0xe79abf54, 0x55d123d9, 0xd1283890, 0x66a16cd9, 0x30e14e5e, 0x71d9fe9c, 0xe2a5e29f, 0x47b49b0c, 0x462a3865, 0x7982a989, 0xc278767a, 0xdf26b163,
		0x3e6d29da, 0x1296e062, 0xa639bf34, 0xf15e893f, 0x6ce30e6d, 0x201ea128, 0x03c2cb1d, 0x8407413f, 0x6505140f, 0xc961281b, 0x8e2ce7c5, 0xdc083646, 0xfe8da8f3, 0x71ebf2be, 0x3bd0a0ff, 0x7e8c0675,
		0x4d737887, 0xbe82bed0, 0x4146c2db, 0x30fa8c2b, 0xa7f0707f, 0x95328654, 0x13685baa, 0xf5fce60b, 0x9f7dbeca, 0x1b418a89, 0x684fb8fd, 0x147b72f6, 0x0dd3cd99, 0xb43a44f0, 0x335366a6, 0x10a1cb0b,
		0x03ec4c5e, 0x05e6734c, 0xaa0e31b4, 0xb0d5cfad, 0xd8ff27ca, 0xf44d149d, 0x42592779, 0xf8c19c7c, 0x20878ccd, 0xa6b86423, 0xb04c9587, 0x2d4e8d5a, 0xb13de799, 0x80b1de60, 0xe94108ad, 0xd5a54167,
		0x9f18e49f, 0x26004215, 0x21d14cfe, 0xb32f9304, 0x4053738f, 0x7eaf8a43, 0xcfd56fca, 0xce95a1d3, 0x2765be5a, 0xad07f62a, 0xa665bea1, 0x69c0c9b4, 0x2c093432, 0x178f014d, 0x9ddbc656, 0x0bd8a6c8,
		0x61388188, 0x6212686b, 0xe7d054f9, 0x78481771, 0x1d29920d, 0x72992986, 0xfa1c74db, 0xb5b8374f, 0xf55795b0, 0x6d6c80df, 0x8bd9748d, 0x08116543, 0xbd79f6a5, 0xb815ebf7, 0x8f60e1e0, 0xf47b3c6e,
		0x8a8a625b, 0xf75c278f, 0x3b4a87e5, 0x40619b32, 0xb1c3c684, 0x104a30a7, 0x036f75ee, 0xef6a9e2f, 0xc89b5010, 0x28294381, 0x9ee9f68a, 0x4881a147, 0xa4cd6c31, 0xa381de9e, 0xff10988c, 0xcfcd439a,
		0x5950c757, 0x271cbdbf, 0x5d7f2803, 0x49b95f89, 0x3c604e34, 0x9802dee5, 0x2b0db242, 0xbbec14b6, 0xe2732fb8, 0x1d7d7e51, 0x1fd384d8, 0x6b50be01, 0x2143d616, 0x18151983, 0x2e2c2b98, 0xdc0ef98b,
		0x0ecaf0bc, 0x31946d3d, 0x8daf7492, 0xd590a4b5, 0xfc406a5e, 0x4b027680, 0xb1366b17, 0x5a7ddb21, 0x821e72ea, 0x8ca8718d, 0x4ed95eb8, 0xb0bffaaf, 0x751d7494, 0x5810dce5, 0x5bf2da46, 0x5c7fa081,
		0xe9361dcb, 0x55027449, 0x0b1aacd2, 0x2326a9f7, 0x33a35b40, 0x688835b9, 0xd52ae1ad, 0x0a5d32b2, 0xe9dc5ae5, 0xb5eb5d77, 0x6c3ac569, 0x570d9893, 0xdf9a87eb, 0xa2b26804, 0xc6a4e6d5, 0x8d5f77bc,
		0x2ad68fc3, 0xd4a91421, 0x18011104, 0x73bbae8d, 0x20ca601c, 0x2fd65dcf, 0xd7295345, 0x0dcc59a8, 0x55ed26ea, 0xd984804e, 0xb837f82b, 0xa07ad5ed, 0x9ffa4e5c, 0x363cfc21, 0xb0818e85, 0xb1eebf7d
	};

	r_underwater_noise = TexMgr_LoadImage (NULL, "noise_texture", NOISESIZE, NOISESIZE, SRC_RGBA, (byte *) noise_data, "", (src_offset_t) noise_data, TEXPREF_LINEAR | TEXPREF_PERSIST);
}


/*
================
GL_WaterAlphaForEntitySurface -- ericw

Returns the water alpha to use for the entity and surface combination.
================
*/
float GL_WaterAlphaForEntitySurface (entity_t *ent, msurface_t *s)
{
	float entalpha;

	if (ent == NULL || ent->alpha == ENTALPHA_DEFAULT)
		entalpha = GL_WaterAlphaForSurface (s);
	else
		entalpha = ENTALPHA_DECODE (ent->alpha);

	return entalpha;
}


/*
=============
R_BeginTransparentDrawing -- ericw
=============
*/
static void R_BeginTransparentDrawing (float entalpha)
{
	if (entalpha < 1.0f)
	{
		GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		GL_DepthState (GL_TRUE, GL_LEQUAL, GL_FALSE);

		// water alpha
		glProgramLocalParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 0, 1, 1, 1, entalpha);
	}
	else
	{
		GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
		GL_DepthState (GL_TRUE, GL_LEQUAL, GL_TRUE);
	}
}


void Warp_SetShaderConstants (void)
{
	// water warp
	glProgramEnvParameter4fARB (GL_VERTEX_PROGRAM_ARB,
		0,
		cl.time - ((M_PI * 2) * (int) (cl.time / (M_PI * 2))),
		cl.time - ((M_PI * 2) * (int) (cl.time / (M_PI * 2))),
		0,
		0
	);
}


/*
================
R_DrawTextureChains_Water -- johnfitz
================
*/
void R_DrawTextureChains_Water (qmodel_t *model, entity_t *ent, texchain_t chain)
{
	R_SetupWorldVBOState ();

	GL_BindPrograms (r_waterwarp_vp, r_waterwarp_fp);

	for (int i = 0; i < model->numtextures; i++)
	{
		texture_t *t = model->textures[i];

		if (!t || !t->texturechains[chain] || !(t->texturechains[chain]->flags & SURF_DRAWTURB))
			continue;

		float entalpha = GL_WaterAlphaForEntitySurface (ent, t->texturechains[chain]);

		R_BeginTransparentDrawing (entalpha);

		GL_BindTexture (GL_TEXTURE0, t->gltexture);

		R_DrawSimpleTexturechain (t->texturechains[chain]);
	}
}


void R_UnderwaterWarp (void)
{
	// set up viewport dims
	int srcx = glx + r_refdef.vrect.x;
	int srcy = gly + glheight - r_refdef.vrect.y - r_refdef.vrect.height;
	int srcw = r_refdef.vrect.width;
	int srch = r_refdef.vrect.height;

	// using GL_TEXTURE5 again for binding points that are not GL_TEXTURE_2D
	// bind to default texture object 0
	glActiveTexture (GL_TEXTURE5);
	glBindTexture (GL_TEXTURE_RECTANGLE, 0);

	// we enforced requiring a rectangle texture extension so we don't need to worry about np2 stuff
	glCopyTexImage2D (GL_TEXTURE_RECTANGLE, 0, GL_RGBA, srcx, srcy, srcw, srch, 0);
	glTexParameteri (GL_TEXTURE_RECTANGLE, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri (GL_TEXTURE_RECTANGLE, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf (GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf (GL_TEXTURE_RECTANGLE, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	// clear cached binding and bind the noise texture
	GL_ClearTextureBindings ();
	GL_BindTexture (GL_TEXTURE0, r_underwater_noise);

	// draw the texture back to the framebuffer
	glDisable (GL_CULL_FACE);

	GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);

	glViewport (srcx, srcy, r_refdef.vrect.width, r_refdef.vrect.height);

	float positions[] = { -1, -1, 1, -1, 1, 1, -1, 1 };
	float texcoord1[] = { 0, 0, srcw, 0, srcw, srch, 0, srch };
	float texcoord2[] = { 0, 0,  (float) srcw / srch, 0, (float) srcw / srch, 1, 0, 1 };

	GL_BindBuffer (GL_ARRAY_BUFFER, 0);
	GL_EnableVertexAttribArrays (VAA0 | VAA1 | VAA2);
	GL_BindPrograms (r_underwater_vp, r_underwater_fp);

	// move the warp
	glProgramLocalParameter4fARB (GL_VERTEX_PROGRAM_ARB, 0, (cl.time * 0.09f) - (int) (cl.time * 0.09f), -((cl.time * 0.11f) - (int) (cl.time * 0.11f)), 0, 0);

	// merge the polyblend into the water warp for perf
	glProgramLocalParameter4fvARB (GL_FRAGMENT_PROGRAM_ARB, 0, v_blend);

	// scale the warp to compensate for non-normalized rectangle texture sizes
	glProgramLocalParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 1, (float) srch / 64.0f, (float) srch / 64.0f, 0, 0);

	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, 0, positions);
	glVertexAttribPointer (1, 2, GL_FLOAT, GL_FALSE, 0, texcoord1);
	glVertexAttribPointer (2, 2, GL_FLOAT, GL_FALSE, 0, texcoord2);

	glDrawArrays (GL_QUADS, 0, 4);
}


