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

#ifndef __VID_DEFS_H
#define __VID_DEFS_H

// vid.h -- video driver defs

// moved here for global use -- kristian
typedef enum { MS_UNINIT, MS_WINDOWED, MS_FULLSCREEN } modestate_t;

extern modestate_t	modestate;

typedef struct vrect_s {
	int	x, y, width, height;
	struct vrect_s *pnext;
} vrect_t;

typedef struct viddef_s {
	int		width;
	int		height;
	int		conwidth;
	int		conheight;
} viddef_t;

extern	viddef_t	vid;				// global video state

extern void (*vid_menudrawfn)(void);
extern void (*vid_menukeyfn)(int key);
extern void (*vid_menucmdfn)(void); // johnfitz

void	VID_Init (void); // johnfitz -- removed palette from argument list

void	VID_Shutdown (void);
// Called at shutdown


void VID_SyncCvars (void);

void VID_Toggle (void);

void *VID_GetWindow (void);
qboolean VID_HasMouseOrInputFocus (void);
qboolean VID_IsMinimized (void);
void	VID_Lock (void);

#endif	/* __VID_DEFS_H */

