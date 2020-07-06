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

#ifndef __MODEL__
#define __MODEL__

#include "modelgen.h"
#include "spritegn.h"

/*

d*_t structures are on-disk representations
m*_t structures are in-memory

*/

// entity effects

#define	EF_BRIGHTFIELD			1
#define	EF_MUZZLEFLASH 			2
#define	EF_BRIGHTLIGHT 			4
#define	EF_DIMLIGHT 			8


/*
==============================================================================

BRUSH MODELS

==============================================================================
*/


// in memory representation
// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct mvertex_s {
	vec3_t		position;
} mvertex_t;

#define	SIDE_FRONT	0
#define	SIDE_BACK	1
#define	SIDE_ON		2


// plane_t structure
// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct mplane_s {
	vec3_t	normal;
	float	dist;
	byte	type;			// for texture axis selection and fast side tests
	byte	signbits;		// signx + signy<<1 + signz<<1
	byte	pad[2];
} mplane_t;

// ericw -- each texture has two chains, so we can clear the model chains
//          without affecting the world
typedef enum {
	chain_world = 0,
	chain_model = 1,
	chain_dlight = 2
} texchain_t;

typedef struct texture_s {
	char				name[16];
	unsigned			width, height;
	struct gltexture_s *gltexture; // johnfitz -- pointer to gltexture
	struct gltexture_s *fullbright; // johnfitz -- fullbright mask texture
	struct msurface_s *texturechains[3];	// for texture chains
	int					anim_total;				// total tenths in sequence ( 0 = no)
	int					anim_min, anim_max;		// time for this frame min <=time< max
	struct texture_s *anim_next;		// in the animation sequence
	struct texture_s *alternate_anims;	// bmodels in frmae 1 use these
	unsigned			offsets[MIPLEVELS];		// four mip maps stored
} texture_t;


#define	SURF_PLANEBACK		2
#define	SURF_DRAWSKY		4
#define SURF_DRAWSPRITE		8
#define SURF_DRAWTURB		0x10
#define SURF_DRAWTILED		0x20
#define SURF_DRAWBACKGROUND	0x40
#define SURF_UNDERWATER		0x80
#define SURF_NOTEXTURE		0x100 // johnfitz
#define SURF_DRAWFENCE		0x200
#define SURF_DRAWLAVA		0x400
#define SURF_DRAWSLIME		0x800
#define SURF_DRAWTELE		0x1000
#define SURF_DRAWWATER		0x2000

// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct medge_s {
	unsigned int	v[2];
	unsigned int	cachededgeoffset;
} medge_t;

typedef struct mtexinfo_s {
	float		vecs[2][4];
	float		mipadjust;
	texture_t *texture;
	int			flags;
} mtexinfo_t;


typedef struct gl_rect_s {
	// use a proper rect
	int left, top, right, bottom;
} gl_rect_t;


typedef struct brushpolyvert_s {
	float xyz[3];
	float st[2];
	unsigned short lm[2];
	signed char norm[4];
} brushpolyvert_t;


typedef struct msurface_s {
	int			visframe;		// should be drawn when node is crossed
	float		mins[3];		// johnfitz -- for frustum culling
	float		maxs[3];		// johnfitz -- for frustum culling

	mplane_t *plane;
	int			flags;

	int			firstedge;	// look up in model->surfedges[], negative numbers
	int			numedges;	// are backwards edges

	short		texturemins[2];
	short		extents[2];

	int			light_s, light_t;	// gl lightmap coordinates

	struct	msurface_s *texturechain;

	mtexinfo_t *texinfo;

	int		firstvertex;		// index of this surface's first vert in the VBO
	int		numindexes;			// number of indexes used in the index buffer

	// lighting info
	int			dlightframe;

	int			lightmaptexturenum;

	union {
		byte		styles[MAXLIGHTMAPS];
		unsigned	fullstyle;
	};

	byte *samples;		// [numstyles*surfsize]
} msurface_t;


typedef struct mnode_s {
	// common with leaf
	int			contents;		// 0, to differentiate from leafs
	int			visframe;		// node needs to be traversed if current

	float		minmaxs[6];		// for bounding box culling

	struct mnode_s *parent;

	// node specific
	mplane_t *plane;
	struct mnode_s *children[2];

	struct msurface_s *surfaces;
	unsigned int		numsurfaces;
} mnode_t;



typedef struct mleaf_s {
	// common with node
	int			contents;		// wil be a negative contents number
	int			visframe;		// node needs to be traversed if current

	float		minmaxs[6];		// for bounding box culling

	struct mnode_s *parent;

	// leaf specific
	byte *compressed_vis;
	efrag_t *efrags;

	msurface_t **firstmarksurface;
	int			nummarksurfaces;
	int			key;			// BSP sequence number for leaf's contents
	byte		ambient_sound_level[NUM_AMBIENTS];
} mleaf_t;

// johnfitz -- for clipnodes>32k
typedef struct mclipnode_s {
	int			planenum;
	int			children[2]; // negative numbers are contents
} mclipnode_t;
// johnfitz

// !!! if this is changed, it must be changed in asm_i386.h too !!!
typedef struct hull_s {
	mclipnode_t *clipnodes; // johnfitz -- was dclipnode_t
	mplane_t *planes;
	int			firstclipnode;
	int			lastclipnode;
	vec3_t		clip_mins;
	vec3_t		clip_maxs;
} hull_t;

/*
==============================================================================

SPRITE MODELS

==============================================================================
*/

// FIXME: shorten these?
typedef struct mspriteframe_s {
	int					width, height;
	float				up, down, left, right;
	float				smax, tmax; // johnfitz -- image might be padded
	int					firstvertex;
	struct gltexture_s *gltexture;
} mspriteframe_t;

typedef struct mspritegroup_s {
	int				numframes;
	float *intervals;
	mspriteframe_t *frames[1];
} mspritegroup_t;

typedef struct mspriteframedesc_s {
	spriteframetype_t	type;
	mspriteframe_t *frameptr;
} mspriteframedesc_t;

typedef struct spritepolyvert_s {
	float framevec[2];
	float texcoord[2];
} spritepolyvert_t;

typedef struct msprite_s {
	int					type;
	int					maxwidth;
	int					maxheight;
	int					numframes;
	spritepolyvert_t *frameverts;
	int				numframeverts;
	float				beamlength;		// remove?
	void *cachespot;		// remove?
	mspriteframedesc_t	frames[1];
} msprite_t;

// creates frames for rendering
void R_CreateSpriteFrames (msprite_t *psprite);


/*
==============================================================================

ALIAS MODELS

Alias models are position independent, so the cache manager can move them.
==============================================================================
*/

// -- from RMQEngine
// split out to keep vertex sizes down
typedef struct aliasmesh_s {
	int st[2];
	unsigned short vertindex;
} aliasmesh_t;

typedef struct meshxyz_s {
	byte position[4];
	signed char normal[4];
} meshxyz_t;

typedef struct meshst_s {
	float st[2];
} meshst_t;
// --

typedef struct maliasframedesc_s {
	int					firstpose;
	int					numposes;
	float				*pintervals;
	trivertx_t			bboxmin;
	trivertx_t			bboxmax;
	int					frame;
	char				name[16];
} maliasframedesc_t;

typedef struct maliasgroupframedesc_s {
	trivertx_t			bboxmin;
	trivertx_t			bboxmax;
	int					frame;
} maliasgroupframedesc_t;

typedef struct maliasgroup_s {
	int						numframes;
	intptr_t				intervals;
	maliasgroupframedesc_t	frames[1];
} maliasgroup_t;


#define	MAX_SKINS	32

typedef struct aliasskin_s {
	struct gltexture_s *gltexture; // johnfitz
	struct gltexture_s *fbtexture; // johnfitz

	// needed for reloading colormapped skins
	byte *ptexels;
} aliasskin_t;

typedef struct aliasskingroup_s {
	aliasskin_t *pskins;
	float *pintervals;
	int numskins;
} aliasskingroup_t;

typedef struct aliashdr_s {
	int			ident;
	int			version;

	float		scale[4];			// padded for shader params
	float		scale_origin[4];	// padded for shader params
	float		boundingradius;

	vec3_t		eyeposition;
	synctype_t	synctype;

	int			flags;
	float		size;

	int			numtris;
	int			numverts;
	int			numposes;

	// mh - fix skin auto-animation
	aliasskingroup_t	*pskingroups;
	int			numskingroups; // same as below?????
	int			numskins; // same as above?????
	int			skinwidth;
	int			skinheight;

	// offsets into the on-disk struct for reloading base geometry data on a vid_restart
	intptr_t	ofs_stverts;
	intptr_t	ofs_triangles;
	intptr_t	ofs_frames;

	// frames follow
	maliasframedesc_t	*pframes;
	int			numframes;
} aliashdr_t;


// ===================================================================

// Whole model

typedef enum { mod_brush, mod_sprite, mod_alias } modtype_t;

#define	EF_ROCKET	1			// leave a trail
#define	EF_GRENADE	2			// leave a trail
#define	EF_GIB		4			// leave a trail
#define	EF_ROTATE	8			// rotate (bonus items)
#define	EF_TRACER	16			// green split trail
#define	EF_ZOMGIB	32			// small blood trail
#define	EF_TRACER2	64			// orange split trail + rotate
#define	EF_TRACER3	128			// purple trail
#define	MF_HOLEY	(1 << 14)		// MarkV/QSS -- make index 255 transparent on mdl's

// bad guy muzzle flashes
#define EF_WIZARDFLASH		(1 << 22)
#define EF_SHALRATHFLASH	(1 << 23)
#define EF_SHAMBLERFLASH	(1 << 24)

// generic flashes (the first 3 of these line up with the id bad guys)
#define EF_GREENFLASH		(1 << 22)
#define EF_PURPLEFLASH		(1 << 23)
#define EF_BLUEFLASH		(1 << 24)
#define EF_ORANGEFLASH		(1 << 25)
#define EF_REDFLASH			(1 << 26)
#define EF_YELLOWFLASH		(1 << 27)


// johnfitz -- extra flags for rendering
#define	MOD_NOLERP		256		// don't lerp when animating
#define	MOD_NOSHADOW	512		//don't cast a shadow
#define	MOD_FBRIGHTHACK	1024	// when fullbrights are disabled, use a hack to render this model brighter
// johnfitz

typedef struct qmodel_s {
	char		name[MAX_QPATH];
	unsigned int	path_id;		// path id of the game directory
							// that this model came from
	qboolean	needload;		// bmodels and sprites don't cache normally

	modtype_t	type;
	int			numframes;
	synctype_t	synctype;

	int			flags;

	// volume occupied by the model graphics
	vec3_t		mins, maxs;

	// solid volume for clipping
	qboolean	clipbox;
	vec3_t		clipmins, clipmaxs;

	// brush model
	int			firstmodelsurface, nummodelsurfaces;

	int			numsubmodels;
	dmodel_t *submodels;

	int			numplanes;
	mplane_t *planes;

	int			numleafs;		// number of visible leafs, not counting 0
	mleaf_t *leafs;

	int			numvertexes;
	mvertex_t *vertexes;

	int			numedges;
	medge_t *edges;

	int			numnodes;
	mnode_t *nodes;

	int			numtexinfo;
	mtexinfo_t *texinfo;

	int			numsurfaces;
	msurface_t *surfaces;

	int			numsurfedges;
	int *surfedges;

	int			numclipnodes;
	mclipnode_t *clipnodes; // johnfitz -- was dclipnode_t

	int			nummarksurfaces;
	msurface_t **marksurfaces;

	hull_t		hulls[MAX_MAP_HULLS];

	int			numtextures;
	texture_t **textures;

	byte *visdata;
	byte *lightdata;
	char *entities;

	// true if the model has coloured light loaded from a .LIT file, and switch on coloured dynamics if so
	qboolean	colouredlight;

	qboolean	viswarn; // for Mod_DecompressVis()

	int			bspversion;

	// vertex buffers
	int			buffsetset;

	// additional model data
	cache_user_t	cache;		// only access through Mod_Extradata

} qmodel_t;

// ============================================================================

void	Mod_Init (void);
void	Mod_ClearAll (void);
void	Mod_ResetAll (void); // for gamedir changes (Host_Game_f)
qmodel_t *Mod_ForName (const char *name, qboolean crash);
void *Mod_Extradata (qmodel_t *mod);	// handles caching
void	Mod_TouchModel (const char *name);

mleaf_t *Mod_PointInLeaf (float *p, qmodel_t *model);
byte *Mod_LeafPVS (mleaf_t *leaf, qmodel_t *model);
byte *Mod_NoVisPVS (qmodel_t *model);

void Mod_SetExtraFlags (qmodel_t *mod);

int Mod_GetAutoAnimation (float *intervals, int numframes, float syncbase);
char *Mod_ValueForKeyFromWorldspawn (char *entities, char *findkey);
qboolean Mod_IsUnderwaterLeaf (mleaf_t *leaf);
float Mod_PlaneDist (mplane_t *plane, float *org);


#endif	// __MODEL__


