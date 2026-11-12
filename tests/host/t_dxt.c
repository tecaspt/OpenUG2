/* PC DXT1 -> PSP -> PC round-trip on random block bytes. The transform is a
 * permutation/repack, so random bytes must come back identical. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dxt.h"

int main(void)
{
	const unsigned w = 16, h = 16;          /* multiples of 4 */
	const size_t n = (size_t)w * h / 2;     /* 128 bytes */
	unsigned char *pc = malloc(n);
	unsigned char *psp = malloc(n);
	unsigned char *back = malloc(n);

	if (!pc || !psp || !back) { printf("FAIL alloc\n"); return 1; }
	srand(4242);
	for (size_t i = 0; i < n; i++)
		pc[i] = (unsigned char)(rand() & 0xff);

	dxt1_pc_to_psp(psp, pc, w, h);
	dxt1_psp_to_pc(back, psp, w, h);

	if (memcmp(pc, back, n) != 0) {
		size_t k = 0;
		while (k < n && pc[k] == back[k])
			k++;
		printf("FAIL dxt round-trip differs at byte %zu (%u vs %u)\n",
		       k, pc[k], back[k]);
		return 1;
	}
	printf("t_dxt PASS (16x16 DXT1)\n");
	free(pc); free(psp); free(back);
	return 0;
}
