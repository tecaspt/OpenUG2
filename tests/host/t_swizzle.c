/* swizzle -> unswizzle round-trip on random data. Width/height must satisfy the
 * GE constraints (width_bytes % 16 == 0, height % 8 == 0): 128 x 64. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "swizzle.h"

int main(void)
{
	const unsigned W = 128, H = 64;         /* width_bytes=128, height=64 */
	const size_t n = (size_t)W * H;
	unsigned char *lin = malloc(n);
	unsigned char *swz = malloc(n);
	unsigned char *back = malloc(n);

	if (!lin || !swz || !back) { printf("FAIL alloc\n"); return 1; }
	srand(20260);
	for (size_t i = 0; i < n; i++)
		lin[i] = (unsigned char)(rand() & 0xff);

	swizzle(swz, lin, W, H);
	unswizzle(back, swz, W, H);

	if (memcmp(lin, back, n) != 0) {
		size_t k = 0;
		while (k < n && lin[k] == back[k])
			k++;
		printf("FAIL swizzle round-trip differs at byte %zu (%u vs %u)\n",
		       k, lin[k], back[k]);
		return 1;
	}
	if (memcmp(lin, swz, n) == 0)
		printf("note: swizzle was the identity on this data (still round-trips)\n");
	printf("t_swizzle PASS (128x64)\n");
	free(lin); free(swz); free(back);
	return 0;
}
