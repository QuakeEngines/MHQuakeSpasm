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

#ifndef _QUAKE_RENDER_H
#define _QUAKE_RENDER_H

// refresh.h -- public interface to refresh functions

#define	MAXCLIPPLANES	11

#define	TOP_RANGE		16			// soldier uniform colors
#define	BOTTOM_RANGE	96

// =============================================================================

typedef struct efrag_s {
	struct efrag_s *leafnext;
	struct entity_s *entity;
} efrag_t;


// johnfitz -- for lerping
#define LERP_MOVESTEP	(1 << 0) // this is a MOVETYPE_STEP entity, enable movement lerp
#define LERP_RESETANIM	(1 << 1) // disable anim lerping until next anim frame
#define LERP_RESETANIM2	(1 << 2) // set this and previous flag to disable anim lerping for two anim frames
#define LERP_RESETMOVE	(1 << 3) // disable movement lerping until next origin/angles change
#define LERP_FINISH		(1 << 4) // use lerpfinish time from server update instead of assuming interval of 0.1
// johnfitz
#define LERP_STATICENT	(1 << 5) // static entity doesn't lerp and may do other stuff
#define LERP_NOMOVE		(1 << 6) // this bit is set if the ent hasn't moved from it's baseline state (we could just check origin i suppose....)

typedef struct entity_s {
	qboolean				forcelink;		// model changed

	int						update_type;
	int						entitynum;		// order allocated

	entity_state_t			baseline;		// to fill in defaults in updates

	// translated "player" texture
	struct {
		struct gltexture_s	*translated;
		int					shirt;
		int					pants;
		struct aliasskin_s	*baseskin;
	} translation;

	qboolean				colormapped;	// if true uses a translated texture
	int						playernum;		// player num to take the shirt and pants colour from

	double					msgtime;		// time of last update
	vec3_t					msg_origins[2];	// last two updates (0 is newest)
	vec3_t					origin;
	vec3_t					msg_angles[2];	// last two updates (0 is newest)
	vec3_t					angles;

	struct qmodel_s *model;			// NULL = no model
	struct efrag_s *efrag;			// linked list of efrags

	int						frame;
	float					syncbase;		// for client-side animations
	int						effects;		// light, particles, etc
	int						skinnum;		// for Alias models
	int						visframe;		// last frame this entity was found in an active leaf
	// rockettrails
	struct {
		vec3_t				oldorigin;
		double				nexttime;
	} trail;

	int						dlightframe;	// dynamic lighting
	int						dlightbits;

	// FIXME: could turn these into a union
	int						trivial_accept;
	struct mnode_s			*topnode;		// for bmodels, first world node that splits bmodel, or NULL if not split

	byte					alpha;			// johnfitz -- alpha
	byte					lerpflags;		// johnfitz -- lerping
	float					lerpfinishtime;		// johnfitz -- lerping -- server sent us a more accurate interval, use it instead of 0.1

	struct {
		float				starttime;		// johnfitz -- animation lerping
		float				interval;		// johnfitz -- animation lerping
		short				previouspose;	// johnfitz -- animation lerping
		short				currentpose;	// johnfitz -- animation lerping
	} animlerp;

	struct {
		float				starttime;	// johnfitz -- transform lerping
		vec3_t				previousorigin;	// johnfitz -- transform lerping
		vec3_t				currentorigin;	// johnfitz -- transform lerping
		vec3_t				previousangles;	// johnfitz -- transform lerping
		vec3_t				currentangles;	// johnfitz -- transform lerping
	} movelerp;
} entity_t;


// !!! if this is changed, it must be changed in asm_draw.h too !!!
typedef struct refdef_s {
	vrect_t		vrect;				// subwindow in video for refresh

	vec3_t		vieworg;
	vec3_t		viewangles;

	float		fov_x, fov_y;
} refdef_t;



extern	refdef_t	r_refdef;
extern float	r_origin[4], vpn[4], vright[4], vup[4];


void R_Init (void);
void R_RenderView (void);		// must set r_refdef first

void R_CheckEfrags (void); // johnfitz
void R_AddEfrags (entity_t *ent);

void R_NewMap (void);


void R_ParseParticleEffect (void);
void R_RunParticleEffect (vec3_t org, vec3_t dir, int color, int count);
void R_RocketTrail (vec3_t start, vec3_t end, int type);
void R_EntityParticles (entity_t *ent, float dist, float beamlength);
void R_BlobExplosion (vec3_t org);
void R_ParticleExplosion (vec3_t org);
void R_ParticleExplosion2 (vec3_t org, int colorStart, int colorLength);
void R_LavaSplash (vec3_t org);
void R_TeleportSplash (vec3_t org);
void R_SingleParticle (vec3_t org, int color, float size);


#endif	/* _QUAKE_RENDER_H */

