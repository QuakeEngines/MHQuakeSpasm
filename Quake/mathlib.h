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

#ifndef __MATHLIB_H
#define __MATHLIB_H

// mathlib.h

#include <math.h>

#ifndef M_PI
#define M_PI		3.14159265358979323846	// matches value in gcc v2 math.h
#endif

#define M_PI_DIV_180 (M_PI / 180.0) // johnfitz

struct mplane_s;

extern vec3_t vec3_origin;

#define	nanmask		(255 << 23)	/* 7F800000 */
#if 0	/* macro is violating strict aliasing rules */
#define	IS_NAN(x)	(((*(int *) (char *) &x) & nanmask) == nanmask)
#else
static inline int IS_NAN (float x)
{
	union { float f; int i; } num;
	num.f = x;
	return ((num.i & nanmask) == nanmask);
}
#endif

#define Q_rint(x) ((x) > 0 ? (int)((x) + 0.5) : (int)((x) - 0.5)) // johnfitz -- from joequake

#define DotProduct(x,y) (x[0]*y[0]+x[1]*y[1]+x[2]*y[2])
#define DoublePrecisionDotProduct(x,y) ((double)x[0]*y[0]+(double)x[1]*y[1]+(double)x[2]*y[2])
#define VectorSubtract(a,b,c) {c[0]=a[0]-b[0];c[1]=a[1]-b[1];c[2]=a[2]-b[2];}
#define VectorAdd(a,b,c) {c[0]=a[0]+b[0];c[1]=a[1]+b[1];c[2]=a[2]+b[2];}
#define VectorCopy(a,b) {b[0]=a[0];b[1]=a[1];b[2]=a[2];}

// johnfitz -- courtesy of lordhavoc
// QuakeSpasm: To avoid strict aliasing violations, use a float/int union instead of type punning.
#define VectorNormalizeFast(_v)\
{\
	union { float f; int i; } _y, _number;\
	_number.f = DotProduct(_v, _v);\
	if (_number.f != 0.0)\
	{\
		_y.i = 0x5f3759df - (_number.i >> 1);\
		_y.f = _y.f * (1.5f - (_number.f * 0.5f * _y.f * _y.f));\
		VectorScale(_v, _y.f, _v);\
	}\
}


void VectorAngles (const vec3_t forward, vec3_t angles); // johnfitz

void VectorMA (vec3_t veca, float scale, vec3_t vecb, vec3_t vecc);

vec_t _DotProduct (vec3_t v1, vec3_t v2);
void _VectorSubtract (vec3_t veca, vec3_t vecb, vec3_t out);
void _VectorAdd (vec3_t veca, vec3_t vecb, vec3_t out);
void _VectorCopy (vec3_t in, vec3_t out);

int VectorCompare (vec3_t v1, vec3_t v2);
vec_t VectorLength (vec3_t v);
float VectorDist (vec3_t v1, vec3_t v2); // mh - use this in a few places
void CrossProduct (vec3_t v1, vec3_t v2, vec3_t cross);
float VectorNormalize (vec3_t v);		// returns vector length
void VectorInverse (vec3_t v);
void VectorScale (vec3_t in, vec_t scale, vec3_t out);
int Q_log2 (int val);

void R_ConcatRotations (float in1[3][3], float in2[3][3], float out[3][3]);
void R_ConcatTransforms (float in1[3][4], float in2[3][4], float out[3][4]);

void FloorDivMod (double numer, double denom, int *quotient,
	int *rem);
fixed16_t Invert24To16 (fixed16_t val);
int GreatestCommonDivisor (int i1, int i2);

void AngleVectors (vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
int BoxOnPlaneSide (vec3_t emins, vec3_t emaxs, struct mplane_s *plane);
float	anglemod (float a);


#define BOX_ON_PLANE_SIDE(emins, emaxs, p)	\
	(((p)->type < 3)?						\
	(										\
		((p)->dist <= (emins)[(p)->type])?	\
			1								\
		:									\
		(									\
			((p)->dist >= (emaxs)[(p)->type])?\
				2							\
			:								\
				3							\
		)									\
	)										\
	:										\
		BoxOnPlaneSide( (emins), (emaxs), (p)))



// matrix stuff
// make this available everywhere
__declspec(align(16)) typedef union _QMATRIX {
	float m4x4[4][4];
	float m16[16];
} QMATRIX;

QMATRIX *R_IdentityMatrix (QMATRIX *m);
QMATRIX *R_MultMatrix (QMATRIX *out, QMATRIX *m1, QMATRIX *m2);
QMATRIX *R_LoadMatrix (QMATRIX *m, float _11, float _12, float _13, float _14, float _21, float _22, float _23, float _24, float _31, float _32, float _33, float _34, float _41, float _42, float _43, float _44);
QMATRIX *R_CopyMatrix (QMATRIX *dst, QMATRIX *src);
QMATRIX *R_FrustumMatrix (QMATRIX *m, float fovx, float fovy);
QMATRIX *R_OrthoMatrix (QMATRIX *m, float left, float right, float bottom, float top, float zNear, float zFar);
QMATRIX *R_TranslateMatrix (QMATRIX *m, float x, float y, float z);
QMATRIX *R_ScaleMatrix (QMATRIX *m, float x, float y, float z);
QMATRIX *R_RotateMatrix (QMATRIX *m, float p, float y, float r);
QMATRIX *R_RotateMatrixAxis (QMATRIX *m, float angle, float x, float y, float z);
QMATRIX *R_CameraMatrix (QMATRIX *m, const float *origin, const float *angles);
void R_InverseTransform (QMATRIX *m, float *out, const float *in);
void R_Transform (QMATRIX *m, float *out, const float *in);


#define DEG2RAD(a) (((a) * M_PI) / 180.0)
#define RAD2DEG(a) (((a) * 180.0) / M_PI)

// common utility funcs
__inline float SafeSqrt (float in)
{
	if (in < 0.00001f)
		return 0;
	else return sqrt (in);
}


__inline float *float2 (float a, float b)
{
	static float f[2];

	f[0] = a;
	f[1] = b;

	return f;
}


__inline float *float3 (float a, float b, float c)
{
	static float f[3];

	f[0] = a;
	f[1] = b;
	f[2] = c;

	return f;
}


__inline float *float4 (float a, float b, float c, float d)
{
	static float f[4];

	f[0] = a;
	f[1] = b;
	f[2] = c;
	f[3] = d;

	return f;
}


// vector functions from directq
__inline void Vector2Madf (float *out, const float *vec, float scale, const float *add)
{
	out[0] = vec[0] * scale + add[0];
	out[1] = vec[1] * scale + add[1];
}


__inline void Vector2Mad (float *out, const float *vec, const float *scale, const float *add)
{
	out[0] = vec[0] * scale[0] + add[0];
	out[1] = vec[1] * scale[1] + add[1];
}


__inline void Vector3Madf (float *out, const float *vec, float scale, const float *add)
{
	out[0] = vec[0] * scale + add[0];
	out[1] = vec[1] * scale + add[1];
	out[2] = vec[2] * scale + add[2];
}


__inline void Vector3Mad (float *out, const float *vec, const float *scale, const float *add)
{
	out[0] = vec[0] * scale[0] + add[0];
	out[1] = vec[1] * scale[1] + add[1];
	out[2] = vec[2] * scale[2] + add[2];
}


__inline void Vector4Madf (float *out, const float *vec, float scale, const float *add)
{
	out[0] = vec[0] * scale + add[0];
	out[1] = vec[1] * scale + add[1];
	out[2] = vec[2] * scale + add[2];
	out[3] = vec[3] * scale + add[3];
}


__inline void Vector4Mad (float *out, const float *vec, const float *scale, const float *add)
{
	out[0] = vec[0] * scale[0] + add[0];
	out[1] = vec[1] * scale[1] + add[1];
	out[2] = vec[2] * scale[2] + add[2];
	out[3] = vec[3] * scale[3] + add[3];
}


__inline void Vector2Scalef (float *dst, const float *vec, float scale)
{
	dst[0] = vec[0] * scale;
	dst[1] = vec[1] * scale;
}


__inline void Vector2Scale (float *dst, const float *vec, const float *scale)
{
	dst[0] = vec[0] * scale[0];
	dst[1] = vec[1] * scale[1];
}


__inline void Vector3Scalef (float *dst, const float *vec, float scale)
{
	dst[0] = vec[0] * scale;
	dst[1] = vec[1] * scale;
	dst[2] = vec[2] * scale;
}


__inline void Vector3Scale (float *dst, const float *vec, const float *scale)
{
	dst[0] = vec[0] * scale[0];
	dst[1] = vec[1] * scale[1];
	dst[2] = vec[2] * scale[2];
}


__inline void Vector4Scalef (float *dst, const float *vec, float scale)
{
	dst[0] = vec[0] * scale;
	dst[1] = vec[1] * scale;
	dst[2] = vec[2] * scale;
	dst[3] = vec[3] * scale;
}


__inline void Vector4Scale (float *dst, const float *vec, const float *scale)
{
	dst[0] = vec[0] * scale[0];
	dst[1] = vec[1] * scale[1];
	dst[2] = vec[2] * scale[2];
	dst[3] = vec[3] * scale[3];
}


__inline void Vector2Recipf (float *dst, const float *vec, float scale)
{
	dst[0] = vec[0] / scale;
	dst[1] = vec[1] / scale;
}


__inline void Vector2Recip (float *dst, const float *vec, const float *scale)
{
	dst[0] = vec[0] / scale[0];
	dst[1] = vec[1] / scale[1];
}


__inline void Vector3Recipf (float *dst, const float *vec, float scale)
{
	dst[0] = vec[0] / scale;
	dst[1] = vec[1] / scale;
	dst[2] = vec[2] / scale;
}


__inline void Vector3Recip (float *dst, const float *vec, const float *scale)
{
	dst[0] = vec[0] / scale[0];
	dst[1] = vec[1] / scale[1];
	dst[2] = vec[2] / scale[2];
}


__inline void Vector4Recipf (float *dst, const float *vec, float scale)
{
	dst[0] = vec[0] / scale;
	dst[1] = vec[1] / scale;
	dst[2] = vec[2] / scale;
	dst[3] = vec[3] / scale;
}


__inline void Vector4Recip (float *dst, const float *vec, const float *scale)
{
	dst[0] = vec[0] / scale[0];
	dst[1] = vec[1] / scale[1];
	dst[2] = vec[2] / scale[2];
	dst[3] = vec[3] / scale[3];
}


__inline void Vector2Copy (float *dst, const float *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
}


__inline void Vector3Copy (float *dst, const float *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
}


__inline void Vector4Copy (float *dst, const float *src)
{
	dst[0] = src[0];
	dst[1] = src[1];
	dst[2] = src[2];
	dst[3] = src[3];
}


__inline void Vector2Addf (float *dst, const float *vec1, float add)
{
	dst[0] = vec1[0] + add;
	dst[1] = vec1[1] + add;
}


__inline void Vector2Add (float *dst, const float *vec1, const float *vec2)
{
	dst[0] = vec1[0] + vec2[0];
	dst[1] = vec1[1] + vec2[1];
}


__inline void Vector2Subtractf (float *dst, const float *vec1, float sub)
{
	dst[0] = vec1[0] - sub;
	dst[1] = vec1[1] - sub;
}


__inline void Vector2Subtract (float *dst, const float *vec1, const float *vec2)
{
	dst[0] = vec1[0] - vec2[0];
	dst[1] = vec1[1] - vec2[1];
}


__inline void Vector3Addf (float *dst, const float *vec1, float add)
{
	dst[0] = vec1[0] + add;
	dst[1] = vec1[1] + add;
	dst[2] = vec1[2] + add;
}


__inline void Vector3Add (float *dst, const float *vec1, const float *vec2)
{
	dst[0] = vec1[0] + vec2[0];
	dst[1] = vec1[1] + vec2[1];
	dst[2] = vec1[2] + vec2[2];
}


__inline void Vector3Subtractf (float *dst, const float *vec1, float sub)
{
	dst[0] = vec1[0] - sub;
	dst[1] = vec1[1] - sub;
	dst[2] = vec1[2] - sub;
}


__inline void Vector3Subtract (float *dst, const float *vec1, const float *vec2)
{
	dst[0] = vec1[0] - vec2[0];
	dst[1] = vec1[1] - vec2[1];
	dst[2] = vec1[2] - vec2[2];
}


__inline void Vector4Addf (float *dst, const float *vec1, float add)
{
	dst[0] = vec1[0] + add;
	dst[1] = vec1[1] + add;
	dst[2] = vec1[2] + add;
	dst[3] = vec1[3] + add;
}


__inline void Vector4Add (float *dst, const float *vec1, const float *vec2)
{
	dst[0] = vec1[0] + vec2[0];
	dst[1] = vec1[1] + vec2[1];
	dst[2] = vec1[2] + vec2[2];
	dst[3] = vec1[3] + vec2[3];
}


__inline void Vector4Subtractf (float *dst, const float *vec1, float sub)
{
	dst[0] = vec1[0] - sub;
	dst[1] = vec1[1] - sub;
	dst[2] = vec1[2] - sub;
	dst[3] = vec1[3] - sub;
}


__inline void Vector4Subtract (float *dst, const float *vec1, const float *vec2)
{
	dst[0] = vec1[0] - vec2[0];
	dst[1] = vec1[1] - vec2[1];
	dst[2] = vec1[2] - vec2[2];
	dst[3] = vec1[3] - vec2[3];
}


__inline float Vector2Dot (const float *x, const float *y)
{
	// http://forums.inside3d.com/viewtopic.php?f=12&t=5516&start=21
	return (float) (((double) x[0] * (double) y[0]) + ((double) x[1] * (double) y[1]));
}


__inline float Vector3Dot (const float *x, const float *y)
{
	// http://forums.inside3d.com/viewtopic.php?f=12&t=5516&start=21
	return (float) (((double) x[0] * (double) y[0]) + ((double) x[1] * (double) y[1]) + ((double) x[2] * (double) y[2]));
}


__inline float Vector4Dot (const float *x, const float *y)
{
	// http://forums.inside3d.com/viewtopic.php?f=12&t=5516&start=21
	return (float) (((double) x[0] * (double) y[0]) + ((double) x[1] * (double) y[1]) + ((double) x[2] * (double) y[2]) + ((double) x[3] * (double) y[3]));
}


__inline void Vector2Lerpf (float *dst, const float *l1, const float *l2, float b)
{
	dst[0] = l1[0] + (l2[0] - l1[0]) * b;
	dst[1] = l1[1] + (l2[1] - l1[1]) * b;
}


__inline void Vector3Lerpf (float *dst, const float *l1, const float *l2, float b)
{
	dst[0] = l1[0] + (l2[0] - l1[0]) * b;
	dst[1] = l1[1] + (l2[1] - l1[1]) * b;
	dst[2] = l1[2] + (l2[2] - l1[2]) * b;
}


__inline void Vector4Lerpf (float *dst, const float *l1, const float *l2, float b)
{
	dst[0] = l1[0] + (l2[0] - l1[0]) * b;
	dst[1] = l1[1] + (l2[1] - l1[1]) * b;
	dst[2] = l1[2] + (l2[2] - l1[2]) * b;
	dst[3] = l1[3] + (l2[3] - l1[3]) * b;
}


__inline void Vector2Lerp (float *dst, const float *l1, const float *l2, const float *b)
{
	dst[0] = l1[0] + (l2[0] - l1[0]) * b[0];
	dst[1] = l1[1] + (l2[1] - l1[1]) * b[1];
}


__inline void Vector3Lerp (float *dst, const float *l1, const float *l2, const float *b)
{
	dst[0] = l1[0] + (l2[0] - l1[0]) * b[0];
	dst[1] = l1[1] + (l2[1] - l1[1]) * b[1];
	dst[2] = l1[2] + (l2[2] - l1[2]) * b[2];
}


__inline void Vector4Lerp (float *dst, const float *l1, const float *l2, const float *b)
{
	dst[0] = l1[0] + (l2[0] - l1[0]) * b[0];
	dst[1] = l1[1] + (l2[1] - l1[1]) * b[1];
	dst[2] = l1[2] + (l2[2] - l1[2]) * b[2];
	dst[3] = l1[3] + (l2[3] - l1[3]) * b[3];
}


__inline void Vector2Set (float *vec, float x, float y)
{
	vec[0] = x;
	vec[1] = y;
}


__inline void Vector3Set (float *vec, float x, float y, float z)
{
	vec[0] = x;
	vec[1] = y;
	vec[2] = z;
}


__inline void Vector4Set (float *vec, float x, float y, float z, float w)
{
	vec[0] = x;
	vec[1] = y;
	vec[2] = z;
	vec[3] = w;
}


__inline void Vector2Clear (float *vec)
{
	vec[0] = vec[1] = 0.0f;
}


__inline void Vector3Clear (float *vec)
{
	vec[0] = vec[1] = vec[2] = 0.0f;
}


__inline void Vector4Clear (float *vec)
{
	vec[0] = vec[1] = vec[2] = vec[3] = 0.0f;
}


__inline void Vector2Clamp (float *vec, float clmp)
{
	if (vec[0] > clmp) vec[0] = clmp;
	if (vec[1] > clmp) vec[1] = clmp;
}


__inline void Vector3Clamp (float *vec, float clmp)
{
	if (vec[0] > clmp) vec[0] = clmp;
	if (vec[1] > clmp) vec[1] = clmp;
	if (vec[2] > clmp) vec[2] = clmp;
}


__inline void Vector4Clamp (float *vec, float clmp)
{
	if (vec[0] > clmp) vec[0] = clmp;
	if (vec[1] > clmp) vec[1] = clmp;
	if (vec[2] > clmp) vec[2] = clmp;
	if (vec[3] > clmp) vec[3] = clmp;
}


__inline void Vector2Cross (float *cross, const float *v1, const float *v2)
{
	// Sys_Error ("Just what do you think you're doing, Dave?");
}


__inline void Vector3Cross (float *cross, const float *v1, const float *v2)
{
	cross[0] = v1[1] * v2[2] - v1[2] * v2[1];
	cross[1] = v1[2] * v2[0] - v1[0] * v2[2];
	cross[2] = v1[0] * v2[1] - v1[1] * v2[0];
}


__inline void Vector4Cross (float *cross, const float *v1, const float *v2)
{
	// Sys_Error ("Just what do you think you're doing, Dave?");
}


__inline float Vector2Length (const float *v)
{
	return SafeSqrt (Vector2Dot (v, v));
}


__inline float Vector3Length (const float *v)
{
	return SafeSqrt (Vector3Dot (v, v));
}


__inline float Vector4Length (const float *v)
{
	return SafeSqrt (Vector4Dot (v, v));
}


__inline float Vector2Normalize (float *v)
{
	float length = Vector2Dot (v, v);

	if ((length = SafeSqrt (length)) > 0)
	{
		float ilength = 1 / length;

		v[0] *= ilength;
		v[1] *= ilength;
	}

	return length;
}


__inline float Vector3Normalize (float *v)
{
	float length = Vector3Dot (v, v);

	if ((length = SafeSqrt (length)) > 0)
	{
		float ilength = 1 / length;

		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
	}

	return length;
}


__inline float Vector4Normalize (float *v)
{
	float length = Vector4Dot (v, v);

	if ((length = SafeSqrt (length)) > 0)
	{
		float ilength = 1 / length;

		v[0] *= ilength;
		v[1] *= ilength;
		v[2] *= ilength;
		v[3] *= ilength;
	}

	return length;
}


__inline qboolean Vector2Compare (const float *v1, const float *v2)
{
	if (v1[0] != v2[0]) return false;
	if (v1[1] != v2[1]) return false;

	return true;
}


__inline qboolean Vector3Compare (const float *v1, const float *v2)
{
	if (v1[0] != v2[0]) return false;
	if (v1[1] != v2[1]) return false;
	if (v1[2] != v2[2]) return false;

	return true;
}


__inline qboolean Vector4Compare (const float *v1, const float *v2)
{
	if (v1[0] != v2[0]) return false;
	if (v1[1] != v2[1]) return false;
	if (v1[2] != v2[2]) return false;
	if (v1[3] != v2[3]) return false;

	return true;
}


#endif	/* __MATHLIB_H */

