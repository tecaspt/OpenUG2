/* ppm_cmp a.ppm b.ppm [max_mse]
 * Prints the mean squared error over all RGB channels and exits 0 when it is
 * within the limit (default 2.0). Binary P6 PPMs only, identical dimensions.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Img {
	unsigned char *px;
	int w, h;
};

static int read_ppm(const char *path, struct Img *out)
{
	FILE *f;
	char magic[3] = {0, 0, 0};
	int w = 0, h = 0, maxv = 0;
	long size;
	unsigned char *px;

	f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "ppm_cmp: cannot open %s\n", path);
		return -1;
	}
	if (fscanf(f, "%2s", magic) != 1 || strcmp(magic, "P6") != 0) {
		fprintf(stderr, "ppm_cmp: %s is not a binary P6 PPM (got %s)\n", path, magic);
		fclose(f);
		return -1;
	}
	if (fscanf(f, "%d %d %d", &w, &h, &maxv) != 3) {
		fprintf(stderr, "ppm_cmp: %s has no size header\n", path);
		fclose(f);
		return -1;
	}
	if (maxv != 255) {
		fprintf(stderr, "ppm_cmp: %s has maxval %d, only 255 supported\n", path, maxv);
		fclose(f);
		return -1;
	}
	if (w <= 0 || h <= 0) {
		fprintf(stderr, "ppm_cmp: %s has bad size %dx%d\n", path, w, h);
		fclose(f);
		return -1;
	}
	fgetc(f);   /* the single whitespace that separates header and pixels */

	size = (long)w * h * 3;
	px = (unsigned char *)malloc((size_t)size);
	if (!px) {
		fprintf(stderr, "ppm_cmp: out of memory\n");
		fclose(f);
		return -1;
	}
	if (fread(px, 1, (size_t)size, f) != (size_t)size) {
		fprintf(stderr, "ppm_cmp: %s is truncated\n", path);
		free(px);
		fclose(f);
		return -1;
	}
	fclose(f);
	out->px = px;
	out->w = w;
	out->h = h;
	return 0;
}

int main(int argc, char **argv)
{
	struct Img a = {NULL, 0, 0}, b = {NULL, 0, 0};
	double max_mse = 2.0, sum = 0.0, mse;
	long i, n;

	if (argc < 3 || argc > 4) {
		fprintf(stderr, "usage: ppm_cmp a.ppm b.ppm [max_mse]\n");
		return 2;
	}
	if (argc == 4)
		max_mse = atof(argv[3]);
	if (read_ppm(argv[1], &a) || read_ppm(argv[2], &b))
		return 2;
	if (a.w != b.w || a.h != b.h) {
		fprintf(stderr, "ppm_cmp: size mismatch %dx%d vs %dx%d\n", a.w, a.h, b.w, b.h);
		return 2;
	}

	n = (long)a.w * a.h * 3;
	for (i = 0; i < n; i++) {
		double d = (double)a.px[i] - (double)b.px[i];
		sum += d * d;
	}
	mse = sum / (double)n;

	printf("ppm_cmp: %dx%d samples=%ld mse=%.6f limit=%.6f\n", a.w, a.h, n, mse, max_mse);
	if (mse <= max_mse) {
		printf("ppm_cmp: PASS\n");
		return 0;
	}
	printf("ppm_cmp: FAIL\n");
	return 1;
}
