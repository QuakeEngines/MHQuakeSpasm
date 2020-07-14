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

// draw.c -- 2d drawing

#include "quakedef.h"

// extern unsigned char d_15to8table[65536]; // johnfitz -- never used

qpic_t *draw_disc;
qpic_t *draw_backtile;

gltexture_t *char_texture; // johnfitz
qpic_t *pic_ovr, *pic_ins; // johnfitz -- new cursor handling
qpic_t *pic_nul; // johnfitz -- for missing gfx, don't crash
qpic_t *pic_crosshair; // MH - custom crosshair

// johnfitz -- new pics
byte pic_ovr_data[8][8] =
{
	{ 255, 255, 255, 255, 255, 255, 255, 255 },
	{ 255, 15, 15, 15, 15, 15, 15, 255 },
	{ 255, 15, 15, 15, 15, 15, 15, 2 },
	{ 255, 15, 15, 15, 15, 15, 15, 2 },
	{ 255, 15, 15, 15, 15, 15, 15, 2 },
	{ 255, 15, 15, 15, 15, 15, 15, 2 },
	{ 255, 15, 15, 15, 15, 15, 15, 2 },
	{ 255, 255, 2, 2, 2, 2, 2, 2 },
};

byte pic_ins_data[9][8] =
{
	{ 15, 15, 255, 255, 255, 255, 255, 255 },
	{ 15, 15, 2, 255, 255, 255, 255, 255 },
	{ 15, 15, 2, 255, 255, 255, 255, 255 },
	{ 15, 15, 2, 255, 255, 255, 255, 255 },
	{ 15, 15, 2, 255, 255, 255, 255, 255 },
	{ 15, 15, 2, 255, 255, 255, 255, 255 },
	{ 15, 15, 2, 255, 255, 255, 255, 255 },
	{ 15, 15, 2, 255, 255, 255, 255, 255 },
	{ 255, 2, 2, 255, 255, 255, 255, 255 },
};

byte pic_nul_data[8][8] =
{
	{ 252, 252, 252, 252, 0, 0, 0, 0 },
	{ 252, 252, 252, 252, 0, 0, 0, 0 },
	{ 252, 252, 252, 252, 0, 0, 0, 0 },
	{ 252, 252, 252, 252, 0, 0, 0, 0 },
	{ 0, 0, 0, 0, 252, 252, 252, 252 },
	{ 0, 0, 0, 0, 252, 252, 252, 252 },
	{ 0, 0, 0, 0, 252, 252, 252, 252 },
	{ 0, 0, 0, 0, 252, 252, 252, 252 },
};

byte pic_stipple_data[8][8] =
{
	{ 255, 0, 0, 0, 255, 0, 0, 0 },
	{ 0, 0, 255, 0, 0, 0, 255, 0 },
	{ 255, 0, 0, 0, 255, 0, 0, 0 },
	{ 0, 0, 255, 0, 0, 0, 255, 0 },
	{ 255, 0, 0, 0, 255, 0, 0, 0 },
	{ 0, 0, 255, 0, 0, 0, 255, 0 },
	{ 255, 0, 0, 0, 255, 0, 0, 0 },
	{ 0, 0, 255, 0, 0, 0, 255, 0 },
};

byte pic_crosshair_data[8][8] =
{
	{ 255, 255, 255, 255, 255, 255, 255, 255 },
	{ 255, 255, 255, 8, 9, 255, 255, 255 },
	{ 255, 255, 255, 6, 8, 2, 255, 255 },
	{ 255, 6, 8, 8, 6, 8, 8, 255 },
	{ 255, 255, 2, 8, 8, 2, 2, 2 },
	{ 255, 255, 255, 7, 8, 2, 255, 255 },
	{ 255, 255, 255, 255, 2, 2, 255, 255 },
	{ 255, 255, 255, 255, 255, 255, 255, 255 },
};
// johnfitz

typedef struct glpic_s {
	gltexture_t *gltexture;
	float		sl, tl, sh, th;
} glpic_t;

canvastype currentcanvas = CANVAS_NONE; // johnfitz -- for GL_SetCanvas

// ==============================================================================
//  PIC CACHING
// ==============================================================================

typedef struct cachepic_s {
	char		name[MAX_QPATH];
	qpic_t		pic;
	byte		padding[32];	// for appended glpic
} cachepic_t;

#define	MAX_CACHED_PICS		128
cachepic_t	menu_cachepics[MAX_CACHED_PICS];
int			menu_numcachepics;

byte		menuplyr_pixels[4096];

//  scrap allocation
//  Allocate all the little status bar obejcts into a single texture
//  to crutch up stupid hardware / drivers

// GLQuake had 2 scrap textures at 256x256 each; this lets us create a single texture with ample extra space, as well as pack more sbar widgets into it
#define	SCRAP_SIZE		512

int			scrap_allocated[SCRAP_SIZE];
unsigned	scrap_block[SCRAP_SIZE * SCRAP_SIZE];
gltexture_t *scrap_texture; // johnfitz


static qboolean Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	int best = SCRAP_SIZE;

	for (int i = 0; i < SCRAP_SIZE - w; i++)
	{
		int j;
		int best2 = 0;

		for (j = 0; j < w; j++)
		{
			if (scrap_allocated[i + j] >= best)
				break;

			if (scrap_allocated[i + j] > best2)
				best2 = scrap_allocated[i + j];
		}

		if (j == w)
		{
			// this is a valid spot
			*x = i;
			*y = best = best2;
		}
	}

	if (best + h > SCRAP_SIZE)
		return false;

	for (int i = 0; i < w; i++)
		scrap_allocated[*x + i] = best + h;

	return true;
}


void Scrap_Init (void)
{
	// empty scrap and reallocate gltextures
	memset (scrap_allocated, 0, sizeof (scrap_allocated));
	memset (scrap_block, 0, sizeof (scrap_block));

	// init the scrap texture - we'll texsubimage load new stuff onto it
	scrap_texture = TexMgr_LoadImage (NULL, "scrap_texture", SCRAP_SIZE, SCRAP_SIZE, SRC_RGBA, (byte *) scrap_block, "", (src_offset_t) scrap_block, TEXPREF_ALPHA | TEXPREF_OVERWRITE);
}


/*
================
Draw_PicFromWad
================
*/
qpic_t *Draw_PicFromWad (const char *name, qboolean scrappable)
{
	qpic_t *p;
	glpic_t	gl;
	src_offset_t offset; // johnfitz

	p = (qpic_t *) W_GetLumpName (name);
	if (!p) return pic_nul; // johnfitz

	// load little ones into the scrap
	if (scrappable)
	{
		int		x, y;

		// if it won't go in the scrap just load it as a regular texture
		if (!Scrap_AllocBlock (p->width, p->height, &x, &y))
			return Draw_PicFromWad (name, false);

		for (int i = 0, k = 0; i < p->height; i++)
			for (int j = 0; j < p->width; j++, k++)
				scrap_block[(y + i) * SCRAP_SIZE + x + j] = d_8to24table[p->data[k]];

		// texsubimage it immediately so that we don't need to track dirty scrap state
		GL_BindTexture (GL_TEXTURE0, scrap_texture);
		glPixelStorei (GL_UNPACK_ROW_LENGTH, SCRAP_SIZE);
		glTexSubImage2D (GL_TEXTURE_2D, 0, x, y, p->width, p->height, GL_RGBA, GL_UNSIGNED_BYTE, &scrap_block[y * SCRAP_SIZE + x]);
		glPixelStorei (GL_UNPACK_ROW_LENGTH, 0);

		// and store it out
		gl.gltexture = scrap_texture;

		// johnfitz -- no longer go from 0.01 to 0.99
		gl.sl = x / (float) SCRAP_SIZE;
		gl.sh = (x + p->width) / (float) SCRAP_SIZE;
		gl.tl = y / (float) SCRAP_SIZE;
		gl.th = (y + p->height) / (float) SCRAP_SIZE;
	}
	else
	{
		char texturename[64]; // johnfitz
		q_snprintf (texturename, sizeof (texturename), "%s:%s", WADFILENAME, name); // johnfitz

		offset = (src_offset_t) p - (src_offset_t) wad_base + sizeof (int) * 2; // johnfitz

		gl.gltexture = TexMgr_LoadImage (NULL, texturename, p->width, p->height, SRC_INDEXED, p->data, WADFILENAME, offset, TEXPREF_ALPHA | TEXPREF_PAD); // johnfitz -- TexMgr
		gl.sl = 0;
		gl.sh = (float) p->width / (float) Image_PadConditional (p->width); // johnfitz
		gl.tl = 0;
		gl.th = (float) p->height / (float) Image_PadConditional (p->height); // johnfitz
	}

	memcpy (p->data, &gl, sizeof (glpic_t));

	return p;
}

/*
================
Draw_CachePic
================
*/
qpic_t *Draw_CachePic (const char *path)
{
	cachepic_t *pic;
	int			i;
	qpic_t *dat;
	glpic_t		gl;

	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
	{
		if (!strcmp (path, pic->name))
			return &pic->pic;
	}
	if (menu_numcachepics == MAX_CACHED_PICS)
		Sys_Error ("menu_numcachepics == MAX_CACHED_PICS");
	menu_numcachepics++;
	strcpy (pic->name, path);

	// load the pic from disk
	dat = (qpic_t *) COM_LoadTempFile (path, NULL);
	if (!dat)
		Sys_Error ("Draw_CachePic: failed to load %s", path);
	SwapPic (dat);

	// HACK HACK HACK --- we need to keep the bytes for
	// the translatable player picture just for the menu
	// configuration dialog
	if (!strcmp (path, "gfx/menuplyr.lmp"))
		memcpy (menuplyr_pixels, dat->data, dat->width * dat->height);

	pic->pic.width = dat->width;
	pic->pic.height = dat->height;

	gl.gltexture = TexMgr_LoadImage (NULL, path, dat->width, dat->height, SRC_INDEXED, dat->data, path, sizeof (int) * 2, TEXPREF_ALPHA | TEXPREF_PAD); // johnfitz -- TexMgr
	gl.sl = 0;
	gl.sh = (float) dat->width / (float) Image_PadConditional (dat->width); // johnfitz
	gl.tl = 0;
	gl.th = (float) dat->height / (float) Image_PadConditional (dat->height); // johnfitz
	memcpy (pic->pic.data, &gl, sizeof (glpic_t));

	return &pic->pic;
}

/*
================
Draw_MakePic -- johnfitz -- generate pics from internal data
================
*/
qpic_t *Draw_MakePic (const char *name, int width, int height, byte *data)
{
	int flags = TEXPREF_NEAREST | TEXPREF_ALPHA | TEXPREF_PERSIST | TEXPREF_PAD;
	qpic_t *pic;
	glpic_t		gl;

	pic = (qpic_t *) Hunk_Alloc (sizeof (qpic_t) - 4 + sizeof (glpic_t));
	pic->width = width;
	pic->height = height;

	gl.gltexture = TexMgr_LoadImage (NULL, name, width, height, SRC_INDEXED, data, "", (src_offset_t) data, flags);
	gl.sl = 0;
	gl.sh = (float) width / (float) Image_PadConditional (width);
	gl.tl = 0;
	gl.th = (float) height / (float) Image_PadConditional (height);
	memcpy (pic->data, &gl, sizeof (glpic_t));

	return pic;
}

// ==============================================================================
//  INIT
// ==============================================================================

/*
===============
Draw_LoadPics -- johnfitz
===============
*/
void Draw_LoadPics (void)
{
	byte *data = (byte *) W_GetLumpName ("conchars");

	if (!data) Sys_Error ("Draw_LoadPics: couldn't load conchars");

	src_offset_t offset = (src_offset_t) data - (src_offset_t) wad_base;

	char_texture = TexMgr_LoadImage (NULL, WADFILENAME":conchars", 128, 128, SRC_INDEXED, data, WADFILENAME, offset, TEXPREF_ALPHA | TEXPREF_NEAREST | TEXPREF_CONCHARS);

	draw_disc = Draw_PicFromWad ("disc", true);
	draw_backtile = Draw_PicFromWad ("backtile", false); // this can't go in the scrap because it needs to repeat
}

/*
===============
Draw_NewGame -- johnfitz
===============
*/
void Draw_NewGame (void)
{
	cachepic_t *pic;
	int			i;

	Scrap_Init ();

	// reload wad pics
	W_LoadWadFile (); // johnfitz -- filename is now hard-coded for honesty
	Draw_LoadPics ();
	SCR_LoadPics ();
	Sbar_LoadPics ();

	// empty lmp cache
	for (pic = menu_cachepics, i = 0; i < menu_numcachepics; pic++, i++)
		pic->name[0] = 0;
	menu_numcachepics = 0;
}

/*
===============
Draw_Init -- johnfitz -- rewritten
===============
*/
void Draw_Init (void)
{
	// this must be static so that the texture loader can grab a copy of the pointer for reloading
	static byte chbase[] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xff, 0xff,
		0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xfe, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	};

	Scrap_Init ();

	// create internal pics
	pic_ins = Draw_MakePic ("ins", 8, 9, &pic_ins_data[0][0]);
	pic_ovr = Draw_MakePic ("ovr", 8, 8, &pic_ovr_data[0][0]);
	pic_nul = Draw_MakePic ("nul", 8, 8, &pic_nul_data[0][0]);
	pic_crosshair = Draw_MakePic ("crosshair", 16, 16, chbase);

	// load game pics
	Draw_LoadPics ();
}


static GLuint draw_textured_vp = 0;
static GLuint draw_textured_fp = 0;
static GLuint draw_coloured_vp = 0;
static GLuint draw_coloured_fp = 0;

void GLDraw_CreateShaders (void)
{
	const GLchar *vp_textured_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# transform position to output\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], vertex.attrib[0];\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], vertex.attrib[0];\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], vertex.attrib[0];\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# copy over colour\n"
		"MOV result.color, vertex.attrib[1];\n"
		"\n"
		"# copy over texcoord\n"
		"MOV result.texcoord[0], vertex.attrib[2];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_textured_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff, fence;\n"
		"\n"
		"# perform the texturing\n"
		"TEX diff, fragment.texcoord[0], texture[0], 2D;\n"
		"\n"
		"# fence texture test\n"
		"SUB fence, diff, 0.666;\n"
		"KIL fence.a;\n"
		"\n"
		"# blend with the colour\n"
		"MUL diff, diff, fragment.color;\n"
		"\n"
		"# apply the contrast\n"
		"MUL diff.rgb, diff, program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW result.color.r, diff.r, program.env[10].y;\n"
		"POW result.color.g, diff.g, program.env[10].y;\n"
		"POW result.color.b, diff.b, program.env[10].y;\n"
		"MOV result.color.a, diff.a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *vp_coloured_source = \
		"!!ARBvp1.0\n"
		"\n"
		"# transform position to output\n"
		"DP4 result.position.x, state.matrix.mvp.row[0], vertex.attrib[0];\n"
		"DP4 result.position.y, state.matrix.mvp.row[1], vertex.attrib[0];\n"
		"DP4 result.position.z, state.matrix.mvp.row[2], vertex.attrib[0];\n"
		"DP4 result.position.w, state.matrix.mvp.row[3], vertex.attrib[0];\n"
		"\n"
		"# copy over colour\n"
		"MOV result.color, vertex.attrib[1];\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	const GLchar *fp_coloured_source = \
		"!!ARBfp1.0\n"
		"\n"
		"TEMP diff;\n"
		"\n"
		"# apply the contrast\n"
		"MUL diff, fragment.color, program.env[10].x;\n"
		"\n"
		"# apply the gamma (POW only operates on scalars)\n"
		"POW result.color.r, diff.r, program.env[10].y;\n"
		"POW result.color.g, diff.g, program.env[10].y;\n"
		"POW result.color.b, diff.b, program.env[10].y;\n"
		"MOV result.color.a, diff.a;\n"
		"\n"
		"# done\n"
		"END\n"
		"\n";

	draw_textured_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_textured_source);
	draw_textured_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_textured_source);
	draw_coloured_vp = GL_CreateARBProgram (GL_VERTEX_PROGRAM_ARB, vp_coloured_source);
	draw_coloured_fp = GL_CreateARBProgram (GL_FRAGMENT_PROGRAM_ARB, fp_coloured_source);
}


/*
==============================================================================

2D DRAWING

==============================================================================
*/

void Draw_TexturedQuad (gltexture_t *texture, float x, float y, float w, float h, unsigned colour, float sl, float sh, float tl, float th)
{
	if (((byte *) &colour)[3] < 255)
		GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	else GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);

	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);
	GL_BindPrograms (draw_textured_vp, draw_textured_fp);
	GL_BindTexture (GL_TEXTURE0, texture);

	glBegin (GL_QUADS);

	glVertexAttrib2f (2, sl, tl);
	glVertexAttrib4Nubv (1, (byte *) &colour);
	glVertexAttrib2f (0, x, y);

	glVertexAttrib2f (2, sh, tl);
	glVertexAttrib4Nubv (1, (byte *) &colour);
	glVertexAttrib2f (0, x + w, y);

	glVertexAttrib2f (2, sh, th);
	glVertexAttrib4Nubv (1, (byte *) &colour);
	glVertexAttrib2f (0, x + w, y + h);

	glVertexAttrib2f (2, sl, th);
	glVertexAttrib4Nubv (1, (byte *) &colour);
	glVertexAttrib2f (0, x, y + h);

	glEnd ();
}


void Draw_ColouredQuad (float x, float y, float w, float h, unsigned colour)
{
	if (((byte *) &colour)[3] < 255)
		GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	else GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);

	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);
	GL_BindPrograms (draw_coloured_vp, draw_coloured_fp);

	glBegin (GL_QUADS);

	glVertexAttrib4Nubv (1, (byte *) &colour);
	glVertexAttrib2f (0, x, y);

	glVertexAttrib4Nubv (1, (byte *) &colour);
	glVertexAttrib2f (0, x + w, y);

	glVertexAttrib4Nubv (1, (byte *) &colour);
	glVertexAttrib2f (0, x + w, y + h);

	glVertexAttrib4Nubv (1, (byte *) &colour);
	glVertexAttrib2f (0, x, y + h);

	glEnd ();
}


/*
================
Draw_String
================
*/
void Draw_StringWithSpacing (int x, int y, const char *str, int spacing)
{
	// totally off screen
	if (y <= -8) return;

	GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);

	GL_BindPrograms (draw_textured_vp, draw_textured_fp);
	GL_BindTexture (GL_TEXTURE0, char_texture);

	glBegin (GL_QUADS);

	for (int i = 0; str[i]; i++, x += 8)
	{
		int num = str[i] & 255;

		// space
		if (num == 32) continue;

		// to do - precalc these
		float frow = (num >> 4) * 0.0625;
		float fcol = (num & 15) * 0.0625;
		float size = 0.0625;
		unsigned colour = 0xffffffff;

		glVertexAttrib2f (2, fcol, frow);
		glVertexAttrib4Nubv (1, (byte *) &colour);
		glVertexAttrib2f (0, x, y);

		glVertexAttrib2f (2, fcol + size, frow);
		glVertexAttrib4Nubv (1, (byte *) &colour);
		glVertexAttrib2f (0, x + 8, y);

		glVertexAttrib2f (2, fcol + size, frow + size);
		glVertexAttrib4Nubv (1, (byte *) &colour);
		glVertexAttrib2f (0, x + 8, y + 8);

		glVertexAttrib2f (2, fcol, frow + size);
		glVertexAttrib4Nubv (1, (byte *) &colour);
		glVertexAttrib2f (0, x, y + 8);
	}

	glEnd ();
}


void Draw_String (int x, int y, const char *str)
{
	// default spacing (8) can be overridden for sbar widgets which sometimes use 6, 7...
	Draw_StringWithSpacing (x, y, str, 8);
}


void Draw_ScrollString (int x, int y, int width, char *str)
{
	// this doesn't scroll smooth like the original but the one-char-at-a-time scroll feels more "Quakey"
	char buf[41];
	char *str2 = va ("%s %c%c ", str, '\35', '\37');
	int len = strlen (str2);
	int start = (int) (realtime * 5) % len;

	for (int i = 0; i < 40; i++)
		buf[i] = str2[(start + i) % len];

	// terminate at the sbar max length
	buf[39] = 0;

	Draw_String (x, y, buf);
}


/*
================
Draw_Character
================
*/
void Draw_Character (int x, int y, int num)
{
	char str[2] = { num, 0 };
	Draw_String (x, y, str);
}


void Draw_ColouredPic (int x, int y, qpic_t *pic, unsigned color)
{
	glpic_t *gl = (glpic_t *) pic->data;
	Draw_TexturedQuad (gl->gltexture, x, y, pic->width, pic->height, color, gl->sl, gl->sh, gl->tl, gl->th);
}


void Draw_AlphaPic (int x, int y, qpic_t *pic, float alpha)
{
	if (alpha > 0)
	{
		if (alpha < 1)
			Draw_ColouredPic (x, y, pic, 0xffffff | (int) (alpha * 255) << 24);
		else Draw_ColouredPic (x, y, pic, 0xffffffff);
	}
}


/*
=============
Draw_Pic -- johnfitz -- modified
=============
*/
void Draw_Pic (int x, int y, qpic_t *pic)
{
	Draw_AlphaPic (x, y, pic, 1);
}


/*
=============
Draw_TransPicTranslate -- johnfitz -- rewritten to use texmgr to do translation

Only used for the player color selection menu
=============
*/
void Draw_TransPicTranslate (int x, int y, qpic_t *pic, int top, int bottom)
{
	static int oldtop = -2;
	static int oldbottom = -2;

	if (top != oldtop || bottom != oldbottom)
	{
		glpic_t *p = (glpic_t *) pic->data;
		gltexture_t *glt = p->gltexture;

		oldtop = top;
		oldbottom = bottom;

		TexMgr_ReloadImage (glt, top, bottom);
	}

	Draw_Pic (x, y, pic);
}

/*
================
Draw_ConsoleBackground -- johnfitz -- rewritten
================
*/
void Draw_ConsoleBackground (void)
{
	qpic_t *pic = Draw_CachePic ("gfx/conback.lmp");
	float alpha = (con_forcedup) ? 1.0 : ((float) scr_con_current / vid.conheight) * 1.2f; // original engine conalpha

	pic->width = vid.conwidth;
	pic->height = vid.conheight;

	GL_SetCanvas (CANVAS_CONSOLE); // in case this is called from weird places
	Draw_AlphaPic (0, 0, pic, alpha);
}


/*
=============
Draw_TileClear

This repeats a 64*64 tile graphic to fill the screen around a sized down
refresh window.
=============
*/
void Draw_TileClear (int x, int y, int w, int h)
{
	glpic_t *gl = (glpic_t *) draw_backtile->data;
	Draw_TexturedQuad (gl->gltexture, x, y, w, h, 0xffffffff, x / 64.0, (x + w) / 64.0, y / 64.0, (y + h) / 64.0);
}


/*
=============
Draw_Fill

Fills a box of pixels with a single color
=============
*/
void Draw_Fill (int x, int y, int w, int h, int c, float alpha) // johnfitz -- added alpha
{
	if (alpha > 0)
	{
		unsigned rgba = d_8to24table[c]; // johnfitz -- use d_8to24table instead of host_basepal

		if (alpha < 1)
		{
			rgba &= 0x00ffffff;
			rgba |= (int) (alpha * 255) << 24;
		}

		Draw_ColouredQuad (x, y, w, h, rgba);
	}
}


/*
================
Draw_FadeScreen -- johnfitz -- revised
================
*/
void Draw_FadeScreen (void)
{
	GL_SetCanvas (CANVAS_DEFAULT);
	Draw_ColouredQuad (0, 0, glwidth, glheight, 0x7f000000);
}


/*
==============
Draw_Crosshair -- johnfitz
==============
*/
void Draw_Crosshair (void)
{
	extern	cvar_t	crosshair;
	extern	cvar_t	crosshaircolor;

	if (!crosshair.value)
		return;

	GL_SetCanvas (CANVAS_CROSSHAIR);
	Draw_ColouredPic ((-pic_crosshair->width) / 2, (-pic_crosshair->height) / 2, pic_crosshair, d_8to24table[(int) crosshaircolor.value & 255]); // johnfitz -- stretched menus
}


/*
================
GL_SetCanvas -- johnfitz -- support various canvas types
================
*/
void GL_SetCanvas (canvastype newcanvas)
{
	float s;
	int lines;

	if (newcanvas == currentcanvas)
		return;

	currentcanvas = newcanvas;

	glMatrixMode (GL_PROJECTION);
	glLoadIdentity ();

	switch (newcanvas)
	{
	case CANVAS_DEFAULT:
		glOrtho (0, glwidth, glheight, 0, -99999, 99999);
		glViewport (glx, gly, glwidth, glheight);
		break;

	case CANVAS_CONSOLE:
		lines = vid.conheight - (scr_con_current * vid.conheight / glheight);
		glOrtho (0, vid.conwidth, vid.conheight + lines, lines, -99999, 99999);
		glViewport (glx, gly, glwidth, glheight);
		break;

	case CANVAS_MENU:
		s = Q_fmin ((float) glwidth / 320.0, (float) glheight / 200.0);
		s = Q_fclamp (scr_menuscale.value, 1.0f, s);
		// ericw -- doubled width to 640 to accommodate long keybindings
		glOrtho (0, 640, 200, 0, -99999, 99999);
		glViewport (glx + (glwidth - 320 * s) / 2, gly + ((glheight - 200 * s) / 3) * 2, 640 * s, 200 * s); // MH - adjust upwards
		break;

	case CANVAS_SBAR:
		s = Q_fclamp (scr_sbarscale.value, 1.0f, (float) glwidth / 320.0);

		if (cl.gametype == GAME_DEATHMATCH)
		{
			glOrtho (0, glwidth / s, 48, 0, -99999, 99999);
			glViewport (glx, gly, glwidth, 48 * s);
		}
		else
		{
			glOrtho (0, 320, 48, 0, -99999, 99999);
			glViewport (glx + (glwidth - 320 * s) / 2, gly, 320 * s, 48 * s);
		}

		break;

	case CANVAS_CROSSHAIR: // 0,0 is center of viewport
		s = Q_fclamp (scr_crosshairscale.value, 1.0, 10.0);
		glOrtho (r_refdef.vrect.width / -2 / s, r_refdef.vrect.width / 2 / s, r_refdef.vrect.height / 2 / s, r_refdef.vrect.height / -2 / s, -99999, 99999);
		glViewport (r_refdef.vrect.x, glheight - r_refdef.vrect.y - r_refdef.vrect.height, r_refdef.vrect.width & ~1, r_refdef.vrect.height & ~1);
		break;

	case CANVAS_BOTTOMLEFT: // used by devstats
		s = (float) glwidth / vid.conwidth; // use console scale
		glOrtho (0, 320, 200, 0, -99999, 99999);
		glViewport (glx, gly, 320 * s, 200 * s);
		break;

	case CANVAS_BOTTOMRIGHT: // used by fps/clock
		s = (float) glwidth / vid.conwidth; // use console scale
		glOrtho (0, 320, 200, 0, -99999, 99999);
		glViewport (glx + glwidth - 320 * s, gly, 320 * s, 200 * s);
		break;

	case CANVAS_TOPRIGHT: // used by disc
		s = 1;
		glOrtho (0, 320, 200, 0, -99999, 99999);
		glViewport (glx + glwidth - 320 * s, gly + glheight - 200 * s, 320 * s, 200 * s);
		break;

	default:
		Sys_Error ("GL_SetCanvas: bad canvas type");
	}

	glMatrixMode (GL_MODELVIEW);
	glLoadIdentity ();
}


/*
================
GL_Set2D -- johnfitz -- rewritten
================
*/
void GL_Set2D (void)
{
	currentcanvas = CANVAS_INVALID;
	GL_SetCanvas (CANVAS_DEFAULT);

	glDisable (GL_CULL_FACE);

	// ensure that no buffer is bound when drawing 2D quads
	GL_BindBuffer (GL_ARRAY_BUFFER, 0);

	// and no vertex arrays used
	GL_EnableVertexAttribArrays (0);
}


void GL_End2D (void)
{
}

