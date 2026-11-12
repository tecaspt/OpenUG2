/* An image with exactly 200 distinct colours quantised at maxc=256 must keep
 * all 200 exactly (quant.c takes the exact-palette path when distinct <= maxc,
 * so no box mean is allowed to shift a colour by even one channel step). */
#include <stdio.h>
#include <stdlib.h>
#include "quant.h"

int main(void)
{
	const int NC = 200;
	const int np = 400;                     /* 400 px drawn from 200 colours */
	uint32_t px[400];
	uint32_t col[200];
	uint32_t pal[256];
	uint8_t idx[400];

	for (int i = 0; i < NC; i++) {
		uint32_t r = (uint32_t)((i * 7 + 3) & 0xff);
		uint32_t g = (uint32_t)((i * 13 + 5) & 0xff);
		uint32_t b = (uint32_t)((i * 29 + 11) & 0xff);
		col[i] = 0xff000000u | (r << 16) | (g << 8) | b;
	}
	for (int i = 0; i < np; i++)
		px[i] = col[i % NC];

	int n = quant_mediacut(px, np, 256, pal, idx);
	if (n <= 0 || n > 256) {
		printf("FAIL bad palette count %d\n", n);
		return 1;
	}
	for (int i = 0; i < np; i++)
		if (pal[idx[i]] != px[i]) {
			printf("FAIL pixel %d mapped to %08x, expected %08x\n",
			       i, pal[idx[i]], px[i]);
			return 1;
		}
	for (int c = 0; c < NC; c++) {
		int found = 0;
		for (int k = 0; k < n; k++)
			if (pal[k] == col[c]) { found = 1; break; }
		if (!found) {
			printf("FAIL source colour %d (%08x) absent from palette\n", c, col[c]);
			return 1;
		}
	}
	printf("t_quant PASS (%d entries, all 200 colours exact)\n", n);
	return 0;
}
