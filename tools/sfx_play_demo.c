/* sfx_play_demo.c — render a scripted effect sequence through the mixer to a WAV.
 *
 *   sfx_play_demo DATAROOT OUT.wav
 *
 * DATAROOT is the directory holding SOUND/ (the same root sfx_groups.h paths are
 * relative to). The banks come from the project's own reader, the mix comes from
 * src/sfx_play.c, and the result is a 44100 Hz mono s16 WAV the owner can listen
 * to. It renders the way the game would: trigger events at their time, then mix a
 * block, 512 frames at a time.
 *
 * Nothing is committed from this program's output: it refuses to write inside a
 * git work tree, because a WAV decoded from retail data must never be
 * committable. Write it under the reports tree instead.
 *
 * build: cc -O2 -std=c99 -Wall -Wextra -Werror -D_DEFAULT_SOURCE -Isrc
 *        tools/sfx_play_demo.c src/sfx_play.c src/sfx_bank.c -lm
 */
#include "sfx_play.h"
#include "sfx_groups.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define OUT_RATE     44100u
#define BLOCK        512                    /* frames per mixing pass, like a callback */
#define CLIPS_PER_GROUP 12                  /* samples decoded from each bank used */

typedef struct {
    SfxBank bank;
    SfxClip clips[CLIPS_PER_GROUP];
    int nclips;
    int loaded;
} Entry;

/* One trigger: start clip `clip` of group `group` at time `t`. */
typedef struct {
    double t;
    const char *group;
    int clip;
    float gain;
    float pitch;
    int loop;
    int slot;                               /* 0 = transient, else remember the handle */
} Event;

/* One parameter change on an earlier slot. */
typedef struct {
    double t;
    int slot;
    float gain;
    float pitch;
} Retune;

typedef struct {
    double t;
    int slot;                               /* 0 = stop everything */
} Halt;

/* A short tour of the effect groups: an ambient bed, menu clicks, shifts, a
 * turbo loop that gets swept in pitch, impacts stacked on purpose, then rain,
 * whoosh and traffic. The bed is deliberately quiet: these retail samples peak
 * near full scale, so the loops leave headroom for the one-shots and the
 * measured rail hits stay where the script intends them. Clipping is still
 * counted and printed, not hidden. */
static const Event SCRIPT[] = {
    {  0.00, "roadnoise", 0, 0.18f, 1.00f, 1, 1 },
    {  0.00, "wind",      0, 0.13f, 1.00f, 1, 2 },
    {  0.30, "menu",      0, 0.65f, 1.00f, 0, 0 },
    {  0.55, "menu",      1, 0.65f, 1.00f, 0, 0 },
    {  0.80, "menu",      2, 0.60f, 1.00f, 0, 0 },
    {  1.00, "shifting",  0, 0.70f, 1.00f, 0, 0 },
    {  1.10, "turbo",     0, 0.18f, 0.90f, 1, 3 },
    {  1.20, "nos",       0, 0.60f, 1.00f, 0, 0 },
    {  2.00, "collision", 0, 0.70f, 1.00f, 0, 0 },
    {  2.35, "skid",      0, 0.25f, 1.00f, 1, 4 },
    {  2.60, "collision", 1, 0.65f, 1.00f, 0, 0 },
    {  3.20, "rain",      0, 0.17f, 1.00f, 1, 5 },
    {  4.00, "collision", 0, 0.70f, 1.00f, 0, 0 },
    {  4.03, "collision", 2, 0.65f, 1.00f, 0, 0 },
    {  4.40, "shifting",  1, 0.65f, 1.00f, 0, 0 },
    {  5.00, "whoosh",    0, 0.60f, 1.00f, 0, 0 },
    {  5.60, "nos",       1, 0.55f, 1.00f, 0, 0 },
    {  6.20, "traffic",   0, 0.30f, 1.00f, 1, 6 },
    {  6.80, "environment", 0, 0.35f, 1.00f, 0, 0 },
    {  7.40, "misc",      0, 0.45f, 1.00f, 0, 0 },
    {  7.80, "collision", 3, 0.70f, 1.00f, 0, 0 },
    {  8.10, "menu",     10, 0.55f, 1.00f, 0, 0 },
    {  8.60, "fx",        0, 0.40f, 1.00f, 0, 0 }
};

static const Retune RETUNES[] = {
    { 1.60, 3, 0.24f, 1.05f },
    { 2.20, 3, 0.28f, 1.25f },
    { 2.80, 3, 0.32f, 1.45f },              /* boost coming in */
    { 3.40, 3, 0.24f, 1.20f },
    { 4.00, 3, 0.18f, 1.00f },
    { 5.00, 4, 0.34f, 1.10f },              /* skid bites harder */
    { 6.50, 2, 0.18f, 1.15f },              /* wind picks up */
    { 7.60, 5, 0.12f, 1.00f }
};

static const Halt HALTS[] = {
    { 4.60, 3 },                             /* turbo loop let go */
    { 7.00, 6 },                             /* traffic loop let go */
    { 9.20, 0 }                              /* everything else */
};

#define N_SCRIPT   ((int)(sizeof SCRIPT   / sizeof SCRIPT[0]))
#define N_RETUNES  ((int)(sizeof RETUNES  / sizeof RETUNES[0]))
#define N_HALTS    ((int)(sizeof HALTS    / sizeof HALTS[0]))

#define TOTAL_SECONDS 10.5

/* A WAV decoded from retail data must never end up committable, so refuse any
 * output path that sits inside a git work tree. */
static int inside_repo(const char *path)
{
    char dir[1024], probe[1100];
    size_t n = strlen(path), i;
    if (n == 0 || n >= sizeof dir) return 1;
    memcpy(dir, path, n + 1);
    if (memchr(path, '/', n) == NULL) return 1;   /* bare name: cwd unknown, refuse */
    for (i = n; i > 0; i--) {
        if (dir[i - 1] != '/') continue;
        dir[i - 1] = '\0';
        if (snprintf(probe, sizeof probe, "%s/.git", dir) >= (int)sizeof probe) return 1;
        if (access(probe, F_OK) == 0) return 1;
        dir[i - 1] = '/';
    }
    return 0;
}

static void w32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v;
    p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16);
    p[3] = (unsigned char)(v >> 24);
}

static int wav_write(const char *path, const int16_t *pcm, size_t frames)
{
    unsigned char hdr[44];
    size_t n = frames * 2;
    size_t d;
    FILE *f;
    f = fopen(path, "wb");
    if (!f) return 0;
    memcpy(hdr, "RIFF", 4);
    w32(hdr + 4, (uint32_t)(36 + n));
    memcpy(hdr + 8, "WAVEfmt ", 8);
    w32(hdr + 16, 16);                            /* fmt chunk length */
    hdr[20] = 1; hdr[21] = 0;                     /* PCM */
    hdr[22] = 1; hdr[23] = 0;                     /* mono */
    w32(hdr + 24, OUT_RATE);
    w32(hdr + 28, OUT_RATE * 2);                  /* bytes per second */
    hdr[32] = 2; hdr[33] = 0;                     /* block align */
    hdr[34] = 16; hdr[35] = 0;                    /* bits */
    memcpy(hdr + 36, "data", 4);
    w32(hdr + 40, (uint32_t)n);
    if (fwrite(hdr, 1, 44, f) != 44) { fclose(f); return 0; }
    for (d = 0; d < n; d += 65536) {
        size_t take = n - d < 65536 ? n - d : 65536;
        if (fwrite((const char *)pcm + d, 1, take, f) != take) { fclose(f); return 0; }
    }
    return fclose(f) == 0;
}

/* First table row whose group matches; the demo uses one bank per group and the
 * other rows are documented alternatives. */
static int group_row(const char *name)
{
    unsigned i;
    for (i = 0; i < SFX_GROUP_COUNT; i++)
        if (strcmp(SFX_GROUPS[i].group, name) == 0) return (int)i;
    return -1;
}

static int load_group(Entry *e, const char *path)
{
    int i;
    if (!sfx_bank_open_file(&e->bank, path)) {
        fprintf(stderr, "  open failed: %s (%s)\n", path, e->bank.error);
        return 0;
    }
    e->loaded = 1;
    for (i = 0; i < CLIPS_PER_GROUP && i < e->bank.count; i++) {
        if (sfx_clip_from_bank(&e->clips[i], &e->bank, i)) e->nclips = i + 1;
        else memset(&e->clips[i], 0, sizeof e->clips[i]);
    }
    return e->nclips > 0;
}

static void free_group(Entry *e)
{
    int i;
    for (i = 0; i < CLIPS_PER_GROUP; i++) sfx_clip_free(&e->clips[i]);
    sfx_bank_close(&e->bank);
    e->loaded = 0;
}

int main(int argc, char **argv)
{
    Entry *table;
    SfxPlayer *pl;
    int16_t *pcm;
    int16_t block[BLOCK];
    int handles[8];
    size_t total_frames, cap;
    int i, ei, ri, hi, frame, peak = 0, rail_run = 0, run = 0, rail_frames = 0;
    int loaded_groups = 0, rows_ok = 0, rows_bad = 0, events = 0, retunes = 0, halts = 0;
    double tsec;

    if (argc != 3) {
        fprintf(stderr, "usage: %s DATAROOT OUT.wav\n", argv[0]);
        return 2;
    }
    if (inside_repo(argv[2])) {
        fprintf(stderr, "refusing to write %s: it is inside a git work tree.\n", argv[2]);
        fprintf(stderr, "This audio is decoded from retail data and must never be "
                        "committable; write it under the reports tree instead.\n");
        return 2;
    }

    table = (Entry *)calloc(SFX_GROUP_COUNT, sizeof *table);
    if (!table) return 1;

    /* Pass 1: every row of the table must open and decode — the table is only
     * as good as the bank behind it. */
    printf("validating the group table (%u rows)\n", SFX_GROUP_COUNT);
    for (i = 0; i < (int)SFX_GROUP_COUNT; i++) {
        char path[1024];
        Entry probe;
        int samples = 0, clips = 0;
        memset(&probe, 0, sizeof probe);
        if (snprintf(path, sizeof path, "%s/%s", argv[1], SFX_GROUPS[i].path)
            >= (int)sizeof path) {
            printf("  %-12s %s: path too long\n", SFX_GROUPS[i].group, SFX_GROUPS[i].path);
            rows_bad++;
            continue;
        }
        if (!load_group(&probe, path)) {
            rows_bad++;
        } else {
            samples = probe.bank.count;
            clips = probe.nclips;
            printf("  %-12s %-40s samples=%-4d decoded=%d\n",
                   SFX_GROUPS[i].group, SFX_GROUPS[i].path, samples, clips);
            rows_ok++;
        }
        free_group(&probe);
    }

    /* Pass 2: load what the script needs and keep it alive for the whole
     * render — a voice only borrows the clip's memory. */
    for (i = 0; i < N_SCRIPT; i++) {
        int row = group_row(SCRIPT[i].group);
        char path[1024];
        if (row < 0 || table[row].loaded) continue;
        if (snprintf(path, sizeof path, "%s/%s", argv[1], SFX_GROUPS[row].path)
            >= (int)sizeof path) continue;
        if (load_group(&table[row], path)) loaded_groups++;
        else printf("  script: group %s unavailable\n", SCRIPT[i].group);
    }
    if (!loaded_groups) {
        fprintf(stderr, "no bank could be loaded from %s\n", argv[1]);
        free(table);
        return 1;
    }

    pl = sfx_player_create(OUT_RATE);
    if (!pl) { free(table); return 1; }

    total_frames = (size_t)(TOTAL_SECONDS * OUT_RATE);
    cap = total_frames + BLOCK;
    pcm = (int16_t *)calloc(cap, sizeof *pcm);
    if (!pcm) { sfx_player_destroy(pl); free(table); return 1; }

    ei = ri = hi = 0;
    memset(handles, 0, sizeof handles);
    for (frame = 0; (size_t)frame < total_frames; frame += BLOCK) {
        int k;
        double now = (double)frame / (double)OUT_RATE;
        while (ei < N_SCRIPT && SCRIPT[ei].t <= now) {
            const Event *ev = &SCRIPT[ei++];
            int row = group_row(ev->group);
            Entry *e = (row >= 0) ? &table[row] : NULL;
            SfxClip *c = NULL;
            int idx = ev->clip;
            if (e && e->nclips) {
                if (idx >= e->nclips) idx = e->nclips - 1;
                c = &e->clips[idx];
            }
            if (c && c->frames) {
                int h = sfx_play(pl, c, ev->gain, ev->pitch, ev->loop);
                if (h && ev->slot && ev->slot < (int)(sizeof handles / sizeof handles[0]))
                    handles[ev->slot] = h;
                if (h) events++;
            }
        }
        while (ri < N_RETUNES && RETUNES[ri].t <= now) {
            const Retune *rt = &RETUNES[ri++];
            if (rt->slot < (int)(sizeof handles / sizeof handles[0]) && handles[rt->slot]) {
                sfx_set(pl, handles[rt->slot], rt->gain, rt->pitch);
                retunes++;
            }
        }
        while (hi < N_HALTS && HALTS[hi].t <= now) {
            const Halt *hl = &HALTS[hi++];
            if (hl->slot == 0) { sfx_stop_all(pl); halts++; }
            else if (hl->slot < (int)(sizeof handles / sizeof handles[0]) && handles[hl->slot]) {
                sfx_stop(pl, handles[hl->slot]);
                handles[hl->slot] = 0;
                halts++;
            }
        }
        memset(block, 0, sizeof block);
        sfx_mix_s16(pl, block, BLOCK);
        for (k = 0; (size_t)(frame + k) < total_frames && k < BLOCK; k++) {
            int16_t s = block[k];
            int v = s < 0 ? -s : s;
            size_t at = (size_t)frame + (size_t)k;
            if (v > peak) peak = v;
            pcm[at] = s;
            if (s == 32767 || s == -32768) {  /* either rail counts as clipping */
                rail_frames++;
                run++;
                if (run > rail_run) rail_run = run;
            } else {
                run = 0;
            }
        }
    }
    tsec = (double)total_frames / (double)OUT_RATE;

    if (!wav_write(argv[2], pcm, total_frames)) {
        fprintf(stderr, "failed to write %s\n", argv[2]);
        return 1;
    }

    printf("banks opened %d (failed %d) | groups used %d\n", rows_ok, rows_bad, loaded_groups);
    printf("events %d, retunes %d, stops %d\n", events, retunes, halts);
    printf("wrote %s: %lu frames, %.2f s, %u Hz mono s16\n",
           argv[2], (unsigned long)total_frames, tsec, OUT_RATE);
    printf("peak amplitude %d | frames on a rail %d (%.3f%%) | longest rail run %d\n",
           peak, rail_frames, 100.0 * (double)rail_frames / (double)total_frames, rail_run);
    printf("%s\n", rail_run > 8 ? "CLIPS: yes, sustained rail hits" : "clips: no sustained rail hits");

    free(pcm);
    for (i = 0; i < (int)SFX_GROUP_COUNT; i++) free_group(&table[i]);
    sfx_player_destroy(pl);
    free(table);
    return 0;
}
