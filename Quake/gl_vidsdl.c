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
// gl_vidsdl.c -- SDL GL vid component

#include "quakedef.h"
#include "cfgfile.h"
#include "bgmusic.h"
#include "resource.h"
#if defined(SDL_FRAMEWORK) || defined(NO_SDL_CONFIG)
#if defined(USE_SDL2)
#include <SDL2/SDL.h>
#else
#include <SDL/SDL.h>
#endif
#else
#include "SDL.h"
#endif

// ericw -- for putting the driver into multithreaded mode
#ifdef __APPLE__
#include <OpenGL/OpenGL.h>
#endif

#define MAX_MODE_LIST	600 // johnfitz -- was 30
#define MAX_BPPS_LIST	5
#define MAX_RATES_LIST	20
#define MAXWIDTH		10000
#define MAXHEIGHT		10000

#define DEFAULT_SDL_FLAGS	SDL_OPENGL

#define DEFAULT_REFRESHRATE	60

typedef struct vmode_s {
	int			width;
	int			height;
	int			refreshrate;
	int			bpp;
} vmode_t;

static const char *gl_vendor;
static const char *gl_renderer;
static const char *gl_version;

static vmode_t	modelist[MAX_MODE_LIST];
static int		nummodes;

static qboolean	vid_initialized = false;

#if defined(USE_SDL2)
static SDL_Window *draw_context;
static SDL_GLContext	gl_context;
#else
static SDL_Surface *draw_context;
#endif

static qboolean	vid_locked = false; // johnfitz
static qboolean	vid_changed = false;

static void VID_Menu_Init (void); // johnfitz
static void VID_Menu_f (void); // johnfitz
static void VID_MenuDraw (void);
static void VID_MenuKey (int key);

static void ClearAllStates (void);
static void GL_Init (void);
static void GL_SetupState (void); // johnfitz

viddef_t	vid;				// global video state
modestate_t	modestate = MS_UNINIT;
qboolean	scr_skipupdate;

qboolean gl_swap_control = false; // johnfitz
qboolean gl_anisotropy_able = false; // johnfitz
float gl_max_anisotropy; // johnfitz
GLint gl_max_texture_units = 0; // ericw
int gl_stencilbits;


// ====================================

// johnfitz -- new cvars
static cvar_t	vid_fullscreen = { "vid_fullscreen", "0", CVAR_ARCHIVE };	// QuakeSpasm, was "1"
static cvar_t	vid_width = { "vid_width", "800", CVAR_ARCHIVE };		// QuakeSpasm, was 640
static cvar_t	vid_height = { "vid_height", "600", CVAR_ARCHIVE };	// QuakeSpasm, was 480
static cvar_t	vid_bpp = { "vid_bpp", "16", CVAR_ARCHIVE };
static cvar_t	vid_refreshrate = { "vid_refreshrate", "60", CVAR_ARCHIVE };
static cvar_t	vid_vsync = { "vid_vsync", "0", CVAR_ARCHIVE };
static cvar_t	vid_fsaa = { "vid_fsaa", "0", CVAR_ARCHIVE }; // QuakeSpasm
static cvar_t	vid_desktopfullscreen = { "vid_desktopfullscreen", "0", CVAR_ARCHIVE }; // QuakeSpasm
static cvar_t	vid_borderless = { "vid_borderless", "0", CVAR_ARCHIVE }; // QuakeSpasm
// johnfitz


#if defined(USE_SDL2)
// mh - improved vsync; under D3D11 swap interval is a param on the Present call and we just check the value to use when calling present at the
// end of the frame; under other APIs is explicit state that must be set, so we need handling for it.
// valid swap interval values are -1, 0 or 1, so set this to a value that will be triggered first time it's seen
static int vid_currentvsync = 666;
#endif

cvar_t		vid_gamma = { "gamma", "1", CVAR_ARCHIVE }; // johnfitz -- moved here from view.c
cvar_t		vid_contrast = { "contrast", "1", CVAR_ARCHIVE }; // QuakeSpasm, MarkV

// ==========================================================================
//  HARDWARE GAMMA -- johnfitz
// ==========================================================================

#define	USE_GAMMA_RAMPS			0

#if USE_GAMMA_RAMPS
static unsigned short vid_gamma_red[256];
static unsigned short vid_gamma_green[256];
static unsigned short vid_gamma_blue[256];

static unsigned short vid_sysgamma_red[256];
static unsigned short vid_sysgamma_green[256];
static unsigned short vid_sysgamma_blue[256];
#endif

static qboolean	gammaworks = false;	// whether hw-gamma works
static int fsaa;

/*
================
VID_Gamma_SetGamma -- apply gamma correction
================
*/
static void VID_Gamma_SetGamma (void)
{
}

/*
================
VID_Gamma_Restore -- restore system gamma
================
*/
static void VID_Gamma_Restore (void)
{
}

/*
================
VID_Gamma_Shutdown -- called on exit
================
*/
static void VID_Gamma_Shutdown (void)
{
	VID_Gamma_Restore ();
}

/*
================
VID_Gamma_f -- callback when the cvar changes
================
*/
static void VID_Gamma_f (cvar_t *var)
{
}

/*
================
VID_Gamma_Init -- call on init
================
*/
static void VID_Gamma_Init (void)
{
	Cvar_RegisterVariable (&vid_gamma);
	Cvar_RegisterVariable (&vid_contrast);
	Cvar_SetCallback (&vid_gamma, VID_Gamma_f);
	Cvar_SetCallback (&vid_contrast, VID_Gamma_f);
}


/*
======================
VID_GetCurrentWidth
======================
*/
static int VID_GetCurrentWidth (void)
{
#if defined(USE_SDL2)
	int w = 0, h = 0;
	SDL_GetWindowSize (draw_context, &w, &h);
	return w;
#else
	return draw_context->w;
#endif
}

/*
=======================
VID_GetCurrentHeight
=======================
*/
static int VID_GetCurrentHeight (void)
{
#if defined(USE_SDL2)
	int w = 0, h = 0;
	SDL_GetWindowSize (draw_context, &w, &h);
	return h;
#else
	return draw_context->h;
#endif
}

/*
====================
VID_GetCurrentRefreshRate
====================
*/
static int VID_GetCurrentRefreshRate (void)
{
#if defined(USE_SDL2)
	SDL_DisplayMode mode;
	int current_display;

	current_display = SDL_GetWindowDisplayIndex (draw_context);

	if (0 != SDL_GetCurrentDisplayMode (current_display, &mode))
		return DEFAULT_REFRESHRATE;

	return mode.refresh_rate;
#else
	// SDL1.2 doesn't support refresh rates
	return DEFAULT_REFRESHRATE;
#endif
}


/*
====================
VID_GetCurrentBPP
====================
*/
static int VID_GetCurrentBPP (void)
{
#if defined(USE_SDL2)
	const Uint32 pixelFormat = SDL_GetWindowPixelFormat (draw_context);
	return SDL_BITSPERPIXEL (pixelFormat);
#else
	return draw_context->format->BitsPerPixel;
#endif
}

/*
====================
VID_GetFullscreen

returns true if we are in regular fullscreen or "desktop fullscren"
====================
*/
static qboolean VID_GetFullscreen (void)
{
#if defined(USE_SDL2)
	return (SDL_GetWindowFlags (draw_context) & SDL_WINDOW_FULLSCREEN) != 0;
#else
	return (draw_context->flags & SDL_FULLSCREEN) != 0;
#endif
}

/*
====================
VID_GetDesktopFullscreen

returns true if we are specifically in "desktop fullscreen" mode
====================
*/
static qboolean VID_GetDesktopFullscreen (void)
{
#if defined(USE_SDL2)
	return (SDL_GetWindowFlags (draw_context) & SDL_WINDOW_FULLSCREEN_DESKTOP) == SDL_WINDOW_FULLSCREEN_DESKTOP;
#else
	return false;
#endif
}

/*
====================
VID_GetVSync
====================
*/
static qboolean VID_GetVSync (void)
{
#if defined(USE_SDL2)
	return SDL_GL_GetSwapInterval () == 1;
#else
	int swap_control;
	if (SDL_GL_GetAttribute (SDL_GL_SWAP_CONTROL, &swap_control) == 0)
		return swap_control > 0;
	return false;
#endif
}

/*
====================
VID_GetWindow

used by pl_win.c
====================
*/
void *VID_GetWindow (void)
{
#if defined(USE_SDL2)
	return draw_context;
#else
	return NULL;
#endif
}

/*
====================
VID_HasMouseOrInputFocus
====================
*/
qboolean VID_HasMouseOrInputFocus (void)
{
#if defined(USE_SDL2)
	return (SDL_GetWindowFlags (draw_context) & (SDL_WINDOW_MOUSE_FOCUS | SDL_WINDOW_INPUT_FOCUS)) != 0;
#else
	return (SDL_GetAppState () & (SDL_APPMOUSEFOCUS | SDL_APPINPUTFOCUS)) != 0;
#endif
}

/*
====================
VID_IsMinimized
====================
*/
qboolean VID_IsMinimized (void)
{
#if defined(USE_SDL2)
	return !(SDL_GetWindowFlags (draw_context) & SDL_WINDOW_SHOWN);
#else
	/* SDL_APPACTIVE in SDL 1.x means "not minimized" */
	return !(SDL_GetAppState () & SDL_APPACTIVE);
#endif
}

#if defined(USE_SDL2)
/*
================
VID_SDL2_GetDisplayMode

Returns a pointer to a statically allocated SDL_DisplayMode structure
if there is one with the requested params on the default display.
Otherwise returns NULL.

This is passed to SDL_SetWindowDisplayMode to specify a pixel format
with the requested bpp. If we didn't care about bpp we could just pass NULL.
================
*/
static SDL_DisplayMode *VID_SDL2_GetDisplayMode (int width, int height, int refreshrate, int bpp)
{
	static SDL_DisplayMode mode;
	const int sdlmodes = SDL_GetNumDisplayModes (0);
	int i;

	for (i = 0; i < sdlmodes; i++)
	{
		if (SDL_GetDisplayMode (0, i, &mode) != 0)
			continue;

		if (mode.w == width && mode.h == height
			&& SDL_BITSPERPIXEL (mode.format) == bpp
			&& mode.refresh_rate == refreshrate)
		{
			return &mode;
		}
	}
	return NULL;
}
#endif /* USE_SDL2 */

/*
================
VID_ValidMode
================
*/
static qboolean VID_ValidMode (int width, int height, int refreshrate, int bpp, qboolean fullscreen)
{
	// ignore width / height / bpp if vid_desktopfullscreen is enabled
	if (fullscreen && vid_desktopfullscreen.value)
		return true;

	if (width < 320)
		return false;

	if (height < 200)
		return false;

#if defined(USE_SDL2)
	if (fullscreen && VID_SDL2_GetDisplayMode (width, height, refreshrate, bpp) == NULL)
		bpp = 0;
#else
	{
		Uint32 flags = DEFAULT_SDL_FLAGS;
		if (fullscreen)
			flags |= SDL_FULLSCREEN;

		bpp = SDL_VideoModeOK (width, height, bpp, flags);
	}
#endif

	switch (bpp)
	{
	case 16:
	case 24:
	case 32:
		break;
	default:
		return false;
	}

	return true;
}

/*
================
VID_SetMode
================
*/
static qboolean VID_SetMode (int width, int height, int refreshrate, int bpp, qboolean fullscreen)
{
	int		temp;
	Uint32	flags;
	char		caption[50];
	int		depthbits, stencilbits;
	int		fsaa_obtained;
#if defined(USE_SDL2)
	int		previous_display;
#endif

	// so Con_Printfs don't mess us up by forcing vid and snd updates
	temp = scr_disabled_for_loading;
	scr_disabled_for_loading = true;

	CDAudio_Pause ();
	BGM_Pause ();

	/* z-buffer depth */
	if (bpp == 16)
	{
		depthbits = 16;
		stencilbits = 0;
	}
	else
	{
		depthbits = 24;
		stencilbits = 8;
	}

	SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, depthbits);
	SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, stencilbits);

	/* fsaa */
	SDL_GL_SetAttribute (SDL_GL_MULTISAMPLEBUFFERS, fsaa > 0 ? 1 : 0);
	SDL_GL_SetAttribute (SDL_GL_MULTISAMPLESAMPLES, fsaa);

	q_snprintf (caption, sizeof (caption), "mhQuakeSpasm; based on QuakeSpasm " QUAKESPASM_VER_STRING);

	/* Create the window if needed, hidden */
	if (!draw_context)
	{
		flags = SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN;

		if (vid_borderless.value)
			flags |= SDL_WINDOW_BORDERLESS;

		draw_context = SDL_CreateWindow (caption, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);

		if (!draw_context)
		{
			// scale back fsaa
			SDL_GL_SetAttribute (SDL_GL_MULTISAMPLEBUFFERS, 0);
			SDL_GL_SetAttribute (SDL_GL_MULTISAMPLESAMPLES, 0);
			draw_context = SDL_CreateWindow (caption, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);
		}

		if (!draw_context)
		{
			// scale back SDL_GL_DEPTH_SIZE
			SDL_GL_SetAttribute (SDL_GL_DEPTH_SIZE, 16);
			draw_context = SDL_CreateWindow (caption, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);
		}

		if (!draw_context)
		{
			// scale back SDL_GL_STENCIL_SIZE
			SDL_GL_SetAttribute (SDL_GL_STENCIL_SIZE, 0);
			draw_context = SDL_CreateWindow (caption, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, flags);
		}

		if (!draw_context)
			Sys_Error ("Couldn't create window");

		previous_display = -1;
	}
	else
	{
		previous_display = SDL_GetWindowDisplayIndex (draw_context);
	}

	/* Ensure the window is not fullscreen */
	if (VID_GetFullscreen ())
	{
		if (SDL_SetWindowFullscreen (draw_context, 0) != 0)
			Sys_Error ("Couldn't set fullscreen state mode");
	}

	/* Set window size and display mode */
	SDL_SetWindowSize (draw_context, width, height);
	if (previous_display >= 0)
		SDL_SetWindowPosition (draw_context, SDL_WINDOWPOS_CENTERED_DISPLAY (previous_display), SDL_WINDOWPOS_CENTERED_DISPLAY (previous_display));
	else
		SDL_SetWindowPosition (draw_context, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
	SDL_SetWindowDisplayMode (draw_context, VID_SDL2_GetDisplayMode (width, height, refreshrate, bpp));
	SDL_SetWindowBordered (draw_context, vid_borderless.value ? SDL_FALSE : SDL_TRUE);

	/* Make window fullscreen if needed, and show the window */

	if (fullscreen)
	{
		Uint32 flags = vid_desktopfullscreen.value ?
			SDL_WINDOW_FULLSCREEN_DESKTOP :
			SDL_WINDOW_FULLSCREEN;
		if (SDL_SetWindowFullscreen (draw_context, flags) != 0)
			Sys_Error ("Couldn't set fullscreen state mode");
	}

	SDL_ShowWindow (draw_context);

	/* Create GL context if needed */
	if (!gl_context)
	{
		gl_context = SDL_GL_CreateContext (draw_context);
		if (!gl_context)
			Sys_Error ("Couldn't create GL context");
	}

	vid_currentvsync = 666; // trigger a vsync change in GL_BeginRendering first time it's seen
	gl_swap_control = true;

	if (SDL_GL_SetSwapInterval ((vid_vsync.value) ? 1 : 0) == -1)
		gl_swap_control = false;

	vid.width = VID_GetCurrentWidth ();
	vid.height = VID_GetCurrentHeight ();
	vid.conwidth = vid.width & 0xFFFFFFF8;
	vid.conheight = vid.conwidth * vid.height / vid.width;

	// read the obtained z-buffer depth
	if (SDL_GL_GetAttribute (SDL_GL_DEPTH_SIZE, &depthbits) == -1)
		depthbits = 0;

	// read obtained fsaa samples
	if (SDL_GL_GetAttribute (SDL_GL_MULTISAMPLESAMPLES, &fsaa_obtained) == -1)
		fsaa_obtained = 0;

	// read stencil bits
	if (SDL_GL_GetAttribute (SDL_GL_STENCIL_SIZE, &gl_stencilbits) == -1)
		gl_stencilbits = 0;

	modestate = VID_GetFullscreen () ? MS_FULLSCREEN : MS_WINDOWED;

	CDAudio_Resume ();
	BGM_Resume ();
	scr_disabled_for_loading = temp;

	// fix the leftover Alt from any Alt-Tab or the like that switched us away
	ClearAllStates ();

	Con_SafePrintf ("Video mode %dx%dx%d %dHz (%d-bit z-buffer, %dx FSAA) initialized\n",
		VID_GetCurrentWidth (),
		VID_GetCurrentHeight (),
		VID_GetCurrentBPP (),
		VID_GetCurrentRefreshRate (),
		depthbits,
		fsaa_obtained
	);

	// no pending changes
	vid_changed = false;

	return true;
}

/*
===================
VID_Changed_f -- kristian -- notify us that a value has changed that requires a vid_restart
===================
*/
static void VID_Changed_f (cvar_t *var)
{
	vid_changed = true;
}


/*
===================
VID_Restart -- johnfitz -- change video modes on the fly
===================
*/
static void VID_Restart (void)
{
	int width, height, refreshrate, bpp;
	qboolean fullscreen;

	if (vid_locked || !vid_changed)
		return;

	width = (int) vid_width.value;
	height = (int) vid_height.value;
	refreshrate = (int) vid_refreshrate.value;
	bpp = (int) vid_bpp.value;
	fullscreen = vid_fullscreen.value ? true : false;

	// validate new mode
	if (!VID_ValidMode (width, height, refreshrate, bpp, fullscreen))
	{
		Con_Printf ("%dx%dx%d %dHz %s is not a valid mode\n",
			width, height, bpp, refreshrate, fullscreen ? "fullscreen" : "windowed");
		return;
	}

	// ericw -- OS X, SDL1: textures, VBO's invalid after mode change
	//          OS X, SDL2: still valid after mode change
	// To handle both cases, delete all GL objects (textures, VBO, GLSL) now.
	// We must not interleave deleting the old objects with creating new ones, because
	// one of the new objects could be given the same ID as an invalid handle
	// which is later deleted.

	TexMgr_DeleteTextureObjects ();
	R_DeleteShaders ();
	GL_DeleteBModelVertexBuffer ();
	R_FreeAllBufferSets ();

	// set new mode
	VID_SetMode (width, height, refreshrate, bpp, fullscreen);

	GL_Init ();
	TexMgr_ReloadImages ();
	GL_BuildBModelVertexBuffer ();
	GLMesh_ReloadVertexBuffers ();
	GL_SetupState ();
	Fog_SetupState ();

	// conwidth and conheight need to be recalculated (push through here so we do it consistently in both places) 
	SCR_Conwidth_f (&scr_conwidth);

	// keep cvars in line with actual mode
	VID_SyncCvars ();

	// update mouse grab
	if (key_dest == key_console || key_dest == key_menu)
	{
		if (modestate == MS_WINDOWED)
			IN_Deactivate (true);
		else if (modestate == MS_FULLSCREEN)
			IN_Activate ();
	}
}


/*
================
VID_Test -- johnfitz -- like vid_restart, but asks for confirmation after switching modes
================
*/
static void VID_Test (void)
{
	int old_width, old_height, old_refreshrate, old_bpp, old_fullscreen;

	if (vid_locked || !vid_changed)
		return;

	// now try the switch
	old_width = VID_GetCurrentWidth ();
	old_height = VID_GetCurrentHeight ();
	old_refreshrate = VID_GetCurrentRefreshRate ();
	old_bpp = VID_GetCurrentBPP ();
	old_fullscreen = VID_GetFullscreen () ? true : false;

	VID_Restart ();

	// pop up confirmation dialoge
	if (!SCR_ModalMessage ("Would you like to keep this\nvideo mode? (y/n)\n", 5.0f))
	{
		// revert cvars and mode
		Cvar_SetValueQuick (&vid_width, old_width);
		Cvar_SetValueQuick (&vid_height, old_height);
		Cvar_SetValueQuick (&vid_refreshrate, old_refreshrate);
		Cvar_SetValueQuick (&vid_bpp, old_bpp);
		Cvar_SetQuick (&vid_fullscreen, old_fullscreen ? "1" : "0");
		VID_Restart ();
	}
}

/*
================
VID_Unlock -- johnfitz
================
*/
static void VID_Unlock (void)
{
	vid_locked = false;
	VID_SyncCvars ();
}

/*
================
VID_Lock -- ericw

Subsequent changes to vid_* mode settings, and vid_restart commands, will
be ignored until the "vid_unlock" command is run.

Used when changing gamedirs so the current settings override what was saved
in the config.cfg.
================
*/
void VID_Lock (void)
{
	vid_locked = true;
}

// ==============================================================================
//	OPENGL STUFF
// ==============================================================================

/*
===============
GL_Info_f -- johnfitz
===============
*/
static void GL_Info_f (void)
{
	Con_SafePrintf ("GL_VENDOR: %s\n", gl_vendor);
	Con_SafePrintf ("GL_RENDERER: %s\n", gl_renderer);
	Con_SafePrintf ("GL_VERSION: %s\n", gl_version);
	Con_SafePrintf ("GL_EXTENSIONS:\n");

	// mh - print GL_EXTENSIONS safely
	const char *extensions = glGetString (GL_EXTENSIONS);
	char oneextension[1024] = { 0 };

	for (int i = 0, j = 0; ; i++)
	{
		if (!extensions[i])
		{
			oneextension[j++] = 0;
			break;
		}
		else if (extensions[i] == ' ')
		{
			oneextension[j++] = 0;
			Con_SafePrintf ("  %s\n", oneextension);
			j = 0;
		}
		else oneextension[j++] = extensions[i];
	}

	Con_SafePrintf ("  %s\n", oneextension);
}

/*
===============
GL_CheckExtensions
===============
*/
static void GL_CheckExtensions (void)
{
	int swap_control;

	if (glewInit () != GLEW_NO_ERROR) Sys_Error ("Failed to initialize GLEW");

	// extensions we must use
	if (!GLEW_VERSION_1_3)
		if (!GLEW_ARB_multitexture)
			Sys_Error ("GL multitexture extension : not found");

	if (!GLEW_VERSION_1_5)
		if (!GLEW_ARB_vertex_buffer_object)
			Sys_Error ("GL vertex_buffer_object extension : not found");

	if (!GLEW_ARB_vertex_program) Sys_Error ("GL_ARB_vertex_program : not found");
	if (!GLEW_ARB_fragment_program) Sys_Error ("GL_ARB_fragment_program : not found");

	// we want one of the rectangle texture extensions but we don't care which - the token define (GL_TEXTURE_RECTANGLE, GL_TEXTURE_RECTANGLE_ARB,
	// GL_TEXTURE_RECTANGLE_EXT or GL_TEXTURE_RECTANGLE_NV) is the same for all (0x84F5), so for our use cases we treat them as equivalent.
	if (!GLEW_VERSION_3_1)
		if (!GLEW_ARB_texture_rectangle)
			if (!GLEW_EXT_texture_rectangle)
				if (!GLEW_NV_texture_rectangle)
					Sys_Error ("GL texture_rectangle extension : not found");

	// we want one of the cube map texture extensions but we don't care which - the token define is the same for all, so for our use cases we treat them as equivalent.
	if (!GLEW_VERSION_1_3)
		if (!GLEW_ARB_texture_cube_map)
			if (!GLEW_EXT_texture_cube_map)
				Sys_Error ("GL texture_cube_map extension : not found");

	// we want one of the clamp-to-edge extensions but we don't care which - the token define is the same for all, so for our use cases we treat them as equivalent.
	if (!GLEW_VERSION_1_2)
		if (!GLEW_EXT_texture_edge_clamp)
			if (!GLEW_SGIS_texture_edge_clamp)
				Sys_Error ("GL texture_edge_clamp extension : not found");

	// we need 6 TMUs and we query GL_MAX_TEXTURE_IMAGE_UNITS because these are the ones that are accessible in shaders via GL_ARB_fragment_program;
	// some GPUs report a hard limit of 4 for GL_MAX_TEXTURE_UNITS which is the FFP limit
	// (note: GL_MAX_TEXTURE_IMAGE_UNITS, GL_MAX_TEXTURE_IMAGE_UNITS_NV and GL_MAX_TEXTURE_IMAGE_UNITS_ARB are all 0x8872)
	glGetIntegerv (GL_MAX_TEXTURE_IMAGE_UNITS, &gl_max_texture_units);

	// diffuse/lmr/lmg/lmb/luma/sky cube
	// OpenGL binding points make it just more robust to reserve a dedicated TMU for the cubemap
	if (gl_max_texture_units < 6)
		Sys_Error ("GL_MAX_TEXTURE_IMAGE_UNITS < 6");

	// swap control
	if (!gl_swap_control)
		Con_Warning ("vertical sync not supported (SDL_GL_SetSwapInterval failed)\n");
	else if ((swap_control = SDL_GL_GetSwapInterval ()) == -1)
	{
		gl_swap_control = false;
		Con_Warning ("vertical sync not supported (SDL_GL_GetSwapInterval failed)\n");
	}
	else if ((vid_vsync.value && swap_control != 1) || (!vid_vsync.value && swap_control != 0))
	{
		gl_swap_control = false;
		Con_Warning ("vertical sync not supported (swap_control doesn't match vid_vsync)\n");
	}
	else Con_Printf ("FOUND: SDL_GL_SetSwapInterval\n");

	// anisotropic filtering - defines are the same here
	if (GLEW_EXT_texture_filter_anisotropic || GLEW_ARB_texture_filter_anisotropic)
	{
		float test1, test2;
		GLuint tex;

		// test to make sure we really have control over it
		// 1.0 and 2.0 should always be legal values
		glGenTextures (1, &tex);
		glBindTexture (GL_TEXTURE_2D, tex);
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 1.0f);
		glGetTexParameterfv (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &test1);
		glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, 2.0f);
		glGetTexParameterfv (GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, &test2);
		glDeleteTextures (1, &tex);

		if (test1 == 1 && test2 == 2)
		{
			if (GLEW_ARB_texture_filter_anisotropic)
				Con_Printf ("FOUND: ARB_texture_filter_anisotropic\n");
			else Con_Printf ("FOUND: EXT_texture_filter_anisotropic\n");
			gl_anisotropy_able = true;
		}
		else
		{
			Con_Warning ("anisotropic filtering locked by driver. Current driver setting is %f\n", test1);
		}

		// get max value either way, so the menu and stuff know it
		glGetFloatv (GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &gl_max_anisotropy);

		if (gl_max_anisotropy < 2)
		{
			gl_anisotropy_able = false;
			gl_max_anisotropy = 1;
			Con_Warning ("anisotropic filtering broken: disabled\n");
		}
	}
	else
	{
		gl_max_anisotropy = 1;
		Con_Warning ("texture_filter_anisotropic not supported\n");
	}
}


/*
===============
GL_SetupState -- johnfitz

does all the stuff from GL_Init that needs to be done every time a new GL render context is created
===============
*/
static void GL_SetupState (void)
{
	glClearColor (0.15, 0.15, 0.15, 0); // johnfitz -- originally 1,0,0,0

	glCullFace (GL_BACK); // johnfitz -- glquake used CCW with backwards culling -- let's do it right
	glFrontFace (GL_CW); // johnfitz -- glquake used CCW with backwards culling -- let's do it right

	glPolygonMode (GL_FRONT_AND_BACK, GL_FILL);
	glShadeModel (GL_SMOOTH);
	glHint (GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST); // johnfitz
	glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	// note: here, these only apply to texture object 0
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameterf (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glDepthRange (0, 1); // johnfitz -- moved here becuase gl_ztrick is gone.

	if (gl_stencilbits)
	{
		// for GL_DrawAliasShadow
		glClearStencil (1);
		glStencilFunc (GL_EQUAL, 1, 2);
		glStencilOp (GL_KEEP, GL_KEEP, GL_INCR);
	}
}


/*
===============
GL_Init
===============
*/
static void GL_Init (void)
{
	gl_vendor = (const char *) glGetString (GL_VENDOR);
	gl_renderer = (const char *) glGetString (GL_RENDERER);
	gl_version = (const char *) glGetString (GL_VERSION);

	Con_SafePrintf ("GL_VENDOR: %s\n", gl_vendor);
	Con_SafePrintf ("GL_RENDERER: %s\n", gl_renderer);
	Con_SafePrintf ("GL_VERSION: %s\n", gl_version);

	GL_CheckExtensions (); // johnfitz

#ifdef __APPLE__
	// ericw -- enable multi-threaded OpenGL, gives a decent FPS boost.
	// https://developer.apple.com/library/mac/technotes/tn2085/
	if (host_parms->numcpus > 1 && kCGLNoError != CGLEnable (CGLGetCurrentContext (), kCGLCEMPEngine))
	{
		Con_Warning ("Couldn't enable multi-threaded OpenGL");
	}
#endif

	// we alloc hunk memory for temporary copies of shader combinations, so...
	int mark = Hunk_LowMark ();

	// create all of our shaders
	GLDraw_CreateShaders ();
	GLAlias_CreateShaders ();
	GLWorld_CreateShaders ();
	GLParticles_CreateShaders ();
	GLWarp_CreateShaders ();
	GLSky_CreateShaders ();
	GLSprite_CreateShaders ();
	GLMain_CreateShaders ();

	// ...and hand back the memory we used for temps
	Hunk_FreeToLowMark (mark);

	GL_ClearBufferBindings ();
}


void VID_CheckVsync (void)
{
}


/*
=================
GL_BeginRendering -- sets values of glx, gly, glwidth, glheight

returns false if the frame should be skipped for any reason
=================
*/
qboolean GL_BeginRendering (int *x, int *y, int *width, int *height)
{
#if defined(USE_SDL2)
	// valid swapinterval values are -1, 0 or 1
	int requestedvsync = 0;

	// check for vsync change - timerefresh and timedemo should never vsync
	if (scr_timerefresh)
		requestedvsync = 0;
	else if (cls.timedemo)
		requestedvsync = 0;
	else requestedvsync = (vid_vsync.value) ? 1 : 0;

	if (requestedvsync != vid_currentvsync)
	{
		// make the change
		if (SDL_GL_SetSwapInterval (requestedvsync) == -1)
			gl_swap_control = false;

		vid_currentvsync = requestedvsync;
	}
#endif

	*x = *y = 0;

	// using the correct window with and height
#if defined(USE_SDL2)
	SDL_GetWindowSize (draw_context, width, height);
#else
	*width = draw_context->w;
	*height = draw_context->h;
#endif

	// set up any state that we want to ensure is set at the start of each frame
	glEnable (GL_VERTEX_PROGRAM_ARB);
	glEnable (GL_FRAGMENT_PROGRAM_ARB);

	GL_EnableVertexAttribArrays (0);
	GL_ClearBufferBindings ();
	GL_BindPrograms (0, 0);
	GL_ClearTextureBindings ();

	// forces states to reset first time they're seen
	GL_BlendState (GL_INVALID_VALUE, GL_INVALID_VALUE, GL_INVALID_VALUE);
	GL_DepthState (GL_INVALID_VALUE, GL_INVALID_VALUE, GL_INVALID_VALUE);

	// alpha test is never used in this engine so this is the only place it's set and it's an explicit disable
	glDisable (GL_ALPHA_TEST);

	// gamma and brightness
	float contrastval = Q_fclamp (vid_contrast.value, 1.0f, 2.0f);
	float gammaval = Q_fclamp (vid_gamma.value, 0.25f, 1.0f);

	// MH - variable overbright
	// overbright clamping is now done entirely in shader code which simplifies the C-side and makes the effect totally consistent everywhere it's used
	float	overbright = (float) (1 << (int) Q_fclamp (gl_overbright.value, 0, 8));

	// and set them all
	glProgramEnvParameter4fARB (GL_FRAGMENT_PROGRAM_ARB, 10, contrastval, gammaval, overbright, 1.0f / overbright);

	// force entity alpha to set next time it's seen
	R_UpdateFragmentProgramAlpha (-1);

	return true;
}


/*
=================
GL_EndRendering
=================
*/
void GL_EndRendering (void)
{
	if (!scr_skipupdate)
	{
#if defined(USE_SDL2)
		SDL_GL_SwapWindow (draw_context);
#else
		SDL_GL_SwapBuffers ();
#endif
	}
}


void	VID_Shutdown (void)
{
	if (vid_initialized)
	{
		VID_Gamma_Shutdown (); // johnfitz

		SDL_QuitSubSystem (SDL_INIT_VIDEO);
		draw_context = NULL;
#if defined(USE_SDL2)
		gl_context = NULL;
#endif
		PL_VID_Shutdown ();
	}
}

/*
===================================================================

MAIN WINDOW

===================================================================
*/

/*
================
ClearAllStates
================
*/
static void ClearAllStates (void)
{
	Key_ClearStates ();
	IN_ClearStates ();
}


// ==========================================================================
//  COMMANDS
// ==========================================================================

/*
=================
VID_DescribeCurrentMode_f
=================
*/
static void VID_DescribeCurrentMode_f (void)
{
	if (draw_context)
		Con_Printf ("%dx%dx%d %dHz %s\n",
			VID_GetCurrentWidth (),
			VID_GetCurrentHeight (),
			VID_GetCurrentBPP (),
			VID_GetCurrentRefreshRate (),
			VID_GetFullscreen () ? "fullscreen" : "windowed");
}

/*
=================
VID_DescribeModes_f -- johnfitz -- changed formatting, and added refresh rates after each mode.
=================
*/
static void VID_DescribeModes_f (void)
{
	int	i;
	int	lastwidth, lastheight, lastbpp, count;

	lastwidth = lastheight = lastbpp = count = 0;

	for (i = 0; i < nummodes; i++)
	{
		if (lastwidth != modelist[i].width || lastheight != modelist[i].height || lastbpp != modelist[i].bpp)
		{
			if (count > 0)
				Con_SafePrintf ("\n");
			Con_SafePrintf ("   %4i x %4i x %i : %i", modelist[i].width, modelist[i].height, modelist[i].bpp, modelist[i].refreshrate);
			lastwidth = modelist[i].width;
			lastheight = modelist[i].height;
			lastbpp = modelist[i].bpp;
			count++;
		}
	}

	Con_Printf ("\n%i modes\n", count);
}

/*
===================
VID_FSAA_f -- ericw -- warn that vid_fsaa requires engine restart
===================
*/
static void VID_FSAA_f (cvar_t *var)
{
	// don't print the warning if vid_fsaa is set during startup
	if (vid_initialized)
		Con_Printf ("%s %d requires engine restart to take effect\n", var->name, (int) var->value);
}

// ==========================================================================
//  INIT
// ==========================================================================

/*
=================
VID_InitModelist
=================
*/
static void VID_InitModelist (void)
{
	const int sdlmodes = SDL_GetNumDisplayModes (0);
	int i;

	nummodes = 0;
	for (i = 0; i < sdlmodes; i++)
	{
		SDL_DisplayMode mode;

		if (nummodes >= MAX_MODE_LIST)
			break;
		if (SDL_GetDisplayMode (0, i, &mode) == 0)
		{
			modelist[nummodes].width = mode.w;
			modelist[nummodes].height = mode.h;
			modelist[nummodes].bpp = SDL_BITSPERPIXEL (mode.format);
			modelist[nummodes].refreshrate = mode.refresh_rate;
			nummodes++;
		}
	}
}


/*
===================
VID_Init
===================
*/
void	VID_Init (void)
{
	static char vid_center[] = "SDL_VIDEO_CENTERED=center";
	int		p, width, height, refreshrate, bpp;
	int		display_width, display_height, display_refreshrate, display_bpp;
	qboolean	fullscreen;
	const char *read_vars[] = { "vid_fullscreen",
		"vid_width",
		"vid_height",
		"vid_refreshrate",
		"vid_bpp",
#if !defined(USE_SDL2)
		// under SDL2 vsync doesn't need a mode change and doesn't need to be read early
		"vid_vsync",
#endif
		"vid_fsaa",
		"vid_desktopfullscreen",
		"vid_borderless" };
#define num_readvars	(sizeof (read_vars) / sizeof (read_vars[0]))

	Cvar_RegisterVariable (&vid_fullscreen); // johnfitz
	Cvar_RegisterVariable (&vid_width); // johnfitz
	Cvar_RegisterVariable (&vid_height); // johnfitz
	Cvar_RegisterVariable (&vid_refreshrate); // johnfitz
	Cvar_RegisterVariable (&vid_bpp); // johnfitz
	Cvar_RegisterVariable (&vid_vsync); // johnfitz
	Cvar_RegisterVariable (&vid_fsaa); // QuakeSpasm
	Cvar_RegisterVariable (&vid_desktopfullscreen); // QuakeSpasm
	Cvar_RegisterVariable (&vid_borderless); // QuakeSpasm
	Cvar_SetCallback (&vid_fullscreen, VID_Changed_f);
	Cvar_SetCallback (&vid_width, VID_Changed_f);
	Cvar_SetCallback (&vid_height, VID_Changed_f);
	Cvar_SetCallback (&vid_refreshrate, VID_Changed_f);
	Cvar_SetCallback (&vid_bpp, VID_Changed_f);

#if defined(USE_SDL2)
	// under SDL2 vsync doesn't need a mode change
	Cvar_SetCallback (&vid_vsync, NULL);
#else
	Cvar_SetCallback (&vid_vsync, VID_Changed_f);
#endif

	Cvar_SetCallback (&vid_fsaa, VID_FSAA_f);
	Cvar_SetCallback (&vid_desktopfullscreen, VID_Changed_f);
	Cvar_SetCallback (&vid_borderless, VID_Changed_f);

	Cmd_AddCommand ("vid_unlock", VID_Unlock); // johnfitz
	Cmd_AddCommand ("vid_restart", VID_Restart); // johnfitz
	Cmd_AddCommand ("vid_test", VID_Test); // johnfitz
	Cmd_AddCommand ("vid_describecurrentmode", VID_DescribeCurrentMode_f);
	Cmd_AddCommand ("vid_describemodes", VID_DescribeModes_f);

	putenv (vid_center);	/* SDL_putenv is problematic in versions <= 1.2.9 */

	if (SDL_InitSubSystem (SDL_INIT_VIDEO) < 0)
		Sys_Error ("Couldn't init SDL video: %s", SDL_GetError ());

#if defined(USE_SDL2)
	{
		SDL_DisplayMode mode;
		if (SDL_GetDesktopDisplayMode (0, &mode) != 0)
			Sys_Error ("Could not get desktop display mode");

		display_width = mode.w;
		display_height = mode.h;
		display_refreshrate = mode.refresh_rate;
		display_bpp = SDL_BITSPERPIXEL (mode.format);
	}
#else
	{
		const SDL_VideoInfo *info = SDL_GetVideoInfo ();
		display_width = info->current_w;
		display_height = info->current_h;
		display_refreshrate = DEFAULT_REFRESHRATE;
		display_bpp = info->vfmt->BitsPerPixel;
	}
#endif

	Cvar_SetValueQuick (&vid_bpp, (float) display_bpp);

	// play nice with other engine's configs - we load and exec config.cfg but we only save to our own .cfg file
	if (CFG_OpenConfig ("mhquakespasm.cfg") == 0)
	{
		CFG_ReadCvars (read_vars, num_readvars);
		CFG_CloseConfig ();
	}
	else if (CFG_OpenConfig ("config.cfg") == 0)
	{
		CFG_ReadCvars (read_vars, num_readvars);
		CFG_CloseConfig ();
	}

	CFG_ReadCvarOverrides (read_vars, num_readvars);

	VID_InitModelist ();

	width = (int) vid_width.value;
	height = (int) vid_height.value;
	refreshrate = (int) vid_refreshrate.value;
	bpp = (int) vid_bpp.value;
	fullscreen = (int) vid_fullscreen.value;
	fsaa = (int) vid_fsaa.value;

	if (COM_CheckParm ("-current"))
	{
		width = display_width;
		height = display_height;
		refreshrate = display_refreshrate;
		bpp = display_bpp;
		fullscreen = true;
	}
	else
	{
		p = COM_CheckParm ("-width");
		if (p && p < com_argc - 1)
		{
			width = Q_atoi (com_argv[p + 1]);

			if (!COM_CheckParm ("-height"))
				height = width * 3 / 4;
		}

		p = COM_CheckParm ("-height");
		if (p && p < com_argc - 1)
		{
			height = Q_atoi (com_argv[p + 1]);

			if (!COM_CheckParm ("-width"))
				width = height * 4 / 3;
		}

		p = COM_CheckParm ("-refreshrate");
		if (p && p < com_argc - 1)
			refreshrate = Q_atoi (com_argv[p + 1]);

		p = COM_CheckParm ("-bpp");
		if (p && p < com_argc - 1)
			bpp = Q_atoi (com_argv[p + 1]);

		if (COM_CheckParm ("-window") || COM_CheckParm ("-w"))
			fullscreen = false;
		else if (COM_CheckParm ("-fullscreen") || COM_CheckParm ("-f"))
			fullscreen = true;
	}

	p = COM_CheckParm ("-fsaa");
	if (p && p < com_argc - 1)
		fsaa = atoi (com_argv[p + 1]);

	if (!VID_ValidMode (width, height, refreshrate, bpp, fullscreen))
	{
		width = (int) vid_width.value;
		height = (int) vid_height.value;
		refreshrate = (int) vid_refreshrate.value;
		bpp = (int) vid_bpp.value;
		fullscreen = (int) vid_fullscreen.value;
	}

	if (!VID_ValidMode (width, height, refreshrate, bpp, fullscreen))
	{
		width = 640;
		height = 480;
		refreshrate = display_refreshrate;
		bpp = display_bpp;
		fullscreen = false;
	}

	vid_initialized = true;

	// set window icon
	PL_SetWindowIcon ();

	VID_SetMode (width, height, refreshrate, bpp, fullscreen);

	GL_Init ();
	GL_SetupState ();
	Cmd_AddCommand ("gl_info", GL_Info_f); // johnfitz

	// johnfitz -- removed code creating "glquake" subdirectory

	vid_menucmdfn = VID_Menu_f; // johnfitz
	vid_menudrawfn = VID_MenuDraw;
	vid_menukeyfn = VID_MenuKey;

	VID_Gamma_Init (); // johnfitz
	VID_Menu_Init (); // johnfitz

	// QuakeSpasm: current vid settings should override config file settings.
	// so we have to lock the vid mode from now until after all config files are read.
	vid_locked = true;
}

// new proc by S.A., called by alt-return key binding.
void	VID_Toggle (void)
{
	// disabling the fast path completely because SDL_SetWindowFullscreen was changing
	// the window size on SDL2/WinXP and we weren't set up to handle it. --ericw
	// TODO: Clear out the dead code, reinstate the fast path using SDL_SetWindowFullscreen
	// inside VID_SetMode, check window size to fix WinXP issue. This will
	// keep all the mode changing code in one place.
	static qboolean vid_toggle_works = false;
	qboolean toggleWorked;
#if defined(USE_SDL2)
	Uint32 flags = 0;
#endif

	S_ClearBuffer ();

	if (!vid_toggle_works)
		goto vrestart;
	else
	{
		// disabling the fast path because with SDL 1.2 it invalidates VBOs (using them
		// causes a crash, sugesting that the fullscreen toggle created a new GL context,
		// although texture objects remain valid for some reason).
		// SDL2 does promise window resizes / fullscreen changes preserve the GL context,
		// so we could use the fast path with SDL2. --ericw
		vid_toggle_works = false;
		goto vrestart;
	}

#if defined(USE_SDL2)
	if (!VID_GetFullscreen ())
	{
		flags = vid_desktopfullscreen.value ? SDL_WINDOW_FULLSCREEN_DESKTOP : SDL_WINDOW_FULLSCREEN;
	}

	toggleWorked = SDL_SetWindowFullscreen (draw_context, flags) == 0;
#else
	toggleWorked = SDL_WM_ToggleFullScreen (draw_context) == 1;
#endif

	if (toggleWorked)
	{
		modestate = VID_GetFullscreen () ? MS_FULLSCREEN : MS_WINDOWED;

		VID_SyncCvars ();

		// update mouse grab
		if (key_dest == key_console || key_dest == key_menu)
		{
			if (modestate == MS_WINDOWED)
				IN_Deactivate (true);
			else if (modestate == MS_FULLSCREEN)
				IN_Activate ();
		}
	}
	else
	{
		vid_toggle_works = false;
		Con_DPrintf ("SDL_WM_ToggleFullScreen failed, attempting VID_Restart\n");
vrestart:
		Cvar_SetQuick (&vid_fullscreen, VID_GetFullscreen () ? "0" : "1");
		Cbuf_AddText ("vid_restart\n");
	}
}

/*
================
VID_SyncCvars -- johnfitz -- set vid cvars to match current video mode
================
*/
void VID_SyncCvars (void)
{
	if (draw_context)
	{
		if (!VID_GetDesktopFullscreen ())
		{
			Cvar_SetValueQuick (&vid_width, VID_GetCurrentWidth ());
			Cvar_SetValueQuick (&vid_height, VID_GetCurrentHeight ());
		}
		Cvar_SetValueQuick (&vid_refreshrate, VID_GetCurrentRefreshRate ());
		Cvar_SetValueQuick (&vid_bpp, VID_GetCurrentBPP ());
		Cvar_SetQuick (&vid_fullscreen, VID_GetFullscreen () ? "1" : "0");
		// don't sync vid_desktopfullscreen, it's a user preference that
		// should persist even if we are in windowed mode.
		Cvar_SetQuick (&vid_vsync, VID_GetVSync () ? "1" : "0");
	}

	vid_changed = false;
}

// ==========================================================================
//  NEW VIDEO MENU -- johnfitz
// ==========================================================================

enum {
	VID_OPT_MODE,
	VID_OPT_BPP,
	VID_OPT_REFRESHRATE,
	VID_OPT_FULLSCREEN,
	VID_OPT_VSYNC,
	VID_OPT_TEST,
	VID_OPT_APPLY,
	VIDEO_OPTIONS_ITEMS
};

static int	video_options_cursor = 0;

typedef struct vid_menu_mode_s {
	int width, height;
} vid_menu_mode;

// TODO: replace these fixed-length arrays with hunk_allocated buffers
static vid_menu_mode vid_menu_modes[MAX_MODE_LIST];
static int vid_menu_nummodes = 0;

static int vid_menu_bpps[MAX_BPPS_LIST];
static int vid_menu_numbpps = 0;

static int vid_menu_rates[MAX_RATES_LIST];
static int vid_menu_numrates = 0;

/*
================
VID_Menu_Init
================
*/
static void VID_Menu_Init (void)
{
	int i, j, h, w;

	for (i = 0; i < nummodes; i++)
	{
		w = modelist[i].width;
		h = modelist[i].height;

		for (j = 0; j < vid_menu_nummodes; j++)
		{
			if (vid_menu_modes[j].width == w &&
				vid_menu_modes[j].height == h)
				break;
		}

		if (j == vid_menu_nummodes)
		{
			vid_menu_modes[j].width = w;
			vid_menu_modes[j].height = h;
			vid_menu_nummodes++;
		}
	}
}

/*
================
VID_Menu_RebuildBppList

regenerates bpp list based on current vid_width and vid_height
================
*/
static void VID_Menu_RebuildBppList (void)
{
	int i, j, b;

	vid_menu_numbpps = 0;

	for (i = 0; i < nummodes; i++)
	{
		if (vid_menu_numbpps >= MAX_BPPS_LIST)
			break;

		// bpp list is limited to bpps available with current width/height
		if (modelist[i].width != vid_width.value ||
			modelist[i].height != vid_height.value)
			continue;

		b = modelist[i].bpp;

		for (j = 0; j < vid_menu_numbpps; j++)
		{
			if (vid_menu_bpps[j] == b)
				break;
		}

		if (j == vid_menu_numbpps)
		{
			vid_menu_bpps[j] = b;
			vid_menu_numbpps++;
		}
	}

	// if there are no valid fullscreen bpps for this width/height, just pick one
	if (vid_menu_numbpps == 0)
	{
		Cvar_SetValueQuick (&vid_bpp, (float) modelist[0].bpp);
		return;
	}

	// if vid_bpp is not in the new list, change vid_bpp
	for (i = 0; i < vid_menu_numbpps; i++)
		if (vid_menu_bpps[i] == (int) (vid_bpp.value))
			break;

	if (i == vid_menu_numbpps)
		Cvar_SetValueQuick (&vid_bpp, (float) vid_menu_bpps[0]);
}

/*
================
VID_Menu_RebuildRateList

regenerates rate list based on current vid_width, vid_height and vid_bpp
================
*/
static void VID_Menu_RebuildRateList (void)
{
	int i, j, r;

	vid_menu_numrates = 0;

	for (i = 0; i < nummodes; i++)
	{
		// rate list is limited to rates available with current width/height/bpp
		if (modelist[i].width != vid_width.value ||
			modelist[i].height != vid_height.value ||
			modelist[i].bpp != vid_bpp.value)
			continue;

		r = modelist[i].refreshrate;

		for (j = 0; j < vid_menu_numrates; j++)
		{
			if (vid_menu_rates[j] == r)
				break;
		}

		if (j == vid_menu_numrates)
		{
			vid_menu_rates[j] = r;
			vid_menu_numrates++;
		}
	}

	// if there are no valid fullscreen refreshrates for this width/height, just pick one
	if (vid_menu_numrates == 0)
	{
		Cvar_SetValue ("vid_refreshrate", (float) modelist[0].refreshrate);
		return;
	}

	// if vid_refreshrate is not in the new list, change vid_refreshrate
	for (i = 0; i < vid_menu_numrates; i++)
		if (vid_menu_rates[i] == (int) (vid_refreshrate.value))
			break;

	if (i == vid_menu_numrates)
		Cvar_SetValue ("vid_refreshrate", (float) vid_menu_rates[0]);
}

/*
================
VID_Menu_ChooseNextMode

chooses next resolution in order, then updates vid_width and
vid_height cvars, then updates bpp and refreshrate lists
================
*/
static void VID_Menu_ChooseNextMode (int dir)
{
	int i;

	if (vid_menu_nummodes)
	{
		for (i = 0; i < vid_menu_nummodes; i++)
		{
			if (vid_menu_modes[i].width == vid_width.value &&
				vid_menu_modes[i].height == vid_height.value)
				break;
		}

		if (i == vid_menu_nummodes) // can't find it in list, so it must be a custom windowed res
		{
			i = 0;
		}
		else
		{
			i += dir;
			if (i >= vid_menu_nummodes)
				i = 0;
			else if (i < 0)
				i = vid_menu_nummodes - 1;
		}

		Cvar_SetValueQuick (&vid_width, (float) vid_menu_modes[i].width);
		Cvar_SetValueQuick (&vid_height, (float) vid_menu_modes[i].height);
		VID_Menu_RebuildBppList ();
		VID_Menu_RebuildRateList ();
	}
}

/*
================
VID_Menu_ChooseNextBpp

chooses next bpp in order, then updates vid_bpp cvar
================
*/
static void VID_Menu_ChooseNextBpp (int dir)
{
	int i;

	if (vid_menu_numbpps)
	{
		for (i = 0; i < vid_menu_numbpps; i++)
		{
			if (vid_menu_bpps[i] == vid_bpp.value)
				break;
		}

		if (i == vid_menu_numbpps) // can't find it in list
		{
			i = 0;
		}
		else
		{
			i += dir;
			if (i >= vid_menu_numbpps)
				i = 0;
			else if (i < 0)
				i = vid_menu_numbpps - 1;
		}

		Cvar_SetValueQuick (&vid_bpp, (float) vid_menu_bpps[i]);
	}
}

/*
================
VID_Menu_ChooseNextRate

chooses next refresh rate in order, then updates vid_refreshrate cvar
================
*/
static void VID_Menu_ChooseNextRate (int dir)
{
	int i;

	for (i = 0; i < vid_menu_numrates; i++)
	{
		if (vid_menu_rates[i] == vid_refreshrate.value)
			break;
	}

	if (i == vid_menu_numrates) // can't find it in list
	{
		i = 0;
	}
	else
	{
		i += dir;
		if (i >= vid_menu_numrates)
			i = 0;
		else if (i < 0)
			i = vid_menu_numrates - 1;
	}

	Cvar_SetValue ("vid_refreshrate", (float) vid_menu_rates[i]);
}


/*
================
VID_MenuKey
================
*/
static void VID_MenuKey (int key)
{
	switch (key)
	{
	case K_ESCAPE:
	case K_BBUTTON:
		VID_SyncCvars (); // sync cvars before leaving menu. FIXME: there are other ways to leave menu
		S_LocalSound ("misc/menu1.wav");
		M_Menu_Options_f ();
		break;

	case K_UPARROW:
		S_LocalSound ("misc/menu1.wav");
		video_options_cursor--;
		if (video_options_cursor < 0)
			video_options_cursor = VIDEO_OPTIONS_ITEMS - 1;
		break;

	case K_DOWNARROW:
		S_LocalSound ("misc/menu1.wav");
		video_options_cursor++;
		if (video_options_cursor >= VIDEO_OPTIONS_ITEMS)
			video_options_cursor = 0;
		break;

	case K_LEFTARROW:
		S_LocalSound ("misc/menu3.wav");
		switch (video_options_cursor)
		{
		case VID_OPT_MODE:
			VID_Menu_ChooseNextMode (1);
			break;
		case VID_OPT_BPP:
			VID_Menu_ChooseNextBpp (1);
			break;
		case VID_OPT_REFRESHRATE:
			VID_Menu_ChooseNextRate (1);
			break;
		case VID_OPT_FULLSCREEN:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break;
		case VID_OPT_VSYNC:
			Cbuf_AddText ("toggle vid_vsync\n"); // kristian
			break;
		default:
			break;
		}
		break;

	case K_RIGHTARROW:
		S_LocalSound ("misc/menu3.wav");
		switch (video_options_cursor)
		{
		case VID_OPT_MODE:
			VID_Menu_ChooseNextMode (-1);
			break;
		case VID_OPT_BPP:
			VID_Menu_ChooseNextBpp (-1);
			break;
		case VID_OPT_REFRESHRATE:
			VID_Menu_ChooseNextRate (-1);
			break;
		case VID_OPT_FULLSCREEN:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break;
		case VID_OPT_VSYNC:
			Cbuf_AddText ("toggle vid_vsync\n");
			break;
		default:
			break;
		}
		break;

	case K_ENTER:
	case K_KP_ENTER:
	case K_ABUTTON:
		m_entersound = true;
		switch (video_options_cursor)
		{
		case VID_OPT_MODE:
			VID_Menu_ChooseNextMode (1);
			break;
		case VID_OPT_BPP:
			VID_Menu_ChooseNextBpp (1);
			break;
		case VID_OPT_REFRESHRATE:
			VID_Menu_ChooseNextRate (1);
			break;
		case VID_OPT_FULLSCREEN:
			Cbuf_AddText ("toggle vid_fullscreen\n");
			break;
		case VID_OPT_VSYNC:
			Cbuf_AddText ("toggle vid_vsync\n");
			break;
		case VID_OPT_TEST:
			Cbuf_AddText ("vid_test\n");
			break;
		case VID_OPT_APPLY:
			Cbuf_AddText ("vid_restart\n");
			key_dest = key_game;
			m_state = m_none;
			IN_Activate ();
			break;
		default:
			break;
		}
		break;

	default:
		break;
	}
}

/*
================
VID_MenuDraw
================
*/
static void VID_MenuDraw (void)
{
	int i, y;
	qpic_t *p;
	const char *title;

	y = 4;

	// plaque
	p = Draw_CachePic ("gfx/qplaque.lmp");
	M_DrawTransPic (16, y, p);

	// p = Draw_CachePic ("gfx/vidmodes.lmp");
	p = Draw_CachePic ("gfx/p_option.lmp");
	M_DrawPic ((320 - p->width) / 2, y, p);

	y += 28;

	// title
	title = "Video Options";
	M_PrintWhite ((320 - 8 * strlen (title)) / 2, y, title);

	y += 16;

	// options
	for (i = 0; i < VIDEO_OPTIONS_ITEMS; i++)
	{
		switch (i)
		{
		case VID_OPT_MODE:
			M_Print (16, y, "        Video mode");
			M_Print (184, y, va ("%ix%i", (int) vid_width.value, (int) vid_height.value));
			break;
		case VID_OPT_BPP:
			M_Print (16, y, "       Color depth");
			M_Print (184, y, va ("%i", (int) vid_bpp.value));
			break;
		case VID_OPT_REFRESHRATE:
			M_Print (16, y, "      Refresh rate");
			M_Print (184, y, va ("%i", (int) vid_refreshrate.value));
			break;
		case VID_OPT_FULLSCREEN:
			M_Print (16, y, "        Fullscreen");
			M_DrawCheckbox (184, y, (int) vid_fullscreen.value);
			break;
		case VID_OPT_VSYNC:
			M_Print (16, y, "     Vertical sync");
			if (gl_swap_control)
				M_DrawCheckbox (184, y, (int) vid_vsync.value);
			else
				M_Print (184, y, "N/A");
			break;
		case VID_OPT_TEST:
			y += 8; // separate the test and apply items
			M_Print (16, y, "      Test changes");
			break;
		case VID_OPT_APPLY:
			M_Print (16, y, "     Apply changes");
			break;
		}

		if (video_options_cursor == i)
			M_DrawSmallCursor (168, y);

		y += 8;
	}
}

/*
================
VID_Menu_f
================
*/
static void VID_Menu_f (void)
{
	IN_Deactivate (modestate == MS_WINDOWED);
	key_dest = key_menu;
	m_state = m_video;
	m_entersound = true;

	// set all the cvars to match the current mode when entering the menu
	VID_SyncCvars ();

	// set up bpp and rate lists based on current cvars
	VID_Menu_RebuildBppList ();
	VID_Menu_RebuildRateList ();
}

