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

#ifndef __GLQUAKE_H
#define __GLQUAKE_H

qboolean GL_BeginRendering (int *x, int *y, int *width, int *height);
void GL_EndRendering (void);
void GL_Set2D (void);
void GL_End2D (void);

extern	int glx, gly, glwidth, glheight;

// r_local.h -- private refresh defs

#define ALIAS_BASE_SIZE_RATIO		(1.0 / 11.0)
					// normalizing factor so player model works out to about
					//  1 pixel per triangle

#define BACKFACE_EPSILON	0.01


void R_TimeRefresh_f (void);
void R_ReadPointFile_f (void);
texture_t *R_TextureAnimation (texture_t *base, int frame);


// ====================================================

extern	vec3_t		modelorg, r_entorigin;
extern	int		r_visframecount;	// ??? what difs?
extern	int		r_framecount;
extern	mplane_t	frustum[4];

// view origin
extern	float	vup[4];
extern	float	vpn[4];
extern	float	vright[4];
extern	float	r_origin[4];

// screen size info
extern	refdef_t	r_refdef;
extern	mleaf_t *r_viewleaf, *r_oldviewleaf;

extern	cvar_t	r_norefresh;
extern	cvar_t	r_drawentities;
extern	cvar_t	r_drawviewmodel;
extern	cvar_t	r_speeds;
extern	cvar_t	r_pos;
extern	cvar_t	r_waterwarp;
extern	cvar_t	r_wateralpha;
extern	cvar_t	r_lavaalpha;
extern	cvar_t	r_telealpha;
extern	cvar_t	r_slimealpha;
extern	cvar_t	r_dynamic;
extern	cvar_t	r_novis;
extern	cvar_t	r_lockpvs;
extern	cvar_t	r_scale;

extern	cvar_t	gl_clear;
extern	cvar_t	gl_cull;
extern	cvar_t	gl_polyblend;
extern	cvar_t	gl_nocolors;

extern	cvar_t	r_shadows;

// mapper crap
extern	cvar_t	r_lightmap;
extern	cvar_t	r_fullbright;
extern cvar_t r_drawflat;
extern cvar_t r_showtris;
extern cvar_t r_showbboxes;
extern qboolean r_drawflat_cheatsafe, r_fullbright_cheatsafe, r_lightmap_cheatsafe; //johnfitz

extern int		gl_stencilbits;

extern cvar_t gl_overbright;

// Multitexture
extern GLint		gl_max_texture_units; // ericw

// johnfitz -- anisotropic filtering
extern	float		gl_max_anisotropy;
extern	qboolean	gl_anisotropy_able;


// johnfitz -- polygon offset
#define OFFSET_BMODEL 1
#define OFFSET_NONE 0
#define OFFSET_DECAL -1
#define OFFSET_FOG -2
#define OFFSET_SHOWTRIS -3

void GL_PolygonOffset (int);


// johnfitz -- rendering statistics
extern int rs_brushpolys, rs_aliaspolys, rs_skypolys, rs_particles, rs_fogpolys;
extern int rs_dynamiclightmaps, rs_brushpasses, rs_aliaspasses, rs_skypasses;
extern float rs_megatexels;

// johnfitz -- track developer statistics that vary every frame
extern cvar_t devstats;

typedef struct devstats_s {
	int		packetsize;
	int		edicts;
	int		visedicts;
	int		efrags;
	int		tempents;
	int		beams;
	int		dlights;
} devstats_t;

extern devstats_t dev_stats, dev_peakstats;

// ohnfitz -- reduce overflow warning spam
typedef struct overflowtimes_s {
	double	packetsize;
	double	efrags;
	double	beams;
	double	varstring;
} overflowtimes_t;

extern overflowtimes_t dev_overflows; // this stores the last time overflow messages were displayed, not the last time overflows occured

#define CONSOLE_RESPAM_TIME 3 // seconds between repeated warning messages


// lightmap stuff
void GL_BindLightmaps (int lightmaptexturenum);
float GL_SetLightmapTexCoord (float base);
void GL_SetSurfaceStyles (msurface_t *surf);

extern float	map_wateralpha, map_lavaalpha, map_telealpha, map_slimealpha; // ericw


// johnfitz -- fog functions called from outside gl_fog.c
void Fog_ParseServerMessage (void);
float *Fog_GetColor (void);
float Fog_GetDensity (void);
void Fog_SetupFrame (void);
void Fog_NewMap (void);
void Fog_Init (void);
void Fog_SetupState (void);

void R_NewGame (void);

void R_AnimateLight (double time);

void R_MarkLeaves (void);
void R_DrawWorld_Old (void);

// --------------------------------------------------------------------------------------------------------------------------------------------
// culling and bboxes
qboolean R_CullBox (vec3_t emins, vec3_t emaxs);
void R_StoreEfrags (efrag_t **ppefrag);
qboolean R_CullModelForEntity (entity_t *e, QMATRIX *localMatrix, qboolean rotated);
void R_RotateBBox (QMATRIX *matrix, const float *inmins, const float *inmaxs, float *outmins, float *outmaxs);
void R_AliasModelBBox (entity_t *e, QMATRIX *localMatrix, qboolean rotated, float *bbmins, float *bbmaxs);
void R_BrushModelBBox (entity_t *e, QMATRIX *localMatrix, qboolean rotated, float *bbmins, float *bbmaxs);
void R_TransformEntityToLocalMatrix (QMATRIX *localMatrix, float *origin, float *angles, modtype_t type);

void R_InitParticles (void);
void R_DrawParticlesARB (void);
void R_ClearParticles (void);

void R_DrawAliasModel (entity_t *e);
void R_DrawBrushModel (entity_t *e);
void R_DrawSpriteModel (entity_t *e);

void R_DrawTextureChains_Water (qmodel_t *model, entity_t *ent, texchain_t chain);

void GL_BuildLightmaps (void);
void GL_DeleteBModelVertexBuffer (void);
void GL_BuildBModelVertexBuffer (void);
void R_ReloadBufferSets (void);


// MH - ARB programs
// common flags
#define SHADERFLAG_NONE		0

// vertex program flags
#define SHADERFLAG_DYNAMIC	(1 << 0)
#define SHADERFLAG_DRAWFLAT	(1 << 1)
#define SHADERFLAG_NOTEX	(1 << 2)

// fragment program flags
#define SHADERFLAG_FENCE	(1 << 0)
#define SHADERFLAG_LUMA		(1 << 1)
#define SHADERFLAG_FOG		(1 << 2)
#define SHADERFLAG_4STYLE	(1 << 3)

// common flag setup for alias and brush
int R_SelectTexturesAndShaders (gltexture_t *tx, gltexture_t *fb, int alphaflag);

GLuint GL_CreateARBProgram (GLenum mode, const GLchar *progstr);
const GLchar *GL_GetVertexProgram (const GLchar *base, int shaderflag);
const GLchar *GL_GetFragmentProgram (const GLchar *base, int shaderflag);
void R_DeleteShaders (void);


void GLDraw_CreateShaders (void);
void GLWorld_CreateShaders (void);
void GLAlias_CreateShaders (void);
void GLParticles_CreateShaders (void);
void GLWarp_CreateShaders (void);
void GLSky_CreateShaders (void);
void GLSprite_CreateShaders (void);
void GLMain_CreateShaders (void);


void Sky_Init (void);
void Sky_NewMap (void);
void Sky_LoadTexture (texture_t *mt, byte *src);
void Sky_LoadSkyBox (const char *name);

void R_ClearTextureChains (qmodel_t *mod, texchain_t chain);
void R_ChainSurface (msurface_t *surf, texchain_t chain);
void R_DrawTextureChains (qmodel_t *model, entity_t *ent, QMATRIX *localMatrix, texchain_t chain);
void R_DrawWorld_Water (void);

void GL_BindBuffer (GLenum target, GLuint buffer);
void GL_ClearBufferBindings ();

float GL_WaterAlphaForSurface (msurface_t *surf);

// array is enabled
#define VAA0 (1 << 0)
#define VAA1 (1 << 1)
#define VAA2 (1 << 2)
#define VAA3 (1 << 3)
#define VAA4 (1 << 4)
#define VAA5 (1 << 5)
#define VAA6 (1 << 6)
#define VAA7 (1 << 7)
#define VAA8 (1 << 8)
#define VAA9 (1 << 9)
#define VAA10 (1 << 10)
#define VAA11 (1 << 11)
#define VAA12 (1 << 12)
#define VAA13 (1 << 13)
#define VAA14 (1 << 14)
#define VAA15 (1 << 15)


void GL_EnableVertexAttribArrays (int arrays);
void GL_BindPrograms (GLuint vp, GLuint fp);

void R_ClearBatch ();
void R_BatchSurface (msurface_t *s);
void R_FlushBatch ();

void R_SetupWorldVBOState (void);

void R_DrawSkychain_ARB (msurface_t *s);

void Sky_ReloadSkyBox (void);
GLuint TexMgr_LoadCubemap (byte *data[6], int width[6], int height[6]);
void TexMgr_SetCubemapFilterModes (void);
void Sky_FreeSkybox (void);
void Sky_SetShaderConstants (void);

void Warp_SetShaderConstants (void);

void GL_BlendState (GLenum enable, GLenum sfactor, GLenum dfactor);
void GL_DepthState (GLenum enable, GLenum testmode, GLenum writemode);

void R_UpdateFragmentProgramAlpha (float alpha);
void R_BeginTransparentDrawing (float alpha);


// new dynamic lights
void R_PushDlights_New (entity_t *e, QMATRIX *localMatrix, qmodel_t *mod, mnode_t *headnode);
const GLchar *GL_GetDynamicLightFragmentProgramSource (void);
const GLchar *GL_GetFullbrightFragmentProgramSource (void);
const GLchar *GL_GetDrawflatFragmentProgramSource (void);
void R_DrawDlightChains (qmodel_t *model, entity_t *ent, dlight_t *dl);
void GL_SetupDynamicLight (dlight_t *dl);

void R_DrawSimpleTexturechain (msurface_t *s);

void GLWarp_CreateTextures (void);
void R_UnderwaterWarp (void);


// registration
// MH - Quake loads models based on name, so name is all we need to match on, so long as we free them all properly between game changes
typedef struct bufferset_s {
	GLuint vertexbuffer;
	GLuint indexbuffer;

	int			numverts;
	int			numindexes;

	int		vboxyzofs;      // offset in vbo of hdr->numposes*hdr->numverts_vbo meshxyz_t
	int		vbostofs;       // offset in vbo of hdr->numverts_vbo meshst_t

	char name[MAX_OSPATH];
	int registration_sequence;
} bufferset_t;


// generic buffersets that may be used for any model type
int R_GetBufferSetForName (char *name);
int R_NewBufferSetForName (char *name);
void R_FreeUnusedBufferSets (void);
void R_FreeAllBufferSets (void);
bufferset_t *R_GetBufferSetForModel (qmodel_t *m);


#endif	/* __GLQUAKE_H */

