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

cvar_t		scr_conalpha = { "scr_conalpha", "0.5", CVAR_ARCHIVE }; // johnfitz

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

#define	MAX_SCRAPS		2
#define	BLOCK_WIDTH		256
#define	BLOCK_HEIGHT	256

int			scrap_allocated[MAX_SCRAPS][BLOCK_WIDTH];
byte		scrap_texels[MAX_SCRAPS][BLOCK_WIDTH * BLOCK_HEIGHT]; // johnfitz -- removed *4 after BLOCK_HEIGHT
qboolean	scrap_dirty;
gltexture_t *scrap_textures[MAX_SCRAPS]; // johnfitz


/*
================
Scrap_AllocBlock

returns an index into scrap_texnums[] and the position inside it
================
*/
int Scrap_AllocBlock (int w, int h, int *x, int *y)
{
	for (int texnum = 0; texnum < MAX_SCRAPS; texnum++)
		if (R_AllocBlock (w, h, x, y, scrap_allocated[texnum], BLOCK_WIDTH, BLOCK_HEIGHT))
			return texnum;

	Sys_Error ("Scrap_AllocBlock: full"); // johnfitz -- correct function name
	return 0; // johnfitz -- shut up compiler
}


/*
================
Scrap_Upload -- johnfitz -- now uses TexMgr

to do - pre-create the scrap texture so that we can just texsubimage new allocations and not have to worry about checking for scrap-dirty at runtime
================
*/
void Scrap_Upload (void)
{
	char name[8];
	int	i;

	for (i = 0; i < MAX_SCRAPS; i++)
	{
		sprintf (name, "scrap%i", i);
		scrap_textures[i] = TexMgr_LoadImage (NULL, name, BLOCK_WIDTH, BLOCK_HEIGHT, SRC_INDEXED, scrap_texels[i], "", (src_offset_t) scrap_texels[i], TEXPREF_ALPHA | TEXPREF_OVERWRITE);
	}

	scrap_dirty = false;
}

/*
================
Draw_PicFromWad
================
*/
qpic_t *Draw_PicFromWad (const char *name)
{
	qpic_t *p;
	glpic_t	gl;
	src_offset_t offset; // johnfitz

	p = (qpic_t *) W_GetLumpName (name);
	if (!p) return pic_nul; // johnfitz

	// load little ones into the scrap
	if (p->width < 64 && p->height < 64)
	{
		int		x, y;
		int		i, j, k;
		int		texnum;

		texnum = Scrap_AllocBlock (p->width, p->height, &x, &y);
		scrap_dirty = true;
		k = 0;
		for (i = 0; i < p->height; i++)
		{
			for (j = 0; j < p->width; j++, k++)
				scrap_texels[texnum][(y + i) * BLOCK_WIDTH + x + j] = p->data[k];
		}
		gl.gltexture = scrap_textures[texnum]; // johnfitz -- changed to an array

		// johnfitz -- no longer go from 0.01 to 0.99
		gl.sl = x / (float) BLOCK_WIDTH;
		gl.sh = (x + p->width) / (float) BLOCK_WIDTH;
		gl.tl = y / (float) BLOCK_WIDTH;
		gl.th = (y + p->height) / (float) BLOCK_WIDTH;
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
	byte *data;
	src_offset_t	offset;

	data = (byte *) W_GetLumpName ("conchars");
	if (!data) Sys_Error ("Draw_LoadPics: couldn't load conchars");
	offset = (src_offset_t) data - (src_offset_t) wad_base;
	char_texture = TexMgr_LoadImage (NULL, WADFILENAME":conchars", 128, 128, SRC_INDEXED, data,
		WADFILENAME, offset, TEXPREF_ALPHA | TEXPREF_NEAREST | TEXPREF_CONCHARS);

	draw_disc = Draw_PicFromWad ("disc");
	draw_backtile = Draw_PicFromWad ("backtile");
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

	// empty scrap and reallocate gltextures
	memset (scrap_allocated, 0, sizeof (scrap_allocated));
	memset (scrap_texels, 255, sizeof (scrap_texels));

	Scrap_Upload (); // creates 2 empty gltextures

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

	Cvar_RegisterVariable (&scr_conalpha);

	// clear scrap and allocate gltextures
	memset (scrap_allocated, 0, sizeof (scrap_allocated));
	memset (scrap_texels, 255, sizeof (scrap_texels));

	Scrap_Upload (); // creates 2 empty textures

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


// ==============================================================================
//  2D DRAWING
// ==============================================================================


typedef struct drawpolyvert_s {
	float xy[2];

	union {
		unsigned colour;
		byte rgba[4];
	};

	float st[2];
} drawpolyvert_t;


// sufficient for a 1024 string
drawpolyvert_t r_drawverts[4096];
int r_numdrawverts;


void Draw_TexturedVertex (drawpolyvert_t *vert, float x, float y, unsigned colour, float s, float t)
{
	vert->xy[0] = x;
	vert->xy[1] = y;
	vert->colour = colour;
	vert->st[0] = s;
	vert->st[1] = t;
}


void Draw_ColouredVertex (drawpolyvert_t *vert, float x, float y, unsigned colour)
{
	vert->xy[0] = x;
	vert->xy[1] = y;
	vert->colour = colour;
}


void Draw_TexturedQuad (gltexture_t *texture, float x, float y, float w, float h, unsigned colour, float sl, float sh, float tl, float th)
{
	if (((byte *) &colour)[3] < 255)
		GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	else GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);

	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);
	GL_BindPrograms (draw_textured_vp, draw_textured_fp);
	GL_BindTexture (GL_TEXTURE0, texture);

	Draw_TexturedVertex (&r_drawverts[0], x, y, colour, sl, tl);
	Draw_TexturedVertex (&r_drawverts[1], x + w, y, colour, sh, tl);
	Draw_TexturedVertex (&r_drawverts[2], x + w, y + h, colour, sh, th);
	Draw_TexturedVertex (&r_drawverts[3], x, y + h, colour, sl, th);

	glDrawArrays (GL_QUADS, 0, 4);
}


void Draw_ColouredQuad (float x, float y, float w, float h, unsigned colour)
{
	if (((byte *) &colour)[3] < 255)
		GL_BlendState (GL_TRUE, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	else GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);

	GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);
	GL_BindPrograms (draw_coloured_vp, draw_coloured_fp);

	Draw_ColouredVertex (&r_drawverts[0], x, y, colour);
	Draw_ColouredVertex (&r_drawverts[1], x + w, y, colour);
	Draw_ColouredVertex (&r_drawverts[2], x + w, y + h, colour);
	Draw_ColouredVertex (&r_drawverts[3], x, y + h, colour);

	glDrawArrays (GL_QUADS, 0, 4);
}


void Draw_BeginString (void)
{
	r_numdrawverts = 0;
}


void Draw_StringCharacter (int x, int y, int num)
{
	if (y <= -8)
		return;			// totally off screen

	num &= 255;

	if (num == 32)
		return; // don't waste verts on spaces

	if (r_numdrawverts + 4 >= 4096)
		Draw_EndString ();

	float frow = (num >> 4) * 0.0625;
	float fcol = (num & 15) * 0.0625;
	float size = 0.0625;

	Draw_TexturedVertex (&r_drawverts[r_numdrawverts++], x, y, 0xffffffff, fcol, frow);
	Draw_TexturedVertex (&r_drawverts[r_numdrawverts++], x + 8, y, 0xffffffff, fcol + size, frow);
	Draw_TexturedVertex (&r_drawverts[r_numdrawverts++], x + 8, y + 8, 0xffffffff, fcol + size, frow + size);
	Draw_TexturedVertex (&r_drawverts[r_numdrawverts++], x, y + 8, 0xffffffff, fcol, frow + size);
}


void Draw_EndString (void)
{
	if (r_numdrawverts)
	{
		GL_DepthState (GL_FALSE, GL_NONE, GL_FALSE);
		GL_BlendState (GL_FALSE, GL_NONE, GL_NONE);
		GL_BindPrograms (draw_textured_vp, draw_textured_fp);
		GL_BindTexture (GL_TEXTURE0, char_texture);

		glDrawArrays (GL_QUADS, 0, r_numdrawverts);
		r_numdrawverts = 0;
	}
}


/*
================
Draw_Character
================
*/
void Draw_Character (int x, int y, int num)
{
	if (y <= -8)
		return;			// totally off screen

	num &= 255;

	if (num == 32)
		return; // don't waste verts on spaces

	float frow = (num >> 4) * 0.0625;
	float fcol = (num & 15) * 0.0625;
	float size = 0.0625;

	Draw_TexturedQuad (char_texture, x, y, 8, 8, 0xffffffff, fcol, fcol + size, frow, frow + size);
}


/*
================
Draw_String
================
*/
void Draw_String (int x, int y, const char *str)
{
	Draw_BeginString ();

	while (*str)
	{
		if (*str != 32) // don't waste verts on spaces
			Draw_StringCharacter (x, y, *str);
		str++;
		x += 8;
	}

	Draw_EndString ();
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


void Draw_ColouredPic (int x, int y, qpic_t *pic, unsigned color)
{
	glpic_t *gl;

	if (scrap_dirty)
		Scrap_Upload ();
	gl = (glpic_t *) pic->data;

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
	qpic_t *pic;
	float alpha;

	pic = Draw_CachePic ("gfx/conback.lmp");
	pic->width = vid.conwidth;
	pic->height = vid.conheight;

	alpha = (con_forcedup) ? 1.0 : scr_conalpha.value;

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
		s = q_min ((float) glwidth / 320.0, (float) glheight / 200.0);
		s = CLAMP (1.0, scr_menuscale.value, s);
		// ericw -- doubled width to 640 to accommodate long keybindings
		glOrtho (0, 640, 200, 0, -99999, 99999);
		glViewport (glx + (glwidth - 320 * s) / 2, gly + ((glheight - 200 * s) / 3) * 2, 640 * s, 200 * s); // MH - adjust upwards
		break;

	case CANVAS_SBAR:
		s = CLAMP (1.0, scr_sbarscale.value, (float) glwidth / 320.0);

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
		s = CLAMP (1.0, scr_crosshairscale.value, 10.0);
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

	// and now set up the vertex arrays
	GL_EnableVertexAttribArrays (VAA0 | VAA1 | VAA2);

	glVertexAttribPointer (0, 2, GL_FLOAT, GL_FALSE, sizeof (drawpolyvert_t), r_drawverts->xy);
	glVertexAttribPointer (1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof (drawpolyvert_t), r_drawverts->rgba);
	glVertexAttribPointer (2, 2, GL_FLOAT, GL_FALSE, sizeof (drawpolyvert_t), r_drawverts->st);
}


void GL_End2D (void)
{
}

