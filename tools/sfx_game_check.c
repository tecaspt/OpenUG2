/* sfx_game_check -- drives sfx_game.h on the installed retail banks with a scripted drive and checks the result.
 * Usage: sfx_game_check DATAROOT [OUT.wav]. Exit 0 ok, 1 failure, 77 data missing (SKIP).
 * The optional WAV is decoded from retail data: write it outside any git work tree, never commit it. */
#include "sfx_game.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100
#define BLOCK 1024
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double rms(const int16_t *p, int n) { double s = 0; for (int i = 0; i < n; i++) s += (double)p[i] * p[i]; return n ? sqrt(s / n) : 0; }

static void put32(FILE *f, uint32_t v) { unsigned char b[4] = { v, v >> 8, v >> 16, v >> 24 }; fwrite(b, 1, 4, f); }
static void put16(FILE *f, uint16_t v) { unsigned char b[2] = { v, v >> 8 }; fwrite(b, 1, 2, f); }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: sfx_game_check DATAROOT [OUT.wav]\n"); return 2; }
    CHECK(sfg_create(NULL, RATE) == NULL && sfg_create(argv[1], 0) == NULL, "bad arguments refused");
    CHECK(sfg_create("/nonexistent-openug2-root", RATE) == NULL, "missing data -> NULL (host keeps synth)");
    SfxGame *g = sfg_create(argv[1], RATE);
    if (!g) { printf("SKIP: no retail banks under %s\n", argv[1]); return 77; }
    unsigned gr = sfg_groups(g);
    CHECK(gr == (SFG_HAS_IMPACT | SFG_HAS_SKID | SFG_HAS_ROAD | SFG_HAS_SHIFT), "all four groups load (got %u)", gr);

    /* 12 s scripted drive: 0-2 s silence, 2-6 s accelerate (road up, gears 1->4), 6 s crash,
     * 7-9 s slide, 9-10 s slide ends, 10-12 s stop (road down, gear 1). */
    int nblk = 12 * RATE / BLOCK;
    int16_t *buf = (int16_t *)calloc((size_t)nblk * BLOCK, sizeof *buf);
    if (!buf) return 1;
    int voices_slide = 0, voices_after = -1;
    for (int b = 0; b < nblk; b++) {
        double t = (double)b * BLOCK / RATE;
        SfgInput in; memset(&in, 0, sizeof in);
        in.master = 1.0f;
        float sp = t < 2 ? 0.0f : t < 6 ? (float)((t - 2) / 4) : t < 10 ? 1.0f : (float)fmax(0, 1 - (t - 10) / 1.5);
        in.road = sp * sp * 0.35f;
        in.gear = t < 2 ? 1 : t < 6 ? 1 + (int)((t - 2) / 1.0) : t < 10 ? 4 : 1;
        if (b == (int)(6.0 * RATE / BLOCK)) in.hit = 0.45f;
        if (t >= 7 && t < 9) in.skid = 0.30f;
        sfg_render(g, &in, buf + (size_t)b * BLOCK, BLOCK);
        if (t > 8.5 && t < 8.6) voices_slide = sfg_voices(g);
        if (t > 9.2 && voices_after < 0) voices_after = sfg_voices(g);
    }
    int16_t *p = buf;
    double quiet = rms(p, 2 * RATE - 2 * BLOCK);
    double cruise = rms(p + (int)(5.2 * RATE), RATE / 2);
    double crash = rms(p + 6 * RATE, RATE / 4);
    double slide = rms(p + (int)(7.5 * RATE), RATE);
    double ended = rms(p + (int)(9.5 * RATE), RATE / 4);
    printf("rms: still %.0f cruise %.0f crash %.0f slide %.0f after-slide %.0f\n", quiet, cruise, crash, slide, ended);
    CHECK(quiet < 1.0, "silent while stopped (rms %.1f)", quiet);
    CHECK(cruise > 300.0, "road bed audible at speed (rms %.0f)", cruise);
    CHECK(crash > cruise * 1.2, "crash louder than the road bed (%.0f vs %.0f)", crash, cruise);
    CHECK(slide > ended * 1.3, "slide audible and gone after release (%.0f vs %.0f)", slide, ended);
    CHECK(voices_slide == 2, "skid + road loops while sliding (%d)", voices_slide);
    CHECK(voices_after == 1, "skid loop released after the slide (%d)", voices_after);
    long rail = 0;
    for (long i = 0; i < (long)nblk * BLOCK; i++) if (p[i] == 32767 || p[i] == -32768) rail++;
    printf("frames on a rail: %ld (%.3f%%)\n", rail, 100.0 * rail / ((double)nblk * BLOCK));
    CHECK(rail < (long)nblk * BLOCK / 500, "effects alone stay below the rails (%ld frames)", rail);
    if (argc > 2) {
        FILE *f = fopen(argv[2], "wb");
        if (f) {
            uint32_t bytes = (uint32_t)nblk * BLOCK * 2;
            fwrite("RIFF", 1, 4, f); put32(f, 36 + bytes); fwrite("WAVEfmt ", 1, 8, f); put32(f, 16); put16(f, 1); put16(f, 1);
            put32(f, RATE); put32(f, RATE * 2); put16(f, 2); put16(f, 16); fwrite("data", 1, 4, f); put32(f, bytes);
            fwrite(p, 2, (size_t)nblk * BLOCK, f); fclose(f);
            printf("wrote %s\n", argv[2]);
        }
    }
    free(buf);
    sfg_destroy(g);
    printf("sfx_game_check: %d checks, %d failures\n%s\n", checks, fails, fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
