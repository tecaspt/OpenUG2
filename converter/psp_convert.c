/* psp_convert - retail NFSU2 data -> U2PK v1 for the PSP runtime.
 *
 *   psp_convert car <NFSU2_DIR> <CARNAME> <out.u2pk>
 *
 * <NFSU2_DIR> is the directory holding CARS/ and GLOBAL/ (the CD mount). It is
 * read only; the program never writes inside it. The named car becomes
 * <out.u2pk> in the file you point at (typically a fresh path under ms0/).
 *
 * After writing the file the driver reads it back and runs it through u2pk_open,
 * the exact reader the PSP uses, plus the cross-checks the container cannot make
 * about itself (the LOD and wheel batch ranges have to tile the BATCHES section
 * exactly). A file that does not survive that round trip is deleted rather than
 * shipped, so a passing line is evidence the runtime can actually load the car.
 *
 * It prints one row per LOD group - player, AI, wheels - with the triangle
 * counts before and after clustering and the LOD budget, which is the table the
 * milestone asks for.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "car_conv.h"
#include "region_conv.h"
#include "u2pk.h"

static char *slurp(const char *path, size_t *outlen)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	if (fseek(f, 0, SEEK_END) != 0) {
		fclose(f);
		return 0;
	}
	long n = ftell(f);
	if (n < 0) {
		fclose(f);
		return 0;
	}
	rewind(f);
	char *b = (char *)malloc((size_t)n + 1);
	if (!b) {
		fclose(f);
		return 0;
	}
	if (fread(b, 1, (size_t)n, f) != (size_t)n) {
		free(b);
		fclose(f);
		return 0;
	}
	fclose(f);
	b[n] = 0;
	*outlen = (size_t)n;
	return b;
}

/* Reads the file back and checks it against the report. Returns 1 when the file
 * is loadable and its own counts agree with what the converter claimed. On
 * failure writes the reason into why (a static buffer, one line). */
static int validate(const char *path, const CarReport *rep, const char **why)
{
	static char msg[256];
	size_t len = 0;
	char *raw = slurp(path, &len);
	if (!raw) {
		*why = "cannot read the file it just wrote";
		return 0;
	}
	U2pkView v;
	int ok = u2pk_open((const uint8_t *)raw, (uint32_t)len, &v);
	if (!ok) {
		snprintf(msg, sizeof msg, "u2pk_open rejected it: %s",
		         v.err ? v.err : "no reason");
		free(raw);
		*why = msg;
		return 0;
	}
	if ((long)len != rep->total) {
		snprintf(msg, sizeof msg, "wrote %ld bytes but the file is %zu",
		         rep->total, len);
		free(raw);
		*why = msg;
		return 0;
	}
	if (v.hdr.kind != U2PK_KIND_CAR || v.hdr.version != U2PK_VERSION) {
		*why = "kind or version is not a v1 car";
		free(raw);
		return 0;
	}
	if (v.nbatch != (uint32_t)rep->nbatch_total || v.ntex != (uint32_t)rep->ntex) {
		snprintf(msg, sizeof msg, "report claims %d batches / %d textures, the "
		                  "file has %u / %u", rep->nbatch_total, rep->ntex,
		         v.nbatch, v.ntex);
		free(raw);
		*why = msg;
		return 0;
	}
	const U2pkCarInfo *ci = v.car;
	uint32_t used = ci->batch_first[0] + ci->batch_count[0] + ci->batch_count[1];
	if (ci->lod_count != U2PK_LODS || ci->batch_first[0] != 0 ||
	    ci->batch_first[1] != ci->batch_count[0] ||
	    ci->wheel_batch_first != used ||
	    used + ci->wheel_batch_count != v.nbatch) {
		snprintf(msg, sizeof msg, "the LOD/wheel batch ranges do not tile the "
		                  "%u batches", v.nbatch);
		free(raw);
		*why = msg;
		return 0;
	}
	if (ci->wheel_batch_count > 0 && !(ci->wheel_radius > 0.0f)) {
		*why = "wheels were written with a non-positive radius";
		free(raw);
		return 0;
	}
	free(raw);
	*why = 0;
	return 1;
}

static void table(const CarReport *r)
{
	static const char *nm[3] = { "LOD0 (player)", "LOD1 (AI)", "wheels" };
	printf("  %-14s tris %6d -> %6d   %s\n", nm[0], r->tris_before[0],
	       r->tris_after[0], r->ok ? "" : "(budget 3000 / 600 KB)");
	printf("  %-14s tris %6d -> %6d\n", nm[1], r->tris_before[1], r->tris_after[1]);
	printf("  %-14s tris %6d -> %6d\n", nm[2], r->tris_before[2], r->tris_after[2]);
	printf("  textures %d  cluts %d  batches %d+%d+%d=%d  file %ld KB  wheel %s "
	       "r=%.3f  skipped %d meshes\n",
	       r->ntex, r->nclut, r->nbatch[0], r->nbatch[1], r->nbatch[2],
	       r->nbatch_total, r->total / 1024, r->wheel_from_global ? "GLOBALB"
	                                                             : "body-seed",
	       r->wheel_radius, r->skipped);
}

static int one(const char *root, const char *name, const char *outpath)
{
	Car car;
	const char *why = 0;
	if (!car_open(&car, root, name, &why)) {
		printf("%-12s FAIL  load: %s\n", name, why ? why : "(no reason)");
		return 0;
	}
	printf("%-12s\n", name);

	CarReport rep;
	if (!car_convert(&car, outpath, &rep)) {
		printf("  FAIL  convert: %s\n", rep.why);
		table(&rep);
		car_close(&car);
		return 0;
	}

	const char *vwhy = 0;
	if (!validate(outpath, &rep, &vwhy)) {
		printf("  FAIL  %s\n", vwhy ? vwhy : "invalid output");
		remove(outpath); /* never ship a file the runtime cannot read */
		car_close(&car);
		return 0;
	}
	car_close(&car);
	table(&rep);
	printf("  VALIDATED\n");
	return 1;
}

static void region_table(const RegionReport *r)
{
	printf("  grid %dx%d = %d cells (%d hold geometry)\n",
	       r->grid_w, r->grid_h, r->ncell, r->ncell_used);
	printf("  tris in %lld -> out %lld (8 m split)\n", r->tris_in, r->tris_out);
	printf("  worst cell %d tris at (%d,%d)\n",
	       r->worst_cell_tris, r->worst_cell_cx, r->worst_cell_cy);
	printf("  worst 3x3 window %d tris / %d batches  (budget 15000 / 250)\n",
	       r->worst_win_tris, r->worst_win_batches);
	printf("  within budget: %s\n", r->within_budget ? "YES" : "NO");
	printf("  in/out by kind: road %lld->%lld  terrain %lld->%lld  other %lld->%lld\n",
	       r->road_in, r->road_out, r->terr_in, r->terr_out,
	       r->other_in, r->other_out);
	printf("  source edge (m): <=2 %d  <=4 %d  <=8 %d  <=16 %d  <=32 %d  >32 %d"
	       "   max %.0f   big(>32) %lld\n",
	       r->src_hist_n[0], r->src_hist_n[1], r->src_hist_n[2],
	       r->src_hist_n[3], r->src_hist_n[4], r->src_hist_n[5],
	       r->max_src_edge, r->big_src_tris);
}

static int region_cmd(const char *root, const char *name)
{
	printf("region %s (survey)\n", name);
	RegionReport r;
	if (!region_survey(root, name, &r)) {
		printf("  FAIL  survey\n");
		return 0;
	}
	region_table(&r);
	return 1;
}

static int region_convert_cmd(const char *root, const char *name,
                              const char *outdir)
{
	printf("region %s (convert -> %s)\n", name, outdir);
	RegionReport r;
	if (!region_convert(root, name, outdir, &r)) {
		printf("  FAIL  convert\n");
		return 0;
	}
	region_table(&r);
	printf("  wrote cells.u2pk %lld KB  index.u2pk %lld B  tex.u2pk %lld B\n",
	       r.cells_bytes / 1024, r.index_bytes, r.tex_bytes);
	printf("  cells %d  batches %d  textures %d\n",
	       r.ncell_written, r.nbatch_total, r.ntex);
	printf("  textures with real art %d / %d  (rest white placeholder)\n",
	       r.ntex_real, r.ntex);
	return 1;
}

static int region_poses_cmd(const char *root, const char *name,
                            const char *cfg, int npose)
{
	printf("region %s (poses -> %s)\n", name, cfg);
	int n = region_poses(root, name, cfg, npose);
	if (n < 0) {
		printf("  FAIL  poses\n");
		return 0;
	}
	printf("  wrote %d pose(s)\n", n);
	return n > 0;
}

int main(int argc, char **argv)
{
	if (argc >= 4 && strcmp(argv[1], "region") == 0) {
		if (strcmp(argv[2], "survey") == 0 && argc == 5) {
			printf("psp_convert: retail=%s\n", argv[3]);
			return region_cmd(argv[3], argv[4]) ? 0 : 1;
		}
		if (strcmp(argv[2], "convert") == 0 && argc == 6) {
			printf("psp_convert: retail=%s\n", argv[3]);
			return region_convert_cmd(argv[3], argv[4], argv[5]) ? 0 : 1;
		}
		if (strcmp(argv[2], "poses") == 0 && (argc == 6 || argc == 7)) {
			int np = argc == 7 ? atoi(argv[6]) : 3;
			printf("psp_convert: retail=%s\n", argv[3]);
			return region_poses_cmd(argv[3], argv[4], argv[5], np) ? 0 : 1;
		}
		fprintf(stderr,
		        "usage: %s region survey  <NFSU2_DIR> <REGION>\n"
		        "       %s region convert <NFSU2_DIR> <REGION> <OUTDIR>\n"
		        "       %s region poses   <NFSU2_DIR> <REGION> <CFG> [n]\n",
		        argv[0], argv[0], argv[0]);
		return 2;
	}
	if (argc != 5 || strcmp(argv[1], "car") != 0) {
		fprintf(stderr,
		        "usage: %s car <NFSU2_DIR> <CARNAME> <out.u2pk>\n"
		        "       %s region survey  <NFSU2_DIR> <REGION>   (e.g. STREAML4RH)\n"
		        "       %s region convert <NFSU2_DIR> <REGION> <OUTDIR>\n"
		        "       %s region poses   <NFSU2_DIR> <REGION> <CFG> [n]\n",
		        argv[0], argv[0], argv[0], argv[0]);
		return 2;
	}
	printf("psp_convert: retail=%s\n", argv[2]);
	return one(argv[2], argv[3], argv[4]) ? 0 : 1;
}
