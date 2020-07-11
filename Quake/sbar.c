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
// sbar.c -- status bar code

#include "quakedef.h"

#define STAT_MINUS		10	// num frame for '-' stats digit

qpic_t *sb_nums[2][11];
qpic_t *sb_colon, *sb_slash;
qpic_t *sb_ibar;
qpic_t *sb_sbar;
qpic_t *sb_scorebar;

qpic_t *sb_weapons[7][8];   // 0 is active, 1 is owned, 2-5 are flashes
qpic_t *sb_ammo[4];
qpic_t *sb_sigil[4];
qpic_t *sb_armor[3];
qpic_t *sb_items[32];

qpic_t *sb_faces[7][2];		// 0 is gibbed, 1 is dead, 2-6 are alive
							// 0 is static, 1 is temporary animation
qpic_t *sb_face_invis;
qpic_t *sb_face_quad;
qpic_t *sb_face_invuln;
qpic_t *sb_face_invis_invuln;

qboolean	sb_showscores;

int		sb_lines;			// scan lines to draw

qpic_t *rsb_invbar[2];
qpic_t *rsb_weapons[5];
qpic_t *rsb_items[2];
qpic_t *rsb_ammo[3];
qpic_t *rsb_teambord;		// PGM 01/19/97 - team color border

// MED 01/04/97 added two more weapons + 3 alternates for grenade launcher
qpic_t *hsb_weapons[7][5];   // 0 is active, 1 is owned, 2-5 are flashes
// MED 01/04/97 added array to simplify weapon parsing
int		hipweapons[4] = { HIT_LASER_CANNON_BIT, HIT_MJOLNIR_BIT, 4, HIT_PROXIMITY_GUN_BIT };
// MED 01/04/97 added hipnotic items array
qpic_t *hsb_items[2];

void Sbar_MiniDeathmatchOverlay (void);
void Sbar_DeathmatchOverlay (void);
void M_DrawPic (int x, int y, qpic_t *pic);


char *Sbar_TimeToString (int timeseconds)
{
	int dig = timeseconds / 60;
	int num = timeseconds - dig * 60;

	return va ("%3i:%02i", dig, num);
}


char *Sbar_TimeToStringUnpadded (int timeseconds)
{
	int dig = timeseconds / 60;
	int num = timeseconds - dig * 60;

	return va ("%i:%02i", dig, num);
}


/*
===============
Sbar_ShowScores

Tab key down
===============
*/
void Sbar_ShowScores (void)
{
	// MH - changed behaviour to a toggle
	sb_showscores = !sb_showscores;
}

/*
===============
Sbar_DontShowScores

Tab key up
===============
*/
void Sbar_DontShowScores (void)
{
}

/*
===============
Sbar_Changed
===============
*/
void Sbar_Changed (void)
{
}

/*
===============
Sbar_LoadPics -- johnfitz -- load all the sbar pics
===============
*/
void Sbar_LoadPics (void)
{
	int		i;

	for (i = 0; i < 10; i++)
	{
		sb_nums[0][i] = Draw_PicFromWad (va ("num_%i", i));
		sb_nums[1][i] = Draw_PicFromWad (va ("anum_%i", i));
	}

	sb_nums[0][10] = Draw_PicFromWad ("num_minus");
	sb_nums[1][10] = Draw_PicFromWad ("anum_minus");

	sb_colon = Draw_PicFromWad ("num_colon");
	sb_slash = Draw_PicFromWad ("num_slash");

	sb_weapons[0][0] = Draw_PicFromWad ("inv_shotgun");
	sb_weapons[0][1] = Draw_PicFromWad ("inv_sshotgun");
	sb_weapons[0][2] = Draw_PicFromWad ("inv_nailgun");
	sb_weapons[0][3] = Draw_PicFromWad ("inv_snailgun");
	sb_weapons[0][4] = Draw_PicFromWad ("inv_rlaunch");
	sb_weapons[0][5] = Draw_PicFromWad ("inv_srlaunch");
	sb_weapons[0][6] = Draw_PicFromWad ("inv_lightng");

	sb_weapons[1][0] = Draw_PicFromWad ("inv2_shotgun");
	sb_weapons[1][1] = Draw_PicFromWad ("inv2_sshotgun");
	sb_weapons[1][2] = Draw_PicFromWad ("inv2_nailgun");
	sb_weapons[1][3] = Draw_PicFromWad ("inv2_snailgun");
	sb_weapons[1][4] = Draw_PicFromWad ("inv2_rlaunch");
	sb_weapons[1][5] = Draw_PicFromWad ("inv2_srlaunch");
	sb_weapons[1][6] = Draw_PicFromWad ("inv2_lightng");

	for (i = 0; i < 5; i++)
	{
		sb_weapons[2 + i][0] = Draw_PicFromWad (va ("inva%i_shotgun", i + 1));
		sb_weapons[2 + i][1] = Draw_PicFromWad (va ("inva%i_sshotgun", i + 1));
		sb_weapons[2 + i][2] = Draw_PicFromWad (va ("inva%i_nailgun", i + 1));
		sb_weapons[2 + i][3] = Draw_PicFromWad (va ("inva%i_snailgun", i + 1));
		sb_weapons[2 + i][4] = Draw_PicFromWad (va ("inva%i_rlaunch", i + 1));
		sb_weapons[2 + i][5] = Draw_PicFromWad (va ("inva%i_srlaunch", i + 1));
		sb_weapons[2 + i][6] = Draw_PicFromWad (va ("inva%i_lightng", i + 1));
	}

	sb_ammo[0] = Draw_PicFromWad ("sb_shells");
	sb_ammo[1] = Draw_PicFromWad ("sb_nails");
	sb_ammo[2] = Draw_PicFromWad ("sb_rocket");
	sb_ammo[3] = Draw_PicFromWad ("sb_cells");

	sb_armor[0] = Draw_PicFromWad ("sb_armor1");
	sb_armor[1] = Draw_PicFromWad ("sb_armor2");
	sb_armor[2] = Draw_PicFromWad ("sb_armor3");

	sb_items[0] = Draw_PicFromWad ("sb_key1");
	sb_items[1] = Draw_PicFromWad ("sb_key2");
	sb_items[2] = Draw_PicFromWad ("sb_invis");
	sb_items[3] = Draw_PicFromWad ("sb_invuln");
	sb_items[4] = Draw_PicFromWad ("sb_suit");
	sb_items[5] = Draw_PicFromWad ("sb_quad");

	sb_sigil[0] = Draw_PicFromWad ("sb_sigil1");
	sb_sigil[1] = Draw_PicFromWad ("sb_sigil2");
	sb_sigil[2] = Draw_PicFromWad ("sb_sigil3");
	sb_sigil[3] = Draw_PicFromWad ("sb_sigil4");

	sb_faces[4][0] = Draw_PicFromWad ("face1");
	sb_faces[4][1] = Draw_PicFromWad ("face_p1");
	sb_faces[3][0] = Draw_PicFromWad ("face2");
	sb_faces[3][1] = Draw_PicFromWad ("face_p2");
	sb_faces[2][0] = Draw_PicFromWad ("face3");
	sb_faces[2][1] = Draw_PicFromWad ("face_p3");
	sb_faces[1][0] = Draw_PicFromWad ("face4");
	sb_faces[1][1] = Draw_PicFromWad ("face_p4");
	sb_faces[0][0] = Draw_PicFromWad ("face5");
	sb_faces[0][1] = Draw_PicFromWad ("face_p5");

	sb_face_invis = Draw_PicFromWad ("face_invis");
	sb_face_invuln = Draw_PicFromWad ("face_invul2");
	sb_face_invis_invuln = Draw_PicFromWad ("face_inv2");
	sb_face_quad = Draw_PicFromWad ("face_quad");

	sb_sbar = Draw_PicFromWad ("sbar");
	sb_ibar = Draw_PicFromWad ("ibar");
	sb_scorebar = Draw_PicFromWad ("scorebar");

	// MED 01/04/97 added new hipnotic weapons
	if (hipnotic)
	{
		hsb_weapons[0][0] = Draw_PicFromWad ("inv_laser");
		hsb_weapons[0][1] = Draw_PicFromWad ("inv_mjolnir");
		hsb_weapons[0][2] = Draw_PicFromWad ("inv_gren_prox");
		hsb_weapons[0][3] = Draw_PicFromWad ("inv_prox_gren");
		hsb_weapons[0][4] = Draw_PicFromWad ("inv_prox");

		hsb_weapons[1][0] = Draw_PicFromWad ("inv2_laser");
		hsb_weapons[1][1] = Draw_PicFromWad ("inv2_mjolnir");
		hsb_weapons[1][2] = Draw_PicFromWad ("inv2_gren_prox");
		hsb_weapons[1][3] = Draw_PicFromWad ("inv2_prox_gren");
		hsb_weapons[1][4] = Draw_PicFromWad ("inv2_prox");

		for (i = 0; i < 5; i++)
		{
			hsb_weapons[2 + i][0] = Draw_PicFromWad (va ("inva%i_laser", i + 1));
			hsb_weapons[2 + i][1] = Draw_PicFromWad (va ("inva%i_mjolnir", i + 1));
			hsb_weapons[2 + i][2] = Draw_PicFromWad (va ("inva%i_gren_prox", i + 1));
			hsb_weapons[2 + i][3] = Draw_PicFromWad (va ("inva%i_prox_gren", i + 1));
			hsb_weapons[2 + i][4] = Draw_PicFromWad (va ("inva%i_prox", i + 1));
		}

		hsb_items[0] = Draw_PicFromWad ("sb_wsuit");
		hsb_items[1] = Draw_PicFromWad ("sb_eshld");
	}

	if (rogue)
	{
		rsb_invbar[0] = Draw_PicFromWad ("r_invbar1");
		rsb_invbar[1] = Draw_PicFromWad ("r_invbar2");

		rsb_weapons[0] = Draw_PicFromWad ("r_lava");
		rsb_weapons[1] = Draw_PicFromWad ("r_superlava");
		rsb_weapons[2] = Draw_PicFromWad ("r_gren");
		rsb_weapons[3] = Draw_PicFromWad ("r_multirock");
		rsb_weapons[4] = Draw_PicFromWad ("r_plasma");

		rsb_items[0] = Draw_PicFromWad ("r_shield1");
		rsb_items[1] = Draw_PicFromWad ("r_agrav1");

		// PGM 01/19/97 - team color border
		rsb_teambord = Draw_PicFromWad ("r_teambord");
		// PGM 01/19/97 - team color border

		rsb_ammo[0] = Draw_PicFromWad ("r_ammolava");
		rsb_ammo[1] = Draw_PicFromWad ("r_ammomulti");
		rsb_ammo[2] = Draw_PicFromWad ("r_ammoplasma");
	}
}

/*
===============
Sbar_Init -- johnfitz -- rewritten
===============
*/
void Sbar_Init (void)
{
	Cmd_AddCommand ("+showscores", Sbar_ShowScores);
	Cmd_AddCommand ("-showscores", Sbar_DontShowScores);

	Sbar_LoadPics ();
}


// =============================================================================

// drawing routines are relative to the status bar location

/*
=============
Sbar_DrawPic -- johnfitz -- rewritten now that GL_SetCanvas is doing the work
=============
*/
void Sbar_DrawPic (int x, int y, qpic_t *pic)
{
	Draw_Pic (x, y + 24, pic);
}

/*
=============
Sbar_DrawPicAlpha -- johnfitz
=============
*/
void Sbar_DrawPicAlpha (int x, int y, qpic_t *pic, float alpha)
{
	// MH - moved GL state stuff out of here!
	Draw_AlphaPic (x, y + 24, pic, alpha);
}


/*
================
Sbar_DrawCharacter -- johnfitz -- rewritten now that GL_SetCanvas is doing the work
================
*/
void Sbar_DrawCharacter (int x, int y, int num)
{
	Draw_Character (x, y + 24, num);
}

/*
================
Sbar_DrawString -- johnfitz -- rewritten now that GL_SetCanvas is doing the work
================
*/
void Sbar_DrawString (int x, int y, const char *str)
{
	Draw_String (x, y + 24, str);
}

/*
===============
Sbar_DrawScrollString -- johnfitz

scroll the string inside a glscissor region
===============
*/
void Sbar_DrawScrollString (int x, int y, int width, char *str)
{
	Draw_ScrollString (x, y + 24, width, str);
}


/*
=============
Sbar_itoa
=============
*/
int Sbar_itoa (int num, char *buf)
{
	char *str;
	int	pow10;
	int	dig;

	str = buf;

	if (num < 0)
	{
		*str++ = '-';
		num = -num;
	}

	for (pow10 = 10; num >= pow10; pow10 *= 10)
		;

	do
	{
		pow10 /= 10;
		dig = num / pow10;
		*str++ = '0' + dig;
		num -= dig * pow10;
	} while (pow10 != 1);

	*str = 0;

	return str - buf;
}


/*
=============
Sbar_DrawNum
=============
*/
void Sbar_DrawNum (int x, int y, int num, int digits, int color)
{
	char	str[12];
	char *ptr;
	int	l, frame;

	num = q_min (999, num); // johnfitz -- cap high values rather than truncating number

	l = Sbar_itoa (num, str);
	ptr = str;
	if (l > digits)
		ptr += (l - digits);
	if (l < digits)
		x += (digits - l) * 24;

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr - '0';

		Sbar_DrawPic (x, y, sb_nums[color][frame]); // johnfitz -- DrawTransPic is obsolete
		x += 24;
		ptr++;
	}
}

// =============================================================================

int		fragsort[MAX_SCOREBOARD];

char		scoreboardtext[MAX_SCOREBOARD][20];
int		scoreboardtop[MAX_SCOREBOARD];
int		scoreboardbottom[MAX_SCOREBOARD];
int		scoreboardcount[MAX_SCOREBOARD];
int		scoreboardlines;

/*
===============
Sbar_SortFrags
===============
*/
void Sbar_SortFrags (void)
{
	int		i, j, k;

	// sort by frags
	scoreboardlines = 0;
	for (i = 0; i < cl.maxclients; i++)
	{
		if (cl.scores[i].name[0])
		{
			fragsort[scoreboardlines] = i;
			scoreboardlines++;
		}
	}

	for (i = 0; i < scoreboardlines; i++)
	{
		for (j = 0; j < scoreboardlines - 1 - i; j++)
		{
			if (cl.scores[fragsort[j]].frags < cl.scores[fragsort[j + 1]].frags)
			{
				k = fragsort[j];
				fragsort[j] = fragsort[j + 1];
				fragsort[j + 1] = k;
			}
		}
	}
}

int	Sbar_ColorForMap (int m)
{
	return m < 128 ? m + 8 : m + 8;
}

/*
===============
Sbar_UpdateScoreboard
===============
*/
void Sbar_UpdateScoreboard (void)
{
	int		i, k;
	int		top, bottom;
	scoreboard_t *s;

	Sbar_SortFrags ();

	// draw the text
	memset (scoreboardtext, 0, sizeof (scoreboardtext));

	for (i = 0; i < scoreboardlines; i++)
	{
		k = fragsort[i];
		s = &cl.scores[k];
		sprintf (&scoreboardtext[i][1], "%3i %s", s->frags, s->name);

		top = s->colors & 0xf0;
		bottom = (s->colors & 15) << 4;
		scoreboardtop[i] = Sbar_ColorForMap (top);
		scoreboardbottom[i] = Sbar_ColorForMap (bottom);
	}
}

/*
===============
Sbar_SoloScoreboard -- johnfitz -- new layout
===============
*/
char *Sbar_MakeSoloStat (char *legend, int stat, int total)
{
	// some maps have none so we don't need to provide a total
	if (cl.stats[total])
		return va ("%s: %i/%i", legend, cl.stats[stat], cl.stats[total]);
	else return va ("%s: %i", legend, cl.stats[stat]);
}

void Sbar_SoloScoreboard (void)
{
	char *kills = Sbar_MakeSoloStat ("Kills", STAT_MONSTERS, STAT_TOTALMONSTERS);
	char *secrets = Sbar_MakeSoloStat ("Secrets", STAT_SECRETS, STAT_TOTALSECRETS);
	char *time = Sbar_TimeToStringUnpadded ((int) (cl.time + 0.5));
	char *map = va ("%s (%s)", cl.levelname, &cl.worldmodel->name[5]);

	// remove ".bsp" from the map file name
	for (int i = strlen (map); i; i--)
	{
		if (map[i] == '.')
		{
			map[i] = ')';
			map[i + 1] = 0;
			break;
		}
	}

	if (strlen (map) > 39)
		Sbar_DrawScrollString (4, 4, 312, map);
	else Sbar_DrawString (160 - strlen (map) * 4, 4, map);

	Sbar_DrawString (8, 12, kills);
	Sbar_DrawString (160 - strlen (time) * 4, 12, time);
	Sbar_DrawString (312 - strlen (secrets) * 8, 12, secrets);
}

/*
===============
Sbar_DrawScoreboard
===============
*/
void Sbar_DrawScoreboard (void)
{
	Sbar_SoloScoreboard ();
	if (cl.gametype == GAME_DEATHMATCH)
		Sbar_DeathmatchOverlay ();
}

// =============================================================================

/*
===============
Sbar_DrawInventory
===============
*/
void Sbar_DrawInventory (void)
{
	int	i, val;
	char	num[6];
	float	time;
	int	flashon;

	if (rogue)
	{
		if (cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN)
			Sbar_DrawPicAlpha (0, -24, rsb_invbar[0], scr_sbaralpha.value); // johnfitz -- scr_sbaralpha
		else
			Sbar_DrawPicAlpha (0, -24, rsb_invbar[1], scr_sbaralpha.value); // johnfitz -- scr_sbaralpha
	}
	else
	{
		Sbar_DrawPicAlpha (0, -24, sb_ibar, scr_sbaralpha.value); // johnfitz -- scr_sbaralpha
	}

	// weapons
	for (i = 0; i < 7; i++)
	{
		if (cl.items & (IT_SHOTGUN << i))
		{
			time = cl.item_gettime[i];
			flashon = (int) ((cl.time - time) * 10);
			if (flashon >= 10)
			{
				if (cl.stats[STAT_ACTIVEWEAPON] == (IT_SHOTGUN << i))
					flashon = 1;
				else
					flashon = 0;
			}
			else
				flashon = (flashon % 5) + 2;

			Sbar_DrawPic (i * 24, -16, sb_weapons[flashon][i]);
		}
	}

	// MED 01/04/97
	// hipnotic weapons
	if (hipnotic)
	{
		int grenadeflashing = 0;
		for (i = 0; i < 4; i++)
		{
			if (cl.items & (1 << hipweapons[i]))
			{
				time = cl.item_gettime[hipweapons[i]];
				flashon = (int) ((cl.time - time) * 10);
				if (flashon >= 10)
				{
					if (cl.stats[STAT_ACTIVEWEAPON] == (1 << hipweapons[i]))
						flashon = 1;
					else
						flashon = 0;
				}
				else
					flashon = (flashon % 5) + 2;

				// check grenade launcher
				if (i == 2)
				{
					if (cl.items & HIT_PROXIMITY_GUN)
					{
						if (flashon)
						{
							grenadeflashing = 1;
							Sbar_DrawPic (96, -16, hsb_weapons[flashon][2]);
						}
					}
				}
				else if (i == 3)
				{
					if (cl.items & (IT_SHOTGUN << 4))
					{
						if (flashon && !grenadeflashing)
						{
							Sbar_DrawPic (96, -16, hsb_weapons[flashon][3]);
						}
						else if (!grenadeflashing)
						{
							Sbar_DrawPic (96, -16, hsb_weapons[0][3]);
						}
					}
					else
						Sbar_DrawPic (96, -16, hsb_weapons[flashon][4]);
				}
				else
					Sbar_DrawPic (176 + (i * 24), -16, hsb_weapons[flashon][i]);
			}
		}
	}

	if (rogue)
	{
		// check for powered up weapon.
		if (cl.stats[STAT_ACTIVEWEAPON] >= RIT_LAVA_NAILGUN)
		{
			for (i = 0; i < 5; i++)
			{
				if (cl.stats[STAT_ACTIVEWEAPON] == (RIT_LAVA_NAILGUN << i))
				{
					Sbar_DrawPic ((i + 2) * 24, -16, rsb_weapons[i]);
				}
			}
		}
	}

	// ammo counts
	for (i = 0; i < 4; i++)
	{
		val = cl.stats[STAT_SHELLS + i];
		val = (val < 0) ? 0 : q_min (999, val);// johnfitz -- cap displayed value to 999
		sprintf (num, "%3i", val);

		// switch the colour
		if (num[0] != ' ') num[0] = 18 + num[0] - '0';
		if (num[1] != ' ') num[1] = 18 + num[1] - '0';
		if (num[2] != ' ') num[2] = 18 + num[2] - '0';

		Sbar_DrawString ((6 * i + 1) * 8 + 2, -24, num);
	}

	flashon = 0;
	// items
	for (i = 0; i < 6; i++)
	{
		if (cl.items & (1 << (17 + i)))
		{
			time = cl.item_gettime[17 + i];
			if (time && time > cl.time - 2 && flashon)
			{
				// flash frame
			}
			else
			{
				// MED 01/04/97 changed keys
				if (!hipnotic || (i > 1))
				{
					Sbar_DrawPic (192 + i * 16, -16, sb_items[i]);
				}
			}
		}
	}
	// MED 01/04/97 added hipnotic items
	// hipnotic items
	if (hipnotic)
	{
		for (i = 0; i < 2; i++)
		{
			if (cl.items & (1 << (24 + i)))
			{
				time = cl.item_gettime[24 + i];
				if (time && time > cl.time - 2 && flashon)
				{
					// flash frame
				}
				else
				{
					Sbar_DrawPic (288 + i * 16, -16, hsb_items[i]);
				}
			}
		}
	}

	if (rogue)
	{
		// new rogue items
		for (i = 0; i < 2; i++)
		{
			if (cl.items & (1 << (29 + i)))
			{
				time = cl.item_gettime[29 + i];
				if (time && time > cl.time - 2 && flashon)
				{
					// flash frame
				}
				else
				{
					Sbar_DrawPic (288 + i * 16, -16, rsb_items[i]);
				}
			}
		}
	}
	else
	{
		// sigils
		for (i = 0; i < 4; i++)
		{
			if (cl.items & (1 << (28 + i)))
			{
				time = cl.item_gettime[28 + i];
				if (time && time > cl.time - 2 && flashon)
				{
					// flash frame
				}
				else
					Sbar_DrawPic (320 - 32 + i * 8, -16, sb_sigil[i]);
			}
		}
	}
}

// =============================================================================

/*
===============
Sbar_DrawFrags -- johnfitz -- heavy revision
===============
*/
void Sbar_DrawFrags (void)
{
	int	numscores, i, x, color;
	char	num[12];
	scoreboard_t *s;

	Sbar_SortFrags ();

	// draw the text
	numscores = q_min (scoreboardlines, 4);

	for (i = 0, x = 184; i < numscores; i++, x += 32)
	{
		s = &cl.scores[fragsort[i]];
		if (!s->name[0])
			continue;

		// top color
		color = s->colors & 0xf0;
		color = Sbar_ColorForMap (color);
		Draw_Fill (x + 10, 1, 28, 4, color, 1);

		// bottom color
		color = (s->colors & 15) << 4;
		color = Sbar_ColorForMap (color);
		Draw_Fill (x + 10, 5, 28, 3, color, 1);

		// number
		sprintf (num, "%3i", s->frags);
		Sbar_DrawString (x + 12, -24, num);

		// brackets
		if (fragsort[i] == cl.viewentity - 1)
		{
			Sbar_DrawCharacter (x + 6, -24, 16);
			Sbar_DrawCharacter (x + 32, -24, 17);
		}
	}
}

// =============================================================================


/*
===============
Sbar_DrawFace
===============
*/
void Sbar_DrawFace (void)
{
	int	f, anim;

	// PGM 01/19/97 - team color drawing
	// PGM 03/02/97 - fixed so color swatch only appears in CTF modes
	if (rogue && (cl.maxclients != 1) && (teamplay.value > 3) && (teamplay.value < 7))
	{
		int	top, bottom;
		int	xofs;
		char	num[12];
		scoreboard_t *s;

		s = &cl.scores[cl.viewentity - 1];
		// draw background
		top = s->colors & 0xf0;
		bottom = (s->colors & 15) << 4;
		top = Sbar_ColorForMap (top);
		bottom = Sbar_ColorForMap (bottom);

		if (cl.gametype == GAME_DEATHMATCH)
			xofs = 113;
		else
			xofs = ((vid.width - 320) >> 1) + 113;

		Sbar_DrawPic (112, 0, rsb_teambord);
		Draw_Fill (xofs, /*vid.height-*/24 + 3, 22, 9, top, 1); // johnfitz -- sbar coords are now relative
		Draw_Fill (xofs, /*vid.height-*/24 + 12, 22, 9, bottom, 1); // johnfitz -- sbar coords are now relative

		// draw number
		f = s->frags;
		sprintf (num, "%3i", f);

		if (top == 8)
		{
			if (num[0] != ' ') Sbar_DrawCharacter (113, 3, 18 + num[0] - '0');
			if (num[1] != ' ') Sbar_DrawCharacter (120, 3, 18 + num[1] - '0');
			if (num[2] != ' ') Sbar_DrawCharacter (127, 3, 18 + num[2] - '0');
		}
		else
		{
			Sbar_DrawCharacter (113, 3, num[0]);
			Sbar_DrawCharacter (120, 3, num[1]);
			Sbar_DrawCharacter (127, 3, num[2]);
		}

		return;
	}
	// PGM 01/19/97 - team color drawing

	if ((cl.items & (IT_INVISIBILITY | IT_INVULNERABILITY)) == (IT_INVISIBILITY | IT_INVULNERABILITY))
	{
		Sbar_DrawPic (112, 0, sb_face_invis_invuln);
		return;
	}
	if (cl.items & IT_QUAD)
	{
		Sbar_DrawPic (112, 0, sb_face_quad);
		return;
	}
	if (cl.items & IT_INVISIBILITY)
	{
		Sbar_DrawPic (112, 0, sb_face_invis);
		return;
	}
	if (cl.items & IT_INVULNERABILITY)
	{
		Sbar_DrawPic (112, 0, sb_face_invuln);
		return;
	}

	// MH - improved this check
	if (cl.stats[STAT_HEALTH] < 0)
		f = 0;
	else if (cl.stats[STAT_HEALTH] >= 100)
		f = 4;
	else
		f = cl.stats[STAT_HEALTH] / 20;

	if (cl.time <= cl.faceanimtime)
	{
		anim = 1;
	}
	else
		anim = 0;
	Sbar_DrawPic (112, 0, sb_faces[f][anim]);
}

/*
===============
Sbar_Draw
===============
*/
void Sbar_Draw (void)
{
	float w; // johnfitz

	if (scr_con_current == vid.height)
		return;		// console is full screen

	if (cl.intermission)
		return; // johnfitz -- never draw sbar during intermission

	GL_SetCanvas (CANVAS_DEFAULT); // johnfitz

	// johnfitz -- don't waste fillrate by clearing the area behind the sbar
	w = CLAMP (320.0f, scr_sbarscale.value * 320.0f, (float) glwidth);
	if (sb_lines && glwidth > w)
	{
		if (scr_sbaralpha.value < 1)
			Draw_TileClear (0, glheight - sb_lines, glwidth, sb_lines);
		if (cl.gametype == GAME_DEATHMATCH)
			Draw_TileClear (w, glheight - sb_lines, glwidth - w, sb_lines);
		else
		{
			Draw_TileClear (0, glheight - sb_lines, (glwidth - w) / 2.0f, sb_lines);
			Draw_TileClear ((glwidth - w) / 2.0f + w, glheight - sb_lines, (glwidth - w) / 2.0f, sb_lines);
		}
	}
	// johnfitz

	GL_SetCanvas (CANVAS_SBAR); // johnfitz

	if (scr_viewsize.value < 110) // johnfitz -- check viewsize instead of sb_lines
	{
		Sbar_DrawInventory ();
		if (cl.maxclients != 1)
			Sbar_DrawFrags ();
	}

	if (sb_showscores || cl.stats[STAT_HEALTH] <= 0)
	{
		Sbar_DrawPicAlpha (0, 0, sb_scorebar, scr_sbaralpha.value); // johnfitz -- scr_sbaralpha
		Sbar_DrawScoreboard ();
	}
	else if (scr_viewsize.value < 120) // johnfitz -- check viewsize instead of sb_lines
	{
		Sbar_DrawPicAlpha (0, 0, sb_sbar, scr_sbaralpha.value); // johnfitz -- scr_sbaralpha

		// keys (hipnotic only)
		// MED 01/04/97 moved keys here so they would not be overwritten
		if (hipnotic)
		{
			if (cl.items & IT_KEY1)
				Sbar_DrawPic (209, 3, sb_items[0]);
			if (cl.items & IT_KEY2)
				Sbar_DrawPic (209, 12, sb_items[1]);
		}
		// armor
		if (cl.items & IT_INVULNERABILITY)
		{
			Sbar_DrawNum (24, 0, 666, 3, 1);
			Sbar_DrawPic (0, 0, draw_disc);
		}
		else
		{
			if (rogue)
			{
				Sbar_DrawNum (24, 0, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
				if (cl.items & RIT_ARMOR3)
					Sbar_DrawPic (0, 0, sb_armor[2]);
				else if (cl.items & RIT_ARMOR2)
					Sbar_DrawPic (0, 0, sb_armor[1]);
				else if (cl.items & RIT_ARMOR1)
					Sbar_DrawPic (0, 0, sb_armor[0]);
			}
			else
			{
				Sbar_DrawNum (24, 0, cl.stats[STAT_ARMOR], 3, cl.stats[STAT_ARMOR] <= 25);
				if (cl.items & IT_ARMOR3)
					Sbar_DrawPic (0, 0, sb_armor[2]);
				else if (cl.items & IT_ARMOR2)
					Sbar_DrawPic (0, 0, sb_armor[1]);
				else if (cl.items & IT_ARMOR1)
					Sbar_DrawPic (0, 0, sb_armor[0]);
			}
		}

		// face
		Sbar_DrawFace ();

		// health
		Sbar_DrawNum (136, 0, cl.stats[STAT_HEALTH], 3, cl.stats[STAT_HEALTH] <= 25);

		// ammo icon
		if (rogue)
		{
			if (cl.items & RIT_SHELLS)
				Sbar_DrawPic (224, 0, sb_ammo[0]);
			else if (cl.items & RIT_NAILS)
				Sbar_DrawPic (224, 0, sb_ammo[1]);
			else if (cl.items & RIT_ROCKETS)
				Sbar_DrawPic (224, 0, sb_ammo[2]);
			else if (cl.items & RIT_CELLS)
				Sbar_DrawPic (224, 0, sb_ammo[3]);
			else if (cl.items & RIT_LAVA_NAILS)
				Sbar_DrawPic (224, 0, rsb_ammo[0]);
			else if (cl.items & RIT_PLASMA_AMMO)
				Sbar_DrawPic (224, 0, rsb_ammo[1]);
			else if (cl.items & RIT_MULTI_ROCKETS)
				Sbar_DrawPic (224, 0, rsb_ammo[2]);
		}
		else
		{
			if (cl.items & IT_SHELLS)
				Sbar_DrawPic (224, 0, sb_ammo[0]);
			else if (cl.items & IT_NAILS)
				Sbar_DrawPic (224, 0, sb_ammo[1]);
			else if (cl.items & IT_ROCKETS)
				Sbar_DrawPic (224, 0, sb_ammo[2]);
			else if (cl.items & IT_CELLS)
				Sbar_DrawPic (224, 0, sb_ammo[3]);
		}

		Sbar_DrawNum (248, 0, cl.stats[STAT_AMMO], 3, cl.stats[STAT_AMMO] <= 10);
	}

	// johnfitz -- removed the vid.width > 320 check here
	if (cl.gametype == GAME_DEATHMATCH)
		Sbar_MiniDeathmatchOverlay ();
}

// =============================================================================

/*
==================
Sbar_IntermissionNumber

==================
*/
void Sbar_IntermissionNumber (int x, int y, int num, int digits, int color)
{
	char	str[12];
	char *ptr;
	int	l, frame;

	l = Sbar_itoa (num, str);
	ptr = str;
	if (l > digits)
		ptr += (l - digits);
	if (l < digits)
		x += (digits - l) * 24;

	while (*ptr)
	{
		if (*ptr == '-')
			frame = STAT_MINUS;
		else
			frame = *ptr - '0';

		Draw_Pic (x, y, sb_nums[color][frame]); // johnfitz -- stretched menus
		x += 24;
		ptr++;
	}
}

/*
==================
Sbar_DeathmatchOverlay

==================
*/
void Sbar_DeathmatchOverlay (void)
{
	qpic_t *pic;
	int	i, k, l;
	int	top, bottom;
	int	x, y, f;
	scoreboard_t *s;

	GL_SetCanvas (CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/ranking.lmp");
	M_DrawPic ((320 - pic->width) / 2, 8, pic);

	// scores
	Sbar_SortFrags ();

	// draw the text
	l = scoreboardlines;

	x = 80; // johnfitz -- simplified becuase some positioning is handled elsewhere
	y = 40;
	for (i = 0; i < l; i++)
	{
		k = fragsort[i];
		s = &cl.scores[k];
		if (!s->name[0])
			continue;

		// draw background
		top = s->colors & 0xf0;
		bottom = (s->colors & 15) << 4;
		top = Sbar_ColorForMap (top);
		bottom = Sbar_ColorForMap (bottom);

		Draw_Fill (x, y, 40, 4, top, 1); // johnfitz -- stretched overlays
		Draw_Fill (x, y + 4, 40, 4, bottom, 1); // johnfitz -- stretched overlays

		// draw number
		f = s->frags;

		if (k == cl.viewentity - 1)
			Draw_String (x - 8, y, va ("%c %3i", 12, f));
		else Draw_String (x + 8, y, va ("%3i", f));

		// draw name
		M_Print (x + 64, y, s->name); // johnfitz -- was Draw_String, changed for stretched overlays

		y += 10;
	}

	GL_SetCanvas (CANVAS_SBAR); // johnfitz
}

/*
==================
Sbar_MiniDeathmatchOverlay
==================
*/
void Sbar_MiniDeathmatchOverlay (void)
{
	int	i, k, top, bottom, x, y, f, numlines;
	float	scale; // johnfitz
	scoreboard_t *s;

	scale = CLAMP (1.0, scr_sbarscale.value, (float) glwidth / 320.0); // johnfitz

	// MAX_SCOREBOARDNAME = 32, so total width for this overlay plus sbar is 632, but we can cut off some i guess
	if (glwidth / scale < 512 || scr_viewsize.value >= 120) // johnfitz -- test should consider scr_sbarscale
		return;

	// scores
	Sbar_SortFrags ();

	// draw the text
	numlines = (scr_viewsize.value >= 110) ? 3 : 6; // johnfitz

	// find us
	for (i = 0; i < scoreboardlines; i++)
		if (fragsort[i] == cl.viewentity - 1)
			break;
	if (i == scoreboardlines) // we're not there
		i = 0;
	else // figure out start
		i = i - numlines / 2;
	if (i > scoreboardlines - numlines)
		i = scoreboardlines - numlines;
	if (i < 0)
		i = 0;

	x = 324;
	y = (scr_viewsize.value >= 110) ? 24 : 0; // johnfitz -- start at the right place
	for (; i < scoreboardlines && y <= 48; i++, y += 8) // johnfitz -- change y init, test, inc
	{
		k = fragsort[i];
		s = &cl.scores[k];
		if (!s->name[0])
			continue;

		// colors
		top = s->colors & 0xf0;
		bottom = (s->colors & 15) << 4;
		top = Sbar_ColorForMap (top);
		bottom = Sbar_ColorForMap (bottom);

		Draw_Fill (x, y + 1, 40, 4, top, 1);
		Draw_Fill (x, y + 5, 40, 3, bottom, 1);

		// number
		f = s->frags;

		if (k == cl.viewentity - 1)
			Draw_String (x, y, va ("%c%3i%c", 16, f, 17));
		else Draw_String (x + 8, y, va ("%3i", f));

		// name
		Draw_String (x + 48, y, s->name);
	}
}


void Sbar_DrawIntermissionStat (int x, int y, char *str)
{
	for (int i = 0;; i++)
	{
		qpic_t *pic = NULL;

		if (!str[i]) break;

		if (str[i] == '-')
			pic = sb_nums[0][STAT_MINUS];
		else if (str[i] == '/')
			pic = sb_slash;
		else if (str[i] == ':')
			pic = sb_colon;
		else
		{
			int frame = str[i] - '0';

			if (frame >= 0 && frame < 10)
				pic = sb_nums[0][frame];
			else pic = NULL;
		}

		if (!pic)
		{
			x += 24;
			continue;
		}

		M_DrawPic (x, y, pic);
		x += pic->width;
	}
}


/*
==================
Sbar_IntermissionOverlay
==================
*/
void Sbar_IntermissionOverlay (void)
{
	if (cl.gametype == GAME_DEATHMATCH)
	{
		Sbar_DeathmatchOverlay ();
		return;
	}

	GL_SetCanvas (CANVAS_MENU); // johnfitz

	Draw_Pic (64, 24, Draw_CachePic ("gfx/complete.lmp"));
	Draw_Pic (0, 56, Draw_CachePic ("gfx/inter.lmp"));

	Sbar_DrawIntermissionStat (160, 64, Sbar_TimeToString (cl.completed_time));

	if (cl.stats[STAT_TOTALSECRETS])
		Sbar_DrawIntermissionStat (136, 104, va ("%4i/%i", cl.stats[STAT_SECRETS], cl.stats[STAT_TOTALSECRETS]));
	else Sbar_DrawIntermissionStat (136, 104, va ("%4i", cl.stats[STAT_SECRETS]));

	if (cl.stats[STAT_TOTALMONSTERS])
		Sbar_DrawIntermissionStat (136, 144, va ("%4i/%i", cl.stats[STAT_MONSTERS], cl.stats[STAT_TOTALMONSTERS]));
	else Sbar_DrawIntermissionStat (136, 144, va ("%4i", cl.stats[STAT_MONSTERS]));
}


/*
==================
Sbar_FinaleOverlay
==================
*/
void Sbar_FinaleOverlay (void)
{
	qpic_t *pic;

	GL_SetCanvas (CANVAS_MENU); // johnfitz

	pic = Draw_CachePic ("gfx/finale.lmp");
	Draw_Pic ((320 - pic->width) / 2, 16, pic); // johnfitz -- stretched menus
}

