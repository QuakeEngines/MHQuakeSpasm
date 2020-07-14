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
// image.c -- image loading

#include "quakedef.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC
#include "stb_image_write.h"

#define LODEPNG_NO_COMPILE_DECODER
#define LODEPNG_NO_COMPILE_CPP
#define LODEPNG_NO_COMPILE_ANCILLARY_CHUNKS
#define LODEPNG_NO_COMPILE_ERROR_TEXT
#include "lodepng.h"
#include "lodepng.c"

static char loadfilename[MAX_OSPATH]; // file scope so that error messages can use it

typedef struct stdio_buffer_s {
	FILE *f;
	unsigned char buffer[1024];
	int size;
	int pos;
} stdio_buffer_t;

static stdio_buffer_t *Buf_Alloc (FILE *f)
{
	stdio_buffer_t *buf = (stdio_buffer_t *) Q_zmalloc (sizeof (stdio_buffer_t));
	buf->f = f;
	return buf;
}

static void Buf_Free (stdio_buffer_t *buf)
{
	free (buf);
}

static inline int Buf_GetC (stdio_buffer_t *buf)
{
	if (buf->pos >= buf->size)
	{
		buf->size = fread (buf->buffer, 1, sizeof (buf->buffer), buf->f);
		buf->pos = 0;

		if (buf->size == 0)
			return EOF;
	}

	return buf->buffer[buf->pos++];
}

/*
============
Image_LoadImage

returns a pointer to hunk allocated RGBA data

TODO: search order: tga png jpg pcx lmp
============
*/
byte *Image_LoadImage (const char *name, int *width, int *height)
{
	FILE *f;

	q_snprintf (loadfilename, sizeof (loadfilename), "%s.tga", name);
	COM_FOpenFile (loadfilename, &f, NULL);
	if (f)
		return Image_LoadTGA (f, width, height);

	q_snprintf (loadfilename, sizeof (loadfilename), "%s.pcx", name);
	COM_FOpenFile (loadfilename, &f, NULL);
	if (f)
		return Image_LoadPCX (f, width, height);

	return NULL;
}

// ==============================================================================
//  TGA
// ==============================================================================

typedef struct targaheader_s {
	unsigned char 	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} targaheader_t;

#define TARGAHEADERSIZE 18 // size on disk

targaheader_t targa_header;

int fgetLittleShort (FILE *f)
{
	byte	b1, b2;

	b1 = fgetc (f);
	b2 = fgetc (f);

	return (short) (b1 + b2 * 256);
}

int fgetLittleLong (FILE *f)
{
	byte	b1, b2, b3, b4;

	b1 = fgetc (f);
	b2 = fgetc (f);
	b3 = fgetc (f);
	b4 = fgetc (f);

	return b1 + (b2 << 8) + (b3 << 16) + (b4 << 24);
}

/*
============
Image_WriteTGA -- writes RGB or RGBA data to a TGA file

returns true if successful

TODO: support BGRA and BGR formats (since opengl can return them, and we don't have to swap)
============
*/
qboolean Image_WriteTGA (const char *name, byte *data, int width, int height, int bpp, qboolean upsidedown)
{
	int		handle, i, size, temp, bytes;
	char	pathname[MAX_OSPATH];
	byte	header[TARGAHEADERSIZE];

	Sys_mkdir (com_gamedir); // if we've switched to a nonexistant gamedir, create it now so we don't crash
	q_snprintf (pathname, sizeof (pathname), "%s/%s", com_gamedir, name);
	handle = Sys_FileOpenWrite (pathname);
	if (handle == -1)
		return false;

	Q_memset (header, 0, TARGAHEADERSIZE);
	header[2] = 2; // uncompressed type
	header[12] = width & 255;
	header[13] = width >> 8;
	header[14] = height & 255;
	header[15] = height >> 8;
	header[16] = bpp; // pixel size
	if (upsidedown)
		header[17] = 0x20; // upside-down attribute

	// swap red and blue bytes
	bytes = bpp / 8;
	size = width * height * bytes;
	for (i = 0; i < size; i += bytes)
	{
		temp = data[i];
		data[i] = data[i + 2];
		data[i + 2] = temp;
	}

	Sys_FileWrite (handle, header, TARGAHEADERSIZE);
	Sys_FileWrite (handle, data, size);
	Sys_FileClose (handle);

	return true;
}

/*
=============
Image_LoadTGA
=============
*/
byte *Image_LoadTGA (FILE *fin, int *width, int *height)
{
	int				columns, rows, numPixels;
	byte *pixbuf;
	int				row, column;
	byte *targa_rgba;
	int				realrow; // johnfitz -- fix for upside-down targas
	qboolean		upside_down; // johnfitz -- fix for upside-down targas
	stdio_buffer_t *buf;

	targa_header.id_length = fgetc (fin);
	targa_header.colormap_type = fgetc (fin);
	targa_header.image_type = fgetc (fin);

	targa_header.colormap_index = fgetLittleShort (fin);
	targa_header.colormap_length = fgetLittleShort (fin);
	targa_header.colormap_size = fgetc (fin);
	targa_header.x_origin = fgetLittleShort (fin);
	targa_header.y_origin = fgetLittleShort (fin);
	targa_header.width = fgetLittleShort (fin);
	targa_header.height = fgetLittleShort (fin);
	targa_header.pixel_size = fgetc (fin);
	targa_header.attributes = fgetc (fin);

	if (targa_header.image_type != 2 && targa_header.image_type != 10)
		Sys_Error ("Image_LoadTGA: %s is not a type 2 or type 10 targa\n", loadfilename);

	if (targa_header.colormap_type != 0 || (targa_header.pixel_size != 32 && targa_header.pixel_size != 24))
		Sys_Error ("Image_LoadTGA: %s is not a 24bit or 32bit targa\n", loadfilename);

	columns = targa_header.width;
	rows = targa_header.height;
	numPixels = columns * rows;
	upside_down = !(targa_header.attributes & 0x20); // johnfitz -- fix for upside-down targas

	targa_rgba = (byte *) Hunk_Alloc (numPixels * 4);

	if (targa_header.id_length != 0)
		fseek (fin, targa_header.id_length, SEEK_CUR);  // skip TARGA image comment

	buf = Buf_Alloc (fin);

	if (targa_header.image_type == 2) // Uncompressed, RGB images
	{
		for (row = rows - 1; row >= 0; row--)
		{
			// johnfitz -- fix for upside-down targas
			realrow = upside_down ? row : rows - 1 - row;
			pixbuf = targa_rgba + realrow * columns * 4;
			// johnfitz
			for (column = 0; column < columns; column++)
			{
				unsigned char red, green, blue, alphabyte;
				switch (targa_header.pixel_size)
				{
				case 24:
					blue = Buf_GetC (buf);
					green = Buf_GetC (buf);
					red = Buf_GetC (buf);
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = 255;
					break;
				case 32:
					blue = Buf_GetC (buf);
					green = Buf_GetC (buf);
					red = Buf_GetC (buf);
					alphabyte = Buf_GetC (buf);
					*pixbuf++ = red;
					*pixbuf++ = green;
					*pixbuf++ = blue;
					*pixbuf++ = alphabyte;
					break;
				}
			}
		}
	}
	else if (targa_header.image_type == 10) // Runlength encoded RGB images
	{
		unsigned char red, green, blue, alphabyte, packetHeader, packetSize, j;
		for (row = rows - 1; row >= 0; row--)
		{
			// johnfitz -- fix for upside-down targas
			realrow = upside_down ? row : rows - 1 - row;
			pixbuf = targa_rgba + realrow * columns * 4;
			// johnfitz
			for (column = 0; column < columns; )
			{
				packetHeader = Buf_GetC (buf);
				packetSize = 1 + (packetHeader & 0x7f);
				if (packetHeader & 0x80) // run-length packet
				{
					switch (targa_header.pixel_size)
					{
					case 24:
						blue = Buf_GetC (buf);
						green = Buf_GetC (buf);
						red = Buf_GetC (buf);
						alphabyte = 255;
						break;
					case 32:
						blue = Buf_GetC (buf);
						green = Buf_GetC (buf);
						red = Buf_GetC (buf);
						alphabyte = Buf_GetC (buf);
						break;
					default: /* avoid compiler warnings */
						blue = red = green = alphabyte = 0;
					}

					for (j = 0; j < packetSize; j++)
					{
						*pixbuf++ = red;
						*pixbuf++ = green;
						*pixbuf++ = blue;
						*pixbuf++ = alphabyte;
						column++;
						if (column == columns) // run spans across rows
						{
							column = 0;
							if (row > 0)
								row--;
							else
								goto breakOut;
							// johnfitz -- fix for upside-down targas
							realrow = upside_down ? row : rows - 1 - row;
							pixbuf = targa_rgba + realrow * columns * 4;
							// johnfitz
						}
					}
				}
				else // non run-length packet
				{
					for (j = 0; j < packetSize; j++)
					{
						switch (targa_header.pixel_size)
						{
						case 24:
							blue = Buf_GetC (buf);
							green = Buf_GetC (buf);
							red = Buf_GetC (buf);
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = 255;
							break;
						case 32:
							blue = Buf_GetC (buf);
							green = Buf_GetC (buf);
							red = Buf_GetC (buf);
							alphabyte = Buf_GetC (buf);
							*pixbuf++ = red;
							*pixbuf++ = green;
							*pixbuf++ = blue;
							*pixbuf++ = alphabyte;
							break;
						default: /* avoid compiler warnings */
							blue = red = green = alphabyte = 0;
						}
						column++;
						if (column == columns) // pixel packet run spans across rows
						{
							column = 0;
							if (row > 0)
								row--;
							else
								goto breakOut;
							// johnfitz -- fix for upside-down targas
							realrow = upside_down ? row : rows - 1 - row;
							pixbuf = targa_rgba + realrow * columns * 4;
							// johnfitz
						}
					}
				}
			}
breakOut:;
		}
	}

	Buf_Free (buf);
	fclose (fin);

	*width = (int) (targa_header.width);
	*height = (int) (targa_header.height);
	return targa_rgba;
}

// ==============================================================================
//  PCX
// ==============================================================================

typedef struct pcxheader_s {
	char			signature;
	char			version;
	char			encoding;
	char			bits_per_pixel;
	unsigned short	xmin, ymin, xmax, ymax;
	unsigned short	hdpi, vdpi;
	byte			colortable[48];
	char			reserved;
	char			color_planes;
	unsigned short	bytes_per_line;
	unsigned short	palette_type;
	char			filler[58];
} pcxheader_t;

/*
============
Image_LoadPCX
============
*/
byte *Image_LoadPCX (FILE *f, int *width, int *height)
{
	pcxheader_t	pcx;
	int			x, y, w, h, readbyte, runlength, start;
	byte *p, *data;
	byte		palette[768];
	stdio_buffer_t *buf;

	start = ftell (f); // save start of file (since we might be inside a pak file, SEEK_SET might not be the start of the pcx)

	fread (&pcx, sizeof (pcx), 1, f);
	pcx.xmin = (unsigned short) LittleShort (pcx.xmin);
	pcx.ymin = (unsigned short) LittleShort (pcx.ymin);
	pcx.xmax = (unsigned short) LittleShort (pcx.xmax);
	pcx.ymax = (unsigned short) LittleShort (pcx.ymax);
	pcx.bytes_per_line = (unsigned short) LittleShort (pcx.bytes_per_line);

	if (pcx.signature != 0x0A)
		Sys_Error ("'%s' is not a valid PCX file", loadfilename);

	if (pcx.version != 5)
		Sys_Error ("'%s' is version %i, should be 5", loadfilename, pcx.version);

	if (pcx.encoding != 1 || pcx.bits_per_pixel != 8 || pcx.color_planes != 1)
		Sys_Error ("'%s' has wrong encoding or bit depth", loadfilename);

	w = pcx.xmax - pcx.xmin + 1;
	h = pcx.ymax - pcx.ymin + 1;

	data = (byte *) Hunk_Alloc ((w * h + 1) * 4); // +1 to allow reading padding byte on last line

	// load palette
	fseek (f, start + com_filesize - 768, SEEK_SET);
	fread (palette, 1, 768, f);

	// back to start of image data
	fseek (f, start + sizeof (pcx), SEEK_SET);

	buf = Buf_Alloc (f);

	for (y = 0; y < h; y++)
	{
		p = data + y * w * 4;

		for (x = 0; x < (pcx.bytes_per_line); ) // read the extra padding byte if necessary
		{
			readbyte = Buf_GetC (buf);

			if (readbyte >= 0xC0)
			{
				runlength = readbyte & 0x3F;
				readbyte = Buf_GetC (buf);
			}
			else
				runlength = 1;

			while (runlength--)
			{
				p[0] = palette[readbyte * 3];
				p[1] = palette[readbyte * 3 + 1];
				p[2] = palette[readbyte * 3 + 2];
				p[3] = 255;
				p += 4;
				x++;
			}
		}
	}

	Buf_Free (buf);
	fclose (f);

	*width = w;
	*height = h;
	return data;
}

// ==============================================================================
//  STB_IMAGE_WRITE
// ==============================================================================

static byte *CopyFlipped (const byte *data, int width, int height, int bpp)
{
	int	y, rowsize;
	byte *flipped;

	rowsize = width * (bpp / 8);
	flipped = (byte *) Q_zmalloc (height * rowsize);
	if (!flipped)
		return NULL;

	for (y = 0; y < height; y++)
	{
		memcpy (&flipped[y * rowsize], &data[(height - 1 - y) * rowsize], rowsize);
	}
	return flipped;
}

/*
============
Image_WriteJPG -- writes using stb_image_write

returns true if successful
============
*/
qboolean Image_WriteJPG (const char *name, byte *data, int width, int height, int bpp, int quality, qboolean upsidedown)
{
	unsigned error;
	char	pathname[MAX_OSPATH];
	byte *flipped;
	int	bytes_per_pixel;

	if (!(bpp == 32 || bpp == 24))
		Sys_Error ("bpp not 24 or 32");

	bytes_per_pixel = bpp / 8;

	Sys_mkdir (com_gamedir); // if we've switched to a nonexistant gamedir, create it now so we don't crash
	q_snprintf (pathname, sizeof (pathname), "%s/%s", com_gamedir, name);

	if (!upsidedown)
	{
		flipped = CopyFlipped (data, width, height, bpp);
		if (!flipped)
			return false;
	}
	else
		flipped = data;

	error = stbi_write_jpg (pathname, width, height, bytes_per_pixel, flipped, quality);
	if (!upsidedown)
		free (flipped);

	return (error != 0);
}

qboolean Image_WritePNG (const char *name, byte *data, int width, int height, int bpp, qboolean upsidedown)
{
	unsigned error;
	char	pathname[MAX_OSPATH];
	byte *flipped;
	unsigned char *filters;
	unsigned char *png;
	size_t		pngsize;
	LodePNGState	state;

	if (!(bpp == 32 || bpp == 24))
		Sys_Error ("bpp not 24 or 32");

	Sys_mkdir (com_gamedir); // if we've switched to a nonexistant gamedir, create it now so we don't crash
	q_snprintf (pathname, sizeof (pathname), "%s/%s", com_gamedir, name);

	flipped = (!upsidedown) ? CopyFlipped (data, width, height, bpp) : data;
	filters = (unsigned char *) Q_zmalloc (height);
	if (!filters || !flipped)
	{
		if (!upsidedown)
			free (flipped);
		free (filters);
		return false;
	}

	// set some options for faster compression
	lodepng_state_init (&state);
	state.encoder.zlibsettings.use_lz77 = 0;
	state.encoder.auto_convert = 0;
	state.encoder.filter_strategy = LFS_PREDEFINED;
	memset (filters, 1, height); // use filter 1; see https://www.w3.org/TR/PNG-Filters.html
	state.encoder.predefined_filters = filters;

	if (bpp == 24)
	{
		state.info_raw.colortype = LCT_RGB;
		state.info_png.color.colortype = LCT_RGB;
	}
	else
	{
		state.info_raw.colortype = LCT_RGBA;
		state.info_png.color.colortype = LCT_RGBA;
	}

	error = lodepng_encode (&png, &pngsize, flipped, width, height, &state);
	if (error == 0) lodepng_save_file (png, pngsize, pathname);
#ifdef LODEPNG_COMPILE_ERROR_TEXT
	else Con_Printf ("WritePNG: %s\n", lodepng_error_text (error));
#endif

	lodepng_state_cleanup (&state);
	free (png);
	free (filters);
	if (!upsidedown)
		free (flipped);

	return (error == 0);
}


/*
================================================================================

	IMAGE PIXELS MANIPULATION

================================================================================
*/


/*
=================
Image_LoadPalette -- johnfitz -- was VID_SetPalette, moved here, renamed, rewritten
=================
*/
void Image_LoadPalette (void)
{
	// mh - i think jf must have been on drugs when he wrote the original version of this
	FILE *f;

	COM_FOpenFile ("gfx/palette.lmp", &f, NULL);
	if (!f)
		Sys_Error ("Couldn't load gfx/palette.lmp");

	int mark = Hunk_LowMark ();
	byte *pal = (byte *) Hunk_Alloc (768);
	fread (pal, 1, 768, f);
	fclose (f);

	for (int i = 0; i < 256; i++, pal += 3)
	{
		int r = pal[0];
		int g = pal[1];
		int b = pal[2];

		d_8to24table[i] = (255 << 24) | (r << 0) | (g << 8) | (b << 16);
		d_8to24table_conchars[i] = (255 << 24) | (r << 0) | (g << 8) | (b << 16);

		if (i > 223)
			d_8to24table_fbright[i] = d_8to24table[i];
		else d_8to24table_fbright[i] = 0;
	}

	// alpha colour
	d_8to24table[255] = 0;
	d_8to24table_fbright[255] = 0;
	d_8to24table_conchars[0] = 0;

	Hunk_FreeToLowMark (mark);
}



// gamma-correct to 16-bit precision, average, then mix back down to 8-bit precision so that we don't lose ultra-darks in the correction process
unsigned short image_mipgammatable[256];
byte image_mipinversegamma[65536];

extern GLint	gl_hardware_maxsize;


int AverageMip2 (int _1, int _2)
{
	return (_1 + _2) >> 1;
}


int AverageMip2GC (int _1, int _2)
{
	// http://filmicgames.com/archives/327
	// gamma-correct to 16-bit precision, average, then mix back down to 8-bit precision so that we don't lose ultra-darks in the correction process
	return image_mipinversegamma[(image_mipgammatable[_1] + image_mipgammatable[_2]) >> 1];
}


int AverageMip4 (int _1, int _2, int _3, int _4)
{
	return (_1 + _2 + _3 + _4) >> 2;
}


int AverageMip4GC (int _1, int _2, int _3, int _4)
{
	// http://filmicgames.com/archives/327
	// gamma-correct to 16-bit precision, average, then mix back down to 8-bit precision so that we don't lose ultra-darks in the correction process
	return image_mipinversegamma[(image_mipgammatable[_1] + image_mipgammatable[_2] + image_mipgammatable[_3] + image_mipgammatable[_4]) >> 2];
}


byte Image_GammaVal8to8 (byte val, float gamma)
{
	float f = powf ((val + 1) / 256.0, gamma);
	float inf = f * 255 + 0.5;

	if (inf < 0) inf = 0;
	if (inf > 255) inf = 255;

	return inf;
}


byte Image_ContrastVal8to8 (byte val, float contrast)
{
	float f = (float) val * contrast;

	if (f < 0) f = 0;
	if (f > 255) f = 255;

	return f;
}


unsigned short Image_GammaVal8to16 (byte val, float gamma)
{
	float f = powf ((val + 1) / 256.0, gamma);
	float inf = f * 65535 + 0.5;

	if (inf < 0) inf = 0;
	if (inf > 65535) inf = 65535;

	return inf;
}


byte Image_GammaVal16to8 (unsigned short val, float gamma)
{
	float f = powf ((val + 1) / 65536.0, gamma);
	float inf = (f * 255) + 0.5;

	if (inf < 0) inf = 0;
	if (inf > 255) inf = 255;

	return inf;
}


unsigned short Image_GammaVal16to16 (unsigned short val, float gamma)
{
	float f = powf ((val + 1) / 65536.0, gamma);
	float inf = (f * 65535) + 0.5;

	if (inf < 0) inf = 0;
	if (inf > 65535) inf = 65535;

	return inf;
}


void Image_Init (void)
{
	// gamma-correct to 16-bit precision, average, then mix back down to 8-bit precision so that we don't lose ultra-darks in the correction process
	for (int i = 0; i < 256; i++) image_mipgammatable[i] = Image_GammaVal8to16 (i, 2.2f);
	for (int i = 0; i < 65536; i++) image_mipinversegamma[i] = Image_GammaVal16to8 (i, 1.0f / 2.2f);

	// palette
	Image_LoadPalette ();
}


/*
================
Image_Pad -- return smallest power of two greater than or equal to s
================
*/
int Image_Pad (int s)
{
	int i;
	for (i = 1; i < s; i <<= 1)
		;
	return i;
}

/*
===============
Image_SafeTextureSize -- return a size with hardware and user prefs in mind
===============
*/
int Image_SafeTextureSize (int s)
{
	if (!GLEW_ARB_texture_non_power_of_two)
		s = Image_Pad (s);
	s = Q_imin (gl_hardware_maxsize, s);
	return s;
}

/*
================
Image_PadConditional -- only pad if a texture of that size would be padded. (used for tex coords)
================
*/
int Image_PadConditional (int s)
{
	if (s < Image_SafeTextureSize (s))
		return Image_Pad (s);
	else
		return s;
}


int AverageMip2 (int _1, int _2);
int AverageMip2GC (int _1, int _2);
int AverageMip4 (int _1, int _2, int _3, int _4);
int AverageMip4GC (int _1, int _2, int _3, int _4);


/*
================
Image_MipMapW
================
*/
unsigned *Image_MipMapW (unsigned *data, int width, int height)
{
	int	i, size;
	byte *out, *in;

	out = in = (byte *) data;
	size = (width * height) >> 1;

	for (i = 0; i < size; i++, out += 4, in += 8)
	{
		out[0] = AverageMip2GC (in[0], in[4]);
		out[1] = AverageMip2GC (in[1], in[5]);
		out[2] = AverageMip2GC (in[2], in[6]);
		out[3] = AverageMip2   (in[3], in[7]);
	}

	return data;
}


/*
================
Image_MipMapH
================
*/
unsigned *Image_MipMapH (unsigned *data, int width, int height)
{
	int	i, j;
	byte *out, *in;

	out = in = (byte *) data;
	height >>= 1;
	width <<= 2;

	for (i = 0; i < height; i++, in += width)
	{
		for (j = 0; j < width; j += 4, out += 4, in += 4)
		{
			out[0] = AverageMip2GC (in[0], in[width + 0]);
			out[1] = AverageMip2GC (in[1], in[width + 1]);
			out[2] = AverageMip2GC (in[2], in[width + 2]);
			out[3] = AverageMip2   (in[3], in[width + 3]);
		}
	}

	return data;
}


/*
================
Image_ResampleTexture -- bilinear resample
================
*/
unsigned *Image_ResampleTextureToSize (unsigned *in, int inwidth, int inheight, int outwidth, int outheight, qboolean alpha)
{
	int mark, i, j;
	unsigned *out, *p1, *p2, fracstep, frac;

	// can this ever happen???
	if (outwidth == inwidth && outheight == inheight) return in;

	// allocating the out buffer before the hunk mark so that we can return it
	out = (unsigned *) Hunk_Alloc (outwidth * outheight * 4);

	// and now get the mark because all allocations after this will be released
	mark = Hunk_LowMark ();

	p1 = (unsigned *) Hunk_Alloc (outwidth * 4);
	p2 = (unsigned *) Hunk_Alloc (outwidth * 4);

	fracstep = inwidth * 0x10000 / outwidth;
	frac = fracstep >> 2;

	for (i = 0; i < outwidth; i++)
	{
		p1[i] = 4 * (frac >> 16);
		frac += fracstep;
	}

	frac = 3 * (fracstep >> 2);

	for (i = 0; i < outwidth; i++)
	{
		p2[i] = 4 * (frac >> 16);
		frac += fracstep;
	}

	for (i = 0; i < outheight; i++)
	{
		unsigned *outrow = out + (i * outwidth);
		unsigned *inrow0 = in + inwidth * (int) (((i + 0.25f) * inheight) / outheight);
		unsigned *inrow1 = in + inwidth * (int) (((i + 0.75f) * inheight) / outheight);

		for (j = 0; j < outwidth; j++)
		{
			byte *pix1 = (byte *) inrow0 + p1[j];
			byte *pix2 = (byte *) inrow0 + p2[j];
			byte *pix3 = (byte *) inrow1 + p1[j];
			byte *pix4 = (byte *) inrow1 + p2[j];

			((byte *) &outrow[j])[0] = AverageMip4 (pix1[0], pix2[0], pix3[0], pix4[0]);
			((byte *) &outrow[j])[1] = AverageMip4 (pix1[1], pix2[1], pix3[1], pix4[1]);
			((byte *) &outrow[j])[2] = AverageMip4 (pix1[2], pix2[2], pix3[2], pix4[2]);

			if (alpha)
				((byte *) &outrow[j])[3] = AverageMip4 (pix1[3], pix2[3], pix3[3], pix4[3]);
			else ((byte *) &outrow[j])[3] = 255;
		}
	}

	Hunk_FreeToLowMark (mark);

	return out;
}


unsigned *Image_ResampleTexture (unsigned *in, int inwidth, int inheight, qboolean alpha)
{
	// special case - just resamples up to next POT
	int outwidth = Image_Pad (inwidth);
	int outheight = Image_Pad (inheight);

	return Image_ResampleTextureToSize (in, inwidth, inheight, outwidth, outheight, alpha);
}


/*
===============
Image_AlphaEdgeFix

eliminate pink edges on sprites, etc.
operates in place on 32bit data
===============
*/
void Image_AlphaEdgeFix (byte *data, int width, int height)
{
	int	i, j, n = 0, b, c[3] = { 0, 0, 0 },
		lastrow, thisrow, nextrow,
		lastpix, thispix, nextpix;
	byte *dest = data;

	for (i = 0; i < height; i++)
	{
		lastrow = width * 4 * ((i == 0) ? height - 1 : i - 1);
		thisrow = width * 4 * i;
		nextrow = width * 4 * ((i == height - 1) ? 0 : i + 1);

		for (j = 0; j < width; j++, dest += 4)
		{
			if (dest[3]) // not transparent
				continue;

			lastpix = 4 * ((j == 0) ? width - 1 : j - 1);
			thispix = 4 * j;
			nextpix = 4 * ((j == width - 1) ? 0 : j + 1);

			b = lastrow + lastpix; if (data[b + 3]) { c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++; }
			b = thisrow + lastpix; if (data[b + 3]) { c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++; }
			b = nextrow + lastpix; if (data[b + 3]) { c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++; }
			b = lastrow + thispix; if (data[b + 3]) { c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++; }
			b = nextrow + thispix; if (data[b + 3]) { c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++; }
			b = lastrow + nextpix; if (data[b + 3]) { c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++; }
			b = thisrow + nextpix; if (data[b + 3]) { c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++; }
			b = nextrow + nextpix; if (data[b + 3]) { c[0] += data[b]; c[1] += data[b + 1]; c[2] += data[b + 2]; n++; }

			// average all non-transparent neighbors
			if (n)
			{
				dest[0] = (byte) (c[0] / n);
				dest[1] = (byte) (c[1] / n);
				dest[2] = (byte) (c[2] / n);

				n = c[0] = c[1] = c[2] = 0;
			}
		}
	}
}


/*
=================
Image_FloodFillSkin

Fill background pixels so mipmapping doesn't have haloes - Ed
=================
*/

typedef struct floodfill_s {
	short		x, y;
} floodfill_t;

// must be a power of 2
#define	FLOODFILL_FIFO_SIZE		0x1000
#define	FLOODFILL_FIFO_MASK		(FLOODFILL_FIFO_SIZE - 1)

#define FLOODFILL_STEP( off, dx, dy )				\
do {								\
	if (pos[off] == fillcolor)				\
	{							\
		pos[off] = 255;					\
		fifo[inpt].x = x + (dx), fifo[inpt].y = y + (dy); \
		inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;	\
	}							\
	else if (pos[off] != 255) fdc = pos[off];		\
} while (0)

void Image_FloodFillSkin (byte *skin, int skinwidth, int skinheight)
{
	byte		fillcolor = *skin; // assume this is the pixel to fill
	floodfill_t	fifo[FLOODFILL_FIFO_SIZE];
	int			inpt = 0, outpt = 0;
	int			filledcolor = -1;
	int			i;

	// scan the data to see if it's single-colour; if so assume a fill is invalid and stop
	// data[0] has already been stored to fillcolor so use that to check for a match
	for (i = 1; i < skinwidth * skinheight; i++)
		if (skin[i] != fillcolor)
			break;

	// everything matched
	if (i == skinwidth * skinheight) return;

	if (filledcolor == -1)
	{
		filledcolor = 0;

		// attempt to find opaque black
		for (i = 0; i < 256; ++i)
		{
			if (d_8to24table[i] == (255 << 0)) // alpha 1.0
			{
				filledcolor = i;
				break;
			}
		}
	}

	// can't fill to filled color or to transparent color (used as visited marker)
	if ((fillcolor == filledcolor) || (fillcolor == 255))
	{
		// printf( "not filling skin from %d to %d\n", fillcolor, filledcolor );
		return;
	}

	fifo[inpt].x = 0, fifo[inpt].y = 0;
	inpt = (inpt + 1) & FLOODFILL_FIFO_MASK;

	while (outpt != inpt)
	{
		int			x = fifo[outpt].x, y = fifo[outpt].y;
		int			fdc = filledcolor;
		byte *pos = &skin[x + skinwidth * y];

		outpt = (outpt + 1) & FLOODFILL_FIFO_MASK;

		if (x > 0)				FLOODFILL_STEP (-1, -1, 0);
		if (x < skinwidth - 1)	FLOODFILL_STEP (1, 1, 0);
		if (y > 0)				FLOODFILL_STEP (-skinwidth, 0, -1);
		if (y < skinheight - 1)	FLOODFILL_STEP (skinwidth, 0, 1);
		skin[x + skinwidth * y] = fdc;
	}
}



/*
===============
Image_PadEdgeFixW -- special case of AlphaEdgeFix for textures that only need it because they were padded

operates in place on 32bit data, and expects unpadded height and width values
===============
*/
void Image_PadEdgeFixW (byte *data, int width, int height)
{
	byte *src, *dst;
	int i, padw, padh;

	padw = Image_PadConditional (width);
	padh = Image_PadConditional (height);

	// copy last full column to first empty column, leaving alpha byte at zero
	src = data + (width - 1) * 4;

	for (i = 0; i < padh; i++)
	{
		src[4] = src[0];
		src[5] = src[1];
		src[6] = src[2];
		src += padw * 4;
	}

	// copy first full column to last empty column, leaving alpha byte at zero
	src = data;
	dst = data + (padw - 1) * 4;

	for (i = 0; i < padh; i++)
	{
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
		src += padw * 4;
		dst += padw * 4;
	}
}

/*
===============
Image_PadEdgeFixH -- special case of AlphaEdgeFix for textures that only need it because they were padded

operates in place on 32bit data, and expects unpadded height and width values
===============
*/
void Image_PadEdgeFixH (byte *data, int width, int height)
{
	byte *src, *dst;
	int i, padw, padh;

	padw = Image_PadConditional (width);
	padh = Image_PadConditional (height);

	// copy last full row to first empty row, leaving alpha byte at zero
	dst = data + height * padw * 4;
	src = dst - padw * 4;

	for (i = 0; i < padw; i++)
	{
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
		src += 4;
		dst += 4;
	}

	// copy first full row to last empty row, leaving alpha byte at zero
	dst = data + (padh - 1) * padw * 4;
	src = data;

	for (i = 0; i < padw; i++)
	{
		dst[0] = src[0];
		dst[1] = src[1];
		dst[2] = src[2];
		src += 4;
		dst += 4;
	}
}

/*
================
Image_8to32
================
*/
unsigned *Image_8to32 (byte *in, int pixels, unsigned int *usepal)
{
	int i;
	unsigned *out, *data;

	out = data = (unsigned *) Hunk_Alloc (pixels * 4);

	for (i = 0; i < pixels; i++)
		*out++ = usepal[*in++];

	return data;
}

/*
================
Image_PadImageW -- return image with width padded up to power-of-two dimentions
================
*/
byte *Image_PadImageW (byte *in, int width, int height, byte padbyte)
{
	int i, j, outwidth;
	byte *out, *data;

	if (width == Image_Pad (width))
		return in;

	outwidth = Image_Pad (width);

	out = data = (byte *) Hunk_Alloc (outwidth * height);

	for (i = 0; i < height; i++)
	{
		for (j = 0; j < width; j++)
			*out++ = *in++;
		for (; j < outwidth; j++)
			*out++ = padbyte;
	}

	return data;
}

/*
================
Image_PadImageH -- return image with height padded up to power-of-two dimentions
================
*/
byte *Image_PadImageH (byte *in, int width, int height, byte padbyte)
{
	int i, srcpix, dstpix;
	byte *data, *out;

	if (height == Image_Pad (height))
		return in;

	srcpix = width * height;
	dstpix = width * Image_Pad (height);

	out = data = (byte *) Hunk_Alloc (dstpix);

	for (i = 0; i < srcpix; i++)
		*out++ = *in++;
	for (; i < dstpix; i++)
		*out++ = padbyte;

	return data;
}


void Image_NewTranslation (int top, int bottom, byte *translation)
{
	for (int i = 0; i < 256; i++)
		translation[i] = i;

	if (top < 128)
	{
		for (int i = 0; i < 16; i++)
			translation[TOP_RANGE + i] = top + i;
	}
	else
	{
		for (int i = 0; i < 16; i++)
			translation[TOP_RANGE + i] = top + 15 - i;
	}

	if (bottom < 128)
	{
		for (int i = 0; i < 16; i++)
			translation[BOTTOM_RANGE + i] = bottom + i;
	}
	else
	{
		for (int i = 0; i < 16; i++)
			translation[BOTTOM_RANGE + i] = bottom + 15 - i;
	}
}


