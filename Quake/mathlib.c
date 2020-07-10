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
// mathlib.c -- math primitives

#include "quakedef.h"

vec3_t vec3_origin = { 0, 0, 0 };

/*-----------------------------------------------------------------*/


float anglemod (float a)
{
	a = (360.0 / 65536) * ((int) (a * (65536 / 360.0)) & 65535);
	return a;
}


/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/
int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, struct mplane_s *p)
{
	float	dist1, dist2;
	int		sides;

	if (p->type < 3)
	{
		if (p->dist <= emins[p->type]) return 1;
		if (p->dist >= emaxs[p->type]) return 2;
		return 3;
	}

	// general case
	switch (p->signbits)
	{
	case 0:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		break;

	case 1:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		break;

	case 2:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		break;

	case 3:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		break;

	case 4:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		break;

	case 5:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emaxs[2];
		break;

	case 6:
		dist1 = p->normal[0] * emaxs[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emins[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		break;

	case 7:
		dist1 = p->normal[0] * emins[0] + p->normal[1] * emins[1] + p->normal[2] * emins[2];
		dist2 = p->normal[0] * emaxs[0] + p->normal[1] * emaxs[1] + p->normal[2] * emaxs[2];
		break;

	default:
		dist1 = dist2 = 0;		// shut up compiler
		Sys_Error ("BoxOnPlaneSide:  Bad signbits");
		break;
	}

	sides = 0;

	if (dist1 >= p->dist) sides = 1;
	if (dist2 < p->dist) sides |= 2;

	return sides;
}


// johnfitz -- the opposite of AngleVectors.  this takes forward and generates pitch yaw roll
// TODO: take right and up vectors to properly set yaw and roll
void VectorAngles (const vec3_t forward, vec3_t angles)
{
	vec3_t temp;

	temp[0] = forward[0];
	temp[1] = forward[1];
	temp[2] = 0;
	angles[PITCH] = -atan2 (forward[2], VectorLength (temp)) / M_PI_DIV_180;
	angles[YAW] = atan2 (forward[1], forward[0]) / M_PI_DIV_180;
	angles[ROLL] = 0;
}


void AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
	float		angle;
	float		sr, sp, sy, cr, cp, cy;

	angle = angles[YAW] * (M_PI * 2 / 360);
	sy = sin (angle);
	cy = cos (angle);
	angle = angles[PITCH] * (M_PI * 2 / 360);
	sp = sin (angle);
	cp = cos (angle);
	angle = angles[ROLL] * (M_PI * 2 / 360);
	sr = sin (angle);
	cr = cos (angle);

	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;
	right[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
	right[1] = (-1 * sr * sp * sy + -1 * cr * cy);
	right[2] = -1 * sr * cp;
	up[0] = (cr * sp * cy + -sr * -sy);
	up[1] = (cr * sp * sy + -sr * cy);
	up[2] = cr * cp;
}


int VectorCompare (vec3_t v1, vec3_t v2)
{
	int		i;

	for (i = 0; i < 3; i++)
		if (v1[i] != v2[i])
			return 0;

	return 1;
}


void VectorMA (vec3_t veca, float scale, vec3_t vecb, vec3_t vecc)
{
	vecc[0] = veca[0] + scale * vecb[0];
	vecc[1] = veca[1] + scale * vecb[1];
	vecc[2] = veca[2] + scale * vecb[2];
}


vec_t _DotProduct (vec3_t v1, vec3_t v2)
{
	return v1[0] * v2[0] + v1[1] * v2[1] + v1[2] * v2[2];
}


void _VectorSubtract (vec3_t veca, vec3_t vecb, vec3_t out)
{
	out[0] = veca[0] - vecb[0];
	out[1] = veca[1] - vecb[1];
	out[2] = veca[2] - vecb[2];
}


void _VectorAdd (vec3_t veca, vec3_t vecb, vec3_t out)
{
	out[0] = veca[0] + vecb[0];
	out[1] = veca[1] + vecb[1];
	out[2] = veca[2] + vecb[2];
}


void _VectorCopy (vec3_t in, vec3_t out)
{
	out[0] = in[0];
	out[1] = in[1];
	out[2] = in[2];
}


void CrossProduct (vec3_t v1, vec3_t v2, vec3_t cross)
{
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}


vec_t VectorLength (vec3_t v)
{
	return sqrt (DotProduct (v, v));
}


float VectorNormalize (vec3_t v)
{
	float	length, ilength;

	length = sqrt (DotProduct (v, v));

	if (length)
	{
		ilength = 1 / length;
		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}

	return length;
}


void VectorInverse (vec3_t v)
{
	v[0] = -v[0];
	v[1] = -v[1];
	v[2] = -v[2];
}


void VectorScale (vec3_t in, vec_t scale, vec3_t out)
{
	out[0] = in[0] * scale;
	out[1] = in[1] * scale;
	out[2] = in[2] * scale;
}


// --------------------------------------------------------------------------------------------------------------------------------------------------------------
// matrix stuff


QMATRIX *R_IdentityMatrix (QMATRIX *m)
{
	m->m4x4[0][0] = 1; m->m4x4[0][1] = 0; m->m4x4[0][2] = 0; m->m4x4[0][3] = 0;
	m->m4x4[1][0] = 0; m->m4x4[1][1] = 1; m->m4x4[1][2] = 0; m->m4x4[1][3] = 0;
	m->m4x4[2][0] = 0; m->m4x4[2][1] = 0; m->m4x4[2][2] = 1; m->m4x4[2][3] = 0;
	m->m4x4[3][0] = 0; m->m4x4[3][1] = 0; m->m4x4[3][2] = 0; m->m4x4[3][3] = 1;

	return m;
}


QMATRIX *R_MultMatrix (QMATRIX *out, QMATRIX *m1, QMATRIX *m2)
{
	return R_LoadMatrix (
		out,
		(m1->m4x4[0][0] * m2->m4x4[0][0]) + (m1->m4x4[0][1] * m2->m4x4[1][0]) + (m1->m4x4[0][2] * m2->m4x4[2][0]) + (m1->m4x4[0][3] * m2->m4x4[3][0]),
		(m1->m4x4[0][0] * m2->m4x4[0][1]) + (m1->m4x4[0][1] * m2->m4x4[1][1]) + (m1->m4x4[0][2] * m2->m4x4[2][1]) + (m1->m4x4[0][3] * m2->m4x4[3][1]),
		(m1->m4x4[0][0] * m2->m4x4[0][2]) + (m1->m4x4[0][1] * m2->m4x4[1][2]) + (m1->m4x4[0][2] * m2->m4x4[2][2]) + (m1->m4x4[0][3] * m2->m4x4[3][2]),
		(m1->m4x4[0][0] * m2->m4x4[0][3]) + (m1->m4x4[0][1] * m2->m4x4[1][3]) + (m1->m4x4[0][2] * m2->m4x4[2][3]) + (m1->m4x4[0][3] * m2->m4x4[3][3]),
		(m1->m4x4[1][0] * m2->m4x4[0][0]) + (m1->m4x4[1][1] * m2->m4x4[1][0]) + (m1->m4x4[1][2] * m2->m4x4[2][0]) + (m1->m4x4[1][3] * m2->m4x4[3][0]),
		(m1->m4x4[1][0] * m2->m4x4[0][1]) + (m1->m4x4[1][1] * m2->m4x4[1][1]) + (m1->m4x4[1][2] * m2->m4x4[2][1]) + (m1->m4x4[1][3] * m2->m4x4[3][1]),
		(m1->m4x4[1][0] * m2->m4x4[0][2]) + (m1->m4x4[1][1] * m2->m4x4[1][2]) + (m1->m4x4[1][2] * m2->m4x4[2][2]) + (m1->m4x4[1][3] * m2->m4x4[3][2]),
		(m1->m4x4[1][0] * m2->m4x4[0][3]) + (m1->m4x4[1][1] * m2->m4x4[1][3]) + (m1->m4x4[1][2] * m2->m4x4[2][3]) + (m1->m4x4[1][3] * m2->m4x4[3][3]),
		(m1->m4x4[2][0] * m2->m4x4[0][0]) + (m1->m4x4[2][1] * m2->m4x4[1][0]) + (m1->m4x4[2][2] * m2->m4x4[2][0]) + (m1->m4x4[2][3] * m2->m4x4[3][0]),
		(m1->m4x4[2][0] * m2->m4x4[0][1]) + (m1->m4x4[2][1] * m2->m4x4[1][1]) + (m1->m4x4[2][2] * m2->m4x4[2][1]) + (m1->m4x4[2][3] * m2->m4x4[3][1]),
		(m1->m4x4[2][0] * m2->m4x4[0][2]) + (m1->m4x4[2][1] * m2->m4x4[1][2]) + (m1->m4x4[2][2] * m2->m4x4[2][2]) + (m1->m4x4[2][3] * m2->m4x4[3][2]),
		(m1->m4x4[2][0] * m2->m4x4[0][3]) + (m1->m4x4[2][1] * m2->m4x4[1][3]) + (m1->m4x4[2][2] * m2->m4x4[2][3]) + (m1->m4x4[2][3] * m2->m4x4[3][3]),
		(m1->m4x4[3][0] * m2->m4x4[0][0]) + (m1->m4x4[3][1] * m2->m4x4[1][0]) + (m1->m4x4[3][2] * m2->m4x4[2][0]) + (m1->m4x4[3][3] * m2->m4x4[3][0]),
		(m1->m4x4[3][0] * m2->m4x4[0][1]) + (m1->m4x4[3][1] * m2->m4x4[1][1]) + (m1->m4x4[3][2] * m2->m4x4[2][1]) + (m1->m4x4[3][3] * m2->m4x4[3][1]),
		(m1->m4x4[3][0] * m2->m4x4[0][2]) + (m1->m4x4[3][1] * m2->m4x4[1][2]) + (m1->m4x4[3][2] * m2->m4x4[2][2]) + (m1->m4x4[3][3] * m2->m4x4[3][2]),
		(m1->m4x4[3][0] * m2->m4x4[0][3]) + (m1->m4x4[3][1] * m2->m4x4[1][3]) + (m1->m4x4[3][2] * m2->m4x4[2][3]) + (m1->m4x4[3][3] * m2->m4x4[3][3])
	);
}


QMATRIX *R_LoadMatrix (QMATRIX *m, float _11, float _12, float _13, float _14, float _21, float _22, float _23, float _24, float _31, float _32, float _33, float _34, float _41, float _42, float _43, float _44)
{
	m->m4x4[0][0] = _11; m->m4x4[0][1] = _12; m->m4x4[0][2] = _13; m->m4x4[0][3] = _14;
	m->m4x4[1][0] = _21; m->m4x4[1][1] = _22; m->m4x4[1][2] = _23; m->m4x4[1][3] = _24;
	m->m4x4[2][0] = _31; m->m4x4[2][1] = _32; m->m4x4[2][2] = _33; m->m4x4[2][3] = _34;
	m->m4x4[3][0] = _41; m->m4x4[3][1] = _42; m->m4x4[3][2] = _43; m->m4x4[3][3] = _44;

	return m;
}


QMATRIX *R_CopyMatrix (QMATRIX *dst, QMATRIX *src)
{
	memcpy (dst, src, sizeof (QMATRIX));
	return dst;
}


QMATRIX *R_FrustumMatrix (QMATRIX *m, float fovx, float fovy, float zNear, float zFar)
{
	float r = zNear * tan ((fovx * M_PI) / 360.0);
	float t = zNear * tan ((fovy * M_PI) / 360.0);

	float l = -r;
	float b = -t;

	// OpenGL frustum matrix
	// https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/xhtml/glFrustum.xml
	QMATRIX m2 = {
		(2 * zNear) / (r - l),
		0,
		0,
		0,
		0,
		(2 * zNear) / (t - b),
		0,
		0,
		(r + l) / (r - l),
		(t + b) / (t - b),
		-((zFar + zNear) / (zFar - zNear)),
		-1,
		0,
		0,
		-((2 * zFar * zNear) / (zFar - zNear)),
		0
	};

	return R_MultMatrix (m, &m2, m);
}



QMATRIX *R_OrthoMatrix (QMATRIX *m, float left, float right, float bottom, float top, float zNear, float zFar)
{
	// https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/xhtml/glOrtho.xml
	QMATRIX m2 = {
		2 / (right - left),
		0,
		0,
		0,
		0,
		2 / (top - bottom),
		0,
		0,
		0,
		0,
		-2 / (zNear - zFar),
		0,
		-((right + left) / (right - left)),
		-((top + bottom) / (top - bottom)),
		-((zFar + zNear) / (zFar - zNear)),
		1
	};

	return R_MultMatrix (m, &m2, m);
}


QMATRIX *R_TranslateMatrix (QMATRIX *m, float x, float y, float z)
{
	m->m4x4[3][0] += x * m->m4x4[0][0] + y * m->m4x4[1][0] + z * m->m4x4[2][0];
	m->m4x4[3][1] += x * m->m4x4[0][1] + y * m->m4x4[1][1] + z * m->m4x4[2][1];
	m->m4x4[3][2] += x * m->m4x4[0][2] + y * m->m4x4[1][2] + z * m->m4x4[2][2];
	m->m4x4[3][3] += x * m->m4x4[0][3] + y * m->m4x4[1][3] + z * m->m4x4[2][3];

	return m;
}


QMATRIX *R_ScaleMatrix (QMATRIX *m, float x, float y, float z)
{
	Vector4Scalef (m->m4x4[0], m->m4x4[0], x);
	Vector4Scalef (m->m4x4[1], m->m4x4[1], y);
	Vector4Scalef (m->m4x4[2], m->m4x4[2], z);

	return m;
}


QMATRIX *R_RotateMatrixAxis (QMATRIX *m, float angle, float x, float y, float z)
{
	float xyz[3] = { x, y, z };
	float sa, ca;
	QMATRIX m2;

	VectorNormalize (xyz);
	angle = DEG2RAD (angle);

	sa = sin (angle);
	ca = cos (angle);

	R_LoadMatrix (
		&m2,
		(1.0f - ca) * xyz[0] * xyz[0] + ca,
		(1.0f - ca) * xyz[1] * xyz[0] + sa * xyz[2],
		(1.0f - ca) * xyz[2] * xyz[0] - sa * xyz[1],
		0.0f,
		(1.0f - ca) * xyz[0] * xyz[1] - sa * xyz[2],
		(1.0f - ca) * xyz[1] * xyz[1] + ca,
		(1.0f - ca) * xyz[2] * xyz[1] + sa * xyz[0],
		0.0f,
		(1.0f - ca) * xyz[0] * xyz[2] + sa * xyz[1],
		(1.0f - ca) * xyz[1] * xyz[2] - sa * xyz[0],
		(1.0f - ca) * xyz[2] * xyz[2] + ca,
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		1.0f
	);

	return R_MultMatrix (m, &m2, m);
}


QMATRIX *R_RotateMatrix (QMATRIX *m, float p, float y, float r)
{
	float sr = sin (DEG2RAD (r));
	float sp = sin (DEG2RAD (p));
	float sy = sin (DEG2RAD (y));
	float cr = cos (DEG2RAD (r));
	float cp = cos (DEG2RAD (p));
	float cy = cos (DEG2RAD (y));

	QMATRIX m2 = {
		(cp * cy),
		(cp * sy),
		-sp,
		0.0f,
		(cr * -sy) + (sr * sp * cy),
		(cr * cy) + (sr * sp * sy),
		(sr * cp),
		0.0f,
		(sr * sy) + (cr * sp * cy),
		(-sr * cy) + (cr * sp * sy),
		(cr * cp),
		0.0f,
		0.0f,
		0.0f,
		0.0f,
		1.0f
	};

	return R_MultMatrix (m, &m2, m);
}


QMATRIX *R_CameraMatrix (QMATRIX *m, const float *origin, const float *angles)
{
	float sr = sin (DEG2RAD (angles[2]));
	float sp = sin (DEG2RAD (angles[0]));
	float sy = sin (DEG2RAD (angles[1]));
	float cr = cos (DEG2RAD (angles[2]));
	float cp = cos (DEG2RAD (angles[0]));
	float cy = cos (DEG2RAD (angles[1]));

	float _11 = -((cr * -sy) + (sr * sp * cy));
	float _21 = -((cr * cy) + (sr * sp * sy));
	float _31 = -(sr * cp);

	float _12 = (sr * sy) + (cr * sp * cy);
	float _22 = (-sr * cy) + (cr * sp * sy);
	float _32 = (cr * cp);

	float _13 = -(cp * cy);
	float _23 = -(cp * sy);
	float _33 = sp;

	QMATRIX m2 = {
		_11, _12, _13, 0.0f,
		_21, _22, _23, 0.0f,
		_31, _32, _33, 0.0f,
		-origin[0] * _11 - origin[1] * _21 - origin[2] * _31,
		-origin[0] * _12 - origin[1] * _22 - origin[2] * _32,
		-origin[0] * _13 - origin[1] * _23 - origin[2] * _33,
		1.0f
	};

	return R_MultMatrix (m, &m2, m);
}


void R_InverseTransform (QMATRIX *m, float *out, const float *in)
{
	// http://content.gpwiki.org/index.php/MathGem:Fast_Matrix_Inversion
	out[0] = in[0] * m->m4x4[0][0] + in[1] * m->m4x4[0][1] + in[2] * m->m4x4[0][2] - Vector3Dot (m->m4x4[0], m->m4x4[3]);
	out[1] = in[0] * m->m4x4[1][0] + in[1] * m->m4x4[1][1] + in[2] * m->m4x4[1][2] - Vector3Dot (m->m4x4[1], m->m4x4[3]);
	out[2] = in[0] * m->m4x4[2][0] + in[1] * m->m4x4[2][1] + in[2] * m->m4x4[2][2] - Vector3Dot (m->m4x4[2], m->m4x4[3]);
}


void R_Transform (QMATRIX *m, float *out, const float *in)
{
	out[0] = in[0] * m->m4x4[0][0] + in[1] * m->m4x4[1][0] + in[2] * m->m4x4[2][0] + m->m4x4[3][0];
	out[1] = in[0] * m->m4x4[0][1] + in[1] * m->m4x4[1][1] + in[2] * m->m4x4[2][1] + m->m4x4[3][1];
	out[2] = in[0] * m->m4x4[0][2] + in[1] * m->m4x4[1][2] + in[2] * m->m4x4[2][2] + m->m4x4[3][2];
}


void R_Rotate (QMATRIX *m, float *out, const float *in)
{
	out[0] = in[0] * m->m4x4[0][0] + in[1] * m->m4x4[1][0] + in[2] * m->m4x4[2][0];
	out[1] = in[0] * m->m4x4[0][1] + in[1] * m->m4x4[1][1] + in[2] * m->m4x4[2][1];
	out[2] = in[0] * m->m4x4[0][2] + in[1] * m->m4x4[1][2] + in[2] * m->m4x4[2][2];
}


