#ifndef ATC_INTERNAL_H
#define ATC_INTERNAL_H

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdbool.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ATC_SATURATION (75.0f / 255.0f)

#if defined(__aarch64__) || defined(_M_ARM64) || defined(__x86_64__) || defined(_M_X64) || defined(__SSE2__)
#define ATC_USE_M4X 1
#else
#define ATC_USE_M4X 0
#endif

#if defined(_MSC_VER)
#define ATC_ALIGN(N) __declspec(align(N))
#else
#define ATC_ALIGN(N) __attribute__((aligned(N)))
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#define ATC_RESTRICT restrict
#else
#define ATC_RESTRICT
#endif

#ifndef ATC_ASSERT
#include <assert.h>
#define ATC_ASSERT(x) assert(x)
#endif

#define ATC_UNUSED(x) ((void)(x))

#define ATC_MIN(a,b) ((a) < (b) ? (a) : (b))
#define ATC_MAX(a,b) ((a) > (b) ? (a) : (b))
#define ATC_CLAMP(v,lo,hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

typedef uint8_t uint8;
typedef int8_t sint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef int16_t sint16;
typedef int32_t sint32;
typedef int64_t sint64;

typedef struct ATC_Vec2 {
	float x;
	float y;
} ATC_Vec2;

typedef struct ATC_Vec3 {
	float x;
	float y;
	float z;
} ATC_Vec3;

typedef struct ATC_Mat2 {
	ATC_Vec2 x;
	ATC_Vec2 y;
} ATC_Mat2;

typedef struct ATC_Mat3 {
	ATC_Vec3 x;
	ATC_Vec3 y;
	ATC_Vec3 z;
} ATC_Mat3;

static inline ATC_Vec2 atc_vec2(float x, float y) {
	ATC_Vec2 v = { x, y };
	return v;
}

static inline ATC_Vec3 atc_vec3(float x, float y, float z) {
	ATC_Vec3 v = { x, y, z };
	return v;
}

static inline ATC_Vec3 atc_vec3_add(ATC_Vec3 a, ATC_Vec3 b) {
	return atc_vec3(a.x + b.x, a.y + b.y, a.z + b.z);
}

static inline ATC_Vec3 atc_vec3_sub(ATC_Vec3 a, ATC_Vec3 b) {
	return atc_vec3(a.x - b.x, a.y - b.y, a.z - b.z);
}

static inline ATC_Vec3 atc_vec3_mul(ATC_Vec3 a, ATC_Vec3 b) {
	return atc_vec3(a.x * b.x, a.y * b.y, a.z * b.z);
}

static inline ATC_Vec3 atc_vec3_mul_scalar(ATC_Vec3 a, float s) {
	return atc_vec3(a.x * s, a.y * s, a.z * s);
}

static inline ATC_Vec3 atc_vec3_div_scalar(ATC_Vec3 a, float s) {
	return atc_vec3(a.x / s, a.y / s, a.z / s);
}

static inline ATC_Vec3 atc_vec3_max(ATC_Vec3 a, ATC_Vec3 b) {
	return atc_vec3(ATC_MAX(a.x, b.x), ATC_MAX(a.y, b.y), ATC_MAX(a.z, b.z));
}

static inline ATC_Vec3 atc_vec3_min(ATC_Vec3 a, ATC_Vec3 b) {
	return atc_vec3(ATC_MIN(a.x, b.x), ATC_MIN(a.y, b.y), ATC_MIN(a.z, b.z));
}

static inline ATC_Vec3 atc_vec3_max0(ATC_Vec3 a) {
	return atc_vec3(ATC_MAX(a.x, 0.0f), ATC_MAX(a.y, 0.0f), ATC_MAX(a.z, 0.0f));
}

static inline ATC_Vec3 atc_vec3_pow(ATC_Vec3 a, float p) {
	return atc_vec3(powf(a.x, p), powf(a.y, p), powf(a.z, p));
}

static inline float atc_vec2_dot(ATC_Vec2 a, ATC_Vec2 b) {
	return a.x * b.x + a.y * b.y;
}

static inline float atc_vec3_dot(ATC_Vec3 a, ATC_Vec3 b) {
	return a.x * b.x + a.y * b.y + a.z * b.z;
}

static inline ATC_Mat2 atc_mat2_rotation(float angle) {
	const float s = sinf(angle);
	const float c = cosf(angle);
	ATC_Mat2 m = { { c, s }, { -s, c } };
	return m;
}

static inline ATC_Vec2 atc_mat2_mul_vec2(ATC_Mat2 m, ATC_Vec2 v) {
	ATC_Vec2 r = {
		m.x.x * v.x + m.y.x * v.y,
		m.x.y * v.x + m.y.y * v.y
	};
	return r;
}

static inline ATC_Vec3 atc_vec3_mul_mat3(ATC_Vec3 v, ATC_Mat3 m) {
	// Match vdfloat3 * vdfloat3x3 in artifacting-lib (row vector * matrix).
	return atc_vec3(
		v.x * m.x.x + v.y * m.y.x + v.z * m.z.x,
		v.x * m.x.y + v.y * m.y.y + v.z * m.z.y,
		v.x * m.x.z + v.y * m.y.z + v.z * m.z.z);
}

static inline ATC_Mat3 atc_mat3_mul(ATC_Mat3 a, ATC_Mat3 b) {
	ATC_Mat3 r;
	r.x = atc_vec3_mul_mat3(a.x, b);
	r.y = atc_vec3_mul_mat3(a.y, b);
	r.z = atc_vec3_mul_mat3(a.z, b);
	return r;
}

static inline ATC_Mat3 atc_mat3_transpose(ATC_Mat3 m) {
	ATC_Mat3 r = {
		{ m.x.x, m.y.x, m.z.x },
		{ m.x.y, m.y.y, m.z.y },
		{ m.x.z, m.y.z, m.z.z }
	};
	return r;
}

static inline uint32 atc_pack_rgb(ATC_Vec3 c) {
	int r = (int)floorf(c.x + 0.5f);
	int g = (int)floorf(c.y + 0.5f);
	int b = (int)floorf(c.z + 0.5f);
	r = ATC_CLAMP(r, 0, 255);
	g = ATC_CLAMP(g, 0, 255);
	b = ATC_CLAMP(b, 0, 255);
	return ((uint32)r << 16) | ((uint32)g << 8) | (uint32)b;
}

static inline ATC_Vec3 atc_rgb8_to_vec3(uint32 rgb) {
	return atc_vec3(
		(float)((rgb >> 16) & 0xFF) / 255.0f,
		(float)((rgb >> 8) & 0xFF) / 255.0f,
		(float)(rgb & 0xFF) / 255.0f);
}

static inline ATC_Vec3 atc_srgb_to_linear(ATC_Vec3 c) {
	ATC_Vec3 out;
	float v;
	v = c.x;
	out.x = (v < 0.04045f) ? (v / 12.92f) : powf((v + 0.055f) / 1.055f, 2.4f);
	v = c.y;
	out.y = (v < 0.04045f) ? (v / 12.92f) : powf((v + 0.055f) / 1.055f, 2.4f);
	v = c.z;
	out.z = (v < 0.04045f) ? (v / 12.92f) : powf((v + 0.055f) / 1.055f, 2.4f);
	return out;
}

static inline ATC_Vec3 atc_linear_to_srgb(ATC_Vec3 c) {
	ATC_Vec3 out;
	float v;
	v = c.x;
	out.x = (v < 0.0031308f) ? (v * 12.92f) : (1.055f * powf(v, 1.0f / 2.4f) - 0.055f);
	v = c.y;
	out.y = (v < 0.0031308f) ? (v * 12.92f) : (1.055f * powf(v, 1.0f / 2.4f) - 0.055f);
	v = c.z;
	out.z = (v < 0.0031308f) ? (v * 12.92f) : (1.055f * powf(v, 1.0f / 2.4f) - 0.055f);
	return out;
}

static inline uint32 atc_float_to_bits(float v) {
	uint32 out = 0;
	memcpy(&out, &v, sizeof(out));
	return out;
}

static inline float atc_bits_to_float(uint32 v) {
	float out = 0.0f;
	memcpy(&out, &v, sizeof(out));
	return out;
}

static inline sint32 atc_round_to_int32(float x) {
	float fx = floorf(x);
	float frac = x - fx;
	if (frac > 0.5f)
		return (sint32)(fx + 1.0f);
	if (frac < 0.5f)
		return (sint32)fx;
	return (((sint32)fx) & 1) ? (sint32)(fx + 1.0f) : (sint32)fx;
}

static inline int atc_round_to_int(float x) {
	return (int)atc_round_to_int32(x);
}

static inline void atc_memset32(void *dst, uint32 value, size_t count) {
	uint32 *d = (uint32 *)dst;
	for (size_t i = 0; i < count; ++i)
		d[i] = value;
}

#endif
