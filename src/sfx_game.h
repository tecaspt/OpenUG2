/* sfx_game.h -- in-game sound effects from the retail .abk banks (OpenUG2 M8, game wiring).
 *
 * Owns the banks, the decoded clips and one sfx_play mixer. The whole thing runs on the audio thread:
 * the host's audio callback fills an SfgInput from the values the game thread already publishes
 * (collision latch, skid, road level, gear) and calls sfg_render once per callback. No locks, no SDL,
 * no allocation after sfg_create. Which retail sample means what is [inferred] (the banks carry no
 * names); every choice is one constant below so an owner capture can correct it in one place.
 * Pure C99. */
#ifndef OPENUG2_SFX_GAME_H
#define OPENUG2_SFX_GAME_H
#include <stdint.h>

typedef struct SfxGame SfxGame;

typedef struct SfgInput {
    float hit;      /* new collision this callback, game units 0..~0.45 (0 = none) */
    float skid;     /* tyre slide level 0..0.35 (0 = gripping) */
    float road;     /* road/wind level 0..~0.35 (speed squared) */
    float master;   /* overall effects gain 0..1 */
    int   gear;     /* current gear (>0 forward); a change between two forward gears plays a shift */
} SfgInput;

/* Loads the banks under dataroot. NULL when none of them opens (the host keeps its synthesized effects).
 * out_rate is the device rate (44100). */
SfxGame *sfg_create(const char *dataroot, uint32_t out_rate);
void sfg_destroy(SfxGame *g);
/* Which groups loaded (bit per SFG_HAS_*), for the host's log and fallbacks. */
enum { SFG_HAS_IMPACT = 1, SFG_HAS_SKID = 2, SFG_HAS_ROAD = 4, SFG_HAS_SHIFT = 8 };
unsigned sfg_groups(const SfxGame *g);
/* Starts/stops/retunes voices from the input, then ADDS the mix into out[0..n) (mono s16, saturating). */
void sfg_render(SfxGame *g, const SfgInput *in, int16_t *out, int n);
/* Voices currently playing (tests). */
int sfg_voices(const SfxGame *g);
#endif
