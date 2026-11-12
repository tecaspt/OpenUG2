/* The per-batch fixed-point contract in docs/U2PK.md and converter/car_conv.c
 * (batch_encode): metres = s16/32768 * pos_scale + pos_offset for positions,
 * uv = u16/32768 * uv_scale + uv_offset for UVs. Random inputs are encoded and
 * decoded and must land within the tolerance the milestone names. The same
 * formulas are what the real converter runs over every car, and its output is
 * checked end to end by the 5-car convert+validate and the M3 screenshot; this
 * test pins the numeric property on synthetic data. */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

static int16_t enc_pos(float v, float scale)
{
	float r = lroundf((v / scale) * 32767.0f);
	if (r > 32767.0f) r = 32767.0f;
	if (r < -32767.0f) r = -32767.0f;
	return (int16_t)r;
}

static float dec_pos(int16_t s, float scale, float off)
{
	return (float)s / 32768.0f * scale + off;
}

static uint16_t enc_uv(float v, float scale)
{
	float r = lroundf((v / scale) * 32768.0f);
	if (r > 65535.0f) r = 65535.0f;
	if (r < 0.0f) r = 0.0f;
	return (uint16_t)r;
}

static float dec_uv(uint16_t s, float scale, float off)
{
	return (float)s / 32768.0f * scale + off;
}

int main(void)
{
	srand(31337);

	/* A batch spanning a 4 m box has half-extent (pos_scale) up to 2 m. */
	const float poff = 0.0f, pscale = 2.0f;
	double worst_pos = 0.0;
	for (int i = 0; i < 200000; i++) {
		float p = (rand() / (float)RAND_MAX) * 4.0f - 2.0f;   /* [-2,2) */
		float d = dec_pos(enc_pos(p - poff, pscale), pscale, poff);
		double e = fabs((double)d - (double)p);
		if (e > worst_pos) worst_pos = e;
		if (e > 0.01) {
			printf("FAIL position err %.6f m (p=%f d=%f) > 1 cm\n", e, p, d);
			return 1;
		}
	}

	/* UV in [0,5]: range 5 > 1.99 so uv_scale = 5/1.99. */
	const float uoff = 0.0f, uscale = 5.0f / 1.99f;
	double worst_uv = 0.0;
	for (int i = 0; i < 200000; i++) {
		float u = (rand() / (float)RAND_MAX) * 5.0f;          /* [0,5] */
		float d = dec_uv(enc_uv(u - uoff, uscale), uscale, uoff);
		double e = fabs((double)d - (double)u);
		if (e > worst_uv) worst_uv = e;
		if (e > 1.0 / 2048.0) {
			printf("FAIL uv err %.6f (u=%f d=%f) > 1/2048\n", e, u, d);
			return 1;
		}
	}

	printf("t_vertex PASS (pos worst %.6f m <= 0.01 ; uv worst %.7f <= %.7f)\n",
	       worst_pos, worst_uv, 1.0 / 2048.0);
	return 0;
}
