#ifndef ATC_ARTIFACTING_FILTERS_H
#define ATC_ARTIFACTING_FILTERS_H

#include "atc_internal.h"

typedef struct ATC_FilterKernel {
	int offset;
	float *coeffs;
	size_t size;
	size_t capacity;
} ATC_FilterKernel;

void atc_filter_kernel_init(ATC_FilterKernel *k);
void atc_filter_kernel_free(ATC_FilterKernel *k);
void atc_filter_kernel_clear(ATC_FilterKernel *k);
void atc_filter_kernel_resize(ATC_FilterKernel *k, size_t n);
void atc_filter_kernel_set_bicubic(ATC_FilterKernel *k, float offset, float A);
void atc_filter_kernel_convolve(ATC_FilterKernel *dst, const ATC_FilterKernel *x, const ATC_FilterKernel *y);
void atc_filter_kernel_reverse(ATC_FilterKernel *k);
float atc_filter_kernel_evaluate(const ATC_FilterKernel *k, const float *src);
void atc_filter_kernel_accumulate(const ATC_FilterKernel *k, float *dst);
void atc_filter_kernel_accumulate_sub(const ATC_FilterKernel *k, float *dst);
void atc_filter_kernel_accumulate_scale(const ATC_FilterKernel *k, float *dst, float scale);
void atc_filter_kernel_accumulate_window(const ATC_FilterKernel *k, float *dst, int offset, int limit, float scale);

void atc_filter_kernel_scale(ATC_FilterKernel *k, float scale);
void atc_filter_kernel_add(ATC_FilterKernel *dst, const ATC_FilterKernel *a, const ATC_FilterKernel *b);
void atc_filter_kernel_sub(ATC_FilterKernel *dst, const ATC_FilterKernel *a, const ATC_FilterKernel *b);
void atc_filter_kernel_modulate(ATC_FilterKernel *dst, const ATC_FilterKernel *a, const ATC_FilterKernel *b);
void atc_filter_kernel_shift(ATC_FilterKernel *dst, const ATC_FilterKernel *src, int offset);
void atc_filter_kernel_negate(ATC_FilterKernel *dst, const ATC_FilterKernel *src);
void atc_filter_kernel_trim(ATC_FilterKernel *dst, const ATC_FilterKernel *src);

void atc_filter_kernel_eval_cubic4(float co[4], float offset, float A);
void atc_filter_kernel_sample_bicubic(ATC_FilterKernel *dst, const ATC_FilterKernel *src, float offset, float step, float A);
void atc_filter_kernel_sample_point(ATC_FilterKernel *dst, const ATC_FilterKernel *src, int offset, int step);

#endif
