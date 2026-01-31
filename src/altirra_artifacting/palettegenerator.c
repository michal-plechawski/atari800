#include "palettegenerator.h"

static const uint32 g_atc_mono_color_white = 0xFFFFFFu;

static ATC_Vec3 atc_clip_linear_color_to_srgb(ATC_Vec3 c) {
	const ATC_Vec3 luma_axis = atc_vec3(0.2126f, 0.7152f, 0.0722f);
	float luma = atc_vec3_dot(c, luma_axis);
	ATC_Vec3 luma_vec = atc_vec3(luma, luma, luma);
	ATC_Vec3 chroma = atc_vec3_sub(c, luma_vec);
	float scale = ATC_MAX(0.0f, ATC_MIN(1.0f, 2.0f * (1.0f - luma)));
	ATC_Vec3 out = atc_vec3_add(luma_vec, atc_vec3_mul_scalar(chroma, scale));
	return atc_linear_to_srgb(out);
}

static bool atc_get_mono_color_linear(int monitor_mode, ATC_Vec3 *out) {
	uint32 srgb = 0;

	switch (monitor_mode) {
		case ATC_MONITOR_MONO_AMBER:
			srgb = 0xFCCA03;
			break;
		case ATC_MONITOR_MONO_GREEN:
			srgb = 0x00FF20;
			break;
		case ATC_MONITOR_MONO_BLUISH_WHITE:
			srgb = 0x8AC2FF;
			break;
		case ATC_MONITOR_MONO_WHITE:
			srgb = g_atc_mono_color_white;
			if (!srgb)
				return false;
			break;
		default:
			return false;
	}

	ATC_Vec3 tint_linear = atc_srgb_to_linear(atc_rgb8_to_vec3(srgb));
	const ATC_Vec3 luma_axis = atc_vec3(0.2126f, 0.7152f, 0.0722f);
	float lum = atc_vec3_dot(tint_linear, luma_axis);
	*out = atc_vec3_div_scalar(tint_linear, lum);
	return true;
}

static bool atc_get_mono_color(int monitor_mode, ATC_Vec3 *out) {
	if (!atc_get_mono_color_linear(monitor_mode, out))
		return false;
	*out = atc_linear_to_srgb(*out);
	return true;
}

void atc_palette_generate(ATC_ColorPaletteGenerator *gen, const ATC_ColorParams *params, int monitor_mode) {
	ATC_Mat2 rot;
	ATC_Vec2 co_r = atc_vec2(0.956f, 0.621f);
	ATC_Vec2 co_g = atc_vec2(-0.272f, -0.647f);
	ATC_Vec2 co_b = atc_vec2(-1.107f, 1.704f);

	rot = atc_mat2_rotation(params->mRedShift * ((float)M_PI / 180.0f));
	co_r = atc_mat2_mul_vec2(rot, co_r);
	co_r.x *= params->mRedScale;
	co_r.y *= params->mRedScale;

	rot = atc_mat2_rotation(params->mGrnShift * ((float)M_PI / 180.0f));
	co_g = atc_mat2_mul_vec2(rot, co_g);
	co_g.x *= params->mGrnScale;
	co_g.y *= params->mGrnScale;

	rot = atc_mat2_rotation(params->mBluShift * ((float)M_PI / 180.0f));
	co_b = atc_mat2_mul_vec2(rot, co_b);
	co_b.x *= params->mBluScale;
	co_b.y *= params->mBluScale;

	const ATC_Mat3 from_ntsc = atc_mat3_transpose((ATC_Mat3){
		{ 0.6068909f, 0.1735011f, 0.2003480f },
		{ 0.2989164f, 0.5865990f, 0.1144845f },
		{ 0.0000000f, 0.0660957f, 1.1162243f }
	});
	const ATC_Mat3 from_pal = atc_mat3_transpose((ATC_Mat3){
		{ 0.4306190f, 0.3415419f, 0.1783091f },
		{ 0.2220379f, 0.7066384f, 0.0713236f },
		{ 0.0201853f, 0.1295504f, 0.9390944f }
	});
	const ATC_Mat3 to_srgb = atc_mat3_transpose((ATC_Mat3){
		{ 3.2404542f, -1.5371385f, -0.4985314f },
		{ -0.9692660f, 1.8760108f, 0.0415560f },
		{ 0.0556434f, -0.2040259f, 1.0572252f }
	});
	const ATC_Mat3 to_adobe = atc_mat3_transpose((ATC_Mat3){
		{ 2.0413690f, -0.5649464f, -0.3446944f },
		{ -0.9692660f, 1.8760108f, 0.0415560f },
		{ 0.0134474f, -0.1183897f, 1.0154096f }
	});

	ATC_Mat3 mx = { {0,0,0}, {0,0,0}, {0,0,0} };
	bool use_matrix = false;
	const ATC_Mat3 *to_mat = NULL;
	gen->output_gamma = 0.0f;

	switch (params->mColorMatchingMode) {
		case ATC_COLOR_MATCH_SRGB:
			to_mat = &to_srgb;
			gen->output_gamma = 0.0f;
			break;
		case ATC_COLOR_MATCH_GAMMA22:
			to_mat = &to_srgb;
			gen->output_gamma = 2.2f;
			break;
		case ATC_COLOR_MATCH_GAMMA24:
			to_mat = &to_srgb;
			gen->output_gamma = 2.4f;
			break;
		case ATC_COLOR_MATCH_ADOBE_RGB:
			to_mat = &to_adobe;
			gen->output_gamma = 2.2f;
			break;
		default:
			break;
	}

	if (to_mat) {
		const ATC_Mat3 *from_mat = params->mUsePALQuirks ? &from_pal : &from_ntsc;
		mx = atc_mat3_mul(*from_mat, *to_mat);
		use_matrix = true;
	}

	float luma_ramp[16];
	atc_compute_luma_ramp(params->mLumaRampMode, luma_ramp);

	const bool use_color_tint = monitor_mode != ATC_MONITOR_COLOR && monitor_mode != ATC_MONITOR_PERITEL;
	ATC_Vec3 tint_color = atc_vec3(0, 0, 0);
	if (use_color_tint)
		use_matrix = false;

	atc_get_mono_color(monitor_mode, &tint_color);

	uint32 *dst = gen->palette;
	uint32 *dst2 = gen->signed_palette;
	uint32 *dstu = gen->uncorrected_palette;

	const float gamma = 1.0f / params->mGammaCorrect;
	const float native_gamma = 2.2f;

	if (monitor_mode == ATC_MONITOR_PERITEL) {
		const ATC_Mat3 peritel = {
			{ 1.00f, 0.00f, 0.00f },
			{ 0.11f, 0.91f, 0.34f },
			{ 0.11f, 0.47f, 0.99f }
		};

		for (int i = 0; i < 8; ++i) {
			ATC_Vec3 c = atc_vec3((i & 1) ? 1.0f : 0.0f, (i & 4) ? 1.0f : 0.0f, (i & 2) ? 1.0f : 0.0f);
			c = atc_vec3_mul_mat3(c, peritel);
			c = atc_vec3_add(atc_vec3_mul_scalar(c, params->mContrast), atc_vec3(params->mBrightness, params->mBrightness, params->mBrightness));

			if (c.x > 0.0f) c.x = powf(c.x, gamma);
			if (c.y > 0.0f) c.y = powf(c.y, gamma);
			if (c.z > 0.0f) c.z = powf(c.z, gamma);

			c = atc_vec3_mul_scalar(c, params->mIntensityScale);
			*dst++ = atc_pack_rgb(atc_vec3(c.x * 255.0f, c.y * 255.0f, c.z * 255.0f));

			ATC_Vec3 c2 = atc_vec3_add(atc_vec3_mul_scalar(c, 127.0f / 255.0f), atc_vec3(64.0f / 255.0f, 64.0f / 255.0f, 64.0f / 255.0f));
			*dst2++ = atc_pack_rgb(atc_vec3(c2.x * 255.0f, c2.y * 255.0f, c2.z * 255.0f));
		}

		for (int i = 0; i < 15; ++i) {
			memcpy(dst, dst - 16, sizeof(*dst) * 16);
			dst += 16;
			memcpy(dst2, dst2 - 16, sizeof(*dst2) * 16);
			dst2 += 16;
		}

		memcpy(gen->uncorrected_palette, gen->palette, sizeof(gen->uncorrected_palette));
	} else {
		float angle = params->mHueStart * (2.0f * (float)M_PI / 360.0f);
		float angle_step = params->mHueRange * (2.0f * (float)M_PI / (360.0f * 15.0f));

		for (int hue = 0; hue < 16; ++hue) {
			float i = 0.0f;
			float q = 0.0f;

			if (hue) {
				if (params->mUsePALQuirks) {
					const ATC_PALPhaseInfo *info = &atc_pal_phase_lookup[hue - 1];
					float angle2 = angle + angle_step * info->even_phase;
					float angle3 = angle + angle_step * info->odd_phase;
					float i2 = cosf(angle2) * info->even_invert;
					float q2 = sinf(angle2) * info->even_invert;
					float i3 = cosf(angle3) * info->odd_invert;
					float q3 = sinf(angle3) * info->odd_invert;
					i = (i2 + i3) * (0.5f * params->mSaturation);
					q = (q2 + q3) * (0.5f * params->mSaturation);
				} else {
					i = params->mSaturation * cosf(angle);
					q = params->mSaturation * sinf(angle);
					angle += angle_step;
				}
			}

			ATC_Vec2 iq = atc_vec2(i, q);
			float cr = atc_vec2_dot(iq, co_r);
			float cg = atc_vec2_dot(iq, co_g);
			float cb = atc_vec2_dot(iq, co_b);

			ATC_Vec3 chroma = atc_vec3(cr, cg, cb);

			for (int luma = 0; luma < 16; ++luma) {
				float y = params->mContrast * luma_ramp[luma] + params->mBrightness;
				ATC_Vec3 rgb0 = atc_vec3_add(atc_vec3(y, y, y), chroma);
				ATC_Vec3 rgb = rgb0;

				if (use_color_tint) {
					float intensity;
					if (hue) {
						const float c = 0.125f;
						float y1 = ATC_MAX(y - c, 0.0f);
						float a = powf(y1, 2.4f);
						float b = powf(y + c, 2.4f);
						intensity = powf((a + b) * 0.5f, 1.0f / 2.4f);
					} else {
						intensity = y;
					}
					rgb = atc_vec3_mul_scalar(tint_color, intensity);
					rgb0 = rgb;
				} else if (use_matrix) {
					rgb = atc_vec3_max0(rgb);
					rgb = atc_vec3_pow(rgb, native_gamma);
					rgb = atc_vec3_mul_mat3(rgb, mx);

					switch (params->mColorMatchingMode) {
						case ATC_COLOR_MATCH_ADOBE_RGB:
						case ATC_COLOR_MATCH_GAMMA22:
							rgb = atc_vec3_pow(atc_vec3_max0(rgb), 1.0f / 2.2f);
							break;
						case ATC_COLOR_MATCH_GAMMA24:
							rgb = atc_vec3_pow(atc_vec3_max0(rgb), 1.0f / 2.4f);
							break;
						case ATC_COLOR_MATCH_SRGB:
						default: {
							ATC_Vec3 t = atc_vec3(0.0031308f, 0.0031308f, 0.0031308f);
							ATC_Vec3 lo = atc_vec3_mul_scalar(rgb, 12.92f);
							ATC_Vec3 hi = atc_vec3_sub(atc_vec3_mul_scalar(atc_vec3_pow(atc_vec3_max0(rgb), 1.0f / 2.4f), 1.055f), atc_vec3(0.055f, 0.055f, 0.055f));
							ATC_Vec3 sel;
							sel.x = (rgb.x < t.x) ? lo.x : hi.x;
							sel.y = (rgb.y < t.y) ? lo.y : hi.y;
							sel.z = (rgb.z < t.z) ? lo.z : hi.z;
							rgb = sel;
							break;
						}
					}
				}

				rgb = atc_vec3_pow(atc_vec3_max0(rgb), gamma);
				rgb = atc_vec3_mul_scalar(rgb, params->mIntensityScale);

				if (use_color_tint) {
					ATC_Vec3 linrgb = atc_srgb_to_linear(rgb);
					rgb = atc_clip_linear_color_to_srgb(linrgb);
				}

				*dst++ = atc_pack_rgb(atc_vec3(rgb.x * 255.0f, rgb.y * 255.0f, rgb.z * 255.0f));
				*dst2++ = atc_pack_rgb(atc_vec3(rgb.x * 127.0f + 64.0f, rgb.y * 127.0f + 64.0f, rgb.z * 127.0f + 64.0f));
				*dstu++ = atc_pack_rgb(atc_vec3(rgb0.x * 255.0f, rgb0.y * 255.0f, rgb0.z * 255.0f));
			}

			if (use_color_tint && hue == 1) {
				for (int i = 0; i < 14; ++i)
					memcpy(dst + i * 16, dst - 16, sizeof(*dst) * 16);
				for (int i = 0; i < 14; ++i)
					memcpy(dst2 + i * 16, dst2 - 16, sizeof(*dst2) * 16);
				for (int i = 0; i < 14; ++i)
					memcpy(dstu + i * 16, dstu - 16, sizeof(*dstu) * 16);
				break;
			}
		}
	}

	for (int i = 0; i < 256; ++i) {
		uint32 v = gen->uncorrected_palette[i];
		v = (v & 0xFFFFFF) + (((v & 0xFF00FF) * 0x130036 + (v & 0xFF00) * 0xB700 + 0x800000) & 0xFF000000);
		gen->uncorrected_palette[i] = v;
	}

	gen->has_color_matching_matrix = use_matrix ? 1 : 0;
	gen->color_matching_matrix = mx;
	gen->has_tint_color = use_color_tint ? 1 : 0;
	gen->tint_color = tint_color;
}

void atc_palette_generate_mono_ramp(const ATC_ColorParams *params, int monitor_mode, uint32 ramp[256]) {
	ATC_Vec3 c = atc_vec3(0, 0, 0);
	if (!atc_get_mono_color_linear(monitor_mode, &c))
		return;

	for (int i = 0; i < 256; ++i) {
		float raw = (float)i / 255.0f;
		float intensity = powf(raw, 2.2f);
		ATC_Vec3 rgb = atc_clip_linear_color_to_srgb(atc_vec3_mul_scalar(c, intensity));
		ramp[i] = atc_pack_rgb(atc_vec3(rgb.x * 255.0f, rgb.y * 255.0f, rgb.z * 255.0f));
	}
	ATC_UNUSED(params);
}

void atc_palette_generate_mono_persistence_ramp(const ATC_ColorParams *params, int monitor_mode, uint32 ramp[1024]) {
	ATC_Vec3 c = atc_vec3(0, 0, 0);
	if (!atc_get_mono_color_linear(monitor_mode, &c))
		return;

	for (int i = 0; i < 1024; ++i) {
		float raw = (float)i / 1023.0f;
		float intensity = raw * raw;
		ATC_Vec3 rgb = atc_clip_linear_color_to_srgb(atc_vec3_mul_scalar(c, intensity));
		ramp[i] = atc_pack_rgb(atc_vec3(rgb.x * 255.0f, rgb.y * 255.0f, rgb.z * 255.0f));
	}
	ATC_UNUSED(params);
}
