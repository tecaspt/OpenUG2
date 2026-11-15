/* sfx_bank_census.c — prove src/sfx_bank.c on every retail sound bank.
 *
 *   sfx_bank_census DATAROOT [--wav OUTDIR]
 *
 * Finds every *.abk under DATAROOT/SOUND and prints one line per bank: sample count, how
 * many are decodable, the rates seen, total seconds, and any problem. Every decodable
 * sample is decoded, so a decode failure or an over-read shows up here. With --wav each
 * decoded sample is also written as a mono s16 WAV for a human to listen to, and that pass
 * prints one line per sample with its peak amplitude.
 *
 * Bank paths are printed relative to DATAROOT, so this output carries no local paths.
 * Exit status: 0 clean, 1 something failed to open or decode, 2 bad usage.
 */
#include "sfx_bank.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#define MAXPATH 1024
#define MIN_PLAUSIBLE_RATE   8000u
#define MAX_PLAUSIBLE_RATE 192000u
#define MAX_DECODE_FRAMES  (64u * 1024u * 1024u)
#define MAX_RATES 32

typedef struct { char **v; size_t n, cap; } List;

static int list_add(List *l, const char *s) {
    if (l->n == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 256;
        char **nv = (char **)realloc(l->v, nc * sizeof *nv);
        if (!nv) return 0;
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n] = strdup(s);
    if (!l->v[l->n]) return 0;
    l->n++;
    return 1;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(const char *const *)a, *(const char *const *)b);
}

static int is_abk(const char *name) {
    size_t n = strlen(name);
    if (n < 5) return 0;
    const char *e = name + n - 4;
    return e[0] == '.' && (e[1] | 0x20) == 'a' && (e[2] | 0x20) == 'b' && (e[3] | 0x20) == 'k';
}

/* Depth-first. lstat, never stat, so a symlinked directory cannot make this loop. */
static void walk(List *l, const char *root, const char *rel, int depth) {
    char full[MAXPATH];
    if (snprintf(full, sizeof full, "%s/%s", root, rel) >= (int)sizeof full) return;
    DIR *d = opendir(full);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;                       /* skips "." and ".." too */
        char cfull[MAXPATH], crel[MAXPATH];
        if (snprintf(cfull, sizeof cfull, "%s/%s", full, e->d_name) >= (int)sizeof cfull) continue;
        if (snprintf(crel, sizeof crel, "%s/%s", rel, e->d_name) >= (int)sizeof crel) continue;
        struct stat st;
        if (lstat(cfull, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < 8) walk(l, root, crel, depth + 1);
        } else if (S_ISREG(st.st_mode) && is_abk(e->d_name)) {
            list_add(l, crel);
        }
    }
    closedir(d);
}

/* mkdir -p, tolerating directories that already exist */
static int make_dirs(const char *path) {
    char tmp[MAXPATH];
    if (snprintf(tmp, sizeof tmp, "%s", path) >= (int)sizeof tmp) return 0;
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return 0;
        *p = '/';
    }
    if (mkdir(tmp, 0777) != 0 && errno != EEXIST) return 0;
    return 1;
}

static int write_wav(const char *path, uint32_t rate, const int16_t *pcm, uint32_t frames) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    uint32_t dlen = frames * 2u, flen = 36u + dlen, bps = rate * 2u;
    unsigned char h[44];
    memcpy(h, "RIFF", 4);
    h[4] = (unsigned char)flen; h[5] = (unsigned char)(flen >> 8);
    h[6] = (unsigned char)(flen >> 16); h[7] = (unsigned char)(flen >> 24);
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = 0; h[18] = 0; h[19] = 0;
    h[20] = 1; h[21] = 0;                       /* PCM */
    h[22] = 1; h[23] = 0;                       /* mono */
    h[24] = (unsigned char)rate; h[25] = (unsigned char)(rate >> 8);
    h[26] = (unsigned char)(rate >> 16); h[27] = (unsigned char)(rate >> 24);
    h[28] = (unsigned char)bps; h[29] = (unsigned char)(bps >> 8);
    h[30] = (unsigned char)(bps >> 16); h[31] = (unsigned char)(bps >> 24);
    h[32] = 2; h[33] = 0;                       /* block align */
    h[34] = 16; h[35] = 0;                      /* bits */
    memcpy(h + 36, "data", 4);
    h[40] = (unsigned char)dlen; h[41] = (unsigned char)(dlen >> 8);
    h[42] = (unsigned char)(dlen >> 16); h[43] = (unsigned char)(dlen >> 24);
    int ok = fwrite(h, 1, sizeof h, f) == sizeof h &&
             (dlen == 0 || fwrite(pcm, 1, dlen, f) == dlen);
    if (fclose(f) != 0) ok = 0;
    return ok;
}

/* appends to a fixed buffer, keeping it NUL-terminated */
static void add_note(char *note, size_t cap, const char *what) {
    size_t n = strlen(note);
    if (n + strlen(what) + 2 >= cap) return;
    note[n] = ' ';
    memcpy(note + n + 1, what, strlen(what) + 1);
}

int main(int argc, char **argv) {
    const char *root = NULL, *wavdir = NULL;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--wav") == 0) {
            if (++i >= argc) { fprintf(stderr, "--wav needs OUTDIR\n"); return 2; }
            wavdir = argv[i];
        } else if (argv[i][0] == '-') {
            fprintf(stderr, "unknown option %s\n", argv[i]);
            return 2;
        } else if (!root) {
            root = argv[i];
        } else {
            fprintf(stderr, "extra argument %s\n", argv[i]);
            return 2;
        }
    }
    if (!root) { fprintf(stderr, "usage: %s DATAROOT [--wav OUTDIR]\n", argv[0]); return 2; }

    List list = { NULL, 0, 0 };
    walk(&list, root, "SOUND", 0);
    if (list.n == 0) { printf("no .abk banks found under %s/SOUND\n", root); return 1; }
    qsort(list.v, list.n, sizeof *list.v, cmp_str);

    struct Rate { uint32_t hz; int n; } rates[MAX_RATES];
    int16_t *pcm = NULL;
    size_t pcap = 0;
    long tot_samples = 0, tot_dec = 0, tot_fail = 0, tot_silent = 0, tot_open_err = 0;
    long tot_undec = 0, tot_toc_mismatch = 0, tot_bad_rate = 0, tot_wav = 0;
    double tot_wav_bytes = 0, tot_seconds = 0.0;
    int global_peak = 0;

    for (size_t i = 0; i < list.n; i++) {
        char full[MAXPATH];
        if (snprintf(full, sizeof full, "%s/%s", root, list.v[i]) >= (int)sizeof full) continue;
        SfxBank b;
        if (!sfx_bank_open_file(&b, full)) {
            printf("%-52s OPEN-ERROR %s\n", list.v[i], b.error);
            tot_open_err++;
            continue;
        }
        int n_rates = 0, dec = 0, fail = 0, silent = 0, bad_rate = 0, peak_bank = 0;
        double secs = 0.0;
        for (int s = 0; s < b.count; s++) {
            const SfxSample *smp = &b.samples[s];
            int known = 0;
            for (int r = 0; r < n_rates; r++) if (rates[r].hz == smp->rate) { rates[r].n++; known = 1; break; }
            if (!known && n_rates < MAX_RATES) { rates[n_rates].hz = smp->rate; rates[n_rates].n = 1; n_rates++; }
            if (smp->rate < MIN_PLAUSIBLE_RATE || smp->rate > MAX_PLAUSIBLE_RATE) bad_rate++;
            if (!smp->decodable) continue;
            dec++;
            if (smp->frames > MAX_DECODE_FRAMES) { fail++; continue; }
            if ((size_t)smp->frames > pcap) {
                size_t want = (size_t)smp->frames < 2048 ? 2048 : (size_t)smp->frames;
                int16_t *np = (int16_t *)realloc(pcm, want * sizeof *pcm);
                if (!np) { fail++; continue; }
                pcm = np;
                pcap = want;
            }
            if (!sfx_bank_decode(&b, s, pcm)) { fail++; continue; }
            int peak = 0;
            for (uint32_t k = 0; k < smp->frames; k++) {
                int v = pcm[k] < 0 ? -pcm[k] : pcm[k];
                if (v > peak) peak = v;
            }
            if (peak == 0) silent++;
            if (peak > peak_bank) peak_bank = peak;
            if (peak > global_peak) global_peak = peak;
            if (smp->rate >= MIN_PLAUSIBLE_RATE && smp->rate <= MAX_PLAUSIBLE_RATE)
                secs += (double)smp->frames / (double)smp->rate;
            if (wavdir) {
                char wdir[MAXPATH], wfile[MAXPATH], base[MAXPATH];
                size_t rl = strlen(list.v[i]);
                snprintf(wdir, sizeof wdir, "%s", wavdir);
                snprintf(wfile, sizeof wfile, "%s", wavdir);
                if (rl > 4 && strlen(list.v[i]) < sizeof base) {
                    snprintf(base, sizeof base, "/%.*s", (int)(rl - 4), list.v[i]);   /* drop ".abk" */
                    strncat(wdir, base, sizeof wdir - strlen(wdir) - 1);
                    strncat(wfile, base, sizeof wfile - strlen(wfile) - 1);
                    strncat(wfile, "/", sizeof wfile - strlen(wfile) - 1);
                    char idx[16];
                    snprintf(idx, sizeof idx, "%d.wav", s);
                    strncat(wfile, idx, sizeof wfile - strlen(wfile) - 1);
                    if (make_dirs(wdir) && write_wav(wfile, smp->rate, pcm, smp->frames)) {
                        tot_wav++;
                        tot_wav_bytes += 44.0 + (double)smp->frames * 2.0;
                        printf("  wav %s#%d rate=%u frames=%u peak=%d%s\n", list.v[i], s,
                               smp->rate, smp->frames, peak, peak ? "" : " ALL-ZERO");
                    } else {
                        fail++;
                        printf("  wav %s#%d WRITE-FAILED\n", list.v[i], s);
                    }
                }
            }
        }

        char rlist[160] = "", note[128] = "";
        for (int r = 0; r < n_rates; r++) {
            char one[24];
            snprintf(one, sizeof one, "%s%u/%d", r ? "," : "", rates[r].hz, rates[r].n);
            strncat(rlist, one, sizeof rlist - strlen(rlist) - 1);
        }
        int toc = sfx_bank_index_count(&b);
        if (toc != b.count) { tot_toc_mismatch++; add_note(note, sizeof note, "toc-index-disagrees"); }
        if (fail) add_note(note, sizeof note, "decode-fail");
        if (silent) add_note(note, sizeof note, "all-zero");
        if (bad_rate) add_note(note, sizeof note, "odd-rate");
        if (!dec) add_note(note, sizeof note, "NOTHING-DECODABLE");

        printf("%-52s samples=%-4d decodable=%-4d decoded=%-4d failed=%-2d silent=%-2d "
               "seconds=%8.2f peak=%-6d rates=%s%s\n",
               list.v[i], b.count, dec, dec - fail, fail, silent, secs, peak_bank, rlist, note);
        tot_samples += b.count;
        tot_dec += dec;
        tot_fail += fail;
        tot_silent += silent;
        tot_bad_rate += bad_rate;
        tot_undec += b.count - dec;
        tot_seconds += secs;
        sfx_bank_close(&b);
    }

    printf("\n== census total ==\n");
    printf("banks              %zu  (open errors %ld)\n", list.n, tot_open_err);
    printf("samples            %ld\n", tot_samples);
    printf("decodable          %ld  (undecodable %ld)\n", tot_dec, tot_undec);
    printf("decoded ok         %ld  (decode failures %ld)\n", tot_dec - tot_fail, tot_fail);
    printf("all-zero samples   %ld\n", tot_silent);
    printf("implausible rate   %ld  (outside %u..%u Hz)\n", tot_bad_rate,
           MIN_PLAUSIBLE_RATE, MAX_PLAUSIBLE_RATE);
    printf("toc mismatches     %ld\n", tot_toc_mismatch);
    printf("audio seconds      %.2f\n", tot_seconds);
    printf("loudest amplitude  %d\n", global_peak);
    if (wavdir) printf("wav files written  %ld  (%.0f MB) in %s\n", tot_wav,
                       tot_wav_bytes / (1024.0 * 1024.0), wavdir);

    for (size_t i = 0; i < list.n; i++) free(list.v[i]);
    free(list.v);
    free(pcm);
    return (tot_fail || tot_open_err) ? 1 : 0;
}
