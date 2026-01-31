#include "artifacting_filters.h"

void atc_filter_kernel_clear(ATC_FilterKernel *k) {
	k->size = 0;
}

static void atc_filter_kernel_reserve(ATC_FilterKernel *k, size_t n) {
	if (n <= k->capacity)
		return;

	float *newbuf = (float *)realloc(k->coeffs, n * sizeof(float));
	if (!newbuf)
		return;
	k->coeffs = newbuf;
	k->capacity = n;
}

void atc_filter_kernel_resize(ATC_FilterKernel *k, size_t n) {
	atc_filter_kernel_reserve(k, n);
	if (n > k->size)
		memset(k->coeffs + k->size, 0, (n - k->size) * sizeof(float));
	k->size = n;
}

void atc_filter_kernel_init(ATC_FilterKernel *k) {
	if (!k)
		return;
	k->offset = 0;
	k->coeffs = NULL;
	k->size = 0;
	k->capacity = 0;
}

void atc_filter_kernel_free(ATC_FilterKernel *k) {
	if (!k)
		return;
	free(k->coeffs);
	k->coeffs = NULL;
	k->size = 0;
	k->capacity = 0;
	k->offset = 0;
}

void atc_filter_kernel_set_bicubic(ATC_FilterKernel *k, float offset, float A) {
	const float t = offset;
	const float t2 = t * t;
	const float t3 = t2 * t;

	const float c1 =       A * t -        2.0f * A * t2 +        A * t3;
	const float c2 = 1.0f        -      (A + 3.0f) * t2 + (A + 2.0f) * t3;
	const float c3 =      -A * t + (2.0f * A + 3.0f) * t2 - (A + 2.0f) * t3;
	const float c4 =                     A * t2 -        A * t3;

	atc_filter_kernel_resize(k, 4);
	k->coeffs[0] = c1;
	k->coeffs[1] = c2;
	k->coeffs[2] = c3;
	k->coeffs[3] = c4;
	k->offset = -1;
}

void atc_filter_kernel_convolve(ATC_FilterKernel *dst, const ATC_FilterKernel *x, const ATC_FilterKernel *y) {
	dst->offset = x->offset + y->offset;

	const size_t m = x->size;
	const size_t n = y->size;
	atc_filter_kernel_resize(dst, m + n - 1);

	float *out = dst->coeffs;
	for (size_t i = 0; i < m + n - 1; ++i)
		out[i] = 0.0f;

	for (size_t i = 0; i < m; ++i) {
		const float s = x->coeffs[i];
		for (size_t j = 0; j < n; ++j)
			out[i + j] += s * y->coeffs[j];
	}
}

void atc_filter_kernel_reverse(ATC_FilterKernel *k) {
	const int n = (int)k->size;
	k->offset = -(k->offset + n - 1);
	for (int i = 0, j = n - 1; i < j; ++i, --j) {
		float tmp = k->coeffs[i];
		k->coeffs[i] = k->coeffs[j];
		k->coeffs[j] = tmp;
	}
}

float atc_filter_kernel_evaluate(const ATC_FilterKernel *k, const float *src) {
	const float *p = src + k->offset;
	float sum = 0.0f;
	for (size_t i = 0; i < k->size; ++i)
		sum += p[i] * k->coeffs[i];
	return sum;
}

void atc_filter_kernel_accumulate(const ATC_FilterKernel *k, float *dst) {
	float *p = dst + k->offset;
	for (size_t i = 0; i < k->size; ++i)
		p[i] += k->coeffs[i];
}

void atc_filter_kernel_accumulate_sub(const ATC_FilterKernel *k, float *dst) {
	float *p = dst + k->offset;
	for (size_t i = 0; i < k->size; ++i)
		p[i] -= k->coeffs[i];
}

void atc_filter_kernel_accumulate_scale(const ATC_FilterKernel *k, float *dst, float scale) {
	float *p = dst + k->offset;
	for (size_t i = 0; i < k->size; ++i)
		p[i] += k->coeffs[i] * scale;
}

void atc_filter_kernel_accumulate_window(const ATC_FilterKernel *k, float *dst, int offset, int limit, float scale) {
	const int start = k->offset + offset;
	const int end = start + (int)k->size;
	int lo = start;
	int hi = end;

	if (lo < 0)
		lo = 0;
	if (hi > limit)
		hi = limit;

	const float *src = k->coeffs + (lo - start);
	const int n = hi - lo;
	for (int i = 0; i < n; ++i)
		dst[lo + i] += src[i] * scale;
}

void atc_filter_kernel_scale(ATC_FilterKernel *k, float scale) {
	for (size_t i = 0; i < k->size; ++i)
		k->coeffs[i] *= scale;
}

void atc_filter_kernel_add(ATC_FilterKernel *dst, const ATC_FilterKernel *a, const ATC_FilterKernel *b) {
	dst->offset = (a->offset < b->offset) ? a->offset : b->offset;
	const size_t m = a->size;
	const size_t n = b->size;
	const size_t len_a = (size_t)(a->offset - dst->offset) + m;
	const size_t len_b = (size_t)(b->offset - dst->offset) + n;
	const size_t out_len = (len_a > len_b) ? len_a : len_b;

	atc_filter_kernel_resize(dst, out_len);
	for (size_t i = 0; i < out_len; ++i)
		dst->coeffs[i] = 0.0f;

	memcpy(dst->coeffs + (size_t)(a->offset - dst->offset), a->coeffs, m * sizeof(float));
	for (size_t i = 0; i < n; ++i) {
		const size_t idx = (size_t)(b->offset - dst->offset) + i;
		dst->coeffs[idx] += b->coeffs[i];
	}
}

void atc_filter_kernel_sub(ATC_FilterKernel *dst, const ATC_FilterKernel *a, const ATC_FilterKernel *b) {
	dst->offset = (a->offset < b->offset) ? a->offset : b->offset;
	const size_t m = a->size;
	const size_t n = b->size;
	const size_t len_a = (size_t)(a->offset - dst->offset) + m;
	const size_t len_b = (size_t)(b->offset - dst->offset) + n;
	const size_t out_len = (len_a > len_b) ? len_a : len_b;

	atc_filter_kernel_resize(dst, out_len);
	for (size_t i = 0; i < out_len; ++i)
		dst->coeffs[i] = 0.0f;

	memcpy(dst->coeffs + (size_t)(a->offset - dst->offset), a->coeffs, m * sizeof(float));
	for (size_t i = 0; i < n; ++i) {
		const size_t idx = (size_t)(b->offset - dst->offset) + i;
		dst->coeffs[idx] -= b->coeffs[i];
	}
}

void atc_filter_kernel_modulate(ATC_FilterKernel *dst, const ATC_FilterKernel *a, const ATC_FilterKernel *b) {
	ATC_FilterKernel r = *a;
	ATC_UNUSED(r);
	// Copy a into dst
	atc_filter_kernel_resize(dst, a->size);
	memcpy(dst->coeffs, a->coeffs, a->size * sizeof(float));
	dst->offset = a->offset;

	const size_t n = b->size;
	int off = b->offset;
	while (off > a->offset)
		off -= (int)n;
	while (off + (int)n <= a->offset)
		off += (int)n;

	ATC_ASSERT(off <= a->offset);
	ATC_ASSERT(a->offset - off < (int)n);

	size_t mod_index = (size_t)(a->offset - off);
	for (size_t i = 0; i < dst->size; ++i) {
		dst->coeffs[i] *= b->coeffs[mod_index];
		mod_index++;
		if (mod_index == n)
			mod_index = 0;
	}
}

void atc_filter_kernel_shift(ATC_FilterKernel *dst, const ATC_FilterKernel *src, int offset) {
	atc_filter_kernel_resize(dst, src->size);
	memcpy(dst->coeffs, src->coeffs, src->size * sizeof(float));
	dst->offset = src->offset + offset;
}

void atc_filter_kernel_negate(ATC_FilterKernel *dst, const ATC_FilterKernel *src) {
	atc_filter_kernel_resize(dst, src->size);
	for (size_t i = 0; i < src->size; ++i)
		dst->coeffs[i] = -src->coeffs[i];
	dst->offset = src->offset;
}

void atc_filter_kernel_trim(ATC_FilterKernel *dst, const ATC_FilterKernel *src) {
	size_t start = 0;
	size_t end = src->size;
	while (start < end && fabsf(src->coeffs[start]) < 1e-4f)
		start++;
	while (end > start && fabsf(src->coeffs[end - 1]) < 1e-4f)
		end--;

	const size_t n = end - start;
	atc_filter_kernel_resize(dst, n);
	if (n)
		memcpy(dst->coeffs, src->coeffs + start, n * sizeof(float));
	dst->offset = src->offset + (int)start;
}

void atc_filter_kernel_eval_cubic4(float co[4], float offset, float A) {
	const float t = offset;
	const float t2 = t * t;
	const float t3 = t2 * t;

	co[0] =       A * t -        2.0f * A * t2 +        A * t3;
	co[1] = 1.0f        -      (A + 3.0f) * t2 + (A + 2.0f) * t3;
	co[2] =      -A * t + (2.0f * A + 3.0f) * t2 - (A + 2.0f) * t3;
	co[3] =                     A * t2 -        A * t3;
}

void atc_filter_kernel_sample_bicubic(ATC_FilterKernel *dst, const ATC_FilterKernel *src, float offset, float step, float A) {
	const int lo = src->offset;
	const int hi = src->offset + (int)src->size;

	const float fstart = ceilf(((float)lo - 3.0f - offset) / step);
	const float flimit = (float)hi;
	const int istart = (int)fstart;

	atc_filter_kernel_clear(dst);
	dst->offset = istart;

	float fpos = fstart * step + offset;
	while (fpos < flimit) {
		float fposf = floorf(fpos);
		int ipos = (int)fposf;
		float co[4];
		atc_filter_kernel_eval_cubic4(co, fpos - fposf, A);

		float sum = 0.0f;
		int iend = ipos + 4;
		if (ipos >= lo) {
			int ilen = 4;
			if (iend > hi)
				ilen = hi - ipos;
			for (int i = 0; i < ilen; ++i)
				sum += co[i] * src->coeffs[(ipos - lo) + i];
		} else if (iend >= lo) {
			if (iend > hi)
				iend = hi;
			for (int i = lo; i < iend; ++i)
				sum += co[i - ipos] * src->coeffs[i - lo];
		}

		atc_filter_kernel_reserve(dst, dst->size + 1);
		dst->coeffs[dst->size++] = sum;

		fpos += step;
	}
}

void atc_filter_kernel_sample_point(ATC_FilterKernel *dst, const ATC_FilterKernel *src, int offset, int step) {
	const int delta = src->offset - offset;
	int pos = -delta % step;
	int out_offset = delta / step;

	if (pos < 0) {
		out_offset++;
		pos += step;
	}

	atc_filter_kernel_clear(dst);
	dst->offset = out_offset;

	const int n = (int)src->size;
	while (pos < n) {
		atc_filter_kernel_reserve(dst, dst->size + 1);
		dst->coeffs[dst->size++] = src->coeffs[pos];
		pos += step;
	}

	if (!dst->size) {
		atc_filter_kernel_reserve(dst, 1);
		dst->coeffs[0] = 0.0f;
		dst->size = 1;
	}
}
