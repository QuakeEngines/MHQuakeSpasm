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

// gl_texmgr.c -- fitzquake's texture manager. manages opengl texture images

#include "quakedef.h"

const int	gl_solid_format = 3;
const int	gl_alpha_format = 4;

static cvar_t	gl_texturemode = { "gl_texturemode", "", CVAR_ARCHIVE };
static cvar_t	gl_texture_anisotropy = { "gl_texture_anisotropy", "1", CVAR_ARCHIVE };

GLint	gl_hardware_maxsize;

static int numgltextures;
static gltexture_t *active_gltextures = NULL, *free_gltextures = NULL;
gltexture_t *notexture, *nulltexture;
gltexture_t *whitetexture, *greytexture, *blacktexture;

unsigned int d_8to24table[256];
unsigned int d_8to24table_fbright[256];
unsigned int d_8to24table_conchars[256];
unsigned int d_8to24table_shirt[256];
unsigned int d_8to24table_pants[256];

/*
================================================================================

	COMMANDS

================================================================================
*/

typedef struct glmode_s {
	int	magfilter;
	int	minfilter;
	const char *name;
} glmode_t;

static glmode_t glmodes[] = {
	{ GL_NEAREST, GL_NEAREST, "GL_NEAREST" },
	{ GL_NEAREST, GL_NEAREST_MIPMAP_NEAREST, "GL_NEAREST_MIPMAP_NEAREST" },
	{ GL_NEAREST, GL_NEAREST_MIPMAP_LINEAR, "GL_NEAREST_MIPMAP_LINEAR" },
	{ GL_LINEAR, GL_LINEAR, "GL_LINEAR" },
	{ GL_LINEAR, GL_LINEAR_MIPMAP_NEAREST, "GL_LINEAR_MIPMAP_NEAREST" },
	{ GL_LINEAR, GL_LINEAR_MIPMAP_LINEAR, "GL_LINEAR_MIPMAP_LINEAR" },
};
#define NUM_GLMODES (int)(sizeof(glmodes)/sizeof(glmodes[0]))
static int glmode_idx = NUM_GLMODES - 1; /* trilinear */

/*
===============
TexMgr_DescribeTextureModes_f -- report available texturemodes
===============
*/
static void TexMgr_DescribeTextureModes_f (void)
{
	int i;

	for (i = 0; i < NUM_GLMODES; i++)
		Con_SafePrintf ("   %2i: %s\n", i + 1, glmodes[i].name);

	Con_Printf ("%i modes\n", i);
}

/*
===============
TexMgr_SetFilterModes
===============
*/
static void TexMgr_SetFilterModes (gltexture_t *glt)
{
	GL_BindTexture (GL_TEXTURE0, glt);

	if (glt->flags & TEXPREF_NEAREST)
	{
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
	}
	else if (glt->flags & TEXPREF_LINEAR)
	{
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	}
	else if (glt->flags & TEXPREF_MIPMAP)
	{
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glmodes[glmode_idx].magfilter);
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glmodes[glmode_idx].minfilter);
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_texture_anisotropy.value);
	}
	else
	{
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glmodes[glmode_idx].magfilter);
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glmodes[glmode_idx].magfilter);
	}
}

/*
===============
TexMgr_TextureMode_f -- called when gl_texturemode changes
===============
*/
static void TexMgr_TextureMode_f (cvar_t *var)
{
	gltexture_t *glt;
	int i;

	for (i = 0; i < NUM_GLMODES; i++)
	{
		if (!Q_strcmp (glmodes[i].name, gl_texturemode.string))
		{
			if (glmode_idx != i)
			{
				glmode_idx = i;
				for (glt = active_gltextures; glt; glt = glt->next)
					TexMgr_SetFilterModes (glt);
			}
			return;
		}
	}

	for (i = 0; i < NUM_GLMODES; i++)
	{
		if (!q_strcasecmp (glmodes[i].name, gl_texturemode.string))
		{
			Cvar_SetQuick (&gl_texturemode, glmodes[i].name);
			return;
		}
	}

	i = atoi (gl_texturemode.string);
	if (i >= 1 && i <= NUM_GLMODES)
	{
		Cvar_SetQuick (&gl_texturemode, glmodes[i - 1].name);
		return;
	}

	Con_Printf ("\"%s\" is not a valid texturemode\n", gl_texturemode.string);
	Cvar_SetQuick (&gl_texturemode, glmodes[glmode_idx].name);
}

/*
===============
TexMgr_Anisotropy_f -- called when gl_texture_anisotropy changes
===============
*/
static void TexMgr_Anisotropy_f (cvar_t *var)
{
	if (gl_texture_anisotropy.value < 1)
	{
		Cvar_SetQuick (&gl_texture_anisotropy, "1");
	}
	else if (gl_texture_anisotropy.value > gl_max_anisotropy)
	{
		Cvar_SetValueQuick (&gl_texture_anisotropy, gl_max_anisotropy);
	}
	else
	{
		for (gltexture_t *glt = active_gltextures; glt; glt = glt->next)
		{
			/*  TexMgr_SetFilterModes (glt);*/
			if (glt->flags & TEXPREF_MIPMAP)
			{
				// fixme - switch to linear if anisotropic > 1
				GL_BindTexture (GL_TEXTURE0, glt);
				glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, glmodes[glmode_idx].magfilter);
				glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, glmodes[glmode_idx].minfilter);
				glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, gl_texture_anisotropy.value);
			}
		}
	}
}

/*
===============
TexMgr_Imagelist_f -- report loaded textures
===============
*/
static void TexMgr_Imagelist_f (void)
{
	float mb;
	float texels = 0;
	gltexture_t *glt;

	for (glt = active_gltextures; glt; glt = glt->next)
	{
		Con_SafePrintf ("   %4i x%4i %s\n", glt->width, glt->height, glt->name);

		if (glt->flags & TEXPREF_MIPMAP)
			texels += glt->width * glt->height * 4.0f / 3.0f;
		else texels += (glt->width * glt->height);
	}

	mb = texels * (Cvar_VariableValue ("vid_bpp") / 8.0f) / 0x100000;
	Con_Printf ("%i textures %i pixels %1.1f megabytes\n", numgltextures, (int) texels, mb);
}


/*
===============
TexMgr_Imagedump_f -- dump all current textures to TGA files
===============
*/
static void TexMgr_Imagedump_f (void)
{
	char tganame[MAX_OSPATH], tempname[MAX_OSPATH], dirname[MAX_OSPATH];
	gltexture_t *glt;
	byte *buffer;
	char *c;

	// create directory
	q_snprintf (dirname, sizeof (dirname), "%s/imagedump", com_gamedir);
	Sys_mkdir (dirname);

	// loop through textures
	for (glt = active_gltextures; glt; glt = glt->next)
	{
		q_strlcpy (tempname, glt->name, sizeof (tempname));
		while ((c = strchr (tempname, ':'))) *c = '_';
		while ((c = strchr (tempname, '/'))) *c = '_';
		while ((c = strchr (tempname, '*'))) *c = '_';
		q_snprintf (tganame, sizeof (tganame), "imagedump/%s.tga", tempname);

		GL_BindTexture (GL_TEXTURE0, glt);
		glPixelStorei (GL_PACK_ALIGNMENT, 1);/* for widths that aren't a multiple of 4 */

		if (glt->flags & TEXPREF_ALPHA)
		{
			buffer = (byte *) Q_zmalloc (glt->width * glt->height * 4);
			glGetTexImage (GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, buffer);
			Image_WriteTGA (tganame, buffer, glt->width, glt->height, 32, true);
		}
		else
		{
			buffer = (byte *) Q_zmalloc (glt->width * glt->height * 3);
			glGetTexImage (GL_TEXTURE_2D, 0, GL_RGB, GL_UNSIGNED_BYTE, buffer);
			Image_WriteTGA (tganame, buffer, glt->width, glt->height, 24, true);
		}

		free (buffer);
		glPixelStorei (GL_PACK_ALIGNMENT, 0);
	}

	Con_Printf ("dumped %i textures to %s\n", numgltextures, dirname);
}


/*
===============
TexMgr_FrameUsage -- report texture memory usage for this frame
===============
*/
float TexMgr_FrameUsage (void)
{
	float mb;
	float texels = 0;
	gltexture_t *glt;

	for (glt = active_gltextures; glt; glt = glt->next)
	{
		if (glt->visframe == r_framecount)
		{
			if (glt->flags & TEXPREF_MIPMAP)
				texels += glt->width * glt->height * 4.0f / 3.0f;
			else
				texels += (glt->width * glt->height);
		}
	}

	mb = texels * (Cvar_VariableValue ("vid_bpp") / 8.0f) / 0x100000;
	return mb;
}

/*
================================================================================

	TEXTURE MANAGER

================================================================================
*/

/*
================
TexMgr_FindTexture
================
*/
gltexture_t *TexMgr_FindTexture (qmodel_t *owner, const char *name)
{
	gltexture_t *glt;

	if (name)
	{
		for (glt = active_gltextures; glt; glt = glt->next)
		{
			if (glt->owner == owner && !strcmp (glt->name, name))
				return glt;
		}
	}

	return NULL;
}

/*
================
TexMgr_NewTexture
================
*/
gltexture_t *TexMgr_NewTexture (void)
{
	gltexture_t *glt;

	// alloc a new texture if needed
	if (!free_gltextures)
	{
		free_gltextures = (gltexture_t *) Q_zmalloc (sizeof (gltexture_t));
		memset (free_gltextures, 0, sizeof (gltexture_t));
		free_gltextures->next = NULL;
	}

	// and now take the texture
	glt = free_gltextures;
	free_gltextures = glt->next;
	glt->next = active_gltextures;
	active_gltextures = glt;

	glGenTextures (1, &glt->texnum);
	numgltextures++;

	return glt;
}

static void GL_DeleteTexture (gltexture_t *texture);

// ericw -- workaround for preventing TexMgr_FreeTexture during TexMgr_ReloadImages
static qboolean in_reload_images;

/*
================
TexMgr_FreeTexture
================
*/
void TexMgr_FreeTexture (gltexture_t *kill)
{
	if (in_reload_images)
		return;

	if (kill == NULL)
	{
		Con_Printf ("TexMgr_FreeTexture: NULL texture\n");
		return;
	}

	if (active_gltextures == kill)
	{
		active_gltextures = kill->next;
		kill->next = free_gltextures;
		free_gltextures = kill;

		GL_DeleteTexture (kill);
		numgltextures--;
		return;
	}

	for (gltexture_t *glt = active_gltextures; glt; glt = glt->next)
	{
		if (glt->next == kill)
		{
			glt->next = kill->next;
			kill->next = free_gltextures;
			free_gltextures = kill;

			GL_DeleteTexture (kill);
			numgltextures--;
			return;
		}
	}

	Con_Printf ("TexMgr_FreeTexture: not found\n");
}

/*
================
TexMgr_FreeTextures

compares each bit in "flags" to the one in glt->flags only if that bit is active in "mask"
================
*/
void TexMgr_FreeTextures (unsigned int flags, unsigned int mask)
{
	gltexture_t *glt, *next;

	for (glt = active_gltextures; glt; glt = next)
	{
		next = glt->next;
		if ((glt->flags & mask) == (flags & mask))
			TexMgr_FreeTexture (glt);
	}

	Sky_FreeSkybox ();
}


/*
================
TexMgr_FreeTexturesForOwner
================
*/
void TexMgr_FreeTexturesForOwner (qmodel_t *owner)
{
	gltexture_t *glt, *next;

	for (glt = active_gltextures; glt; glt = next)
	{
		next = glt->next;
		if (glt && glt->owner == owner)
			TexMgr_FreeTexture (glt);
	}
}


/*
================
TexMgr_DeleteTextureObjects
================
*/
void TexMgr_DeleteTextureObjects (void)
{
	for (gltexture_t *glt = active_gltextures; glt; glt = glt->next)
		GL_DeleteTexture (glt);

	Sky_FreeSkybox ();
}

/*
================================================================================

	INIT

================================================================================
*/


void Image_LoadPalette (void);
void Image_Init (void);

/*
================
TexMgr_NewGame
================
*/
void TexMgr_NewGame (void)
{
	TexMgr_FreeTextures (0, TEXPREF_PERSIST); // deletes all textures where TEXPREF_PERSIST is unset
	Image_LoadPalette ();
}


/*
================
TexMgr_Init

must be called before any texture loading
================
*/
void TexMgr_Init (void)
{
	static byte notexture_data[16] = { 159, 91, 83, 255, 0, 0, 0, 255, 0, 0, 0, 255, 159, 91, 83, 255 }; // black and pink checker
	static byte nulltexture_data[16] = { 127, 191, 255, 255, 0, 0, 0, 255, 0, 0, 0, 255, 127, 191, 255, 255 }; // black and blue checker
	static byte whitetexture_data[16] = { 255, 255, 255, 255 }; // 1x1 white
	static byte greytexture_data[16] = { 127, 127, 127, 255 }; // 1x1 grey
	static byte blacktexture_data[16] = { 0, 0, 0, 255 }; // 1x1 black
	extern texture_t *r_notexture_mip, *r_notexture_mip2;

	// init texture list
	free_gltextures = NULL;
	active_gltextures = NULL;
	numgltextures = 0;

	Image_Init ();

	Cvar_RegisterVariable (&gl_texture_anisotropy);
	Cvar_SetCallback (&gl_texture_anisotropy, &TexMgr_Anisotropy_f);
	gl_texturemode.string = glmodes[glmode_idx].name;
	Cvar_RegisterVariable (&gl_texturemode);
	Cvar_SetCallback (&gl_texturemode, &TexMgr_TextureMode_f);
	Cmd_AddCommand ("gl_describetexturemodes", &TexMgr_DescribeTextureModes_f);
	Cmd_AddCommand ("imagelist", &TexMgr_Imagelist_f);
	Cmd_AddCommand ("imagedump", &TexMgr_Imagedump_f);

	// poll max size from hardware
	glGetIntegerv (GL_MAX_TEXTURE_SIZE, &gl_hardware_maxsize);

	// load notexture images
	notexture = TexMgr_LoadImage (NULL, "notexture", 2, 2, SRC_RGBA, notexture_data, "", (src_offset_t) notexture_data, TEXPREF_NEAREST | TEXPREF_PERSIST);
	nulltexture = TexMgr_LoadImage (NULL, "nulltexture", 2, 2, SRC_RGBA, nulltexture_data, "", (src_offset_t) nulltexture_data, TEXPREF_NEAREST | TEXPREF_PERSIST);
	whitetexture = TexMgr_LoadImage (NULL, "whitetexture", 1, 1, SRC_RGBA, whitetexture_data, "", (src_offset_t) whitetexture_data, TEXPREF_NEAREST | TEXPREF_PERSIST);
	greytexture = TexMgr_LoadImage (NULL, "greytexture", 1, 1, SRC_RGBA, greytexture_data, "", (src_offset_t) greytexture_data, TEXPREF_NEAREST | TEXPREF_PERSIST);
	blacktexture = TexMgr_LoadImage (NULL, "blacktexture", 1, 1, SRC_RGBA, blacktexture_data, "", (src_offset_t) blacktexture_data, TEXPREF_NEAREST | TEXPREF_PERSIST);

	// create other textures we need
	GLWarp_CreateTextures ();

	// have to assign these here becuase Mod_Init is called before TexMgr_Init
	r_notexture_mip->gltexture = r_notexture_mip2->gltexture = notexture;
}


/*
================================================================================

	IMAGE LOADING

================================================================================
*/

unsigned *Image_ResampleTextureToSize (unsigned *in, int inwidth, int inheight, int outwidth, int outheight, qboolean alpha);
unsigned *Image_ResampleTexture (unsigned *in, int inwidth, int inheight, qboolean alpha);
void Image_AlphaEdgeFix (byte *data, int width, int height);
void Image_FloodFillSkin (byte *skin, int skinwidth, int skinheight);
void Image_PadEdgeFixW (byte *data, int width, int height);
void Image_PadEdgeFixH (byte *data, int width, int height);
unsigned *Image_8to32 (byte *in, int pixels, unsigned int *usepal);
byte *Image_PadImageW (byte *in, int width, int height, byte padbyte);
byte *Image_PadImageH (byte *in, int width, int height, byte padbyte);
unsigned *Image_MipMapW (unsigned *data, int width, int height);
unsigned *Image_MipMapH (unsigned *data, int width, int height);
void Image_NewTranslation (int top, int bottom, byte *translation);


/*
================
TexMgr_LoadImage32 -- handles 32bit source data
================
*/
static void TexMgr_LoadImage32 (gltexture_t *glt, unsigned *data)
{
	int	internalformat;

	if (!GLEW_ARB_texture_non_power_of_two)
	{
		// resample up
		data = Image_ResampleTexture (data, glt->width, glt->height, glt->flags & TEXPREF_ALPHA);
		glt->width = Image_Pad (glt->width);
		glt->height = Image_Pad (glt->height);
	}

	// mipmap down
	int mipwidth = Image_SafeTextureSize (glt->width);
	int mipheight = Image_SafeTextureSize (glt->height);

	while ((int) glt->width > mipwidth)
	{
		Image_MipMapW (data, glt->width, glt->height);
		glt->width >>= 1;
		if (glt->flags & TEXPREF_ALPHA)
			Image_AlphaEdgeFix ((byte *) data, glt->width, glt->height);
	}

	while ((int) glt->height > mipheight)
	{
		Image_MipMapH (data, glt->width, glt->height);
		glt->height >>= 1;
		if (glt->flags & TEXPREF_ALPHA)
			Image_AlphaEdgeFix ((byte *) data, glt->width, glt->height);
	}

	// upload
	GL_BindTexture (GL_TEXTURE0, glt);
	internalformat = (glt->flags & TEXPREF_ALPHA) ? gl_alpha_format : gl_solid_format;
	glTexImage2D (GL_TEXTURE_2D, 0, internalformat, glt->width, glt->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

	// upload mipmaps
	if (glt->flags & TEXPREF_MIPMAP)
	{
		// this can make hunk allocs if we resample an np2 tex so take a mark
		int mark = Hunk_LowMark ();

		mipwidth = glt->width;
		mipheight = glt->height;

		for (int miplevel = 1; mipwidth > 1 || mipheight > 1; miplevel++)
		{
			// choose the appropriate filter
			if (mipwidth > 1)
			{
				// 2x2 box filter doesn't work on np2 textures where a dimension is not evenly divisible by 2, so resample it down first
				if (mipwidth & 1)
				{
					data = Image_ResampleTextureToSize (data, mipwidth, mipheight, mipwidth - 1, mipheight, (glt->flags & TEXPREF_ALPHA));
					mipwidth--;
				}

				Image_MipMapW (data, mipwidth, mipheight);
				mipwidth >>= 1;
			}

			if (mipheight > 1)
			{
				// 2x2 box filter doesn't work on np2 textures where a dimension is not evenly divisible by 2, so resample it down first
				if (mipheight & 1)
				{
					data = Image_ResampleTextureToSize (data, mipwidth, mipheight, mipwidth, mipheight - 1, (glt->flags & TEXPREF_ALPHA));
					mipheight--;
				}

				Image_MipMapH (data, mipwidth, mipheight);
				mipheight >>= 1;
			}

			// and load this miplevel
			glTexImage2D (GL_TEXTURE_2D, miplevel, internalformat, mipwidth, mipheight, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);
		}

		Hunk_FreeToLowMark (mark);
	}

	// set filter modes
	TexMgr_SetFilterModes (glt);
}


/*
================
TexMgr_LoadImage8 -- handles 8bit source data, then passes it to LoadImage32
================
*/
static void TexMgr_LoadImage8 (gltexture_t *glt, byte *data)
{
	extern cvar_t gl_fullbrights;
	qboolean padw = false, padh = false;
	byte padbyte;
	unsigned int *usepal;
	int i;

	// HACK HACK HACK -- taken from tomazquake
	if (strstr (glt->name, "shot1sid") && glt->width == 32 && glt->height == 32 && CRC_Block (data, 1024) == 65393)
	{
		// This texture in b_shell1.bsp has some of the first 32 pixels painted white.
		// They are invisible in software, but look really ugly in GL. So we just copy
		// 32 pixels from the bottom to make it look nice.
		memcpy (data, data + 32 * 31, 32);
	}

	// perform the floodfill here so that it will run on both initial load and video restart
	if (glt->flags & TEXPREF_FLOODFILL)
		Image_FloodFillSkin (data, glt->width, glt->height);

	// detect false alpha cases
	if ((glt->flags & TEXPREF_ALPHA) && !(glt->flags & TEXPREF_CONCHARS))
	{
		for (i = 0; i < (int) (glt->width * glt->height); i++)
			if (data[i] == 255) // transparent index
				break;

		if (i == (int) (glt->width * glt->height))
			glt->flags &= ~TEXPREF_ALPHA;
	}

	// choose palette and padbyte
	if (glt->flags & TEXPREF_FULLBRIGHT)
	{
		usepal = d_8to24table_fbright;
		padbyte = 0;
	}
	else if (glt->flags & TEXPREF_CONCHARS)
	{
		usepal = d_8to24table_conchars;
		padbyte = 0;
	}
	else
	{
		usepal = d_8to24table;
		padbyte = 255;
	}

	// fixme - shouldn't all this be done in TexMgr_LoadImage32 so that it more generally applies to 8-bit and 
	// pad each dimention, but only if it's not going to be downsampled later
	if (glt->flags & TEXPREF_PAD)
	{
		if ((int) glt->width < Image_SafeTextureSize (glt->width))
		{
			data = Image_PadImageW (data, glt->width, glt->height, padbyte);
			glt->width = Image_Pad (glt->width);
			padw = true;
		}

		if ((int) glt->height < Image_SafeTextureSize (glt->height))
		{
			data = Image_PadImageH (data, glt->width, glt->height, padbyte);
			glt->height = Image_Pad (glt->height);
			padh = true;
		}
	}

	// convert to 32bit
	data = (byte *) Image_8to32 (data, glt->width * glt->height, usepal);

	// fix edges
	if (glt->flags & TEXPREF_ALPHA)
		Image_AlphaEdgeFix (data, glt->width, glt->height);
	else
	{
		if (padw) Image_PadEdgeFixW (data, glt->source_width, glt->source_height);
		if (padh) Image_PadEdgeFixH (data, glt->source_width, glt->source_height);
	}

	// upload it
	TexMgr_LoadImage32 (glt, (unsigned *) data);
}

/*
================
TexMgr_LoadLightmap -- handles lightmap data
================
*/
static void TexMgr_LoadLightmap (gltexture_t *glt, byte *data)
{
	// upload it
	GL_BindTexture (GL_TEXTURE1, glt);
	glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA, glt->width, glt->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, data);

	// set filter modes
	TexMgr_SetFilterModes (glt);
}


void TexMgr_LoadImageBySourceFormat (gltexture_t *glt, byte *data)
{
	switch (glt->source_format)
	{
	case SRC_INDEXED:
		TexMgr_LoadImage8 (glt, data);
		break;

	case SRC_LIGHTMAP:
		TexMgr_LoadLightmap (glt, data);
		break;

	case SRC_RGBA:
		TexMgr_LoadImage32 (glt, (unsigned *) data);
		break;
	}
}


/*
================
TexMgr_LoadImage -- the one entry point for loading all textures
================
*/
gltexture_t *TexMgr_LoadImage (qmodel_t *owner, const char *name, int width, int height, enum srcformat format, byte *data, const char *source_file, src_offset_t source_offset, unsigned flags)
{
	unsigned short crc;
	gltexture_t *glt;
	int mark;

	if (isDedicated)
		return NULL;

	// cache check
	switch (format)
	{
	case SRC_INDEXED:
		crc = CRC_Block (data, width * height);
		break;

	case SRC_LIGHTMAP:
		crc = CRC_Block (data, width * height * 4);
		break;

	case SRC_RGBA:
		crc = CRC_Block (data, width * height * 4);
		break;

	default: /* not reachable but avoids compiler warnings */
		crc = 0;
	}

	if ((flags & TEXPREF_OVERWRITE) && (glt = TexMgr_FindTexture (owner, name)))
	{
		if (glt->source_crc == crc)
			return glt;
	}
	else glt = TexMgr_NewTexture ();

	// copy data
	glt->owner = owner;
	q_strlcpy (glt->name, name, sizeof (glt->name));
	glt->width = width;
	glt->height = height;
	glt->flags = flags;
	glt->shirt = -1;
	glt->pants = -1;
	q_strlcpy (glt->source_file, source_file, sizeof (glt->source_file));
	glt->source_offset = source_offset;
	glt->source_format = format;
	glt->source_width = width;
	glt->source_height = height;
	glt->source_crc = crc;

	// upload it
	mark = Hunk_LowMark ();
	TexMgr_LoadImageBySourceFormat (glt, data);
	Hunk_FreeToLowMark (mark);

	return glt;
}

/*
================================================================================

	COLORMAPPING AND TEXTURE RELOADING

================================================================================
*/

/*
================
TexMgr_ReloadImage -- reloads a texture, and colormaps it if needed
================
*/
void TexMgr_ReloadImage (gltexture_t *glt, int shirt, int pants)
{
	byte *data = NULL;
	int	mark;

	// get source data
	mark = Hunk_LowMark ();

	if (glt->source_file[0] && glt->source_offset)
	{
		// lump inside file
		long size;
		FILE *f;

		COM_FOpenFile (glt->source_file, &f, NULL);

		if (!f)
			goto invalid;

		fseek (f, glt->source_offset, SEEK_CUR);
		size = (long) (glt->source_width * glt->source_height);

		/* should be SRC_INDEXED, but no harm being paranoid:  */
		if (glt->source_format == SRC_RGBA)
			size *= 4;
		else if (glt->source_format == SRC_LIGHTMAP)
			size *= 4;

		data = (byte *) Hunk_Alloc (size);
		fread (data, 1, size, f);
		fclose (f);
	}
	else if (glt->source_file[0] && !glt->source_offset)
		data = Image_LoadImage (glt->source_file, (int *) &glt->source_width, (int *) &glt->source_height); // simple file
	else if (!glt->source_file[0] && glt->source_offset)
		data = (byte *) glt->source_offset; // image in memory

	if (!data)
	{
invalid:;
		Con_Printf ("TexMgr_ReloadImage: invalid source for %s\n", glt->name);
		goto done;
	}

	glt->width = glt->source_width;
	glt->height = glt->source_height;

	// apply shirt and pants colors
	// if shirt and pants are -1,-1, use existing shirt and pants colors
	// if existing shirt and pants colors are -1,-1, don't bother colormapping
	if (shirt > -1 && pants > -1)
	{
		if (glt->source_format == SRC_INDEXED)
		{
			glt->shirt = shirt;
			glt->pants = pants;
		}
		else
			Con_Printf ("TexMgr_ReloadImage: can't colormap a non SRC_INDEXED texture: %s\n", glt->name);
	}

	if (glt->shirt > -1 && glt->pants > -1)
	{
		byte translation[256];

		// create new translation table
		shirt = glt->shirt * 16;
		pants = glt->pants * 16;

		Image_NewTranslation (shirt, pants, translation);

		// translate texture
		int size = glt->width * glt->height;
		byte *translated = (byte *) Hunk_Alloc (size);

		for (int i = 0; i < size; i++)
			translated[i] = translation[data[i]];

		data = translated;
	}

	// upload it
	TexMgr_LoadImageBySourceFormat (glt, data);

done:;
	Hunk_FreeToLowMark (mark);
}


/*
================
TexMgr_ReloadImages -- reloads all texture images. called only by vid_restart
================
*/
void TexMgr_ReloadImages (void)
{
	// ericw -- tricky bug: if the hunk is almost full, an allocation in TexMgr_ReloadImage
	// triggers cache items to be freed, which calls back into TexMgr to free the
	// texture. If this frees 'glt' in the loop below, the active_gltextures
	// list gets corrupted.
	// A test case is jam3_tronyn.bsp with -heapsize 65536, and do several mode
	// switches/fullscreen toggles
	// 2015-09-04 -- Cache_Flush workaround was causing issues (http://sourceforge.net/p/quakespasm/bugs/10/)
	// switching to a boolean flag.
	in_reload_images = true;

	for (gltexture_t *glt = active_gltextures; glt; glt = glt->next)
	{
		// defer reloading lightmaps
		if (glt->source_format == SRC_LIGHTMAP) continue;

		// reload the texture
		glGenTextures (1, &glt->texnum);
		TexMgr_ReloadImage (glt, -1, -1);
	}

	// now reload the lightmaps
	// this will rebuild the lightmaps from surfaces in the exact same order as they were originally loaded, so we guarantee that everything will match up
	GL_BuildLightmaps ();

	// reload anything else
	Sky_ReloadSkyBox ();
	in_reload_images = false;
}


/*
================================================================================

	CUBEMAP LOADING AND MANIPULATION FOR SKYBOXES

================================================================================
*/

GLuint TexMgr_LoadCubemap (byte *data[6], int width[6], int height[6])
{
	int maxsize = 0;
	GLuint cubemap_texture;

	// figure the size of the cubemap which will be the max size of all 6 faces
	for (int i = 0; i < 6; i++)
	{
		// some maps or mods deliberately have incomplete skyboxes; e.g a bottom face may be missing
		if (data[i])
		{
			if (width[i] > maxsize) maxsize = width[i];
			if (height[i] > maxsize) maxsize = height[i];
		}
	}

	// if we found any faces at all, maxsize will be > 0
	if (maxsize > 0)
	{
		// adjust for POT and hardware limits
		maxsize = Image_SafeTextureSize (maxsize);

		// make a texture for it
		glGenTextures (1, &cubemap_texture);

		// explicitly bind to TMU 3 to bypass the texture manager
		glActiveTexture (GL_TEXTURE5);
		glBindTexture (GL_TEXTURE_CUBE_MAP, cubemap_texture);

		for (int i = 0; i < 6; i++)
		{
			// in case we need to make any allocations e.g. for resampling
			int mark = Hunk_LowMark ();

			if (!data[i])
			{
				// face was not provided
				glTexImage2D (GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, maxsize, maxsize, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
			}
			else
			{
				// some maps or mods provide different-sized faces
				if (width[i] != maxsize || height[i] != maxsize)
				{
					// resample the face
					data[i] = (byte *) Image_ResampleTextureToSize ((unsigned *) data[i], width[i], height[i], maxsize, maxsize, false);
				}

				// load this face
				glTexImage2D (GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, maxsize, maxsize, 0, GL_RGBA, GL_UNSIGNED_BYTE, data[i]);
			}

			Hunk_FreeToLowMark (mark);
		}

		// clamp mode for cubemaps
		glTexParameterf (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameterf (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexParameterf (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

		// obey gl_texturemode
		TexMgr_SetCubemapFilterModes ();

		// force a rebind after explicitly calling glActiveTexture
		GL_ClearTextureBindings ();

		return cubemap_texture;
	}

	// nothing was loaded
	return 0;
}


void TexMgr_SetCubemapFilterModes (void)
{
	// assumes that the cubemap is already bound
	glTexParameterf (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, glmodes[glmode_idx].magfilter);
	glTexParameterf (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, glmodes[glmode_idx].magfilter);
}


/*
================================================================================

	TEXTURE BINDING / TEXTURE UNIT SWITCHING

================================================================================
*/

// MH - made these GL_INVALID_VALUE
// we use a 6th TMU for cubemaps and rectangle textures but access to it doesn't go through this abstraction
static GLuint	currenttexture[5] = { GL_INVALID_VALUE, GL_INVALID_VALUE, GL_INVALID_VALUE, GL_INVALID_VALUE, GL_INVALID_VALUE }; // to avoid unnecessary texture sets
static GLenum	currenttarget = GL_INVALID_VALUE;


/*
================
GL_ActiveTexture -- johnfitz -- rewritten
================
*/
void GL_ActiveTexture (GLenum target)
{
	if (target == currenttarget)
		return;

	glActiveTexture (target);
	currenttarget = target;
}


/*
================
GL_Bind -- johnfitz -- heavy revision
================
*/
void GL_BindTexture (GLenum target, gltexture_t *texture)
{
	if (!texture)
		texture = nulltexture;

	// with shaders we don't need to worry about enabling texture2d or crap like that
	GL_ActiveTexture (target);

	if (texture->texnum != currenttexture[currenttarget - GL_TEXTURE0])
	{
		currenttexture[currenttarget - GL_TEXTURE0] = texture->texnum;
		glBindTexture (GL_TEXTURE_2D, texture->texnum);
		texture->visframe = r_framecount;
	}
}


/*
================
GL_DeleteTexture -- ericw

Wrapper around glDeleteTextures that also clears the given texture number
from our per-TMU cached texture binding table.
================
*/
static void GL_DeleteTexture (gltexture_t *texture)
{
	glDeleteTextures (1, &texture->texnum);

	for (int i = 0; i < 5; i++)
		if (texture->texnum == currenttexture[i])
			currenttexture[i] = GL_INVALID_VALUE;

	texture->texnum = 0;
}

/*
================
GL_ClearTextureBindings -- ericw

Invalidates cached bindings, so the next GL_Bind calls for each TMU will
make real glBindTexture calls.
Call this after changing the binding outside of GL_Bind.

MH - also invalidates the current TMU so the next binding will explicitly activate it's TMU
================
*/
void GL_ClearTextureBindings (void)
{
	for (int i = 0; i < 5; i++)
		currenttexture[i] = GL_INVALID_VALUE;

	currenttarget = GL_INVALID_VALUE;
}


