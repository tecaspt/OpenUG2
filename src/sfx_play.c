/* sfx_play.c — see sfx_play.h for the contract.
 *
 * Voices are a fixed array of SFX_MAX_VOICES slots. A handle carries the slot
 * and a per-slot generation so a stale handle can never touch a later sound.
 * Resampling is linear interpolation driven by a double read position; at
 * pitch 1 with clip rate == out_rate the step is exactly 1.0 and the
 * interpolation is an identity, so a clip mixes back out sample for sample.
 *
 * Clean-room: nothing here was copied from game code; the only input is the
 * project's own sfx_bank.h.
 */
#include "sfx_play.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HANDLE_SLOT_BITS 5                                    /* 32 > SFX_MAX_VOICES */
#define HANDLE_SLOT_MASK ((1u << HANDLE_SLOT_BITS) - 1u)
#define GEN_BITS           (8u * sizeof(unsigned) - HANDLE_SLOT_BITS - 1u)
#define GEN_MASK           ((1u << GEN_BITS) - 1u)

typedef struct Voice {
    const int16_t *pcm;          /* borrowed from the caller's clip */
    uint32_t frames;
    uint32_t rate;               /* clip rate, so sfx_set can re-derive step */
    double   step;               /* source frames consumed per output frame */
    double   pos;                /* read position, always 0 <= pos < frames */
    float    gain;               /* gain actually applied to the next sample */
    float    gain_target;
    float    gain_delta;         /* per-output-frame slew while ramping */
    unsigned ramp_left;          /* output frames still under ramp */
    int      loop;
    int      used;
    unsigned gen;                /* generation stamped into live handles */
} Voice;

struct SfxPlayer {
    uint32_t out_rate;
    unsigned ramp_frames;        /* ~SFX_GAIN_RAMP_MS of output frames */
    Voice voice[SFX_MAX_VOICES];
};

static float clampf(float v, float lo, float hi)
{
    if (!(v > lo)) return lo;    /* also routes NaN to lo */
    if (v > hi) return hi;
    return v;
}

/* Saturating add into one s16 slot. Both inputs are already in s16 range, so
 * the int sum cannot overflow; clamping once here is what keeps 24 stacked
 * voices from wrapping. */
static int16_t sat_add(int16_t a, int b)
{
    int s = (int)a + b;
    if (s > 32767) return 32767;
    if (s < -32768) return -32768;
    return (int16_t)s;
}

/* Releases a slot and burns its generation, so every handle that referred to
 * the old sound is dead. The slot's counter keeps running across allocations. */
static void voice_release(SfxPlayer *p, int i)
{
    Voice *v = &p->voice[i];
    unsigned gen;
    if (!v->used) return;
    gen = v->gen;
    memset(v, 0, sizeof *v);
    gen = (gen + 1u) & GEN_MASK;
    v->gen = gen ? gen : 1;      /* generation 0 is reserved for a fresh slot */
}

static unsigned handle_make(const Voice *v, int i)
{
    return (v->gen << HANDLE_SLOT_BITS) | (unsigned)(i + 1);
}

/* Resolves a handle to a live voice. Read-only, so it also serves sfx_active. */
static int handle_live(const SfxPlayer *p, int voice)
{
    unsigned h;
    int slot;
    if (!p || voice <= 0) return 0;
    h = (unsigned)voice;
    slot = (int)(h & HANDLE_SLOT_MASK) - 1;
    if (slot < 0 || slot >= SFX_MAX_VOICES) return 0;
    if ((h >> HANDLE_SLOT_BITS) != p->voice[slot].gen) return 0;
    return p->voice[slot].used;
}

/* Resolves a handle to a live voice, or NULL. */
static Voice *handle_get(SfxPlayer *p, int voice)
{
    int slot;
    if (!handle_live(p, voice)) return NULL;
    slot = (int)((unsigned)voice & HANDLE_SLOT_MASK) - 1;
    return &p->voice[slot];
}

int sfx_clip_from_bank(SfxClip *c, const SfxBank *b, int index)
{
    if (c) { c->pcm = NULL; c->frames = 0; c->rate = 0; }
    if (!c || !b || index < 0 || index >= b->count || !b->samples) return 0;
    if (!b->samples[index].decodable || !b->samples[index].frames) return 0;
    {
        uint32_t n = b->samples[index].frames;
        int16_t *pcm = (int16_t *)malloc((size_t)n * sizeof *pcm);
        if (!pcm) return 0;
        if (!sfx_bank_decode(b, index, pcm)) { free(pcm); return 0; }
        c->pcm = pcm;
        c->frames = n;
        c->rate = b->samples[index].rate ? b->samples[index].rate : 22050;
    }
    return 1;
}

void sfx_clip_free(SfxClip *c)
{
    if (!c) return;
    free(c->pcm);
    c->pcm = NULL;
    c->frames = 0;
    c->rate = 0;
}

SfxPlayer *sfx_player_create(uint32_t out_rate)
{
    SfxPlayer *p;
    if (!out_rate) return NULL;
    p = (SfxPlayer *)calloc(1, sizeof *p);
    if (!p) return NULL;
    p->out_rate = out_rate;
    p->ramp_frames = (unsigned)(out_rate * SFX_GAIN_RAMP_MS / 1000u);
    if (!p->ramp_frames) p->ramp_frames = 1;
    return p;
}

void sfx_player_destroy(SfxPlayer *p)
{
    free(p);
}

int sfx_play(SfxPlayer *p, const SfxClip *c, float gain, float pitch, int loop)
{
    int pick = -1, v;
    if (!p || !c || !c->pcm || !c->frames || !c->rate || !p->out_rate) return 0;

    for (v = 0; v < SFX_MAX_VOICES; v++) {
        if (!p->voice[v].used) { pick = v; break; }
    }
    if (pick < 0) {
        /* All slots busy: steal the quietest one-shot. Loops are never stolen,
         * so a wall of loops means there is genuinely nothing to give. */
        float quiet = 0.0f;
        for (v = 0; v < SFX_MAX_VOICES; v++) {
            Voice *cand = &p->voice[v];
            if (!cand->used || cand->loop) continue;
            if (pick < 0 || cand->gain < quiet) { pick = v; quiet = cand->gain; }
        }
        if (pick < 0) return 0;
        voice_release(p, pick);
    }

    {
        Voice *vo = &p->voice[pick];
        unsigned gen = vo->gen ? vo->gen : 1;
        double step = ((double)c->rate / (double)p->out_rate) *
                      clampf(pitch, 0.25f, 4.0f);
        memset(vo, 0, sizeof *vo);
        vo->pcm = c->pcm;
        vo->frames = c->frames;
        vo->rate = c->rate;
        vo->step = step;
        vo->pos = 0.0;
        vo->gain = vo->gain_target = clampf(gain, 0.0f, 1.0f);
        vo->loop = loop ? 1 : 0;
        vo->used = 1;
        vo->gen = gen;
        return (int)handle_make(vo, pick);
    }
}

void sfx_set(SfxPlayer *p, int voice, float gain, float pitch)
{
    Voice *vo = handle_get(p, voice);
    float want;
    if (!vo) return;                            /* stale handle: no-op */
    if (pitch > 0.0f && vo->rate && p->out_rate) {
        vo->step = ((double)vo->rate / (double)p->out_rate) *
                   clampf(pitch, 0.25f, 4.0f);
    }
    /* Ramp from the gain in force right now, not from the previous target, so
     * retargeting mid-ramp cannot jump. Repeated calls for the same target must
     * not restart (and so lengthen) the ramp. sfx_play is not ramped at all:
     * the attack of a percussive effect would be smeared over 5 ms. */
    want = clampf(gain, 0.0f, 1.0f);
    if (want != vo->gain_target) {
        vo->gain_target = want;
        vo->gain_delta = (want - vo->gain) / (float)p->ramp_frames;
        vo->ramp_left = p->ramp_frames;
    }
}

void sfx_stop(SfxPlayer *p, int voice)
{
    Voice *vo = handle_get(p, voice);
    if (!vo) return;
    voice_release(p, (int)((unsigned)voice & HANDLE_SLOT_MASK) - 1);
}

void sfx_stop_all(SfxPlayer *p)
{
    int v;
    if (!p) return;
    for (v = 0; v < SFX_MAX_VOICES; v++) voice_release(p, v);
}

int sfx_active(const SfxPlayer *p, int voice)
{
    return handle_live(p, voice);
}

void sfx_mix_s16(SfxPlayer *p, int16_t *out, int n)
{
    int v, k;
    if (!p || !out || n <= 0) return;
    for (v = 0; v < SFX_MAX_VOICES; v++) {
        Voice *vo = &p->voice[v];
        if (!vo->used) continue;
        for (k = 0; k < n; k++) {
            uint32_t i, j;
            double frac;
            float x;
            i = (uint32_t)vo->pos;                  /* pos < frames always */
            j = i + 1u;
            if (j >= vo->frames) j = vo->loop ? 0u : vo->frames - 1u;
            frac = vo->pos - (double)i;
            x = (float)vo->pcm[i] + (float)frac * ((float)vo->pcm[j] - (float)vo->pcm[i]);
            out[k] = sat_add(out[k], (int)floorf(x * vo->gain + 0.5f));

            if (vo->ramp_left) {
                vo->gain += vo->gain_delta;
                if (!--vo->ramp_left) vo->gain = vo->gain_target;
            }
            vo->pos += vo->step;
            if (vo->loop) {
                while (vo->pos >= (double)vo->frames) vo->pos -= (double)vo->frames;
            } else if (vo->pos >= (double)vo->frames) {
                voice_release(p, v);
                break;                              /* this voice is done */
            }
        }
    }
}
