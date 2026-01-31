#ifndef ATC_GTIATABLES_H
#define ATC_GTIATABLES_H

#include "atc_internal.h"
#include "artifacting_c.h"

typedef struct ATC_PALPhaseInfo {
	float even_phase;
	float even_invert;
	float odd_phase;
	float odd_invert;
} ATC_PALPhaseInfo;

extern const ATC_ALIGN(16) uint8 atc_analysis_color_table[24];
extern const ATC_PALPhaseInfo atc_pal_phase_lookup[15];

void atc_init_gtia_priority_tables(uint8 priority_tables[32][256]);
void atc_compute_luma_ramp(int mode, float luma_ramp[16]);

#endif
