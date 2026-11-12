/* Host unit test for shared/ground.c. Builds synthetic COLLIDE blobs (no retail
 * bytes) and checks the ground query's height, up-normal, "highest surface at or
 * below z_from" rule, miss-outside, malformed-blob bounds and determinism. Run
 * by make test and again under ASan+UBSan by make asan. */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ground.h"

static int fails;

/* One ground triangle's 9 world floats appended to a grow buffer. */
typedef struct {
	uint8_t *b;
	size_t len, cap;
} Blob;

static void bput(Blob *bl, const void *p, size_t n)
{
	if (bl->len + n > bl->cap) {
		bl->cap = (bl->len + n) * 2 + 64;
		bl->b = realloc(bl->b, bl->cap);
		if (!bl->b)
			abort();
	}
	memcpy(bl->b + bl->len, p, n);
	bl->len += n;
}

/* Build a COLLIDE blob from ntri triangles (world xyz triples), all bucketed
 * into a single grid cell (ggw=ggh=1). `ords` lists triangle indices in list
 * order; here all go to bucket 0, so start = {0, ntri}. */
static void build_collapse(Blob *bl, const float (*tris)[9], int ntri)
{
	int32_t n = ntri, ggw = 1, ggh = 1;
	float gx0 = 0.0f, gy0 = 0.0f;
	int i;
	uint16_t start2[2];

	bl->b = 0;
	bl->len = bl->cap = 0;
	bput(bl, &n, 4);
	bput(bl, &gx0, 4);
	bput(bl, &gy0, 4);
	bput(bl, &ggw, 4);
	bput(bl, &ggh, 4);
	for (i = 0; i < ntri; i++)
		bput(bl, tris[i], 9 * sizeof(float));
	start2[0] = 0;
	start2[1] = (uint16_t)ntri;
	bput(bl, start2, 2 * sizeof(uint16_t));
	for (i = 0; i < ntri; i++) {
		uint16_t ord = (uint16_t)i;
		bput(bl, &ord, 2);
	}
}

static int near(float a, float b)
{
	return fabsf(a - b) < 1e-3f;
}

static void check(const char *what, int ok)
{
	printf("%-52s %s\n", what, ok ? "ok" : "FAIL");
	if (!ok)
		++fails;
}

int main(void)
{
	/* Case 1: a flat quad at z=1.5, two triangles. Query inside and outside. */
	{
		float q[2][9] = {
			{ 0, 0, 1.5f, 10, 0, 1.5f, 10, 10, 1.5f },
			{ 0, 0, 1.5f, 10, 10, 1.5f, 0, 10, 1.5f },
		};
		Blob bl;
		float z, n[3];
		int hit;

		build_collapse(&bl, q, 2);
		hit = u2pk_ground_at(bl.b, (uint32_t)bl.len, 5, 5, 5.0f, &z, n);
		check("flat quad: interior hits at z=1.5",
		      hit && near(z, 1.5f));
		check("flat quad: normal is +Z",
		      hit && near(n[0], 0) && near(n[1], 0) && near(n[2], 1));
		hit = u2pk_ground_at(bl.b, (uint32_t)bl.len, 20, 20, 5.0f, &z, n);
		check("flat quad: far outside misses", !hit);
		free(bl.b);
	}

	/* Case 2: two stacked surfaces; the ray takes the highest below z_from. */
	{
		float q[4][9] = {
			/* low surface z=1.0 */
			{ 0, 0, 1.0f, 10, 0, 1.0f, 10, 10, 1.0f },
			{ 0, 0, 1.0f, 10, 10, 1.0f, 0, 10, 1.0f },
			/* high surface z=3.0 (same footprint) */
			{ 0, 0, 3.0f, 10, 0, 3.0f, 10, 10, 3.0f },
			{ 0, 0, 3.0f, 10, 10, 3.0f, 0, 10, 3.0f },
		};
		Blob bl;
		float z, n[3];
		int hit;

		build_collapse(&bl, q, 4);
		hit = u2pk_ground_at(bl.b, (uint32_t)bl.len, 5, 5, 10.0f, &z, n);
		check("stacked: from z=10 takes the high surface (3.0)",
		      hit && near(z, 3.0f));
		hit = u2pk_ground_at(bl.b, (uint32_t)bl.len, 5, 5, 2.0f, &z, n);
		check("stacked: from z=2 skips high surface, takes low (1.0)",
		      hit && near(z, 1.0f));
		hit = u2pk_ground_at(bl.b, (uint32_t)bl.len, 5, 5, 0.5f, &z, n);
		check("stacked: from z=0.5 both surfaces above -> miss", !hit);
		free(bl.b);
	}

	/* Case 3: a sloped triangle; interpolated z and up-facing normal. */
	{
		float q[1][9] = { { 0, 0, 0, 10, 0, 0, 0, 10, 10 } };
		Blob bl;
		float z, n[3], invsqrt2 = 1.0f / sqrtf(2.0f);
		int hit;

		build_collapse(&bl, q, 1);
		hit = u2pk_ground_at(bl.b, (uint32_t)bl.len, 2, 2, 20.0f, &z, n);
		check("slope: z interpolated to 2.0 at (2,2)", hit && near(z, 2.0f));
		check("slope: up-facing normal (0,-0.707,0.707)",
		      hit && near(n[0], 0) && near(n[1], -invsqrt2) &&
		          near(n[2], invsqrt2));
		free(bl.b);
	}

	/* Case 4: reversed winding still yields an up-facing normal. */
	{
		float q[1][9] = { { 0, 0, 1.5f, 0, 10, 1.5f, 10, 0, 1.5f } };
		Blob bl;
		float z, n[3];
		int hit;

		build_collapse(&bl, q, 1);
		hit = u2pk_ground_at(bl.b, (uint32_t)bl.len, 5, 5, 5.0f, &z, n);
		check("reversed winding still returns +Z normal",
		      hit && near(n[2], 1));
		free(bl.b);
	}

	/* Case 5: malformed/truncated blobs must be refused, not read out of
	 * bounds. */
	{
		float q[1][9] = { { 0, 0, 1.0f, 10, 0, 1.0f, 10, 10, 1.0f } };
		Blob bl;
		float z, n[3];

		build_collapse(&bl, q, 1);
		check("blob: empty/NULL refused",
		      !u2pk_ground_at(0, 0, 1, 1, 5, &z, n));
		check("blob: header-only (size<20) refused",
		      !u2pk_ground_at(bl.b, 8, 1, 1, 5, &z, n));
		check("blob: truncated triangle list refused",
		      !u2pk_ground_at(bl.b, (uint32_t)bl.len - 1, 5, 5, 5, &z, n));
		free(bl.b);
	}

	/* Case 6: determinism - the same call twice is bit-identical. */
	{
		float q[2][9] = {
			{ 0, 0, 1.5f, 10, 0, 1.5f, 10, 10, 1.5f },
			{ 0, 0, 1.5f, 10, 10, 1.5f, 0, 10, 1.5f },
		};
		Blob bl;
		float za, zb, na[3], nb[3];
		int ha, hb;

		build_collapse(&bl, q, 2);
		ha = u2pk_ground_at(bl.b, (uint32_t)bl.len, 3.3f, 7.1f, 5.0f, &za, na);
		hb = u2pk_ground_at(bl.b, (uint32_t)bl.len, 3.3f, 7.1f, 5.0f, &zb, nb);
		check("determinism: same call, bit-identical result",
		      ha == hb && memcmp(&za, &zb, sizeof za) == 0 &&
		          memcmp(na, nb, sizeof na) == 0);
		free(bl.b);
	}

	if (fails) {
		printf("\nGROUND TEST FAILED (%d)\n", fails);
		return 1;
	}
	printf("\nALL GROUND CHECKS PASS\n");
	return 0;
}
