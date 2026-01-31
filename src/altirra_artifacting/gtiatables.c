#include "gtiatables.h"

enum {
	kColorP0 = 0,
	kColorP1,
	kColorP2,
	kColorP3,
	kColorPF0,
	kColorPF1,
	kColorPF2,
	kColorPF3,
	kColorBAK,
	kColorBlack,
	kColorP0P1,
	kColorP2P3,
	kColorPF0P0,
	kColorPF0P1,
	kColorPF0P0P1,
	kColorPF1P0,
	kColorPF1P1,
	kColorPF1P0P1,
	kColorPF2P2,
	kColorPF2P3,
	kColorPF2P2P3,
	kColorPF3P2,
	kColorPF3P3,
	kColorPF3P2P3
};

const ATC_ALIGN(16) uint8 atc_analysis_color_table[24] = {
	0x1a, 0x5a, 0x7a, 0x9a,
	0x03, 0x07, 0x0b, 0x0f,
	0x01,
	0x00,
	0x3a, 0x8a,
	0x13, 0x53, 0x33,
	0x17, 0x57, 0x37,
	0x1b, 0x5b, 0x3b,
	0x1f, 0x5f, 0x3f
};

const ATC_PALPhaseInfo atc_pal_phase_lookup[15] = {
	{  0.0f,  1,  0.0f,  1 },
	{  1.0f,  1,  1.0f,  1 },
	{ -6.0f, -1,  2.0f,  1 },
	{ -5.0f, -1, -5.0f, -1 },
	{ -4.0f, -1, -4.0f, -1 },
	{ -3.0f, -1, -3.0f, -1 },
	{ -1.0f, -1, -1.0f, -1 },
	{  0.0f, -1,  0.0f, -1 },
	{  1.0f, -1,  1.0f, -1 },
	{ -6.0f,  1,  2.0f, -1 },
	{ -4.0f,  1, -4.0f,  1 },
	{ -3.0f,  1, -3.0f,  1 },
	{ -2.0f,  1, -2.0f,  1 },
	{ -1.0f,  1, -1.0f,  1 },
	{  0.0f,  1,  0.0f,  1 }
};

void atc_init_gtia_priority_tables(uint8 priority_tables[32][256]) {
	memset(priority_tables, 0, 32 * 256);

	for (int prior = 0; prior < 32; ++prior) {
		const bool multi = (prior & 16) != 0;
		const bool pri0 = (prior & 1) != 0;
		const bool pri1 = (prior & 2) != 0;
		const bool pri2 = (prior & 4) != 0;
		const bool pri3 = (prior & 8) != 0;
		const bool pri01 = pri0 | pri1;
		const bool pri12 = pri1 | pri2;
		const bool pri23 = pri2 | pri3;
		const bool pri03 = pri0 | pri3;

		for (int i = 0; i < 256; ++i) {
			static const uint8 kPlayfieldPriorityTable[8] = { 0, 1, 2, 2, 4, 4, 4, 4 };
			const uint8 v = kPlayfieldPriorityTable[i & 7];

			const bool pf0 = (v & 1) != 0;
			const bool pf1 = (v & 2) != 0;
			const bool pf2 = (v & 4) != 0;
			const bool pf3 = (i & 8) != 0;
			const bool p0 = (i & 16) != 0;
			const bool p1 = (i & 32) != 0;
			const bool p2 = (i & 64) != 0;
			const bool p3 = (i & 128) != 0;

			const bool p01 = p0 | p1;
			const bool p23 = p2 | p3;
			const bool pf01 = pf0 | pf1;
			const bool pf23 = pf2 | pf3;

			const bool sp0 = p0 & !(pf01 & pri23) & !(pri2 & pf23);
			const bool sp1 = p1 & !(pf01 & pri23) & !(pri2 & pf23) & (!p0 || multi);
			const bool sp2 = p2 & !p01 & !(pf23 & pri12) & !(pf01 & !pri0);
			const bool sp3 = p3 & !p01 & !(pf23 & pri12) & !(pf01 & !pri0) & (!p2 || multi);

			const bool sf3 = pf3 & !(p23 & pri03) & !(p01 & !pri2);
			const bool sf2 = pf2 & !(p23 & pri03) & !(p01 & !pri2) & !sf3;
			const bool sf1 = pf1 & !(p23 & pri0) & !(p01 & pri01) & !sf3;
			const bool sf0 = pf0 & !(p23 & pri0) & !(p01 & pri01) & !sf3;

			const bool sb = !p01 & !p23 & !pf01 & !pf23;

			int out = 0;
			if (sf0) out += 0x001;
			if (sf1) out += 0x002;
			if (sf2) out += 0x004;
			if (sf3) out += 0x008;
			if (sp0) out += 0x010;
			if (sp1) out += 0x020;
			if (sp2) out += 0x040;
			if (sp3) out += 0x080;
			if (sb)  out += 0x100;

			uint8 c;
			switch (out) {
				default:
					c = kColorBlack;
					break;
				case 0x000: c = kColorBlack; break;
				case 0x001: c = kColorPF0; break;
				case 0x002: c = kColorPF1; break;
				case 0x004: c = kColorPF2; break;
				case 0x008: c = kColorPF3; break;
				case 0x010: c = kColorP0; break;
				case 0x011: c = kColorPF0P0; break;
				case 0x012: c = kColorPF1P0; break;
				case 0x020: c = kColorP1; break;
				case 0x021: c = kColorPF0P1; break;
				case 0x022: c = kColorPF1P1; break;
				case 0x030: c = kColorP0P1; break;
				case 0x031: c = kColorPF0P0P1; break;
				case 0x032: c = kColorPF1P0P1; break;
				case 0x040: c = kColorP2; break;
				case 0x044: c = kColorPF2P2; break;
				case 0x048: c = kColorPF3P2; break;
				case 0x080: c = kColorP3; break;
				case 0x084: c = kColorPF2P3; break;
				case 0x088: c = kColorPF3P3; break;
				case 0x0c0: c = kColorP2P3; break;
				case 0x0c4: c = kColorPF2P2P3; break;
				case 0x0c8: c = kColorPF3P2P3; break;
				case 0x100: c = kColorBAK; break;
			}

			priority_tables[prior][i] = c;
		}
	}
}

void atc_compute_luma_ramp(int mode, float luma_ramp[16]) {
	if (mode == ATC_LUMA_RAMP_LINEAR) {
		for (int i = 0; i < 16; ++i)
			luma_ramp[i] = (float)i / 15.0f;
		return;
	}

	const float alt_ramp[16] = {
		0.0f,
		0.0658340f,
		0.1435022f,
		0.2093362f,
		0.2750246f,
		0.3408586f,
		0.4185267f,
		0.4843608f,
		0.5156392f,
		0.5814733f,
		0.6591414f,
		0.7249754f,
		0.7906638f,
		0.8564978f,
		0.9341660f,
		1.0f
	};

	memcpy(luma_ramp, alt_ramp, sizeof(float) * 16);
}
