/* sfx_game.c -- see sfx_game.h. */
#include "sfx_game.h"
#include "sfx_play.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Bank choices: paths [data] (sfx_groups.h, all decode), sample meanings [inferred] from length and loop shape. */
#define SFG_BANK_IMPACT "SOUND/IG_GLOBAL/MB_Stich_Collision.abk"   /* 94 one-shots, 0.03..1.5 s */
#define SFG_BANK_SKID   "SOUND/SKIDS/SKID_PAV_MB.abk"              /* 5 loops (start rms == end rms) */
#define SFG_BANK_ROAD   "SOUND/IG_GLOBAL/ROADNOISE_00_MB.abk"      /* loops 0,1,4,5,6; one-shot bumps 7..13 */
#define SFG_BANK_SHIFT  "SOUND/SHIFTING/GEAR_MED_Base.abk"         /* 2 one-shots: up, down; medium engine class */
#define SFG_SKID_LOOP   0      /* [inferred] plain pavement slide */
#define SFG_ROAD_LOOP   4      /* [inferred] the quietest steady loop = smooth asphalt */
#define SFG_SHIFT_UP    0      /* [inferred] */
#define SFG_SHIFT_DOWN  1      /* [inferred] */
#define SFG_LIGHT_S     0.20f  /* impact clips shorter than this are light knocks */
#define SFG_HEAVY_S     0.45f  /* longer than this are heavy crashes */
#define SFG_HIT_FULL    0.45f  /* game hit value treated as the hardest crash (audio_wall_contact caps at .45) */
#define SFG_SKID_FULL   0.35f  /* main.c caps g_skid at .35 */
#define SFG_ROAD_FULL   0.35f  /* g_road_vol = speed^2 * .35 at top speed */
#define SFG_IMPACT_GAP  0.08f  /* seconds between two impact one-shots */
#define SFG_MAX_IMPACTS 128

struct SfxGame {
    SfxPlayer *mix;
    uint32_t rate;
    SfxBank bimpact, bskid, broad, bshift;
    int have_impact, have_skid, have_road, have_shift;
    SfxClip impact[SFG_MAX_IMPACTS]; int nimpact;
    int light[SFG_MAX_IMPACTS], nlight, medium[SFG_MAX_IMPACTS], nmedium, heavy[SFG_MAX_IMPACTS], nheavy;
    SfxClip skid, road, up, down;
    int vskid, vroad, last_gear, skid_quiet;
    uint32_t since_impact, seed;
};

static int open_bank(SfxBank *b, const char *root, const char *rel)
{
    char path[1024];
    if (snprintf(path, sizeof path, "%s/%s", root, rel) >= (int)sizeof path) return 0;
    if (!sfx_bank_open_file(b, path)) { fprintf(stderr, "sfx: %s unavailable\n", rel); return 0; }
    return 1;
}

static uint32_t rnd(SfxGame *g) { g->seed = g->seed * 1664525u + 1013904223u; return g->seed >> 8; }

SfxGame *sfg_create(const char *dataroot, uint32_t out_rate)
{
    if (!dataroot || !out_rate) return NULL;
    SfxGame *g = (SfxGame *)calloc(1, sizeof *g);
    if (!g) return NULL;
    g->rate = out_rate; g->seed = 0x5f3759dfu; g->since_impact = out_rate;
    g->mix = sfx_player_create(out_rate);
    if (!g->mix) { free(g); return NULL; }
    if (open_bank(&g->bimpact, dataroot, SFG_BANK_IMPACT)) {
        g->have_impact = 1;
        for (int i = 0; i < g->bimpact.count && g->nimpact < SFG_MAX_IMPACTS; i++) {
            SfxClip *c = &g->impact[g->nimpact];
            if (!sfx_clip_from_bank(c, &g->bimpact, i)) continue;
            float s = c->rate ? (float)c->frames / (float)c->rate : 0.0f;
            int k = g->nimpact++;
            if (s < SFG_LIGHT_S) g->light[g->nlight++] = k;
            else if (s > SFG_HEAVY_S) g->heavy[g->nheavy++] = k;
            else g->medium[g->nmedium++] = k;
        }
        if (!g->nlight || !g->nmedium || !g->nheavy) g->have_impact = 0;
    }
    if (open_bank(&g->bskid, dataroot, SFG_BANK_SKID))
        g->have_skid = sfx_clip_from_bank(&g->skid, &g->bskid, SFG_SKID_LOOP);
    if (open_bank(&g->broad, dataroot, SFG_BANK_ROAD))
        g->have_road = sfx_clip_from_bank(&g->road, &g->broad, SFG_ROAD_LOOP);
    if (open_bank(&g->bshift, dataroot, SFG_BANK_SHIFT))
        g->have_shift = sfx_clip_from_bank(&g->up, &g->bshift, SFG_SHIFT_UP) &&
                        sfx_clip_from_bank(&g->down, &g->bshift, SFG_SHIFT_DOWN);
    if (!sfg_groups(g)) { sfg_destroy(g); return NULL; }
    return g;
}

void sfg_destroy(SfxGame *g)
{
    if (!g) return;
    sfx_player_destroy(g->mix);   /* voices first: they borrow the clips */
    for (int i = 0; i < g->nimpact; i++) sfx_clip_free(&g->impact[i]);
    sfx_clip_free(&g->skid); sfx_clip_free(&g->road); sfx_clip_free(&g->up); sfx_clip_free(&g->down);
    sfx_bank_close(&g->bimpact); sfx_bank_close(&g->bskid); sfx_bank_close(&g->broad); sfx_bank_close(&g->bshift);
    free(g);
}

unsigned sfg_groups(const SfxGame *g)
{
    if (!g) return 0;
    return (g->have_impact ? SFG_HAS_IMPACT : 0u) | (g->have_skid ? SFG_HAS_SKID : 0u) |
           (g->have_road ? SFG_HAS_ROAD : 0u) | (g->have_shift ? SFG_HAS_SHIFT : 0u);
}

static float clamp01(float v) { return v != v ? 0.0f : v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v; }

void sfg_render(SfxGame *g, const SfgInput *in, int16_t *out, int n)
{
    if (!g || !in || !out || n <= 0) return;
    float master = clamp01(in->master);
    /* impacts: one-shot picked by strength from the light / medium / heavy sets */
    if (g->have_impact && in->hit > 0.0f && g->since_impact >= (uint32_t)(SFG_IMPACT_GAP * (float)g->rate)) {
        float s = clamp01(in->hit / SFG_HIT_FULL);
        const int *set = s < 0.35f ? g->light : s < 0.7f ? g->medium : g->heavy;
        int nset = s < 0.35f ? g->nlight : s < 0.7f ? g->nmedium : g->nheavy;
        const SfxClip *c = &g->impact[set[rnd(g) % (uint32_t)nset]];
        float pitch = 0.92f + 0.16f * (float)(rnd(g) % 1000u) / 1000.0f;
        sfx_play(g->mix, c, master * (0.25f + 0.55f * s), pitch, 0);
        g->since_impact = 0;
    }
    /* tyre slide: one loop, level and pitch follow the slide */
    if (g->have_skid) {
        float s = clamp01(in->skid / SFG_SKID_FULL);
        if (s > 0.02f) {
            if (!sfx_active(g->mix, g->vskid)) g->vskid = sfx_play(g->mix, &g->skid, 0.0f, 1.0f, 1);
            sfx_set(g->mix, g->vskid, master * 0.7f * s, 0.9f + 0.2f * s);
            g->skid_quiet = 0;
        } else if (sfx_active(g->mix, g->vskid)) {
            /* release: ramp to silence (SFX_GAIN_RAMP_MS), stop once a whole callback has passed */
            if (g->skid_quiet++ == 0) sfx_set(g->mix, g->vskid, 0.0f, 1.0f);
            else { sfx_stop(g->mix, g->vskid); g->vskid = 0; g->skid_quiet = 0; }
        }
    }
    /* road / wind bed: always-on loop, level with speed squared, pitch with speed */
    if (g->have_road) {
        float r = clamp01(in->road / SFG_ROAD_FULL);
        if (!sfx_active(g->mix, g->vroad)) g->vroad = sfx_play(g->mix, &g->road, 0.0f, 1.0f, 1);
        sfx_set(g->mix, g->vroad, master * 0.5f * r, 0.75f + 0.5f * sqrtf(r));
    }
    /* gear shift between two forward gears */
    if (g->have_shift && in->gear > 0 && g->last_gear > 0 && in->gear != g->last_gear) {
        const SfxClip *c = in->gear > g->last_gear ? &g->up : &g->down;
        /* [inferred] Match the engine mixer's 11000/32768 output scale;
         * the effects player adds full-scale PCM after the engine mix. */
        sfx_play(g->mix, c, master * (0.45f * 11000.0f / 32768.0f), 1.0f, 0);
    }
    g->last_gear = in->gear;
    if (g->since_impact < 0x7fffffffu - (uint32_t)n) g->since_impact += (uint32_t)n;
    sfx_mix_s16(g->mix, out, n);
}

int sfg_voices(const SfxGame *g)
{
    return g ? (sfx_active(g->mix, g->vskid) + sfx_active(g->mix, g->vroad)) : 0;
}
