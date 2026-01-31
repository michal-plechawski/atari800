#include "atc_internal.h"
#include "artifacting_filters.h"
#include "palettegenerator.h"
#include "gtiatables.h"
#include "artifacting_c.h"

// Forward declarations from artifacting_pal_scalar.c
void atc_artifact_pal_luma(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
void atc_artifact_pal_chroma(uint32 *dst, const uint8 *src, uint32 n, const uint32 *kernels);
void atc_artifact_pal_final(uint32 *dst, const uint32 *ybuf, const uint32 *ubuf, const uint32 *vbuf, uint32 *ulbuf, uint32 *vlbuf, uint32 n);
void atc_artifact_pal_final_mono(uint32 *dst, const uint32 *ybuf, uint32 n, const uint32 *mono_table);
void atc_artifact_pal32(uint32 *dst, uint8 *delay_line, uint32 n, int compress_extended_range);

struct ATC_ArtifactingEngine {
	bool mb_pal;
	bool mb_high_ntsc_tables_inited;
	bool mb_high_pal_tables_inited;
	bool mb_high_tables_signed;
	bool mb_active_tables_inited;
	bool mb_active_tables_signed;
	bool mb_chroma_artifacts;
	bool mb_chroma_artifacts_hi;
	bool mb_blend_active;
	bool mb_blend_copy;
	bool mb_blend_linear;
	bool mb_blend_mono_persistence;
	bool mb_scanline_delay_valid;
	bool mb_gamma_identity;
	bool mb_enable_color_correction;
	bool mb_bypass_output_correction;
	bool mb_expanded_range_input;
	bool mb_expanded_range_output;
	bool mb_deinterlacing;

	bool mb_saved_pal;
	bool mb_saved_chroma_artifacts;
	bool mb_saved_chroma_artifacts_hi;
	bool mb_saved_bypass_output_correction;
	bool mb_saved_blend_active;
	bool mb_saved_blend_copy;
	bool mb_saved_blend_linear;
	bool mb_saved_expanded_range_input;
	bool mb_saved_expanded_range_output;

	float m_mono_persistence_f1;
	float m_mono_persistence_f2;
	float m_mono_persistence_limit;

	bool mb_tint_color_enabled;
	ATC_Vec3 m_raw_tint_color;
	ATC_Vec3 m_tint_color;

	ATC_Mat3 m_color_matching_matrix;
	sint16 m_color_matching_matrix16[3][3];

	ATC_ColorParams m_color_params;
	int m_monitor_mode;
	int m_pal_phase;
	bool mb_color_tables_mono_persistence;

	ATC_ArtifactingParams m_artifacting_params;

	ATC_ALIGN(16) sint16 m_chroma_vectors[16][4];
	ATC_ALIGN(16) sint16 m_luma_ramp[16];
	ATC_ALIGN(16) sint16 m_artifact_ramp[31][4];

	uint8 m_gamma_table[256];
	sint16 m_correct_linear_table[256];
	uint8 m_correct_gamma_table[1024];

	uint32 m_palette[256];
	uint32 m_signed_palette[256];
	uint32 m_corrected_palette[256];
	uint32 m_corrected_signed_palette[256];
	uint32 m_mono_table[256];
	uint32 m_mono_table2[1024];

	uint32 m_active_palette[256];
	ATC_ALIGN(16) sint16 m_active_chroma_vectors[16][4];
	ATC_ALIGN(16) sint16 m_active_luma_ramp[16];
	ATC_ALIGN(16) sint16 m_active_artifact_ramp[31][4];

	union {
		uint8 m_pal_delay_line[ATC_ARTIFACTING_N];
		uint8 m_pal_delay_line32[ATC_ARTIFACTING_N * 2 * 4];
		ATC_ALIGN(16) uint32 m_pal_delay_line_uv[2][ATC_ARTIFACTING_N];
	};

	union {
		uint32 m_prev_frame_7mhz[ATC_ARTIFACTING_M * 2][ATC_ARTIFACTING_N];
		uint32 m_prev_frame_14mhz[ATC_ARTIFACTING_M * 2][ATC_ARTIFACTING_N * 2];
	};

	uint32 m_deinterlace_delay_line[ATC_ARTIFACTING_M * 2][ATC_ARTIFACTING_N * 2];

	struct {
		ATC_ALIGN(8) uint32 m_pal_to_r[256][2][12];
		ATC_ALIGN(8) uint32 m_pal_to_g[256][2][12];
		ATC_ALIGN(8) uint32 m_pal_to_b[256][2][12];
		ATC_ALIGN(8) uint32 m_pal_to_r_twin[256][12];
		ATC_ALIGN(8) uint32 m_pal_to_g_twin[256][12];
		ATC_ALIGN(8) uint32 m_pal_to_b_twin[256][12];
	} m2x;

	struct {
		ATC_ALIGN(16) uint32 m_pal_to_r[256][4][16];
		ATC_ALIGN(16) uint32 m_pal_to_r_twin[256][2][16];
		ATC_ALIGN(16) uint32 m_pal_to_r_quad[256][16];

		ATC_ALIGN(16) uint32 m_pal_to_g[256][4][16];
		ATC_ALIGN(16) uint32 m_pal_to_g_twin[256][2][16];
		ATC_ALIGN(16) uint32 m_pal_to_g_quad[256][16];

		ATC_ALIGN(16) uint32 m_pal_to_b[256][4][16];
		ATC_ALIGN(16) uint32 m_pal_to_b_twin[256][2][16];
		ATC_ALIGN(16) uint32 m_pal_to_b_quad[256][16];
	} m4x;

	struct {
		uint32 m_pal_to_y[2][256][8][4];
		uint32 m_pal_to_u[2][256][8][12];
		uint32 m_pal_to_v[2][256][8][12];
	} m_pal2x;
};

enum {
	ATC_LEFT_BORDER_7MHZ = 34 * 2,
	ATC_RIGHT_BORDER_7MHZ = 222 * 2,
	ATC_LEFT_BORDER_14MHZ = ATC_LEFT_BORDER_7MHZ * 2,
	ATC_RIGHT_BORDER_14MHZ = ATC_RIGHT_BORDER_7MHZ * 2,
	ATC_LEFT_BORDER_7MHZ_4 = ATC_LEFT_BORDER_7MHZ & ~3,
	ATC_RIGHT_BORDER_7MHZ_4 = (ATC_RIGHT_BORDER_7MHZ + 3) & ~3,
	ATC_LEFT_BORDER_14MHZ_4 = ATC_LEFT_BORDER_14MHZ & ~3,
	ATC_RIGHT_BORDER_14MHZ_4 = (ATC_RIGHT_BORDER_14MHZ + 3) & ~3
};

static const float ATC_PERSISTENCE_TC1 = 0.0f;
static const float ATC_PERSISTENCE_TC2 = 0.247f;

static float atc_u32_to_float(uint32 v) {
	float f;
	memcpy(&f, &v, sizeof(f));
	return f;
}

static uint32 atc_float_to_u32(float v) {
	uint32 u;
	memcpy(&u, &v, sizeof(u));
	return u;
}

static void atc_gamma_correct(uint8 *dst8, uint32 pixels, const uint8 *gamma_tab) {
	for (uint32 i = 0; i < pixels; ++i) {
		dst8[0] = gamma_tab[dst8[0]];
		dst8[1] = gamma_tab[dst8[1]];
		dst8[2] = gamma_tab[dst8[2]];
		dst8 += 4;
	}
}

static void atc_rotate(float *xr, float *yr, float cs, float sn) {
	float x0 = *xr;
	float y0 = *yr;
	*xr = x0 * cs + y0 * sn;
	*yr = -x0 * sn + y0 * cs;
}

static void atc_rotate2(float *xr, float *yr, float angle) {
	float sn = sinf(angle);
	float cs = cosf(angle);
	atc_rotate(xr, yr, cs, sn);
}

static ATC_Vec3 atc_clip_linear_color_to_srgb_local(ATC_Vec3 c) {
	const ATC_Vec3 luma_axis = atc_vec3(0.2126f, 0.7152f, 0.0722f);
	float luma = atc_vec3_dot(c, luma_axis);
	ATC_Vec3 luma_vec = atc_vec3(luma, luma, luma);
	ATC_Vec3 chroma = atc_vec3_sub(c, luma_vec);
	float scale = ATC_MAX(0.0f, ATC_MIN(1.0f, 2.0f * (1.0f - luma)));
	ATC_Vec3 out = atc_vec3_add(luma_vec, atc_vec3_mul_scalar(chroma, scale));
	return atc_linear_to_srgb(out);
}

static inline uint32 atc_twin_add(uint32 x, uint32 y) {
	return (x & 0x7FFF7FFFu) + (y & 0x7FFF7FFFu) ^ ((x ^ y) & 0x80008000u);
}

static void atc_recompute_pal_tables(ATC_ArtifactingEngine *e) {
	const ATC_ColorParams *params = &e->m_color_params;
	const bool signed_output = e->mb_expanded_range_output;

	e->mb_high_ntsc_tables_inited = false;
	e->mb_high_pal_tables_inited = true;
	e->mb_high_tables_signed = signed_output;

	float luma_ramp[16];
	atc_compute_luma_ramp(params->mLumaRampMode, luma_ramp);

	const float sat2 = params->mArtifactSat;
	const float sat1 = params->mSaturation / ATC_MAX(0.001f, sat2);
	const float chroma_phase_step = -params->mHueRange * ((2.0f * (float)M_PI / 360.0f) / 15.0f);

	const float co_vr = 1.1402509f;
	const float co_ub = 2.0325203f;

	float utab[2][16] = {0};
	float vtab[2][16] = {0};
	float ytab[16] = {0};

	float *utab0 = utab[e->m_pal_phase & 1];
	float *vtab0 = vtab[e->m_pal_phase & 1];
	float *utab1 = utab[1 - (e->m_pal_phase & 1)];
	float *vtab1 = vtab[1 - (e->m_pal_phase & 1)];

	utab0[0] = 0.0f;
	utab1[0] = 0.0f;
	vtab0[0] = 0.0f;
	vtab1[0] = 0.0f;

	const float chroma_phase_offset = (123.0f - params->mHueStart) * (2.0f * (float)M_PI / 360.0f);
	for (int i = 0; i < 15; ++i) {
		const ATC_PALPhaseInfo *info = &atc_pal_phase_lookup[i];
		float t1 = chroma_phase_offset + info->even_phase * chroma_phase_step;
		float t2 = chroma_phase_offset + info->odd_phase * chroma_phase_step;

		utab0[i + 1] = -cosf(t1) * info->even_invert;
		vtab0[i + 1] = sinf(t1) * info->even_invert;
		utab1[i + 1] = -cosf(t2) * info->odd_invert;
		vtab1[i + 1] = -sinf(t2) * info->odd_invert;
	}

	for (int i = 0; i < 16; ++i)
		ytab[i] = params->mBrightness + params->mContrast * luma_ramp[i];

	ATC_FilterKernel kernbase;
	ATC_FilterKernel kerncfilt;
	ATC_FilterKernel kernumod;
	ATC_FilterKernel kernvmod;
	ATC_FilterKernel kernysep;
	ATC_FilterKernel kerncsep;
	ATC_FilterKernel kerncdemod;

	atc_filter_kernel_init(&kernbase);
	atc_filter_kernel_init(&kerncfilt);
	atc_filter_kernel_init(&kernumod);
	atc_filter_kernel_init(&kernvmod);
	atc_filter_kernel_init(&kernysep);
	atc_filter_kernel_init(&kerncsep);
	atc_filter_kernel_init(&kerncdemod);

	kernbase.offset = 0;
	atc_filter_kernel_resize(&kernbase, 5);
	for (int i = 0; i < 5; ++i)
		kernbase.coeffs[i] = 1.0f;
	atc_filter_kernel_scale(&kernbase, params->mIntensityScale);

	kerncfilt.offset = -5;
	atc_filter_kernel_resize(&kerncfilt, 11);
	kerncfilt.coeffs[0] = 1.0f / 1024.0f;
	kerncfilt.coeffs[1] = 10.0f / 1024.0f;
	kerncfilt.coeffs[2] = 45.0f / 1024.0f;
	kerncfilt.coeffs[3] = 120.0f / 1024.0f;
	kerncfilt.coeffs[4] = 210.0f / 1024.0f;
	kerncfilt.coeffs[5] = 252.0f / 1024.0f;
	kerncfilt.coeffs[6] = 210.0f / 1024.0f;
	kerncfilt.coeffs[7] = 120.0f / 1024.0f;
	kerncfilt.coeffs[8] = 45.0f / 1024.0f;
	kerncfilt.coeffs[9] = 10.0f / 1024.0f;
	kerncfilt.coeffs[10] = 1.0f / 1024.0f;

	const float ivrt2 = 0.70710678118654752440f;
	kernumod.offset = 0;
	atc_filter_kernel_resize(&kernumod, 8);
	kernumod.coeffs[0] = 1.0f;
	kernumod.coeffs[1] = ivrt2;
	kernumod.coeffs[2] = 0.0f;
	kernumod.coeffs[3] = -ivrt2;
	kernumod.coeffs[4] = -1.0f;
	kernumod.coeffs[5] = -ivrt2;
	kernumod.coeffs[6] = 0.0f;
	kernumod.coeffs[7] = ivrt2;
	atc_filter_kernel_scale(&kernumod, sat1);

	kernvmod.offset = 0;
	atc_filter_kernel_resize(&kernvmod, 8);
	kernvmod.coeffs[0] = 0.0f;
	kernvmod.coeffs[1] = ivrt2;
	kernvmod.coeffs[2] = 1.0f;
	kernvmod.coeffs[3] = ivrt2;
	kernvmod.coeffs[4] = 0.0f;
	kernvmod.coeffs[5] = -ivrt2;
	kernvmod.coeffs[6] = -1.0f;
	kernvmod.coeffs[7] = -ivrt2;
	atc_filter_kernel_scale(&kernvmod, sat1);

	kernysep.offset = -4;
	atc_filter_kernel_resize(&kernysep, 9);
	kernysep.coeffs[0] = 0.5f / 8.0f;
	for (int i = 1; i < 8; ++i)
		kernysep.coeffs[i] = 1.0f / 8.0f;
	kernysep.coeffs[8] = 0.5f / 8.0f;

	kerncsep.offset = -16;
	atc_filter_kernel_resize(&kerncsep, 33);
	{
		float coeffs[33] = {
			1,0,0,0,-2,0,0,0,2,0,0,0,-2,0,0,0,2,0,0,0,-2,0,0,0,2,0,0,0,-2,0,0,0,1
		};
		for (int i = 0; i < 33; ++i)
			kerncsep.coeffs[i] = coeffs[i] * (1.0f / 16.0f);
	}

	kerncdemod.offset = 0;
	atc_filter_kernel_resize(&kerncdemod, 8);
	kerncdemod.coeffs[0] = -1.0f;
	kerncdemod.coeffs[1] = -1.0f;
	kerncdemod.coeffs[2] = 1.0f;
	kerncdemod.coeffs[3] = 1.0f;
	kerncdemod.coeffs[4] = 1.0f;
	kerncdemod.coeffs[5] = 1.0f;
	kerncdemod.coeffs[6] = -1.0f;
	kerncdemod.coeffs[7] = -1.0f;
	atc_filter_kernel_scale(&kerncdemod, sat2 * 0.5f);

	const float ycphase = (params->mArtifactHue + 90.0f) * (float)M_PI / 180.0f;
	const float ycphasec = cosf(ycphase);
	const float ycphases = sinf(ycphase);

	ATC_FilterKernel kerny2y[8];
	ATC_FilterKernel kernu2y[8];
	ATC_FilterKernel kernv2y[8];
	ATC_FilterKernel kerny2u[8];
	ATC_FilterKernel kernu2u[8];
	ATC_FilterKernel kernv2u[8];
	ATC_FilterKernel kerny2v[8];
	ATC_FilterKernel kernu2v[8];
	ATC_FilterKernel kernv2v[8];

	for (int i = 0; i < 8; ++i) {
		atc_filter_kernel_init(&kerny2y[i]);
		atc_filter_kernel_init(&kernu2y[i]);
		atc_filter_kernel_init(&kernv2y[i]);
		atc_filter_kernel_init(&kerny2u[i]);
		atc_filter_kernel_init(&kernu2u[i]);
		atc_filter_kernel_init(&kernv2u[i]);
		atc_filter_kernel_init(&kerny2v[i]);
		atc_filter_kernel_init(&kernu2v[i]);
		atc_filter_kernel_init(&kernv2v[i]);
	}

	for (int phase = 0; phase < 8; ++phase) {
		ATC_FilterKernel kernbase2;
		ATC_FilterKernel kernsignaly;
		ATC_FilterKernel kernsignalu;
		ATC_FilterKernel kernsignalv;
		ATC_FilterKernel u;
		ATC_FilterKernel v;
		atc_filter_kernel_init(&kernbase2);
		atc_filter_kernel_init(&kernsignaly);
		atc_filter_kernel_init(&kernsignalu);
		atc_filter_kernel_init(&kernsignalv);
		atc_filter_kernel_init(&u);
		atc_filter_kernel_init(&v);

		atc_filter_kernel_shift(&kernbase2, &kernbase, 5 * phase);
		atc_filter_kernel_shift(&kernsignaly, &kernbase2, 0);

		ATC_FilterKernel tmp1;
		atc_filter_kernel_init(&tmp1);
		atc_filter_kernel_convolve(&tmp1, &kernbase2, &kerncfilt);
		atc_filter_kernel_modulate(&kernsignalu, &tmp1, &kernumod);
		atc_filter_kernel_modulate(&kernsignalv, &tmp1, &kernvmod);

		ATC_FilterKernel tmpy;
		atc_filter_kernel_init(&tmpy);
		atc_filter_kernel_convolve(&tmpy, &kernsignaly, &kernysep);
		atc_filter_kernel_sample_bicubic(&kerny2y[phase], &tmpy, 0.0f, 2.5f, -0.75f);

		ATC_FilterKernel tmpu;
		atc_filter_kernel_init(&tmpu);
		atc_filter_kernel_convolve(&tmpu, &kernsignalu, &kernysep);
		atc_filter_kernel_sample_bicubic(&kernu2y[phase], &tmpu, 0.0f, 2.5f, -0.75f);

		ATC_FilterKernel tmpv;
		atc_filter_kernel_init(&tmpv);
		atc_filter_kernel_convolve(&tmpv, &kernsignalv, &kernysep);
		atc_filter_kernel_sample_bicubic(&kernv2y[phase], &tmpv, 0.0f, 2.5f, -0.75f);

		ATC_FilterKernel tmpa;
		ATC_FilterKernel tmpb;
		atc_filter_kernel_init(&tmpa);
		atc_filter_kernel_init(&tmpb);

		atc_filter_kernel_convolve(&tmpa, &kernsignaly, &kerncsep);
		atc_filter_kernel_modulate(&tmpa, &tmpa, &kerncdemod);
		atc_filter_kernel_sample_point(&tmpb, &tmpa, 0, 4);
		atc_filter_kernel_sample_bicubic(&u, &tmpb, 0.0f, 0.625f, -0.75f);

		atc_filter_kernel_convolve(&tmpa, &kernsignalu, &kerncsep);
		atc_filter_kernel_modulate(&tmpa, &tmpa, &kerncdemod);
		atc_filter_kernel_sample_point(&tmpb, &tmpa, 0, 4);
		atc_filter_kernel_sample_bicubic(&kernu2u[phase], &tmpb, 0.0f, 0.625f, -0.75f);

		atc_filter_kernel_convolve(&tmpa, &kernsignalv, &kerncsep);
		atc_filter_kernel_modulate(&tmpa, &tmpa, &kerncdemod);
		atc_filter_kernel_sample_point(&tmpb, &tmpa, 0, 4);
		atc_filter_kernel_sample_bicubic(&kernv2u[phase], &tmpb, 0.0f, 0.625f, -0.75f);

		atc_filter_kernel_convolve(&tmpa, &kernsignaly, &kerncsep);
		atc_filter_kernel_modulate(&tmpa, &tmpa, &kerncdemod);
		atc_filter_kernel_sample_point(&tmpb, &tmpa, 2, 4);
		atc_filter_kernel_sample_bicubic(&v, &tmpb, -0.5f, 0.625f, -0.75f);

		atc_filter_kernel_convolve(&tmpa, &kernsignalu, &kerncsep);
		atc_filter_kernel_modulate(&tmpa, &tmpa, &kerncdemod);
		atc_filter_kernel_sample_point(&tmpb, &tmpa, 2, 4);
		atc_filter_kernel_sample_bicubic(&kernu2v[phase], &tmpb, -0.5f, 0.625f, -0.75f);

		atc_filter_kernel_convolve(&tmpa, &kernsignalv, &kerncsep);
		atc_filter_kernel_modulate(&tmpa, &tmpa, &kerncdemod);
		atc_filter_kernel_sample_point(&tmpb, &tmpa, 2, 4);
		atc_filter_kernel_sample_bicubic(&kernv2v[phase], &tmpb, -0.5f, 0.625f, -0.75f);

		if (e->mb_tint_color_enabled) {
			const float sensitivity_factor = 0.50f;
			ATC_FilterKernel tmpc;
			ATC_FilterKernel tmpd;
			atc_filter_kernel_init(&tmpc);
			atc_filter_kernel_init(&tmpd);

			atc_filter_kernel_scale(&kernu2y[phase], 0.0f);
			atc_filter_kernel_scale(&kernv2y[phase], 0.0f);

			atc_filter_kernel_scale(&kerny2u[phase], 0.0f);
			atc_filter_kernel_scale(&kerny2v[phase], 0.0f);
			atc_filter_kernel_scale(&kernu2u[phase], 0.0f);
			atc_filter_kernel_scale(&kernu2v[phase], 0.0f);
			atc_filter_kernel_scale(&kernv2u[phase], 0.0f);
			atc_filter_kernel_scale(&kernv2v[phase], 0.0f);

			atc_filter_kernel_convolve(&tmpc, &kernsignalu, &kernysep);
			atc_filter_kernel_scale(&tmpc, ycphasec);
			atc_filter_kernel_convolve(&tmpd, &kernsignalv, &kernysep);
			atc_filter_kernel_scale(&tmpd, ycphases);
			atc_filter_kernel_sub(&tmpc, &tmpc, &tmpd);
			atc_filter_kernel_sample_bicubic(&kernu2y[phase], &tmpc, 0.0f, 2.5f, -0.75f);
			atc_filter_kernel_scale(&kernu2y[phase], sensitivity_factor);

			atc_filter_kernel_convolve(&tmpc, &kernsignalu, &kernysep);
			atc_filter_kernel_scale(&tmpc, ycphases);
			atc_filter_kernel_convolve(&tmpd, &kernsignalv, &kernysep);
			atc_filter_kernel_scale(&tmpd, ycphasec);
			atc_filter_kernel_add(&tmpc, &tmpc, &tmpd);
			atc_filter_kernel_sample_bicubic(&kernv2y[phase], &tmpc, 0.0f, 2.5f, -0.75f);
			atc_filter_kernel_scale(&kernv2y[phase], sensitivity_factor);

			atc_filter_kernel_free(&tmpc);
			atc_filter_kernel_free(&tmpd);
		} else {
			ATC_FilterKernel u_c;
			ATC_FilterKernel v_c;
			atc_filter_kernel_init(&u_c);
			atc_filter_kernel_init(&v_c);

			atc_filter_kernel_shift(&u_c, &u, 0);
			atc_filter_kernel_shift(&v_c, &v, 0);
			atc_filter_kernel_scale(&u_c, ycphasec);
			atc_filter_kernel_scale(&v_c, ycphases);
			atc_filter_kernel_sub(&kerny2u[phase], &u_c, &v_c);

			atc_filter_kernel_shift(&u_c, &u, 0);
			atc_filter_kernel_shift(&v_c, &v, 0);
			atc_filter_kernel_scale(&u_c, ycphases);
			atc_filter_kernel_scale(&v_c, ycphasec);
			atc_filter_kernel_add(&kerny2v[phase], &u_c, &v_c);

			atc_filter_kernel_free(&u_c);
			atc_filter_kernel_free(&v_c);
		}

		atc_filter_kernel_free(&kernbase2);
		atc_filter_kernel_free(&kernsignaly);
		atc_filter_kernel_free(&kernsignalu);
		atc_filter_kernel_free(&kernsignalv);
		atc_filter_kernel_free(&u);
		atc_filter_kernel_free(&v);
		atc_filter_kernel_free(&tmp1);
		atc_filter_kernel_free(&tmpy);
		atc_filter_kernel_free(&tmpu);
		atc_filter_kernel_free(&tmpv);
		atc_filter_kernel_free(&tmpa);
		atc_filter_kernel_free(&tmpb);
	}

	for (int k = 0; k < 2; ++k) {
		float v_invert = (k != (e->m_pal_phase & 1)) ? -1.0f : 1.0f;

		for (int j = 0; j < 256; ++j) {
			float uval = utab[k][j >> 4];
			float vval = vtab[k][j >> 4];
			float yval = ytab[j & 15];

			float scale = 64.0f * 255.0f;
			if (signed_output)
				scale *= 0.5f;

			float yerror[8][2] = {{0}};
			float uerror[8][2] = {{0}};
			float verror[8][2] = {{0}};

			for (int phase = 0; phase < 8; ++phase) {
				float p2yw[8 + 8] = {0};
				float p2uw[24 + 8] = {0};
				float p2vw[24 + 8] = {0};

				int ypos = 4 - phase * 2;
				int cpos = 13 - phase * 2;

				atc_filter_kernel_accumulate_window(&kerny2y[phase], p2yw, ypos, 8, yval);
				atc_filter_kernel_accumulate_window(&kernu2y[phase], p2yw, ypos, 8, uval);
				atc_filter_kernel_accumulate_window(&kernv2y[phase], p2yw, ypos, 8, vval);

				atc_filter_kernel_accumulate_window(&kerny2u[phase], p2uw, cpos, 24, yval * co_ub);
				atc_filter_kernel_accumulate_window(&kernu2u[phase], p2uw, cpos, 24, uval * co_ub);
				atc_filter_kernel_accumulate_window(&kernv2u[phase], p2uw, cpos, 24, vval * co_ub);

				atc_filter_kernel_accumulate_window(&kerny2v[phase], p2vw, cpos, 24, yval * co_vr * v_invert);
				atc_filter_kernel_accumulate_window(&kernu2v[phase], p2vw, cpos, 24, uval * co_vr * v_invert);
				atc_filter_kernel_accumulate_window(&kernv2v[phase], p2vw, cpos, 24, vval * co_vr * v_invert);

				uint32 *kerny16 = e->m_pal2x.m_pal_to_y[k][j][phase];
				uint32 *kernu16 = e->m_pal2x.m_pal_to_u[k][j][phase];
				uint32 *kernv16 = e->m_pal2x.m_pal_to_v[k][j][phase];

				for (int offset = 0; offset < 4; ++offset) {
					int idx = (offset + phase) & 7;
					float fw0 = p2yw[offset * 2 + 0] * scale + yerror[idx][0];
					float fw1 = p2yw[offset * 2 + 1] * scale + yerror[idx][1];
					sint32 w0 = atc_round_to_int32(fw0);
					sint32 w1 = atc_round_to_int32(fw1);
					yerror[idx][0] = fw0 - (float)w0;
					yerror[idx][1] = fw1 - (float)w1;
					kerny16[offset] = ((uint32)w1 << 16) + (uint32)w0;
				}

				kerny16[3] += signed_output ? 0x50005000u : 0x40004000u;

				for (int offset = 0; offset < 12; ++offset) {
					int idx = (offset + phase) & 7;
					float fw0 = p2uw[offset * 2 + 0] * scale + uerror[idx][0];
					float fw1 = p2uw[offset * 2 + 1] * scale + uerror[idx][1];
					sint32 w0 = atc_round_to_int32(fw0);
					sint32 w1 = atc_round_to_int32(fw1);
					uerror[idx][0] = fw0 - (float)w0;
					uerror[idx][1] = fw1 - (float)w1;
					kernu16[offset] = ((uint32)w1 << 16) + (uint32)w0;
				}

				for (int offset = 0; offset < 12; ++offset) {
					int idx = (offset + phase) & 7;
					float fw0 = p2vw[offset * 2 + 0] * scale + verror[idx][0];
					float fw1 = p2vw[offset * 2 + 1] * scale + verror[idx][1];
					sint32 w0 = atc_round_to_int32(fw0);
					sint32 w1 = atc_round_to_int32(fw1);
					verror[idx][0] = fw0 - (float)w0;
					verror[idx][1] = fw1 - (float)w1;
					kernv16[offset] = ((uint32)w1 << 16) + (uint32)w0;
				}
			}
		}
	}

	for (int i = 0; i < 8; ++i) {
		atc_filter_kernel_free(&kerny2y[i]);
		atc_filter_kernel_free(&kernu2y[i]);
		atc_filter_kernel_free(&kernv2y[i]);
		atc_filter_kernel_free(&kerny2u[i]);
		atc_filter_kernel_free(&kernu2u[i]);
		atc_filter_kernel_free(&kernv2u[i]);
		atc_filter_kernel_free(&kerny2v[i]);
		atc_filter_kernel_free(&kernu2v[i]);
		atc_filter_kernel_free(&kernv2v[i]);
	}

	atc_filter_kernel_free(&kernbase);
	atc_filter_kernel_free(&kerncfilt);
	atc_filter_kernel_free(&kernumod);
	atc_filter_kernel_free(&kernvmod);
	atc_filter_kernel_free(&kernysep);
	atc_filter_kernel_free(&kerncsep);
	atc_filter_kernel_free(&kerncdemod);
}

static void atc_recompute_ntsc_tables(ATC_ArtifactingEngine *e) {
	const ATC_ColorParams *params = &e->m_color_params;
	const bool signed_output = e->mb_expanded_range_output;

	e->mb_high_ntsc_tables_inited = true;
	e->mb_high_pal_tables_inited = false;
	e->mb_high_tables_signed = signed_output;

	ATC_Vec3 y_to_rgb[16][2][24];
	ATC_Vec3 chroma_to_rgb[16][2][24];
	memset(y_to_rgb, 0, sizeof(y_to_rgb));
	memset(chroma_to_rgb, 0, sizeof(chroma_to_rgb));

	float chroma_signal_amp = 0.5f / ATC_MAX(0.10f, params->mArtifactSat);
	float chroma_signal_inv_amp = params->mArtifactSat * 2.0f;

	float phadjust = -params->mArtifactHue * (2.0f * (float)M_PI / 360.0f) + (float)M_PI * 1.25f;
	float cp = cosf(phadjust);
	float sp = sinf(phadjust);

	float co_ir = 0.956f;
	float co_qr = 0.620f;
	float co_ig = -0.272f;
	float co_qg = -0.647f;
	float co_ib = -1.108f;
	float co_qb = 1.705f;
	atc_rotate2(&co_ir, &co_qr, -params->mRedShift * ((float)M_PI / 180.0f));
	atc_rotate2(&co_ig, &co_qg, -params->mGrnShift * ((float)M_PI / 180.0f));
	atc_rotate2(&co_ib, &co_qb, -params->mBluShift * ((float)M_PI / 180.0f));
	co_ir *= params->mRedScale;
	co_qr *= params->mRedScale;
	co_ig *= params->mGrnScale;
	co_qg *= params->mGrnScale;
	co_ib *= params->mBluScale;
	co_qb *= params->mBluScale;

	atc_rotate(&co_ir, &co_qr, cp, -sp);
	atc_rotate(&co_ig, &co_qg, cp, -sp);
	atc_rotate(&co_ib, &co_qb, cp, -sp);

	const float saturation_scale = params->mSaturation * 2.0f;
	const ATC_Vec3 co_i = atc_vec3(co_ir, co_ig, co_ib);
	const ATC_Vec3 co_q = atc_vec3(co_qr, co_qg, co_qb);

	float luma_ramp[16];
	atc_compute_luma_ramp(params->mLumaRampMode, luma_ramp);

	for (int i = 0; i < 15; ++i) {
		float chromatab[4];
		float phase = phadjust + (2.0f * (float)M_PI) * ((params->mHueStart / 360.0f) + (float)i / 15.0f * (params->mHueRange / 360.0f));

		for (int j = 0; j < 4; ++j) {
			float v = sinf((0.25f * 2.0f * (float)M_PI * (float)j) - phase);
			chromatab[j] = v;
		}

		float c0 = chromatab[0];
		float c1 = chromatab[1];
		float c2 = chromatab[2];
		float c3 = chromatab[3];

		const float chroma_sharp = 0.50f;
		float t[28] = {0};
		t[11 - 5] = c3 * ((1.0f - chroma_sharp) / 3.0f);
		t[12 - 5] = c0 * ((2.0f + chroma_sharp) / 3.0f);
		t[13 - 5] = c1;
		t[14 - 5] = c2;
		t[15 - 5] = c3 * ((2.0f + chroma_sharp) / 3.0f);
		t[16 - 5] = c0 * ((1.0f - chroma_sharp) / 3.0f);

		ATC_Vec3 rgbtab[2][22];
		memset(rgbtab, 0, sizeof(rgbtab));

		if (e->mb_tint_color_enabled) {
			const float sensitivity_factor = 0.50f;
			for (int j = 0; j < 6; ++j) {
				float intensity = t[6 + j] * (chroma_signal_amp * sensitivity_factor);
				ATC_Vec3 c = atc_vec3(intensity, intensity, intensity);
				rgbtab[0][6 + j] = atc_vec3_mul_scalar(c, -1.0f);
				rgbtab[1][6 + j] = c;
			}
		} else {
			float ytab[22] = {0};
			float itab[22] = {0};
			float qtab[22] = {0};

			ytab[7 - 1] = 0.0f;
			ytab[8 - 1] = (1.0f * c2 + 2.0f * c3) * (1.0f / 16.0f);
			ytab[9 - 1] = (1.0f * c0 + 2.0f * c1 + 1.0f * c2) * (1.0f / 16.0f);
			ytab[10 - 1] = (1.0f * c0 + 2.0f * c2 + 4.0f * c3) * (1.0f / 16.0f);
			ytab[11 - 1] = (2.0f * c0 + 4.0f * c1 + 2.0f * c2) * (1.0f / 16.0f);
			ytab[12 - 1] = (2.0f * c0 + 1.0f * c2 + 2.0f * c3) * (1.0f / 16.0f);
			ytab[13 - 1] = (1.0f * c0 + 2.0f * c1 + 1.0f * c2) * (1.0f / 16.0f);
			ytab[14 - 1] = (1.0f * c0) * (1.0f / 16.0f);

			for (int j = 0; j < 26; ++j) {
				if (j & 2)
					t[j] = -t[j];
			}

			float u[28] = {0};
			for (int j = 6; j < 28; ++j) {
				u[j] = (1.0f * t[j - 6])
					+ (0.9732320952f * t[j - 4])
					+ (0.9732320952f * t[j - 2])
					+ (1.0f * t[j])
					+ (0.1278410428f * u[j - 2]);
			}

			const float lpf_gain = (2.0f + 0.9732320952f * 2.0f) / (1.0f - 0.1278410428f);
			for (int j = 0; j < 28; ++j)
				u[j] = u[j] / 4.0f / lpf_gain;

			for (int j = 0; j < 22; ++j) {
				if (!(j & 1)) {
					itab[j] = (u[j + 2] + u[j + 4]) * 0.625f - (u[j] + u[j + 6]) * 0.125f;
					qtab[j] = u[j + 3];
				} else {
					itab[j] = u[j + 3];
					qtab[j] = (u[j + 2] + u[j + 4]) * 0.625f - (u[j] + u[j + 6]) * 0.125f;
				}
			}

			for (int j = 0; j < 22; ++j) {
				float fy = 0.0f;
				float fi = itab[j];
				float fq = qtab[j];

				ATC_Vec3 fc = atc_vec3_add(atc_vec3_mul_scalar(co_i, fi), atc_vec3_mul_scalar(co_q, fq));
				fc = atc_vec3_mul_scalar(fc, saturation_scale);

				ATC_Vec3 fyv = atc_vec3(fy, fy, fy);
				ATC_Vec3 f0 = atc_vec3_sub(fc, fyv);
				ATC_Vec3 f1 = atc_vec3_add(fc, fyv);

				rgbtab[0][j] = atc_vec3_mul_scalar(f0, params->mIntensityScale);
				rgbtab[1][j] = atc_vec3_mul_scalar(f1, params->mIntensityScale);
			}
		}

		for (int k = 0; k < 2; ++k) {
			for (int j = 0; j < 2; ++j) {
				rgbtab[k][j + 14] = atc_vec3_add(rgbtab[k][j + 14], rgbtab[k][j + 18]);
				rgbtab[k][j + 18] = atc_vec3(0.0f, 0.0f, 0.0f);
			}

			for (int j = 0; j < 22; ++j)
				chroma_to_rgb[i + 1][k][j] = rgbtab[k][j];
		}
	}

	const float luma_sharpness = params->mArtifactSharpness;
	float lumapulse[16] = {
		(1.0f - luma_sharpness) / 3.0f,
		(2.0f + luma_sharpness) / 3.0f,
		(2.0f + luma_sharpness) / 3.0f,
		(1.0f - luma_sharpness) / 3.0f
	};
	for (int i = 4; i < 16; ++i)
		lumapulse[i] = 0.0f;

	for (int i = 0; i < 16; ++i) {
		float y = luma_ramp[i] * params->mContrast + params->mBrightness;

		float t[30] = {0};
		t[11] = y * ((1.0f - 1.0f) / 3.0f);
		t[12] = y * ((2.0f + 1.0f) / 3.0f);
		t[13] = y * ((2.0f + 1.0f) / 3.0f);
		t[14] = y * ((1.0f - 1.0f) / 3.0f);

		for (int j = 0; j < 30; ++j) {
			if (!(j & 2))
				t[j] = -t[j];
		}

		float u[28] = {0};
		for (int j = 4; j < 20; ++j) {
			u[j] = (t[j - 4] * 0.25f + t[j - 2] * 0.625f + t[j] * 0.75f + t[j + 2] * 0.625f + t[j + 4] * 0.25f) / 10.0f;
		}

		float ytab[22] = {0};
		for (int j = 0; j < 11; ++j)
			ytab[7 + j] = y * lumapulse[j];

		ATC_Vec3 rgbtab[2][22];
		memset(rgbtab, 0, sizeof(rgbtab));

		if (e->mb_tint_color_enabled) {
			for (int j = 0; j < 22; ++j) {
				rgbtab[0][j] = atc_vec3(ytab[j], ytab[j], ytab[j]);
				rgbtab[1][j] = atc_vec3(ytab[j], ytab[j], ytab[j]);
			}
		} else {
			float itab[22] = {0};
			float qtab[22] = {0};

			for (int j = 0; j < 22; ++j) {
				if (j & 1) {
					itab[j] = (u[j + 2] + u[j + 4]) * 0.575f - (u[j] + u[j + 6]) * 0.065f;
					qtab[j] = u[j + 3];
				} else {
					itab[j] = u[j + 3];
					qtab[j] = (u[j + 2] + u[j + 4]) * 0.575f - (u[j] + u[j + 6]) * 0.065f;
				}
			}

			const float anti_chroma_scale = 1.3333333f + luma_sharpness * 2.666666f;
			for (int j = 0; j < 22; ++j) {
				float cs = cosf((0.25f * 2.0f * (float)M_PI) * (float)(j + 2));
				float sn = sinf((0.25f * 2.0f * (float)M_PI) * (float)(j + 2));
				ytab[j] -= (cs * itab[j] + sn * qtab[j]) * anti_chroma_scale;
			}

			for (int j = 0; j < 22; ++j) {
				float fy = ytab[j];
				float fi = itab[j];
				float fq = qtab[j];

				ATC_Vec3 fc = atc_vec3_add(atc_vec3_mul_scalar(co_i, fi), atc_vec3_mul_scalar(co_q, fq));
				fc = atc_vec3_mul_scalar(fc, chroma_signal_inv_amp);

				ATC_Vec3 fyv = atc_vec3(fy, fy, fy);
				ATC_Vec3 f0 = atc_vec3_add(fyv, fc);
				ATC_Vec3 f1 = atc_vec3_sub(fyv, fc);

				rgbtab[0][j] = atc_vec3_mul_scalar(f0, params->mIntensityScale);
				rgbtab[1][j] = atc_vec3_mul_scalar(f1, params->mIntensityScale);
			}
		}

		for (int k = 0; k < 2; ++k) {
			for (int j = 0; j < 4; ++j) {
				rgbtab[k][j + 14] = atc_vec3_add(rgbtab[k][j + 14], rgbtab[k][j + 18]);
				rgbtab[k][j + 18] = atc_vec3(0.0f, 0.0f, 0.0f);
			}

			for (int j = 0; j < 22; ++j)
				y_to_rgb[i][k][j] = rgbtab[k][j];
		}
	}

	const float encoding_scale = 16.0f * 255.0f * (signed_output ? 0.5f : 1.0f);

#if ATC_USE_M4X
	memset(&e->m4x, 0, sizeof(e->m4x));

	for (int idx = 0; idx < 256; ++idx) {
		int cidx = idx >> 4;
		int lidx = idx & 15;

		ATC_Vec3 e0[16] = {0};
		ATC_Vec3 e1[16] = {0};

		for (int k = 0; k < 4; ++k) {
			if (k & 1) {
				for (int i = 0; i < 16; ++i) {
					const int j0 = (i - k) * 2 + 0;
					const int j1 = (i - k) * 2 + 1;

					ATC_Vec3 pal_to_rgb0 = (j0 >= 0 && j0 < 24) ? y_to_rgb[lidx][1][j0] : atc_vec3(0.0f, 0.0f, 0.0f);
					ATC_Vec3 pal_to_rgb1 = (j1 >= 0 && j1 < 24) ? y_to_rgb[lidx][1][j1] : atc_vec3(0.0f, 0.0f, 0.0f);

					pal_to_rgb0 = atc_vec3_add(atc_vec3_mul_scalar(pal_to_rgb0, encoding_scale), e0[i]);
					pal_to_rgb1 = atc_vec3_add(atc_vec3_mul_scalar(pal_to_rgb1, encoding_scale), e1[i]);

					if (k == 3 && i >= 4) {
						pal_to_rgb0 = atc_vec3_add(pal_to_rgb0, e0[i - 4]);
						pal_to_rgb1 = atc_vec3_add(pal_to_rgb1, e1[i - 4]);
					}

					const int r0 = atc_round_to_int32(pal_to_rgb0.x);
					const int g0 = atc_round_to_int32(pal_to_rgb0.y);
					const int b0 = atc_round_to_int32(pal_to_rgb0.z);
					const int r1 = atc_round_to_int32(pal_to_rgb1.x);
					const int g1 = atc_round_to_int32(pal_to_rgb1.y);
					const int b1 = atc_round_to_int32(pal_to_rgb1.z);

					e0[i] = atc_vec3(pal_to_rgb0.x - (float)r0, pal_to_rgb0.y - (float)g0, pal_to_rgb0.z - (float)b0);
					e1[i] = atc_vec3(pal_to_rgb1.x - (float)r1, pal_to_rgb1.y - (float)g1, pal_to_rgb1.z - (float)b1);

					e->m4x.m_pal_to_r[idx][k][i] = ((uint32)r0 & 0xFFFFu) + (((uint32)r1 & 0xFFFFu) << 16);
					e->m4x.m_pal_to_g[idx][k][i] = ((uint32)g0 & 0xFFFFu) + (((uint32)g1 & 0xFFFFu) << 16);
					e->m4x.m_pal_to_b[idx][k][i] = ((uint32)b0 & 0xFFFFu) + (((uint32)b1 & 0xFFFFu) << 16);
				}
			} else {
				for (int i = 0; i < 16; ++i) {
					const int j0 = (i - k) * 2 + 0;
					const int j1 = (i - k) * 2 + 1;

					ATC_Vec3 pal_to_rgb0 = (j0 >= 0 && j0 < 24) ? atc_vec3_add(chroma_to_rgb[cidx][0][j0], y_to_rgb[lidx][0][j0]) : atc_vec3(0.0f, 0.0f, 0.0f);
					ATC_Vec3 pal_to_rgb1 = (j1 >= 0 && j1 < 24) ? atc_vec3_add(chroma_to_rgb[cidx][0][j1], y_to_rgb[lidx][0][j1]) : atc_vec3(0.0f, 0.0f, 0.0f);

					if (j0 >= 2 && j0 < 26)
						pal_to_rgb0 = atc_vec3_add(pal_to_rgb0, chroma_to_rgb[cidx][1][j0 - 2]);
					if (j1 >= 2 && j1 < 26)
						pal_to_rgb1 = atc_vec3_add(pal_to_rgb1, chroma_to_rgb[cidx][1][j1 - 2]);

					pal_to_rgb0 = atc_vec3_add(atc_vec3_mul_scalar(pal_to_rgb0, encoding_scale), e0[i]);
					pal_to_rgb1 = atc_vec3_add(atc_vec3_mul_scalar(pal_to_rgb1, encoding_scale), e1[i]);

					const int r0 = atc_round_to_int32(pal_to_rgb0.x);
					const int g0 = atc_round_to_int32(pal_to_rgb0.y);
					const int b0 = atc_round_to_int32(pal_to_rgb0.z);
					const int r1 = atc_round_to_int32(pal_to_rgb1.x);
					const int g1 = atc_round_to_int32(pal_to_rgb1.y);
					const int b1 = atc_round_to_int32(pal_to_rgb1.z);

					e0[i] = atc_vec3(pal_to_rgb0.x - (float)r0, pal_to_rgb0.y - (float)g0, pal_to_rgb0.z - (float)b0);
					e1[i] = atc_vec3(pal_to_rgb1.x - (float)r1, pal_to_rgb1.y - (float)g1, pal_to_rgb1.z - (float)b1);

					e->m4x.m_pal_to_r[idx][k][i] = ((uint32)r0 & 0xFFFFu) + (((uint32)r1 & 0xFFFFu) << 16);
					e->m4x.m_pal_to_g[idx][k][i] = ((uint32)g0 & 0xFFFFu) + (((uint32)g1 & 0xFFFFu) << 16);
					e->m4x.m_pal_to_b[idx][k][i] = ((uint32)b0 & 0xFFFFu) + (((uint32)b1 & 0xFFFFu) << 16);
				}
			}
		}

		const uint32 bias = signed_output ? 0x04080408u : 0x00080008u;
		e->m4x.m_pal_to_r[idx][0][0] = atc_twin_add(e->m4x.m_pal_to_r[idx][0][0], bias);
		e->m4x.m_pal_to_r[idx][0][1] = atc_twin_add(e->m4x.m_pal_to_r[idx][0][1], bias);
		e->m4x.m_pal_to_r[idx][0][2] = atc_twin_add(e->m4x.m_pal_to_r[idx][0][2], bias);
		e->m4x.m_pal_to_r[idx][0][3] = atc_twin_add(e->m4x.m_pal_to_r[idx][0][3], bias);
		e->m4x.m_pal_to_g[idx][0][0] = atc_twin_add(e->m4x.m_pal_to_g[idx][0][0], bias);
		e->m4x.m_pal_to_g[idx][0][1] = atc_twin_add(e->m4x.m_pal_to_g[idx][0][1], bias);
		e->m4x.m_pal_to_g[idx][0][2] = atc_twin_add(e->m4x.m_pal_to_g[idx][0][2], bias);
		e->m4x.m_pal_to_g[idx][0][3] = atc_twin_add(e->m4x.m_pal_to_g[idx][0][3], bias);
		e->m4x.m_pal_to_b[idx][0][0] = atc_twin_add(e->m4x.m_pal_to_b[idx][0][0], bias);
		e->m4x.m_pal_to_b[idx][0][1] = atc_twin_add(e->m4x.m_pal_to_b[idx][0][1], bias);
		e->m4x.m_pal_to_b[idx][0][2] = atc_twin_add(e->m4x.m_pal_to_b[idx][0][2], bias);
		e->m4x.m_pal_to_b[idx][0][3] = atc_twin_add(e->m4x.m_pal_to_b[idx][0][3], bias);

		for (int i = 0; i < 16; ++i) {
			e->m4x.m_pal_to_r_twin[idx][0][i] = atc_twin_add(e->m4x.m_pal_to_r[idx][0][i], e->m4x.m_pal_to_r[idx][1][i]);
			e->m4x.m_pal_to_g_twin[idx][0][i] = atc_twin_add(e->m4x.m_pal_to_g[idx][0][i], e->m4x.m_pal_to_g[idx][1][i]);
			e->m4x.m_pal_to_b_twin[idx][0][i] = atc_twin_add(e->m4x.m_pal_to_b[idx][0][i], e->m4x.m_pal_to_b[idx][1][i]);
		}

		for (int i = 0; i < 16; ++i) {
			e->m4x.m_pal_to_r_twin[idx][1][i] = atc_twin_add(e->m4x.m_pal_to_r[idx][2][i], e->m4x.m_pal_to_r[idx][3][i]);
			e->m4x.m_pal_to_g_twin[idx][1][i] = atc_twin_add(e->m4x.m_pal_to_g[idx][2][i], e->m4x.m_pal_to_g[idx][3][i]);
			e->m4x.m_pal_to_b_twin[idx][1][i] = atc_twin_add(e->m4x.m_pal_to_b[idx][2][i], e->m4x.m_pal_to_b[idx][3][i]);
		}

		for (int i = 0; i < 16; ++i) {
			e->m4x.m_pal_to_r_quad[idx][i] = atc_twin_add(e->m4x.m_pal_to_r_twin[idx][0][i], e->m4x.m_pal_to_r_twin[idx][1][i]);
			e->m4x.m_pal_to_g_quad[idx][i] = atc_twin_add(e->m4x.m_pal_to_g_twin[idx][0][i], e->m4x.m_pal_to_g_twin[idx][1][i]);
			e->m4x.m_pal_to_b_quad[idx][i] = atc_twin_add(e->m4x.m_pal_to_b_twin[idx][0][i], e->m4x.m_pal_to_b_twin[idx][1][i]);
		}
	}
#else
	memset(&e->m2x, 0, sizeof(e->m2x));

	for (int idx = 0; idx < 256; ++idx) {
		int cidx = idx >> 4;
		int lidx = idx & 15;

		for (int k = 0; k < 2; ++k) {
			for (int i = 0; i < 10; ++i) {
				ATC_Vec3 pal_to_rgb0 = atc_vec3_add(chroma_to_rgb[cidx][k][i * 2 + 0], y_to_rgb[lidx][k][i * 2 + 0]);
				ATC_Vec3 pal_to_rgb1 = atc_vec3_add(chroma_to_rgb[cidx][k][i * 2 + 1], y_to_rgb[lidx][k][i * 2 + 1]);
				pal_to_rgb0 = atc_vec3_mul_scalar(pal_to_rgb0, encoding_scale);
				pal_to_rgb1 = atc_vec3_mul_scalar(pal_to_rgb1, encoding_scale);

				e->m2x.m_pal_to_r[idx][k][i + k] = (uint32)atc_round_to_int32(pal_to_rgb0.x) + ((uint32)atc_round_to_int32(pal_to_rgb1.x) << 16);
				e->m2x.m_pal_to_g[idx][k][i + k] = (uint32)atc_round_to_int32(pal_to_rgb0.y) + ((uint32)atc_round_to_int32(pal_to_rgb1.y) << 16);
				e->m2x.m_pal_to_b[idx][k][i + k] = (uint32)atc_round_to_int32(pal_to_rgb0.z) + ((uint32)atc_round_to_int32(pal_to_rgb1.z) << 16);
			}
		}

		if (signed_output) {
			e->m2x.m_pal_to_r[idx][0][0] += 0x04000400;
			e->m2x.m_pal_to_r[idx][0][1] += 0x04000400;
			e->m2x.m_pal_to_g[idx][0][0] += 0x04000400;
			e->m2x.m_pal_to_g[idx][0][1] += 0x04000400;
			e->m2x.m_pal_to_b[idx][0][0] += 0x04000400;
			e->m2x.m_pal_to_b[idx][0][1] += 0x04000400;
		}

		for (int i = 0; i < 12; ++i) {
			e->m2x.m_pal_to_r_twin[idx][i] = e->m2x.m_pal_to_r[idx][0][i] + e->m2x.m_pal_to_r[idx][1][i];
			e->m2x.m_pal_to_g_twin[idx][i] = e->m2x.m_pal_to_g[idx][0][i] + e->m2x.m_pal_to_g[idx][1][i];
			e->m2x.m_pal_to_b_twin[idx][i] = e->m2x.m_pal_to_b[idx][0][i] + e->m2x.m_pal_to_b[idx][1][i];
		}
	}
#endif
}

static void atc_recompute_color_tables(ATC_ArtifactingEngine *e) {
	const ATC_ColorParams *params = &e->m_color_params;
	float luma_ramp[16];
	atc_compute_luma_ramp(params->mLumaRampMode, luma_ramp);

	const float yscale = params->mContrast * params->mIntensityScale;
	const float ybias = params->mBrightness * params->mIntensityScale;

	ATC_Vec2 co_r = atc_mat2_mul_vec2(atc_mat2_rotation(params->mRedShift * ((float)M_PI / 180.0f)), atc_vec2(0.9563f, 0.6210f));
	ATC_Vec2 co_g = atc_mat2_mul_vec2(atc_mat2_rotation(params->mGrnShift * ((float)M_PI / 180.0f)), atc_vec2(-0.2721f, -0.6474f));
	ATC_Vec2 co_b = atc_mat2_mul_vec2(atc_mat2_rotation(params->mBluShift * ((float)M_PI / 180.0f)), atc_vec2(-1.1070f, 1.7046f));

	co_r.x *= params->mRedScale * params->mIntensityScale;
	co_r.y *= params->mRedScale * params->mIntensityScale;
	co_g.x *= params->mGrnScale * params->mIntensityScale;
	co_g.y *= params->mGrnScale * params->mIntensityScale;
	co_b.x *= params->mBluScale * params->mIntensityScale;
	co_b.y *= params->mBluScale * params->mIntensityScale;

	const float artphase = params->mArtifactHue * (2.0f * (float)M_PI / 360.0f);
	ATC_Vec2 rot_art = atc_vec2(cosf(artphase), sinf(artphase));

	float artr = 64.0f * 255.0f * atc_vec2_dot(rot_art, co_r) * ATC_SATURATION / 15.0f * params->mArtifactSat;
	float artg = 64.0f * 255.0f * atc_vec2_dot(rot_art, co_g) * ATC_SATURATION / 15.0f * params->mArtifactSat;
	float artb = 64.0f * 255.0f * atc_vec2_dot(rot_art, co_b) * ATC_SATURATION / 15.0f * params->mArtifactSat;

	for (int i = -15; i < 16; ++i) {
		int ar = atc_round_to_int32(artr * (float)i);
		int ag = atc_round_to_int32(artg * (float)i);
		int ab = atc_round_to_int32(artb * (float)i);
		if (ar != (sint16)ar) ar = (ar < 0) ? -0x8000 : 0x7FFF;
		if (ag != (sint16)ag) ag = (ag < 0) ? -0x8000 : 0x7FFF;
		if (ab != (sint16)ab) ab = (ab < 0) ? -0x8000 : 0x7FFF;
		e->m_artifact_ramp[i + 15][0] = ab;
		e->m_artifact_ramp[i + 15][1] = ag;
		e->m_artifact_ramp[i + 15][2] = ar;
		e->m_artifact_ramp[i + 15][3] = 0;
	}

	memset(e->m_chroma_vectors, 0, sizeof(e->m_chroma_vectors));

	ATC_Vec3 cvec[16];
	cvec[0] = atc_vec3(0, 0, 0);

	for (int j = 0; j < 15; ++j) {
		float i = 0.0f;
		float q = 0.0f;
		if (params->mUsePALQuirks) {
			const float step = params->mHueRange * (2.0f * (float)M_PI / (15.0f * 360.0f));
			const float theta = params->mHueStart * (2.0f * (float)M_PI / 360.0f);
			const ATC_PALPhaseInfo *info = &atc_pal_phase_lookup[j];
			float angle2 = theta + step * info->even_phase;
			float angle3 = theta + step * info->odd_phase;
			float i2 = cosf(angle2) * info->even_invert;
			float q2 = sinf(angle2) * info->even_invert;
			float i3 = cosf(angle3) * info->odd_invert;
			float q3 = sinf(angle3) * info->odd_invert;
			i = (i2 + i3) * 0.5f;
			q = (q2 + q3) * 0.5f;
		} else {
			float theta = 2.0f * (float)M_PI * (params->mHueStart / 360.0f + (float)j * (params->mHueRange / (15.0f * 360.0f)));
			i = cosf(theta);
			q = sinf(theta);
		}
		ATC_Vec2 iq = atc_vec2(i, q);
		ATC_Vec3 chroma = atc_vec3(atc_vec2_dot(co_r, iq), atc_vec2_dot(co_g, iq), atc_vec2_dot(co_b, iq));
		chroma = atc_vec3_mul_scalar(chroma, params->mSaturation);
		cvec[j + 1] = chroma;

		int icr = atc_round_to_int(chroma.x * (64.0f * 255.0f));
		int icg = atc_round_to_int(chroma.y * (64.0f * 255.0f));
		int icb = atc_round_to_int(chroma.z * (64.0f * 255.0f));
		if (icr != (sint16)icr) icr = (icr < 0) ? -0x8000 : 0x7FFF;
		if (icg != (sint16)icg) icg = (icg < 0) ? -0x8000 : 0x7FFF;
		if (icb != (sint16)icb) icb = (icb < 0) ? -0x8000 : 0x7FFF;
		e->m_chroma_vectors[j + 1][0] = icb;
		e->m_chroma_vectors[j + 1][1] = icg;
		e->m_chroma_vectors[j + 1][2] = icr;
	}

	for (int i = 0; i < 16; ++i) {
		float y = luma_ramp[i & 15] * yscale + ybias;
		e->m_luma_ramp[i] = atc_round_to_int32(y * (64.0f * 255.0f)) + 32;
	}

	const float gamma = 1.0f / params->mGammaCorrect;
	e->mb_gamma_identity = fabsf(params->mGammaCorrect - 1.0f) < 1e-5f;
	if (e->mb_gamma_identity) {
		for (int i = 0; i < 256; ++i)
			e->m_gamma_table[i] = (uint8)i;
	} else {
		for (int i = 0; i < 256; ++i)
			e->m_gamma_table[i] = (uint8)atc_round_to_int32(powf((float)i / 255.0f, gamma) * 255.0f);
	}

	const bool use_color_tint = e->mb_tint_color_enabled;
	const bool use_mono_persistence = use_color_tint && e->mb_blend_mono_persistence;
	ATC_Vec3 tint_color_val = use_color_tint
		? (use_mono_persistence ? atc_vec3(1.0f, 1.0f, 1.0f) : e->m_raw_tint_color)
		: atc_vec3(0.0f, 0.0f, 0.0f);
	e->m_tint_color = tint_color_val;

	for (int i = 0; i < 256; ++i) {
		int c = i >> 4;

		float y = luma_ramp[i & 15] * yscale + ybias;
		float r = cvec[c].x + y;
		float g = cvec[c].y + y;
		float b = cvec[c].z + y;
		ATC_Vec3 rgb;

		if (use_color_tint) {
			if (c) {
				const float chroma = 0.125f;
				float intensity = powf((powf(ATC_MAX(y - chroma, 0.0f), 2.4f) + powf(y + chroma, 2.4f)) * 0.5f, 1.0f / 2.4f);
				rgb = atc_vec3_mul_scalar(tint_color_val, intensity);
			} else {
				rgb = atc_vec3_mul_scalar(tint_color_val, y);
			}

			if (use_mono_persistence) {
				ATC_Vec3 linrgb = atc_srgb_to_linear(rgb);
				rgb = atc_vec3(sqrtf(linrgb.x), sqrtf(linrgb.y), sqrtf(linrgb.z));
			} else {
				ATC_Vec3 linrgb = atc_srgb_to_linear(rgb);
				rgb = atc_clip_linear_color_to_srgb_local(linrgb);
			}
		} else {
			rgb = atc_vec3(r, g, b);
		}

		e->m_palette[i] = atc_pack_rgb(atc_vec3(rgb.x * 255.0f, rgb.y * 255.0f, rgb.z * 255.0f));
		e->m_signed_palette[i] = atc_pack_rgb(atc_vec3(rgb.x * 127.0f + 64.0f, rgb.y * 127.0f + 64.0f, rgb.z * 127.0f + 64.0f));

		if (e->mb_enable_color_correction) {
			e->m_corrected_palette[i] = e->m_palette[i];
			e->m_corrected_signed_palette[i] = e->m_signed_palette[i];
		} else {
			ATC_Vec3 rgb2 = atc_vec3_pow(atc_vec3_max0(rgb), gamma);
			e->m_corrected_palette[i] = atc_pack_rgb(atc_vec3(rgb2.x * 255.0f, rgb2.y * 255.0f, rgb2.z * 255.0f));
			e->m_corrected_signed_palette[i] = atc_pack_rgb(atc_vec3(rgb2.x * 127.0f + 64.0f, rgb2.y * 127.0f + 64.0f, rgb2.z * 127.0f + 64.0f));
		}
	}

	memset(e->m_mono_table, 0, sizeof(e->m_mono_table));
	memset(e->m_mono_table2, 0, sizeof(e->m_mono_table2));
	if (e->mb_tint_color_enabled) {
		atc_palette_generate_mono_ramp(params, e->m_monitor_mode, e->m_mono_table);
		if (e->mb_blend_mono_persistence)
			atc_palette_generate_mono_persistence_ramp(params, e->m_monitor_mode, e->m_mono_table2);
	}

	e->mb_high_ntsc_tables_inited = false;
	e->mb_high_pal_tables_inited = false;
	e->mb_active_tables_inited = false;
	e->mb_color_tables_mono_persistence = e->mb_blend_mono_persistence;
}

static void atc_recompute_mono_persistence(ATC_ArtifactingEngine *e, float dt) {
	const float tc1 = ATC_PERSISTENCE_TC1;
	const float tc2 = ATC_PERSISTENCE_TC2;

	e->m_mono_persistence_f1 = tc1 > 0.0f ? 1.0f - expf(-dt / tc1) : 0.0f;
	e->m_mono_persistence_f2 = tc2 > 0.0f ? 1.0f - expf(-dt / tc2) : 0.0f;

	if (e->m_mono_persistence_f2 > 0.0f)
		e->m_mono_persistence_limit = (sqrtf(e->m_mono_persistence_f1 * e->m_mono_persistence_f1 + 4.0f * e->m_mono_persistence_f2) - e->m_mono_persistence_f1) / (2.0f * e->m_mono_persistence_f2);
	else if (e->m_mono_persistence_f1 > 0.0f)
		e->m_mono_persistence_limit = 1.0f / e->m_mono_persistence_f1;
	else
		e->m_mono_persistence_limit = 0.0f;
}

static void atc_blit_no_artifacts(ATC_ArtifactingEngine *e, uint32 *dst, const uint8 *src, bool scanline_has_hires) {
	const uint32 *palette = e->mb_bypass_output_correction ? e->m_palette : e->m_corrected_palette;
	if (scanline_has_hires) {
		for (size_t x = 0; x < ATC_ARTIFACTING_N; ++x)
			dst[x] = palette[src[x]];
	} else {
		for (size_t x = 0; x < ATC_ARTIFACTING_N; x += 2) {
			const uint32 c = palette[src[x]];
			dst[x] = c;
			dst[x + 1] = c;
		}
	}
}

static void atc_artifact_pal_hi(ATC_ArtifactingEngine *e, uint32 dst[ATC_ARTIFACTING_N * 2], const uint8 src0[ATC_ARTIFACTING_N], bool scanline_has_hires, bool odd_line) {
	ATC_ALIGN(16) uint32 ybuf[32 + ATC_ARTIFACTING_N];
	ATC_ALIGN(16) uint32 ubuf[32 + ATC_ARTIFACTING_N];
	ATC_ALIGN(16) uint32 vbuf[32 + ATC_ARTIFACTING_N];

	uint32 *ulbuf = e->m_pal_delay_line_uv[0];
	uint32 *vlbuf = e->m_pal_delay_line_uv[1];

	uint8 src[ATC_ARTIFACTING_N + 16];
	memset(src, 0, 2);
	memcpy(src + 2, src0, ATC_ARTIFACTING_N);
	memset(src + 2 + ATC_ARTIFACTING_N, 0, 14);

	atc_memset32(ubuf, 0x20002000, sizeof(ubuf) / sizeof(ubuf[0]));
	atc_memset32(vbuf, 0x20002000, sizeof(vbuf) / sizeof(vbuf[0]));

	if (scanline_has_hires) {
		atc_artifact_pal_luma(ybuf, src, ATC_ARTIFACTING_N + 16, &e->m_pal2x.m_pal_to_y[odd_line][0][0][0]);
		if (!e->mb_tint_color_enabled) {
			atc_artifact_pal_chroma(ubuf, src, ATC_ARTIFACTING_N + 16, &e->m_pal2x.m_pal_to_u[odd_line][0][0][0]);
			atc_artifact_pal_chroma(vbuf, src, ATC_ARTIFACTING_N + 16, &e->m_pal2x.m_pal_to_v[odd_line][0][0][0]);
		}
	} else {
		atc_artifact_pal_luma(ybuf, src, ATC_ARTIFACTING_N + 16, &e->m_pal2x.m_pal_to_y[odd_line][0][0][0]);
		if (!e->mb_tint_color_enabled) {
			atc_artifact_pal_chroma(ubuf, src, ATC_ARTIFACTING_N + 16, &e->m_pal2x.m_pal_to_u[odd_line][0][0][0]);
			atc_artifact_pal_chroma(vbuf, src, ATC_ARTIFACTING_N + 16, &e->m_pal2x.m_pal_to_v[odd_line][0][0][0]);
		}
	}

	if (e->mb_tint_color_enabled)
		atc_artifact_pal_final_mono(dst, ybuf + 4, ATC_ARTIFACTING_N + 16, e->m_mono_table);
	else
		atc_artifact_pal_final(dst, ybuf + 4, ubuf + 4, vbuf + 4, ulbuf, vlbuf, ATC_ARTIFACTING_N);

	if (!e->mb_bypass_output_correction && !e->mb_gamma_identity)
		atc_gamma_correct((uint8 *)dst, ATC_ARTIFACTING_N * 2, e->m_gamma_table);
}

static inline sint32 atc_s16_from_u16(uint16 v) {
	return (v & 0x8000u) ? (sint32)(v | 0xFFFF0000u) : (sint32)v;
}

static inline sint32 atc_add_wrap_s16(sint32 a, sint32 b) {
	uint32 sum = ((uint32)(a + b)) & 0xFFFFu;
	return (sum & 0x8000u) ? (sint32)(sum | 0xFFFF0000u) : (sint32)sum;
}

static void atc_vec_add_s16(sint32 acc[8], const uint16 *vec) {
	for (int i = 0; i < 8; ++i)
		acc[i] = atc_add_wrap_s16(acc[i], atc_s16_from_u16(vec[i]));
}

static void atc_vec_set_s16(sint32 acc[8], const uint16 *vec) {
	for (int i = 0; i < 8; ++i)
		acc[i] = atc_s16_from_u16(vec[i]);
}

static void atc_vec_copy_s16(sint32 dst[8], const sint32 src[8]) {
	for (int i = 0; i < 8; ++i)
		dst[i] = src[i];
}

static void atc_vec_store_u8(uint8 *dst, const sint32 acc[8]) {
	for (int i = 0; i < 8; ++i) {
		int v = acc[i] >> 4;
		if (v < 0) v = 0;
		else if (v > 255) v = 255;
		dst[i] = (uint8)v;
	}
}

static void atc_ntsc_accum_m4x(uint8 *dst, const uint32 table[256][4][16], const uint8 *src, uint32 count) {
	const uint16 *table16 = (const uint16 *)table;
	sint32 acc0[8] = {0};
	sint32 acc1[8] = {0};
	sint32 acc2[8] = {0};

	const size_t stride = 16u * 8u;
	count >>= 2;

	do {
		const uint8 p0 = src[0];
		const uint8 p1 = src[1];
		const uint8 p2 = src[2];
		const uint8 p3 = src[3];

		const uint16 *base0 = table16 + (size_t)p0 * stride;
		const uint16 *base1 = table16 + (size_t)p1 * stride;
		const uint16 *base2 = table16 + (size_t)p2 * stride;
		const uint16 *base3 = table16 + (size_t)p3 * stride;

		atc_vec_add_s16(acc0, base0 + 0 * 8);
		atc_vec_add_s16(acc1, base0 + 1 * 8);
		atc_vec_set_s16(acc2, base0 + 2 * 8);

		atc_vec_add_s16(acc0, base1 + 4 * 8);
		atc_vec_add_s16(acc1, base1 + 5 * 8);
		atc_vec_add_s16(acc2, base1 + 6 * 8);

		atc_vec_add_s16(acc0, base2 + 8 * 8);
		atc_vec_add_s16(acc1, base2 + 9 * 8);
		atc_vec_add_s16(acc2, base2 + 10 * 8);

		atc_vec_add_s16(acc0, base3 + 12 * 8);
		atc_vec_add_s16(acc1, base3 + 13 * 8);
		atc_vec_add_s16(acc2, base3 + 14 * 8);

		atc_vec_store_u8(dst, acc0);
		dst += 8;

		atc_vec_copy_s16(acc0, acc1);
		atc_vec_copy_s16(acc1, acc2);

		src += 4;
	} while (--count);

	atc_vec_store_u8(dst, acc0);
	dst += 8;
	atc_vec_store_u8(dst, acc1);
}

static void atc_ntsc_accum_twin_m4x(uint8 *dst, const uint32 table[256][2][16], const uint8 *src, uint32 count) {
	const uint16 *table16 = (const uint16 *)table;
	sint32 acc0[8] = {0};
	sint32 acc1[8] = {0};
	sint32 acc2[8] = {0};

	const size_t stride = 8u * 8u;
	count >>= 2;

	do {
		const uint8 p0 = src[0];
		const uint8 p2 = src[2];
		const uint16 *base0 = table16 + (size_t)p0 * stride;
		const uint16 *base1 = table16 + (size_t)p2 * stride;

		atc_vec_add_s16(acc0, base0 + 0 * 8);
		atc_vec_add_s16(acc1, base0 + 1 * 8);
		atc_vec_set_s16(acc2, base0 + 2 * 8);

		atc_vec_add_s16(acc0, base1 + 4 * 8);
		atc_vec_add_s16(acc1, base1 + 5 * 8);
		atc_vec_add_s16(acc2, base1 + 6 * 8);

		atc_vec_store_u8(dst, acc0);
		dst += 8;

		atc_vec_copy_s16(acc0, acc1);
		atc_vec_copy_s16(acc1, acc2);

		src += 4;
	} while (--count);

	atc_vec_store_u8(dst, acc0);
	dst += 8;
	atc_vec_store_u8(dst, acc1);
}

#if !ATC_USE_M4X
static void atc_ntsc_accum(uint32 *dst, const uint32 table[256][2][12], const uint8 *src, uint32 count) {
	count >>= 1;
	do {
		uint8 p0 = *src++;
		uint8 p1 = *src++;
		const uint32 *pr0 = table[p0][0];
		const uint32 *pr1 = table[p1][1];

		dst[0] += pr0[0];
		dst[1] += pr0[1] + pr1[1];
		dst[2] += pr0[2] + pr1[2];
		dst[3] += pr0[3] + pr1[3];
		dst[4] += pr0[4] + pr1[4];
		dst[5] += pr0[5] + pr1[5];
		dst[6] += pr0[6] + pr1[6];
		dst[7] += pr0[7] + pr1[7];
		dst[8] += pr0[8] + pr1[8];
		dst[9] += pr0[9] + pr1[9];
		dst[10] += pr0[10] + pr1[10];
		dst[11] += pr1[11];

		dst += 2;
	} while (--count);
}

static void atc_ntsc_accum_twin(uint32 *dst, const uint32 table[256][12], const uint8 *src, uint32 count) {
	count >>= 1;
	do {
		uint8 p = *src;
		src += 2;
		const uint32 *pr = table[p];

		dst[0] += pr[0];
		dst[1] += pr[1];
		dst[2] += pr[2];
		dst[3] += pr[3];
		dst[4] += pr[4];
		dst[5] += pr[5];
		dst[6] += pr[6];
		dst[7] += pr[7];
		dst[8] += pr[8];
		dst[9] += pr[9];
		dst[10] += pr[10];
		dst[11] += pr[11];

		dst += 2;
	} while (--count);
}

static void atc_ntsc_final(uint32 *dst, const uint32 *srcr, const uint32 *srcg, const uint32 *srcb, uint32 count) {
	uint8 *dst8 = (uint8 *)dst;
	do {
		uint32 rp = *srcr++;
		uint32 gp = *srcg++;
		uint32 bp = *srcb++;
		int r0 = ((int)(rp & 0xffff) - 0x7ff8) >> 4;
		int g0 = ((int)(gp & 0xffff) - 0x7ff8) >> 4;
		int b0 = ((int)(bp & 0xffff) - 0x7ff8) >> 4;
		int r1 = ((int)(rp >> 16) - 0x7ff8) >> 4;
		int g1 = ((int)(gp >> 16) - 0x7ff8) >> 4;
		int b1 = ((int)(bp >> 16) - 0x7ff8) >> 4;

		if (r0 < 0) r0 = 0; else if (r0 > 255) r0 = 255;
		if (g0 < 0) g0 = 0; else if (g0 > 255) g0 = 255;
		if (b0 < 0) b0 = 0; else if (b0 > 255) b0 = 255;
		if (r1 < 0) r1 = 0; else if (r1 > 255) r1 = 255;
		if (g1 < 0) g1 = 0; else if (g1 > 255) g1 = 255;
		if (b1 < 0) b1 = 0; else if (b1 > 255) b1 = 255;

		*dst8++ = (uint8)b0;
		*dst8++ = (uint8)g0;
		*dst8++ = (uint8)r0;
		*dst8++ = 0;
		*dst8++ = (uint8)b1;
		*dst8++ = (uint8)g1;
		*dst8++ = (uint8)r1;
		*dst8++ = 0;
	} while (--count);
}
#endif

static void atc_ntsc_final_u8(uint32 *dst0, const uint8 *srcr, const uint8 *srcg, const uint8 *srcb, uint32 count) {
	uint8 *dst8 = (uint8 *)dst0;
	uint32 total = count * 2;

	for (uint32 i = 0; i < total; ++i) {
		*dst8++ = srcb[i];
		*dst8++ = srcg[i];
		*dst8++ = srcr[i];
		*dst8++ = 0;
	}
}

#if !ATC_USE_M4X
static void atc_ntsc_final_mono_s16(uint32 *dst0, const void *src0, uint32 count, const uint32 mono_tab[256]) {
	uint32 *dst = dst0;
	const uint16 *src = (const uint16 *)src0;

	do {
		int idx0 = ((int)*src++ - 0x7ff8) >> 4;
		int idx1 = ((int)*src++ - 0x7ff8) >> 4;

		if (idx0 < 0) idx0 = 0;
		if (idx0 > 255) idx0 = 255;
		if (idx1 < 0) idx1 = 0;
		if (idx1 > 255) idx1 = 255;

		*dst++ = mono_tab[(unsigned)idx0];
		*dst++ = mono_tab[(unsigned)idx1];
	} while (--count);
}
#endif

static void atc_ntsc_final_mono_u8(uint32 *dst0, const void *src0, uint32 count, const uint32 mono_tab[256]) {
	uint32 *dst = dst0;
	const uint8 *src = (const uint8 *)src0;
	uint32 total = count * 2;

	for (uint32 i = 0; i < total; ++i)
		dst[i] = mono_tab[src[i]];
}

static void atc_artifact_ntsc(ATC_ArtifactingEngine *e, uint32 dst[ATC_ARTIFACTING_N], const uint8 src[ATC_ARTIFACTING_N], bool scanline_has_hires, bool include_hblank) {
	if (!scanline_has_hires) {
		atc_blit_no_artifacts(e, dst, src, false);
		return;
	}

	uint8 luma[ATC_ARTIFACTING_N + 4];
	uint8 luma2[ATC_ARTIFACTING_N];
	sint8 inv[ATC_ARTIFACTING_N];

	for (int i = 0; i < ATC_ARTIFACTING_N; ++i)
		luma[i + 2] = src[i] & 15;

	luma[0] = luma[1] = luma[2];
	luma[ATC_ARTIFACTING_N + 2] = luma[ATC_ARTIFACTING_N + 3] = luma[ATC_ARTIFACTING_N + 1];

	int artsum = 0;
	for (int i = 0; i < ATC_ARTIFACTING_N; ++i) {
		int y0 = luma[i + 1];
		int y1 = luma[i + 2];
		int y2 = luma[i + 3];

		int d = 0;
		if (y1 < y0 && y1 < y2) {
			d = (y0 < y2) ? (y1 - y0) : (y1 - y2);
		} else if (y1 > y0 && y1 > y2) {
			d = (y0 > y2) ? (y1 - y0) : (y1 - y2);
		}

		if (i & 1)
			d = -d;

		artsum |= d;
		inv[i] = (sint8)d;

		if (d)
			luma2[i] = (uint8)((y0 + 2 * y1 + y2 + 2) >> 2);
		else
			luma2[i] = (uint8)y1;
	}

	if (!artsum) {
		atc_blit_no_artifacts(e, dst, src, true);
		return;
	}

	const uint32 *palette = e->mb_expanded_range_output ? e->m_signed_palette : e->m_palette;
	uint32 *dst2 = dst;

	for (int x = 0; x < ATC_ARTIFACTING_N; ++x) {
		uint8 p = src[x];
		int art = inv[x];

		if (!art) {
			*dst2++ = palette[p];
		} else {
			int c = p >> 4;
			int cr = e->m_chroma_vectors[c][2];
			int cg = e->m_chroma_vectors[c][1];
			int cb = e->m_chroma_vectors[c][0];
			int y = e->m_luma_ramp[luma2[x]];

			cr += e->m_artifact_ramp[art + 15][2] + y;
			cg += e->m_artifact_ramp[art + 15][1] + y;
			cb += e->m_artifact_ramp[art + 15][0] + y;

			cr >>= 6;
			cg >>= 6;
			cb >>= 6;

			cr = ATC_CLAMP(cr, 0, 255);
			cg = ATC_CLAMP(cg, 0, 255);
			cb = ATC_CLAMP(cb, 0, 255);

			*dst2++ = (uint32)cb + ((uint32)cg << 8) + ((uint32)cr << 16);
		}
	}

	if (!e->mb_bypass_output_correction && !e->mb_gamma_identity) {
		uint32 xpost = include_hblank ? 0u : (uint32)ATC_LEFT_BORDER_7MHZ;
		uint32 npost = include_hblank ? (uint32)ATC_ARTIFACTING_N : (uint32)(ATC_RIGHT_BORDER_7MHZ - ATC_LEFT_BORDER_7MHZ);
		atc_gamma_correct((uint8 *)(dst + xpost), npost, e->m_gamma_table);
	}
}

static void atc_artifact_ntsc_hi(ATC_ArtifactingEngine *e, uint32 dst[ATC_ARTIFACTING_N * 2], const uint8 src[ATC_ARTIFACTING_N], bool scanline_has_hires, bool include_hblank) {
	ATC_ALIGN(16) uint32 rout[ATC_ARTIFACTING_N + 16];
	ATC_ALIGN(16) uint32 gout[ATC_ARTIFACTING_N + 16];
	ATC_ALIGN(16) uint32 bout[ATC_ARTIFACTING_N + 16];

#if ATC_USE_M4X
	if (scanline_has_hires) {
		atc_ntsc_accum_m4x((uint8 *)(rout + 2), e->m4x.m_pal_to_r, src, ATC_ARTIFACTING_N);
		atc_ntsc_accum_m4x((uint8 *)(gout + 2), e->m4x.m_pal_to_g, src, ATC_ARTIFACTING_N);
		atc_ntsc_accum_m4x((uint8 *)(bout + 2), e->m4x.m_pal_to_b, src, ATC_ARTIFACTING_N);
	} else {
		atc_ntsc_accum_twin_m4x((uint8 *)(rout + 2), e->m4x.m_pal_to_r_twin, src, ATC_ARTIFACTING_N);
		atc_ntsc_accum_twin_m4x((uint8 *)(gout + 2), e->m4x.m_pal_to_g_twin, src, ATC_ARTIFACTING_N);
		atc_ntsc_accum_twin_m4x((uint8 *)(bout + 2), e->m4x.m_pal_to_b_twin, src, ATC_ARTIFACTING_N);
	}
#else
	for (int i = 0; i < ATC_ARTIFACTING_N + 16; ++i)
		rout[i] = 0x80008000;
	if (scanline_has_hires)
		atc_ntsc_accum(rout, e->m2x.m_pal_to_r, src, ATC_ARTIFACTING_N);
	else
		atc_ntsc_accum_twin(rout, e->m2x.m_pal_to_r_twin, src, ATC_ARTIFACTING_N);

	for (int i = 0; i < ATC_ARTIFACTING_N + 16; ++i)
		gout[i] = 0x80008000;
	if (scanline_has_hires)
		atc_ntsc_accum(gout, e->m2x.m_pal_to_g, src, ATC_ARTIFACTING_N);
	else
		atc_ntsc_accum_twin(gout, e->m2x.m_pal_to_g_twin, src, ATC_ARTIFACTING_N);

	for (int i = 0; i < ATC_ARTIFACTING_N + 16; ++i)
		bout[i] = 0x80008000;
	if (scanline_has_hires)
		atc_ntsc_accum(bout, e->m2x.m_pal_to_b, src, ATC_ARTIFACTING_N);
	else
		atc_ntsc_accum_twin(bout, e->m2x.m_pal_to_b_twin, src, ATC_ARTIFACTING_N);
#endif

	const int xdfinal = include_hblank ? 0 : ATC_LEFT_BORDER_14MHZ;
	const int xfinal = include_hblank ? 0 : (ATC_LEFT_BORDER_7MHZ & ~7) / 2;
	const int nfinal = include_hblank ? ATC_ARTIFACTING_N : ((ATC_RIGHT_BORDER_7MHZ - (ATC_LEFT_BORDER_7MHZ & ~7)) + 7) & ~7;

	if (e->mb_blend_mono_persistence || !e->mb_tint_color_enabled) {
#if ATC_USE_M4X
		atc_ntsc_final_u8(dst + xdfinal, (const uint8 *)(rout + 4 + xfinal), (const uint8 *)(gout + 4 + xfinal), (const uint8 *)(bout + 4 + xfinal), (uint32)nfinal);
#else
		atc_ntsc_final(dst + xdfinal, rout + 4 + xfinal * 2, gout + 4 + xfinal * 2, bout + 4 + xfinal * 2, (uint32)nfinal);
#endif
	} else {
#if ATC_USE_M4X
		atc_ntsc_final_mono_u8(dst + xdfinal, (const uint8 *)(rout + 4 + xfinal), (uint32)nfinal, e->m_mono_table);
#else
		atc_ntsc_final_mono_s16(dst + xdfinal, rout + 4 + xfinal * 2, (uint32)nfinal, e->m_mono_table);
#endif
	}

	if (!e->mb_bypass_output_correction && !e->mb_gamma_identity) {
		int xpost = include_hblank ? 0 : ATC_LEFT_BORDER_14MHZ;
		int npost = include_hblank ? ATC_ARTIFACTING_N * 2 : ATC_RIGHT_BORDER_14MHZ - ATC_LEFT_BORDER_14MHZ;
		atc_gamma_correct((uint8 *)(dst + xpost), (uint32)npost, e->m_gamma_table);
	}
}

static void atc_blend_exchange_const(uint32 *dst, const uint32 *src, uint32 n) {
	for (uint32 x = 0; x < n; ++x) {
		uint32 a = dst[x];
		uint32 b = src[x];
		dst[x] = (a | b) - (((a ^ b) >> 1) & 0x7f7f7f7f);
	}
}

static void atc_blend_exchange_update(uint32 *dst, uint32 *src, uint32 n) {
	for (uint32 x = 0; x < n; ++x) {
		uint32 a = dst[x];
		uint32 b = src[x];
		src[x] = a;
		dst[x] = (a | b) - (((a ^ b) >> 1) & 0x7f7f7f7f);
	}
}

static void atc_blend_exchange_linear_const(uint32 *dst, const uint32 *src, uint32 n, bool extended_range) {
	for (uint32 x = 0; x < n; ++x) {
		union {
			uint32 p;
			uint8 b[4];
		} a = { dst[x] }, b = { src[x] };

		if (extended_range) {
			a.b[0] = (a.b[0] >= 0x40) ? (uint8)(a.b[0] - 0x40) : 0;
			a.b[1] = (a.b[1] >= 0x40) ? (uint8)(a.b[1] - 0x40) : 0;
			a.b[2] = (a.b[2] >= 0x40) ? (uint8)(a.b[2] - 0x40) : 0;
		}

		a.b[0] = (uint8)(0.5f + sqrtf(((float)a.b[0] * (float)a.b[0] + (float)b.b[0] * (float)b.b[0]) * 0.5f));
		a.b[1] = (uint8)(0.5f + sqrtf(((float)a.b[1] * (float)a.b[1] + (float)b.b[1] * (float)b.b[1]) * 0.5f));
		a.b[2] = (uint8)(0.5f + sqrtf(((float)a.b[2] * (float)a.b[2] + (float)b.b[2] * (float)b.b[2]) * 0.5f));

		if (extended_range) {
			a.b[0] = (a.b[0] >= 0xC0) ? 0xFF : (uint8)(a.b[0] + 0x40);
			a.b[1] = (a.b[1] >= 0xC0) ? 0xFF : (uint8)(a.b[1] + 0x40);
			a.b[2] = (a.b[2] >= 0xC0) ? 0xFF : (uint8)(a.b[2] + 0x40);
		}

		dst[x] = a.p;
	}
}

static void atc_blend_exchange_linear_update(uint32 *dst, uint32 *src, uint32 n, bool extended_range) {
	for (uint32 x = 0; x < n; ++x) {
		union {
			uint32 p;
			uint8 b[4];
		} a = { dst[x] }, b = { src[x] };

		if (extended_range) {
			a.b[0] = (a.b[0] >= 0x40) ? (uint8)(a.b[0] - 0x40) : 0;
			a.b[1] = (a.b[1] >= 0x40) ? (uint8)(a.b[1] - 0x40) : 0;
			a.b[2] = (a.b[2] >= 0x40) ? (uint8)(a.b[2] - 0x40) : 0;
		}

		src[x] = a.p;

		a.b[0] = (uint8)(0.5f + sqrtf(((float)a.b[0] * (float)a.b[0] + (float)b.b[0] * (float)b.b[0]) * 0.5f));
		a.b[1] = (uint8)(0.5f + sqrtf(((float)a.b[1] * (float)a.b[1] + (float)b.b[1] * (float)b.b[1]) * 0.5f));
		a.b[2] = (uint8)(0.5f + sqrtf(((float)a.b[2] * (float)a.b[2] + (float)b.b[2] * (float)b.b[2]) * 0.5f));

		if (extended_range) {
			a.b[0] = (a.b[0] >= 0xC0) ? 0xFF : (uint8)(a.b[0] + 0x40);
			a.b[1] = (a.b[1] >= 0xC0) ? 0xFF : (uint8)(a.b[1] + 0x40);
			a.b[2] = (a.b[2] >= 0xC0) ? 0xFF : (uint8)(a.b[2] + 0x40);
		}

		dst[x] = a.p;
	}
}

static void atc_blend_mono_persistence(uint32 *dst, const uint32 *blend_src, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	while (n--) {
		float v = (float)(*dst & 255) * (1.0f / 255.0f);
		v *= v;

		float phosphor_energy = atc_u32_to_float(*blend_src) - 1.0f;
		if (phosphor_energy < 0.0f)
			phosphor_energy = 0.0f;
		if (phosphor_energy > limit)
			phosphor_energy = limit;

		phosphor_energy += v;

		float emission = phosphor_energy * (factor + phosphor_energy * factor2);
		phosphor_energy -= emission;

		++blend_src;

		emission = sqrtf(ATC_MAX(0.0f, emission));
		emission = ATC_MAX(0.0f, ATC_MIN(emission * 1023.0f, 1023.0f));
		int pal_idx = (int)(emission + 0.5f);
		*dst++ = palette[pal_idx];
	}
}

static void atc_blend_exchange_mono_persistence(uint32 *dst, uint32 *blend_dst, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	while (n--) {
		float v = (float)(*dst & 255) * (1.0f / 255.0f);
		v *= v;

		float phosphor_energy = atc_u32_to_float(*blend_dst) - 1.0f;
		if (phosphor_energy < 0.0f)
			phosphor_energy = 0.0f;
		if (phosphor_energy > limit)
			phosphor_energy = limit;

		phosphor_energy += v;

		float emission = phosphor_energy * (factor + phosphor_energy * factor2);
		phosphor_energy -= emission;

		*blend_dst++ = atc_float_to_u32(phosphor_energy + 1.0f);

		emission = sqrtf(ATC_MAX(0.0f, emission));
		emission = ATC_MAX(0.0f, ATC_MIN(emission * 1023.0f, 1023.0f));
		int pal_idx = (int)(emission + 0.5f);
		*dst++ = palette[pal_idx];
	}
}

static void atc_blend_copy_mono_persistence(uint32 *dst, uint32 *blend_dst, const uint32 *palette, float factor, float factor2, float limit, uint32 n) {
	ATC_UNUSED(factor);
	ATC_UNUSED(factor2);
	ATC_UNUSED(limit);

	while (n--) {
		float v = (float)(*dst & 255) * (1.0f / 255.0f);
		v *= v;

		float phosphor_energy = v;
		float emission = phosphor_energy;

		*blend_dst++ = atc_float_to_u32(phosphor_energy + 1.0f);

		emission = sqrtf(ATC_MAX(0.0f, emission));
		emission = ATC_MAX(0.0f, ATC_MIN(emission * 1023.0f, 1023.0f));
		int pal_idx = (int)(emission + 0.5f);
		*dst++ = palette[pal_idx];
	}
}

static void atc_blend(ATC_ArtifactingEngine *e, uint32 *dst, const uint32 *src, uint32 n) {
	if (e->mb_blend_mono_persistence) {
		atc_blend_mono_persistence(dst, src, e->m_mono_table2, e->m_mono_persistence_f1, e->m_mono_persistence_f2, e->m_mono_persistence_limit, n);
		return;
	}

	if (e->mb_blend_linear)
		atc_blend_exchange_linear_const(dst, src, n, e->mb_expanded_range_output);
	else
		atc_blend_exchange_const(dst, src, n);
}

static void atc_blend_exchange(ATC_ArtifactingEngine *e, uint32 *dst, uint32 *blend_dst, uint32 n) {
	if (e->mb_blend_mono_persistence) {
		atc_blend_exchange_mono_persistence(dst, blend_dst, e->m_mono_table2, e->m_mono_persistence_f1, e->m_mono_persistence_f2, e->m_mono_persistence_limit, n);
		return;
	}

	if (e->mb_blend_linear)
		atc_blend_exchange_linear_update(dst, blend_dst, n, e->mb_expanded_range_output);
	else
		atc_blend_exchange_update(dst, blend_dst, n);
}

static void atc_blend_copy(ATC_ArtifactingEngine *e, uint32 *dst, uint32 *blend_dst, uint32 n) {
	if (e->mb_blend_mono_persistence) {
		atc_blend_copy_mono_persistence(dst, blend_dst, e->m_mono_table2, e->m_mono_persistence_f1, e->m_mono_persistence_f2, e->m_mono_persistence_limit, n);
		return;
	}

	memcpy(blend_dst, dst, n * sizeof(uint32));
}

ATC_ArtifactingEngine *atc_artifacting_create(void) {
	ATC_ArtifactingEngine *e = (ATC_ArtifactingEngine *)calloc(1, sizeof(ATC_ArtifactingEngine));
	if (!e)
		return NULL;
	atc_artifacting_params_default(&e->m_artifacting_params);
	return e;
}

void atc_artifacting_destroy(ATC_ArtifactingEngine *engine) {
	free(engine);
}

void atc_color_params_default_pal(ATC_ColorParams *out_params) {
	if (!out_params)
		return;
	ATC_ColorParams p = {0};
	p.mHueStart = -12.0f;
	p.mHueRange = 18.3f * 15.0f;
	p.mBrightness = 0.0f;
	p.mContrast = 1.0f;
	p.mSaturation = 0.29f;
	p.mGammaCorrect = 1.0f;
	p.mIntensityScale = 1.0f;
	p.mArtifactHue = 80.0f;
	p.mArtifactSat = 0.80f;
	p.mArtifactSharpness = 0.50f;
	p.mRedShift = 0.0f;
	p.mRedScale = 1.0f;
	p.mGrnShift = 0.0f;
	p.mGrnScale = 1.0f;
	p.mBluShift = 0.0f;
	p.mBluScale = 1.0f;
	p.mUsePALQuirks = 1;
	p.mLumaRampMode = ATC_LUMA_RAMP_XL;
	p.mColorMatchingMode = ATC_COLOR_MATCH_NONE;
	*out_params = p;
}

void atc_color_params_default_ntsc(ATC_ColorParams *out_params) {
	if (!out_params)
		return;
	ATC_ColorParams p = {0};
	p.mHueStart = -57.0f;
	p.mHueRange = 27.1f * 15.0f;
	p.mBrightness = -0.04f;
	p.mContrast = 1.04f;
	p.mSaturation = 0.20f;
	p.mGammaCorrect = 1.0f;
	p.mIntensityScale = 1.0f;
	p.mArtifactHue = 252.0f;
	p.mArtifactSat = 1.15f;
	p.mArtifactSharpness = 0.50f;
	p.mRedShift = 0.0f;
	p.mRedScale = 1.0f;
	p.mGrnShift = 0.0f;
	p.mGrnScale = 1.0f;
	p.mBluShift = 0.0f;
	p.mBluScale = 1.0f;
	p.mUsePALQuirks = 0;
	p.mLumaRampMode = ATC_LUMA_RAMP_XL;
	p.mColorMatchingMode = ATC_COLOR_MATCH_SRGB;
	*out_params = p;
}

void atc_artifacting_params_default(ATC_ArtifactingParams *out_params) {
	if (!out_params)
		return;
	out_params->mScanlineIntensity = 0.75f;
	out_params->mDistortionViewAngleX = 0.0f;
	out_params->mDistortionYRatio = 0.0f;
	out_params->mEnableBloom = 0;
	out_params->mBloomScanlineCompensation = 1;
	out_params->mBloomRadius = 0.0f;
	out_params->mBloomDirectIntensity = 0.80f;
	out_params->mBloomIndirectIntensity = 0.40f;
	out_params->mEnableHDR = 0;
	out_params->mSDRIntensity = 200.0f;
	out_params->mHDRIntensity = 350.0f;
	out_params->mUseSystemSDR = 0;
	out_params->mUseSystemSDRAsHDR = 0;
}

void atc_artifacting_set_color_params(
	ATC_ArtifactingEngine *engine,
	const ATC_ColorParams *params,
	const float *matrix3x3,
	const float *tint_color3,
	int monitor_mode,
	int pal_phase) {
	if (!engine || !params)
		return;

	engine->m_color_params = *params;
	engine->m_monitor_mode = monitor_mode;
	engine->m_pal_phase = pal_phase;
	engine->mb_tint_color_enabled = tint_color3 != NULL;
	if (tint_color3) {
		engine->m_raw_tint_color = atc_vec3(tint_color3[0], tint_color3[1], tint_color3[2]);
	}
	engine->mb_enable_color_correction = matrix3x3 != NULL;
	ATC_UNUSED(matrix3x3);

	atc_recompute_color_tables(engine);
}

void atc_artifacting_set_artifacting_params(
	ATC_ArtifactingEngine *engine,
	const ATC_ArtifactingParams *params) {
	if (!engine || !params)
		return;
	engine->m_artifacting_params = *params;
	engine->mb_high_pal_tables_inited = false;
	engine->mb_high_ntsc_tables_inited = false;
}

void atc_artifacting_get_artifacting_params(
	const ATC_ArtifactingEngine *engine,
	ATC_ArtifactingParams *out_params) {
	if (!engine || !out_params)
		return;
	*out_params = engine->m_artifacting_params;
}

void atc_artifacting_begin_frame(
	ATC_ArtifactingEngine *engine,
	int pal,
	int chroma_artifact,
	int chroma_artifact_hi,
	int blend_in,
	int blend_out,
	int blend_linear,
	int blend_mono_persistence,
	int bypass_output_correction,
	int extended_range_input,
	int extended_range_output,
	int deinterlacing) {
	if (!engine)
		return;

	engine->mb_pal = pal != 0;
	engine->mb_chroma_artifacts = chroma_artifact != 0;
	engine->mb_chroma_artifacts_hi = chroma_artifact_hi != 0;
	engine->mb_bypass_output_correction = bypass_output_correction != 0;
	engine->mb_expanded_range_input = extended_range_input != 0;
	engine->mb_expanded_range_output = extended_range_output != 0;
	engine->mb_deinterlacing = deinterlacing != 0;

	engine->mb_blend_copy = blend_in == 0;
	engine->mb_blend_active = blend_out != 0;
	engine->mb_blend_linear = engine->mb_blend_active && (blend_linear != 0);
	engine->mb_blend_mono_persistence = engine->mb_blend_active && (blend_mono_persistence != 0);

	if (engine->mb_blend_mono_persistence)
		atc_recompute_mono_persistence(engine, engine->mb_pal ? 1.0f / 50.0f : 1.0f / 60.0f);

	if (engine->mb_blend_mono_persistence != engine->mb_color_tables_mono_persistence)
		atc_recompute_color_tables(engine);

	if (engine->mb_pal && engine->mb_chroma_artifacts && engine->mb_chroma_artifacts_hi) {
		if (!engine->mb_high_pal_tables_inited || engine->mb_high_tables_signed != engine->mb_expanded_range_output)
			atc_recompute_pal_tables(engine);
		atc_memset32(engine->m_pal_delay_line_uv, 0x20002000, sizeof(engine->m_pal_delay_line_uv) / sizeof(engine->m_pal_delay_line_uv[0][0]));
	} else if (!engine->mb_pal && engine->mb_chroma_artifacts && engine->mb_chroma_artifacts_hi) {
		if (!engine->mb_high_ntsc_tables_inited || engine->mb_high_tables_signed != engine->mb_expanded_range_output)
			atc_recompute_ntsc_tables(engine);
	} else if (engine->mb_pal && engine->mb_chroma_artifacts) {
		memset(engine->m_pal_delay_line32, 0, sizeof(engine->m_pal_delay_line32));
	}
}

void atc_artifacting_suspend_frame(ATC_ArtifactingEngine *engine) {
	if (!engine)
		return;
	engine->mb_saved_pal = engine->mb_pal;
	engine->mb_saved_chroma_artifacts = engine->mb_chroma_artifacts;
	engine->mb_saved_chroma_artifacts_hi = engine->mb_chroma_artifacts_hi;
	engine->mb_saved_bypass_output_correction = engine->mb_bypass_output_correction;
	engine->mb_saved_blend_active = engine->mb_blend_active;
	engine->mb_saved_blend_copy = engine->mb_blend_copy;
	engine->mb_saved_blend_linear = engine->mb_blend_linear;
	engine->mb_saved_expanded_range_input = engine->mb_expanded_range_input;
	engine->mb_saved_expanded_range_output = engine->mb_expanded_range_output;
}

void atc_artifacting_resume_frame(ATC_ArtifactingEngine *engine) {
	if (!engine)
		return;
	engine->mb_pal = engine->mb_saved_pal;
	engine->mb_chroma_artifacts = engine->mb_saved_chroma_artifacts;
	engine->mb_chroma_artifacts_hi = engine->mb_saved_chroma_artifacts_hi;
	engine->mb_bypass_output_correction = engine->mb_saved_bypass_output_correction;
	engine->mb_blend_active = engine->mb_saved_blend_active;
	engine->mb_blend_copy = engine->mb_saved_blend_copy;
	engine->mb_blend_linear = engine->mb_saved_blend_linear;
	engine->mb_expanded_range_input = engine->mb_saved_expanded_range_input;
	engine->mb_expanded_range_output = engine->mb_saved_expanded_range_output;
}

void atc_artifacting_artifact8(
	ATC_ArtifactingEngine *engine,
	uint32 y,
	uint32 *dst,
	const uint8 *src,
	int scanline_has_hires,
	int temporary_update,
	int include_hblank) {
	if (!engine || !dst || !src)
		return;

	if (!engine->mb_chroma_artifacts) {
		atc_blit_no_artifacts(engine, dst, src, scanline_has_hires != 0);
	} else if (engine->mb_pal) {
		if (engine->mb_chroma_artifacts_hi)
			atc_artifact_pal_hi(engine, dst, src, scanline_has_hires != 0, (y & 1) != 0);
		else
			atc_blit_no_artifacts(engine, dst, src, scanline_has_hires != 0);
	} else {
		if (engine->mb_chroma_artifacts_hi)
			atc_artifact_ntsc_hi(engine, dst, src, scanline_has_hires != 0, include_hblank != 0);
		else
			atc_artifact_ntsc(engine, dst, src, scanline_has_hires != 0, include_hblank != 0);
	}

	if (engine->mb_blend_active && y < ATC_ARTIFACTING_M) {
		uint32 *blend_dst = engine->mb_chroma_artifacts_hi ? engine->m_prev_frame_14mhz[y] : engine->m_prev_frame_7mhz[y];
		uint32 n = engine->mb_chroma_artifacts_hi ? ATC_ARTIFACTING_N * 2 : ATC_ARTIFACTING_N;

		if (!include_hblank) {
			if (engine->mb_chroma_artifacts_hi) {
				blend_dst += ATC_LEFT_BORDER_14MHZ_4;
				dst += ATC_LEFT_BORDER_14MHZ_4;
				n = ATC_RIGHT_BORDER_14MHZ_4 - ATC_LEFT_BORDER_14MHZ_4;
			} else {
				blend_dst += ATC_LEFT_BORDER_7MHZ_4;
				dst += ATC_LEFT_BORDER_7MHZ_4;
				n = ATC_RIGHT_BORDER_7MHZ_4 - ATC_LEFT_BORDER_7MHZ_4;
			}
		}

		if (engine->mb_blend_copy) {
			if (!temporary_update)
				atc_blend_copy(engine, dst, blend_dst, n);
		} else {
			if (temporary_update)
				atc_blend(engine, dst, blend_dst, n);
			else
				atc_blend_exchange(engine, dst, blend_dst, n);
		}
	}

}

static void atc_artifact_compress_range(uint32 *dst, uint32 width) {
	while (width--) {
		uint32 c = *dst;
		uint32 r = (c >> 16) & 0xFF;
		uint32 g = (c >> 8) & 0xFF;
		uint32 b = c & 0xFF;

		r = (r < 0x40) ? 0 : (r >= 0xC0) ? 0xFF : (r - 0x40) * 2;
		g = (g < 0x40) ? 0 : (g >= 0xC0) ? 0xFF : (g - 0x40) * 2;
		b = (b < 0x40) ? 0 : (b >= 0xC0) ? 0xFF : (b - 0x40) * 2;

		*dst = (c & 0xFF000000) | (r << 16) | (g << 8) | b;
		++dst;
	}
}

void atc_artifacting_artifact32(
	ATC_ArtifactingEngine *engine,
	uint32 y,
	uint32 *dst,
	uint32 width,
	int temporary_update,
	int include_hblank) {
	if (!engine || !dst)
		return;

	if (engine->mb_pal && engine->mb_chroma_artifacts) {
		int compress_output = engine->mb_expanded_range_input && !engine->mb_expanded_range_output;
		atc_artifact_pal32(dst, engine->m_pal_delay_line32, width, compress_output);
	} else if (engine->mb_expanded_range_input && !engine->mb_expanded_range_output) {
		atc_artifact_compress_range(dst, width);
	}

	if (engine->mb_blend_active && y < ATC_ARTIFACTING_M && width <= ATC_ARTIFACTING_N * 2) {
		uint32 *blend_dst = width > ATC_ARTIFACTING_N ? engine->m_prev_frame_14mhz[y] : engine->m_prev_frame_7mhz[y];

		if (engine->mb_blend_copy) {
			if (!temporary_update)
				atc_blend_copy(engine, dst, blend_dst, width);
		} else {
			if (temporary_update)
				atc_blend(engine, dst, blend_dst, width);
			else
				atc_blend_exchange(engine, dst, blend_dst, width);
		}
	}

	if (!engine->mb_bypass_output_correction && !engine->mb_gamma_identity)
		atc_gamma_correct((uint8 *)dst, width, engine->m_gamma_table);

	ATC_UNUSED(include_hblank);
}

void atc_artifacting_interpolate_scanlines(
	ATC_ArtifactingEngine *engine,
	uint32 *dst,
	const uint32 *src1,
	const uint32 *src2,
	uint32 count) {
	ATC_UNUSED(engine);
	if (!dst || !src1 || !src2)
		return;
	for (uint32 i = 0; i < count; ++i) {
		uint32 a = src1[i];
		uint32 b = src2[i];
		dst[i] = (a | b) - (((a ^ b) >> 1) & 0x7f7f7f7f);
	}
}

void atc_artifacting_deinterlace(
	ATC_ArtifactingEngine *engine,
	uint32 frame_y,
	uint32 *dst,
	const uint32 *src1,
	const uint32 *src2,
	uint32 count) {
	ATC_UNUSED(engine);
	ATC_UNUSED(frame_y);
	if (!dst)
		return;
	if (src1 && src2) {
		for (uint32 i = 0; i < count; ++i)
			dst[i] = (src1[i] | src2[i]) - (((src1[i] ^ src2[i]) >> 1) & 0x7f7f7f7f);
	} else if (src2) {
		memcpy(dst, src2, count * sizeof(uint32));
	}
}
