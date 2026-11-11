/* Provenance census for a race direction-arrow asset (route guidance).
 *
 *   route_arrow_census DATAROOT [OUTDIR]
 *
 * Walks the in-game and global presentation bundles with the project's own
 * readers (ugui_bundle/ugui_tex/ugui_fng): every texture-pack record and every
 * screen-package (.fng) resource whose name suggests an arrow, direction aid,
 * GPS or route marker, plus every package object that references such a
 * resource. Prints names, sizes and formats only. With OUTDIR it also writes
 * each decoded candidate as RGBA PAM for local inspection: those files are
 * derived from retail data and must never be committed or shipped. */
#include "ugui_bundle.h"
#include "ugui_tex.h"
#include "ugui_fng.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *FILES[] = {
    "GLOBAL/InGameCommon.bun", "GLOBAL/InGameRace.bun", "GLOBAL/InGameDrag.bun",
    "GLOBAL/InGameDrift.bun", "GLOBAL/InGameFreeRoam.bun", "GLOBAL/InGamePerformanceTuning.bun",
    "GLOBAL/GLOBALA.BUN", "GLOBAL/GLOBALB.BUN", "GLOBAL/HUD_CustomTextures_ALL.bin",
    "GLOBAL/GlobalTexturesNA.bin", "GLOBAL/GlobalTexturesROW.bin", "GLOBAL/DYNTEX.BIN",
    "LANGUAGES/LanguageTextures.bin", "FRONTEND/FrontB.lzc",
};

static int interesting(const char *s) {
    static const char *kw[] = {"ARROW", "DIRECTION", "GPS", "MARKER", "NAVIG", "CHEVRON", "POINTER", "COMPASS", "ROUTE"};
    char up[256]; size_t n = 0;
    for (; s[n] && n + 1 < sizeof up; n++) up[n] = (char)toupper((unsigned char)s[n]);
    up[n] = 0;
    for (size_t k = 0; k < sizeof kw / sizeof *kw; k++) if (strstr(up, kw[k])) return 1;
    return 0;
}

typedef struct { UgTexLib *lib; const char *file; int packages; int textures_before; } Walk;

static int visit(void *user, const UgChunkRef *c) {
    Walk *w = (Walk *)user;
    char err[256];
    if (c->id == 0xB3300000u) { ug_texpack_parse(w->lib, c, err, sizeof err); return 1; }
    if (c->id != UG_CHUNK_PKG_HASHED && c->id != UG_CHUNK_PKG_RAW) return 0;
    UgPackageBlob blob;
    if (!ug_package_from_chunk(c, &blob, err, sizeof err)) return 1;
    UgFngPackage p;
    if (ug_fng_parse(blob.data, blob.len, &p, err, sizeof err)) {
        w->packages++;
        for (int r = 0; r < p.nres; r++) {
            if (!p.res[r].name || !interesting(p.res[r].name)) continue;
            int users = 0, scripted = 0;
            for (int o = 0; o < p.nobj; o++) if (p.obj[o].res_index == r) { users++; scripted += p.obj[o].nscripts > 0; }
            printf("PACKAGE-RESOURCE %s package=%s resource=%s kind=%u objects=%d scripted=%d\n",
                   w->file, p.name ? p.name : "?", p.res[r].name, p.res[r].kind, users, scripted);
        }
        ug_fng_free(&p);
    }
    ug_package_free(&blob);
    return 1;
}

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "usage: route_arrow_census DATAROOT [OUTDIR]\n"); return 2; }
    int candidates = 0;
    for (size_t f = 0; f < sizeof FILES / sizeof *FILES; f++) {
        char path[4096], err[256]; snprintf(path, sizeof path, "%s/%s", argv[1], FILES[f]);
        UgBuf buf = {0};
        if (!ug_bundle_read(path, &buf, err, sizeof err)) { printf("FILE %s unreadable (%s)\n", FILES[f], err); continue; }
        UgTexLib lib; ug_texlib_init(&lib);
        Walk w = {&lib, FILES[f], 0, 0};
        ug_chunks_walk(buf.data, buf.len, visit, &w, err, sizeof err);
        printf("FILE %s bytes=%zu textures=%d packages=%d\n", FILES[f], buf.len, lib.n, w.packages);
        for (int i = 0; i < lib.n; i++) {
            UgTexture *t = lib.t + i;
            if (!interesting(t->name)) continue;
            candidates++;
            const uint8_t *px = ug_texlib_pixels(&lib, i);
            long opaque = 0, coloured = 0, n = (long)t->w * t->h;
            for (long k = 0; px && k < n; k++) {
                opaque += px[4*k+3] > 16;
                int mx = px[4*k] > px[4*k+1] ? px[4*k] : px[4*k+1]; mx = mx > px[4*k+2] ? mx : px[4*k+2];
                int mn = px[4*k] < px[4*k+1] ? px[4*k] : px[4*k+1]; mn = mn < px[4*k+2] ? mn : px[4*k+2];
                coloured += px[4*k+3] > 16 && mx - mn > 40;
            }
            printf("TEXTURE %s name=%s key=%08x %dx%d fmt=0x%02x decoded=%d opaque=%.3f coloured=%.3f\n",
                   FILES[f], t->name, t->key, t->w, t->h, t->fmt, px != NULL,
                   n ? (double)opaque / n : 0, n ? (double)coloured / n : 0);
            if (argc > 2 && px) {
                char out[4096]; snprintf(out, sizeof out, "%s/%s.pam", argv[2], t->name);
                FILE *o = fopen(out, "wb");
                if (o) { fprintf(o, "P7\nWIDTH %d\nHEIGHT %d\nDEPTH 4\nMAXVAL 255\nTUPLTYPE RGB_ALPHA\nENDHDR\n", t->w, t->h);
                         fwrite(px, 4, (size_t)n, o); fclose(o); }
            }
        }
        ug_texlib_free(&lib); ug_buf_free(&buf);
    }
    printf("CANDIDATES %d\n", candidates);
    return 0;
}
