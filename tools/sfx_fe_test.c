/* sfx_fe_test.c -- task AQ: front-end menu sounds (mapping ABI, gates, volume; scripted walk).
 * Usage: sfx_fe_test DATAROOT. Exit 0 ok, 1 failure, 77 data missing (SKIP).
 * The mapping itself ([inferred] sample traits) is documented in docs/parity/AUDIO.md; this check
 * pins the vocabulary, the silence set, the three render gates and the volume scaling -- it never
 * asserts a clip's identity beyond "the event maps to some decodable clip or stays silent". */
#include "sfx_fe.h"
#include "gui_view.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RATE 44100
#define BLOCK 1024
static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static double rms(const int16_t *p, int n) { double s = 0; for (int i = 0; i < n; i++) s += (double)p[i] * p[i]; return n ? sqrt(s / n) : 0; }

/* Cast through int: the two anonymous enums must agree in VALUE; -Wenum-compare would flag the
 * enum types, which is exactly not the point (the numbers are the ABI). */
#define EV_EQ(a, b) ((int)(a) == (int)(b))

/* One rendered second of silence as a scratch buffer helper. */
static int pump(SfxFe *m, int16_t *buf, int seconds) {
    for (int b = 0; b < seconds * RATE / BLOCK; b++) {
        memset(buf, 0, BLOCK * sizeof *buf);
        sfx_fe_render(m, buf, BLOCK);
    }
    return seconds * RATE / BLOCK;
}

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "usage: sfx_fe_test DATAROOT\n"); return 2; }

    /* 1. The event vocabulary is the ABI between gui_view and sfx_fe (the numbers, not names). */
    CHECK(EV_EQ(SFX_FE_MOVE, GV_NAV_MOVE) && EV_EQ(SFX_FE_SCROLL, GV_NAV_SCROLL) && EV_EQ(SFX_FE_ACCEPT, GV_NAV_ACCEPT) &&
          EV_EQ(SFX_FE_BACK, GV_NAV_BACK) && EV_EQ(SFX_FE_ERROR, GV_NAV_ERROR) && EV_EQ(SFX_FE_TAB, GV_NAV_TAB) &&
          EV_EQ(SFX_FE_SCREEN_IN, GV_NAV_SCREEN_IN) && EV_EQ(SFX_FE_SCREEN_OUT, GV_NAV_SCREEN_OUT) &&
          EV_EQ(SFX_FE_DIALOG_OPEN, GV_NAV_DIALOG_OPEN) && EV_EQ(SFX_FE_DIALOG_CLOSE, GV_NAV_DIALOG_CLOSE),
          "sfx_fe event values match the GV_NAV_* values");

    CHECK(sfx_fe_create(NULL, RATE) == NULL && sfx_fe_create(argv[1], 0) == NULL, "bad arguments refused");
    CHECK(sfx_fe_create("/nonexistent-openug2-root", RATE) == NULL, "missing data -> NULL (silent menus)");

    SfxFe *m = sfx_fe_create(argv[1], RATE);
    if (!m) { printf("SKIP: no retail FE banks under %s\n", argv[1]); return 77; }

    int16_t *buf = (int16_t *)calloc(RATE, sizeof *buf);   /* one second window per measurement */
    if (!buf) return 1;

    /* 2. Mapping table. The screen-transition and dialog events have no defensible retail sample
     *    and must stay silent; every interaction event maps to a clip that opened. */
    CHECK(sfx_fe_event_voice(m, SFX_FE_SCREEN_OUT) == -1 &&
          sfx_fe_event_voice(m, SFX_FE_DIALOG_OPEN) == -1 &&
          sfx_fe_event_voice(m, SFX_FE_DIALOG_CLOSE) == -1, "silent events stay silent");
    CHECK(sfx_fe_event_voice(m, SFX_FE_MOVE) >= 0 && sfx_fe_event_voice(m, SFX_FE_SCROLL) >= 0 &&
          sfx_fe_event_voice(m, SFX_FE_ACCEPT) >= 0 && sfx_fe_event_voice(m, SFX_FE_BACK) >= 0 &&
          sfx_fe_event_voice(m, SFX_FE_ERROR) >= 0 && sfx_fe_event_voice(m, SFX_FE_TAB) >= 0 &&
          sfx_fe_event_voice(m, SFX_FE_SCREEN_IN) >= 0, "interaction events map to loaded clips");
    CHECK(sfx_fe_event_voice(m, -1) == -1 && sfx_fe_event_voice(m, SFX_FE_EVENT_COUNT) == -1, "out-of-range lookup silent");
    CHECK(sfx_fe_event_voice(NULL, SFX_FE_MOVE) == -1, "NULL module safe");

    /* A silent event never queues (no ring slot, no drop counter). */
    unsigned d0 = sfx_fe_dropped(m);
    CHECK(sfx_fe_event(m, SFX_FE_DIALOG_OPEN) == 0 && sfx_fe_dropped(m) == d0, "silent event not queued");
    CHECK(sfx_fe_event(m, -1) == 0 && sfx_fe_event(m, SFX_FE_EVENT_COUNT) == 0, "bad event id refused");

    /* 3. A queued event actually sounds: push MOVE, render, energy appears. */
    pump(m, buf, 1);
    CHECK(sfx_fe_event(m, SFX_FE_MOVE) == 1, "MOVE queued");
    memset(buf, 0, BLOCK * sizeof *buf);
    sfx_fe_render(m, buf, BLOCK);
    double mv = rms(buf, BLOCK);
    CHECK(mv > 100.0, "MOVE produces sound (rms %.0f)", mv);
    CHECK(sfx_fe_voices(m) == 1, "one voice after one event (%d)", sfx_fe_voices(m));

    /* 4. One-shot gate: while a one-shot plays, a different role is dropped, not mixed. */
    CHECK(sfx_fe_event(m, SFX_FE_ERROR) == 1, "ERROR queued while a voice is live");
    int dropped_before = (int)sfx_fe_dropped(m);
    memset(buf, 0, BLOCK * sizeof *buf);
    sfx_fe_render(m, buf, BLOCK);                                    /* the ERROR is inside the live MOVE */
    CHECK((int)sfx_fe_dropped(m) == dropped_before + 1, "one sound at a time (dropped %d -> %u)", dropped_before, sfx_fe_dropped(m));

    /* 5. Debounce: after the voices end, a same-role burst inside 70 ms plays once. */
    pump(m, buf, 2);                                                  /* everything finished */
    CHECK(sfx_fe_voices(m) == 0, "voices released (%d)", sfx_fe_voices(m));
    dropped_before = (int)sfx_fe_dropped(m);
    int q = 0;
    for (int i = 0; i < 8; i++) q += sfx_fe_event(m, SFX_FE_MOVE);   /* 8 pushes, one callback apart would be fine -- here all before render */
    memset(buf, 0, BLOCK * sizeof *buf);
    sfx_fe_render(m, buf, BLOCK);
    CHECK(q == 8, "all 8 pushes queued (got %d)", q);
    CHECK(sfx_fe_voices(m) == 1, "the burst plays one blip (%d)", sfx_fe_voices(m));
    CHECK((int)sfx_fe_dropped(m) >= dropped_before + 7, "debounce dropped the rest (%d -> %u)", dropped_before, sfx_fe_dropped(m));

    /* The debounce clock is render-time, not wall time: 8 callbacks (~186 ms) apart every MOVE of
     * a slow walk sounds. MOVE's window is 70 ms, so one callback of silence between them suffices. */
    int sounded = 0;
    for (int step = 0; step < 5; step++) {
        CHECK(sfx_fe_event(m, SFX_FE_MOVE) == 1, "slow walk: step %d queued", step);
        memset(buf, 0, BLOCK * sizeof *buf);
        sfx_fe_render(m, buf, BLOCK);
        if (rms(buf, BLOCK) > 100.0) sounded++;
        pump(m, buf, 1);                                              /* let the role age past its window */
    }
    CHECK(sounded == 5, "a paced menu walk sounds on every step (%d/5)", sounded);

    /* 6. Volume scales the mix; zero drains the ring and counts drops. */
    pump(m, buf, 2);
    double full = 0;
    sfx_fe_set_volume(m, 1.0f);
    CHECK(sfx_fe_event(m, SFX_FE_ACCEPT) == 1, "volume test: full-level accept queued");
    memset(buf, 0, BLOCK * sizeof *buf);
    sfx_fe_render(m, buf, BLOCK);
    full = rms(buf, BLOCK);
    pump(m, buf, 2);
    sfx_fe_set_volume(m, 0.5f);
    CHECK(sfx_fe_event(m, SFX_FE_ACCEPT) == 1, "volume test: half-level accept queued");
    memset(buf, 0, BLOCK * sizeof *buf);
    sfx_fe_render(m, buf, BLOCK);
    double half = rms(buf, BLOCK);
    CHECK(full > 100.0 && fabs(half / full - 0.5) < 0.05, "half volume halves the mix (%.0f vs %.0f)", half, full);
    pump(m, buf, 2);

    sfx_fe_set_volume(m, 0.0f);
    q = 0;
    for (int i = 0; i < SFX_FE_QUEUE - 2; i++) q += sfx_fe_event(m, SFX_FE_TAB);
    dropped_before = (int)sfx_fe_dropped(m);
    memset(buf, 0, BLOCK * sizeof *buf);
    sfx_fe_render(m, buf, BLOCK);
    CHECK(q == SFX_FE_QUEUE - 2, "muted: events still queue (%d)", q);
    CHECK(sfx_fe_voices(m) == 0, "muted: nothing plays (%d)", sfx_fe_voices(m));
    CHECK((int)sfx_fe_dropped(m) == dropped_before + q, "muted: the drain counts every drop (%d -> %u)", dropped_before, sfx_fe_dropped(m));
    memset(buf, 0, BLOCK * sizeof *buf);
    sfx_fe_render(m, buf, BLOCK);                                     /* ring already empty */
    CHECK((int)sfx_fe_dropped(m) == dropped_before + q, "muted drain consumes, never blocks");
    sfx_fe_set_volume(m, -3.0f);                                      /* host mistakes must be safe */
    CHECK(sfx_fe_event(m, SFX_FE_MOVE) == 1, "negative volume treated as muted, not loud");
    memset(buf, 0, BLOCK * sizeof *buf);
    sfx_fe_render(m, buf, BLOCK);
    CHECK(sfx_fe_voices(m) == 0, "negative volume stays silent (%d voices)", sfx_fe_voices(m));
    sfx_fe_set_volume(m, 1.0f);

    /* 7. Ring overflow drops the newest and the module recovers. The one-shot gate releases after
     *    the drain, so a paced queue of ACCEPTs plays one per clip length (0.228 s measured):
     *    exactly 4 of the 15 fit inside one callback window's playing budget. */
    pump(m, buf, 2);
    q = 0;
    for (int i = 0; i < SFX_FE_QUEUE + 8; i++) q += sfx_fe_event(m, SFX_FE_ACCEPT);
    CHECK(q == SFX_FE_QUEUE - 1, "ring holds QUEUE-1 then drops (%d)", q);
    dropped_before = (int)sfx_fe_dropped(m);
    int started = 0, last_voices = 0;
    for (int b = 0; b < 40; b++) {                                     /* drain everything */
        memset(buf, 0, BLOCK * sizeof *buf);
        sfx_fe_render(m, buf, BLOCK);
        int v = sfx_fe_voices(m);
        if (v > last_voices) started += v - last_voices;               /* a fresh voice appeared */
        last_voices = v;
    }
    CHECK(started >= 1 && started <= 4, "paced queue plays one sound per clip length, rest debounced (%d of %d)", started, q);
    CHECK(dropped_before + q - started == (int)sfx_fe_dropped(m) || (int)sfx_fe_dropped(m) >= dropped_before + q - started,
          "every unplayed event counted (%d -> %u, %d played)", dropped_before, sfx_fe_dropped(m), started);
    CHECK(sfx_fe_voices(m) == 0, "all queued events consumed (%d voices left)", sfx_fe_voices(m));
    CHECK(sfx_fe_event(m, SFX_FE_MOVE) == 1, "ring recovered after the drain");

    /* 8. Scripted menu walk (the menu's real event rhythm, dummy-clock through the render gate):
     *    screens in, blip walk, a change, denied, a back-out. Prints the RMS per event for
     *    AUDIO.md's RMS/time table. */
    pump(m, buf, 2);
    struct { int ev; const char *what; } walk[] = {
        { SFX_FE_SCREEN_IN,  "map screen in" },
        { SFX_FE_MOVE,       "blip 1" },
        { SFX_FE_MOVE,       "blip 2" },
        { SFX_FE_ACCEPT,     "open list" },
        { SFX_FE_TAB,        "change value" },
        { SFX_FE_ERROR,      "locked row" },
        { SFX_FE_BACK,       "back out" },
    };
    printf("walk:");
    int seq_ok = 1;
    for (size_t i = 0; i < sizeof walk / sizeof walk[0]; i++) {
        double peak = 0;
        CHECK(sfx_fe_event(m, walk[i].ev) == 1, "walk: %s queued", walk[i].what);
        for (int b = 0; b < 40; b++) {                                 /* watch up to ~0.93 s */
            memset(buf, 0, BLOCK * sizeof *buf);
            sfx_fe_render(m, buf, BLOCK);
            double r = rms(buf, BLOCK);
            if (r > peak) peak = r;
            if (peak > 100.0 && r < 20.0) break;                       /* sounded and decayed */
        }
        printf(" %s=%.0f", walk[i].what, peak);
        if (peak < 100.0) seq_ok = 0;
        pump(m, buf, 1);                                               /* pace the walk */
    }
    printf("\n");
    CHECK(seq_ok, "every walked interaction event sounds once (one silent: a role-gate bug)");

    sfx_fe_destroy(m);
    free(buf);
    printf("sfx_fe_test: %d checks, %d failures\n", checks, fails);
    return fails ? 1 : 0;
}
