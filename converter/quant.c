/* quant.c - box-filter downscale and median-cut quantisation.
 * See quant.h. Host C99, no dependencies beyond libc.
 */
#include <stdlib.h>
#include <string.h>

#include "quant.h"

#define CH_P(p, c) ((uint32_t)((p) >> ((c) * 8)) & 0xffu)

int img_halve_to(uint32_t **px, int *w, int *h, int maxdim)
{
	if (!px || !*px || !w || !h || *w <= 0 || *h <= 0 || maxdim < 1)
		return 0;
	while (*w > maxdim || *h > maxdim) {
		int sw = *w, sh = *h;
		int dw = (*w > maxdim) ? (*w + 1) / 2 : *w;
		int dh = (*h > maxdim) ? (*h + 1) / 2 : *h;
		if (dw < 1) dw = 1;
		if (dh < 1) dh = 1;
		if (dw == sw && dh == sh)      /* cannot shrink further (1 px axis) */
			return 0;
		uint32_t *dst = (uint32_t *)malloc((size_t)dw * dh * sizeof *dst);
		if (!dst)
			return 0;
		for (int y = 0; y < dh; y++) {
			int y0 = y * sh / dh, y1 = (y + 1) * sh / dh;
			if (y1 <= y0) y1 = y0 + 1;
			if (y1 > sh) y1 = sh;
			for (int x = 0; x < dw; x++) {
				int x0 = x * sw / dw, x1 = (x + 1) * sw / dw;
				if (x1 <= x0) x1 = x0 + 1;
				if (x1 > sw) x1 = sw;
				uint64_t sum[4] = { 0, 0, 0, 0 };
				uint64_t n = 0;
				for (int yy = y0; yy < y1; yy++)
					for (int xx = x0; xx < x1; xx++) {
						uint32_t p = (*px)[(size_t)yy * sw + xx];
						for (int c = 0; c < 4; c++)
							sum[c] += CH_P(p, c);
						n++;
					}
				uint32_t o = 0;
				for (int c = 0; c < 4; c++)
					o |= (sum[c] / (n ? n : 1)) << (c * 8);
				dst[(size_t)y * dw + x] = o;
			}
		}
		free(*px);
		*px = dst;
		*w = dw;
		*h = dh;
	}
	return 1;
}

/* ---- median cut ---------------------------------------------------------- */

typedef struct {
	int beg, end;          /* half-open range into ord[] */
	uint32_t mean;         /* filled at the end */
	int fixed;             /* 1 = already one colour, cannot split */
} Box;

typedef struct {
	uint32_t *px;          /* the caller's pixels, unmodified */
	int *ord;              /* pixel indices, partitioned by box */
} Cut;

/* Squared distance over all four channels; 8 bits per channel so this cannot
 * overflow a 32-bit int (4 * 255^2 = 260100). */
static uint32_t dist2(uint32_t a, uint32_t b)
{
	uint32_t d = 0;
	for (int c = 0; c < 4; c++) {
		int s = (int)CH_P(a, c) - (int)CH_P(b, c);
		d += (uint32_t)(s * s);
	}
	return d;
}

/* Range of channel c inside ord[beg,end). Also reports whether the box is
 * already a single colour. */
static void box_range(const Cut *cu, const Box *b, int *best_ch, uint32_t *best_r,
                      int *pure, uint32_t *only)
{
	uint32_t lo[4], hi[4];
	for (int c = 0; c < 4; c++) {
		lo[c] = 255;
		hi[c] = 0;
	}
	for (int i = b->beg; i < b->end; i++) {
		uint32_t p = cu->px[cu->ord[i]];
		for (int c = 0; c < 4; c++) {
			uint32_t v = CH_P(p, c);
			if (v < lo[c]) lo[c] = v;
			if (v > hi[c]) hi[c] = v;
		}
	}
	int ch = 0;
	uint32_t r = 0;
	for (int c = 0; c < 4; c++)
		if (hi[c] - lo[c] > r) {
			r = hi[c] - lo[c];
			ch = c;
		}
	*best_ch = ch;
	*best_r = r;
	*pure = r == 0;
	*only = cu->px[cu->ord[b->beg]];
}

/* Partitions ord[beg,end) so that the median of channel ch lands at mid. */
static void nth_median(const Cut *cu, int *ord, int n, int ch)
{
	if (n < 2)
		return;
	const uint32_t *px = cu->px;
	int k = n / 2;
	int lo = 0, hi = n - 1;
	while (lo < hi) {
		uint32_t pivot = CH_P(px[ord[(lo + hi) / 2]], ch);
		int i = lo, j = hi;
		while (i <= j) {
			while (i <= j && CH_P(px[ord[i]], ch) < pivot) i++;
			while (j >= i && CH_P(px[ord[j]], ch) > pivot) j--;
			if (i <= j) {
				int t = ord[i];
				ord[i] = ord[j];
				ord[j] = t;
				i++;
				j--;
			}
		}
		if (j < k) lo = i;
		if (i > k) hi = j;
	}
}

static int cmp_u32(const void *a, const void *b)
{
	uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
	return x < y ? -1 : x > y ? 1 : 0;
}

int quant_mediacut(const uint32_t *px, int npix, int maxc, uint32_t *pal,
                   uint8_t *idx)
{
	if (!px || !pal || !idx || npix <= 0 || maxc < 1 || maxc > 256)
		return 0;

	/* Exact palette when it fits. Cheap to detect: sort a copy of the pixels
	 * and count the runs. Real car skins blow past 256 and fall through to
	 * median cut; small logos and decals do not, and should not be shifted. */
	uint32_t *sorted = (uint32_t *)malloc((size_t)npix * sizeof *sorted);
	if (!sorted)
		return 0;
	memcpy(sorted, px, (size_t)npix * sizeof *sorted);
	qsort(sorted, (size_t)npix, sizeof *sorted, cmp_u32);
	int ndistinct = 1;
	for (int i = 1; i < npix; i++)
		if (sorted[i] != sorted[i - 1])
			ndistinct++;
	if (ndistinct <= maxc) {
		int n = 0;
		for (int i = 0; i < npix; i++)
			if (i == 0 || sorted[i] != sorted[i - 1])
				pal[n++] = sorted[i];
		for (int i = 0; i < npix; i++) {
			int lo = 0, hi = n - 1;
			while (lo < hi) {
				int m = (lo + hi) / 2;
				if (pal[m] < px[i]) lo = m + 1;
				else hi = m;
			}
			idx[i] = (uint8_t)lo;
		}
		free(sorted);
		return n;
	}
	free(sorted);

	Cut cu;
	cu.px = (uint32_t *)px;
	cu.ord = (int *)malloc((size_t)npix * sizeof *cu.ord);
	Box *box = (Box *)malloc((size_t)maxc * sizeof *box);
	if (!cu.ord || !box) {
		free(cu.ord);
		free(box);
		return 0;
	}
	for (int i = 0; i < npix; i++)
		cu.ord[i] = i;
	box[0].beg = 0;
	box[0].end = npix;
	box[0].fixed = 0;
	int nbox = 1;

	while (nbox < maxc) {
		int pick = -1;
		uint32_t pr = 0;
		int pch = 0;
		for (int b = 0; b < nbox; b++) {
			if (box[b].fixed || box[b].end - box[b].beg < 2)
				continue;
			int ch, pure;
			uint32_t r, only;
			box_range(&cu, &box[b], &ch, &r, &pure, &only);
			if (pure) {
				box[b].fixed = 1;
				box[b].mean = only;
				continue;
			}
			if (r > pr) {
				pr = r;
				pch = ch;
				pick = b;
			}
		}
		if (pick < 0)
			break;                        /* every box is one colour already */
		Box *b = &box[pick];
		int n = b->end - b->beg;
		nth_median(&cu, cu.ord + b->beg, n, pch);
		int mid = b->beg + n / 2;
		if (mid <= b->beg || mid >= b->end) {   /* split made no progress */
			b->fixed = 1;
			int ch, pure;
			uint32_t r, only;
			box_range(&cu, b, &ch, &r, &pure, &only);
			b->mean = only;
			continue;
		}
		/* The new box has to go at the end of the array: everything outside
		 * [b->beg, b->end) is already partitioned and must not move. */
		box[nbox].beg = mid;
		box[nbox].end = b->end;
		box[nbox].fixed = 0;
		b->end = mid;
		nbox++;
	}

	int n = 0;
	for (int b = 0; b < nbox && n < maxc; b++) {
		Box *bx = &box[b];
		if (bx->fixed) {
			pal[n++] = bx->mean;
			continue;
		}
		if (bx->end <= bx->beg)
			continue;
		uint64_t s[4] = { 0, 0, 0, 0 };
		uint64_t cnt = 0;
		int ch, pure;
		uint32_t r, only;
		box_range(&cu, bx, &ch, &r, &pure, &only);
		if (pure) {
			pal[n++] = only;
			continue;
		}
		for (int i = bx->beg; i < bx->end; i++) {
			uint32_t p = cu.px[cu.ord[i]];
			for (int c = 0; c < 4; c++)
				s[c] += CH_P(p, c);
			cnt++;
		}
		uint32_t o = 0;
		for (int c = 0; c < 4; c++)
			o |= (uint32_t)(s[c] / (cnt ? cnt : 1)) << (c * 8);
		pal[n++] = o;
	}
	free(cu.ord);
	free(box);
	if (!n)
		return 0;

	for (int i = 0; i < npix; i++) {
		uint32_t best = 0xffffffffu;
		int bi = 0;
		for (int c = 0; c < n; c++) {
			uint32_t d = dist2(px[i], pal[c]);
			if (d < best) {
				best = d;
				bi = c;
			}
		}
		idx[i] = (uint8_t)bi;
	}
	return n;
}
