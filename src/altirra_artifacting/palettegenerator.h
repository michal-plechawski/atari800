#ifndef ATC_PALETTEGENERATOR_H
#define ATC_PALETTEGENERATOR_H

#include "atc_internal.h"
#include "artifacting_c.h"
#include "gtiatables.h"

typedef struct ATC_ColorPaletteGenerator {
	uint32 palette[256];
	uint32 signed_palette[256];
	uint32 uncorrected_palette[256];
	int has_color_matching_matrix;
	ATC_Mat3 color_matching_matrix;
	int has_tint_color;
	ATC_Vec3 tint_color;
	float output_gamma;
} ATC_ColorPaletteGenerator;

void atc_palette_generate(ATC_ColorPaletteGenerator *gen, const ATC_ColorParams *params, int monitor_mode);
void atc_palette_generate_mono_ramp(const ATC_ColorParams *params, int monitor_mode, uint32 ramp[256]);
void atc_palette_generate_mono_persistence_ramp(const ATC_ColorParams *params, int monitor_mode, uint32 ramp[1024]);

#endif
