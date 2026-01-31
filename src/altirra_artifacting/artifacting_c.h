#ifndef ARTIFACTING_C_H
#define ARTIFACTING_C_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
	ATC_ARTIFACTING_N = 456,
	ATC_ARTIFACTING_M = 312
};

typedef enum ATC_LumaRampMode {
	ATC_LUMA_RAMP_LINEAR = 0,
	ATC_LUMA_RAMP_XL = 1,
	ATC_LUMA_RAMP_COUNT
} ATC_LumaRampMode;

typedef enum ATC_ColorMatchingMode {
	ATC_COLOR_MATCH_NONE = 0,
	ATC_COLOR_MATCH_SRGB,
	ATC_COLOR_MATCH_ADOBE_RGB,
	ATC_COLOR_MATCH_GAMMA22,
	ATC_COLOR_MATCH_GAMMA24,
	ATC_COLOR_MATCH_COUNT
} ATC_ColorMatchingMode;

typedef enum ATC_MonitorMode {
	ATC_MONITOR_COLOR = 0,
	ATC_MONITOR_PERITEL,
	ATC_MONITOR_MONO_GREEN,
	ATC_MONITOR_MONO_AMBER,
	ATC_MONITOR_MONO_BLUISH_WHITE,
	ATC_MONITOR_MONO_WHITE,
	ATC_MONITOR_COUNT
} ATC_MonitorMode;

typedef struct ATC_ColorParams {
	float mHueStart;
	float mHueRange;
	float mBrightness;
	float mContrast;
	float mSaturation;
	float mGammaCorrect;
	float mIntensityScale;
	float mArtifactHue;
	float mArtifactSat;
	float mArtifactSharpness;
	float mRedShift;
	float mRedScale;
	float mGrnShift;
	float mGrnScale;
	float mBluShift;
	float mBluScale;
	int mUsePALQuirks;
	int mLumaRampMode;
	int mColorMatchingMode;
} ATC_ColorParams;

typedef struct ATC_ArtifactingParams {
	float mScanlineIntensity;
	float mDistortionViewAngleX;
	float mDistortionYRatio;
	int mEnableBloom;
	int mBloomScanlineCompensation;
	float mBloomRadius;
	float mBloomDirectIntensity;
	float mBloomIndirectIntensity;
	int mEnableHDR;
	float mSDRIntensity;
	float mHDRIntensity;
	int mUseSystemSDR;
	int mUseSystemSDRAsHDR;
} ATC_ArtifactingParams;

typedef struct ATC_ArtifactingEngine ATC_ArtifactingEngine;

ATC_ArtifactingEngine *atc_artifacting_create(void);
void atc_artifacting_destroy(ATC_ArtifactingEngine *engine);

void atc_color_params_default_pal(ATC_ColorParams *out_params);
void atc_color_params_default_ntsc(ATC_ColorParams *out_params);
void atc_artifacting_params_default(ATC_ArtifactingParams *out_params);

void atc_artifacting_set_color_params(
	ATC_ArtifactingEngine *engine,
	const ATC_ColorParams *params,
	const float *matrix3x3,
	const float *tint_color3,
	int monitor_mode,
	int pal_phase);

void atc_artifacting_set_artifacting_params(
	ATC_ArtifactingEngine *engine,
	const ATC_ArtifactingParams *params);

void atc_artifacting_get_artifacting_params(
	const ATC_ArtifactingEngine *engine,
	ATC_ArtifactingParams *out_params);

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
	int deinterlacing);

void atc_artifacting_suspend_frame(ATC_ArtifactingEngine *engine);
void atc_artifacting_resume_frame(ATC_ArtifactingEngine *engine);

void atc_artifacting_artifact8(
	ATC_ArtifactingEngine *engine,
	uint32_t y,
	uint32_t *dst,
	const uint8_t *src,
	int scanline_has_hires,
	int temporary_update,
	int include_hblank);

void atc_artifacting_artifact32(
	ATC_ArtifactingEngine *engine,
	uint32_t y,
	uint32_t *dst,
	uint32_t width,
	int temporary_update,
	int include_hblank);

void atc_artifacting_interpolate_scanlines(
	ATC_ArtifactingEngine *engine,
	uint32_t *dst,
	const uint32_t *src1,
	const uint32_t *src2,
	uint32_t count);

void atc_artifacting_deinterlace(
	ATC_ArtifactingEngine *engine,
	uint32_t frame_y,
	uint32_t *dst,
	const uint32_t *src1,
	const uint32_t *src2,
	uint32_t count);

#ifdef __cplusplus
}
#endif

#endif
