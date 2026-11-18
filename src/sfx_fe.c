/* sfx_fe.c -- front-end menu sounds (OpenUG2 task AQ). Layout facts and every [inferred] claim
 * are documented in docs/parity/AUDIO.md (FE-sounds section); the measurements behind the clip
 * picks are tools/sfx_fe_traits.py output, kept with the task evidence. */
#include "sfx_fe.h"

#include "sfx_bank.h"
#include "sfx_play.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Both banks decode to mono EA-XA at 44100 Hz (measured, every sample). */
#define SFXFE_BANK_FE     "SOUND/FE/FE_MB.abk"
#define SFXFE_BANK_COMMON "SOUND/GLOBAL/FE_COMMON_MB.abk"

/* Clip roles. FE_COMMON_MB (18 samples) carries the navigation blips, FE_MB (68) the bigger
 * pieces. Indices are 0-based in bank order as sfx_bank.c walks it; the 1-based retail rank
 * (data-offset order, the rank a sample-indexing tool would show) is in AUDIO.md.
 *   NAV      0.169 s, the shortest FE_COMMON blip, bright (zcr 1393 measured on the envelope's
 *            band proxy) -- a move tick [inferred]
 *   ACCEPT   0.228 s, 1.09x the move's loudness, mid-bright zcr 5995 -- a confirm [inferred]
 *   BACK     0.211 s, move-length neighbour, zcr 6249 -- the accept's pair [inferred]
 *   DENIED   0.390 s, the bank's second-loudest (rms 0.365) and its darkest (zcr 1452): a
 *            negative is long and dull [inferred]; 1.4x ACCEPT gain keeps the intent clear
 *   SELECT   0.411 s, a step longer/brighter than ACCEPT -- a category change [inferred]
 *   LOOP     2.551 s and the only FE_MB sample whose last 100 ms still measure rms 0.1404
 *            (does-not-decay); it is FE_MB's named handler PlayFrontEndSample_loop (id 0x5abc,
 *            the tail table proves the name, the loop candidate proves the use) [data/inferred]
 *   DRIVE    0.755 s, FE_COMMON's brightest (zcr 8955), loud (rms 0.220) -- the sting an FE
 *            entry plays, cf. the event literally named FEDriveOn (UG2_FE_AEMS.csi) [inferred]
 *   TICK     FE_MB's own short tick, 0.200 s and its brightest (zcr 7816), with a measured tail
 *            of 0.1208 -- the same bank's loop handler (index 47) proves the bank carries real
 *            FE material, so its first short bright sample serves the commonest interaction
 *            (list scrolling) [inferred] */
enum { ROLE_NAV, ROLE_ACCEPT, ROLE_BACK, ROLE_DENIED, ROLE_SELECT, ROLE_LOOP, ROLE_DRIVE,
       ROLE_TICK, ROLE_COUNT };
static const int role_bank[ROLE_COUNT] = { 1, 1, 1, 1, 1, 0, 1, 0 };    /* 0 = FE_MB, 1 = FE_COMMON */
static const int role_index[ROLE_COUNT] = { 14, 9, 8, 13, 11, 47, 16, 0 };
/* Voice gains: measured bank RMS is 0.10..0.36 everywhere, so perceptual order follows the
 * sample's own traits; DENIED rides loudest for clarity. LOOP is a bed, not a hit. */
static const float role_gain[ROLE_COUNT] = { 0.55f, 0.60f, 0.60f, 0.85f, 0.60f, 0.12f, 0.80f, 0.45f };
/* Debounce per role, milliseconds: held navigation must not machine-gun; the sting and the loop
 * cue are gated by the one-voice rule anyway. */
static const int role_debounce_ms[ROLE_COUNT] = { 70, 120, 120, 250, 120, 3000, 1000, 70 };

/* Event -> role, -1 = silent. Screen transitions, dialogs and purchases have no defensible
 * sample in the two banks and stay silent rather than guess [data-absent]. */
static const int event_role[SFX_FE_EVENT_COUNT] = {
    [SFX_FE_MOVE]         = ROLE_NAV,
    [SFX_FE_SCROLL]       = ROLE_NAV,
    [SFX_FE_ACCEPT]       = ROLE_ACCEPT,
    [SFX_FE_BACK]         = ROLE_BACK,
    [SFX_FE_ERROR]        = ROLE_DENIED,
    [SFX_FE_TAB]          = ROLE_SELECT,
    [SFX_FE_SCREEN_IN]    = ROLE_DRIVE,
    [SFX_FE_SCREEN_OUT]   = -1,
    [SFX_FE_DIALOG_OPEN]  = -1,
    [SFX_FE_DIALOG_CLOSE] = -1,
};

struct SfxFe {
    SfxBank bank[2];
    SfxClip clip[ROLE_COUNT];
    int role_ok[ROLE_COUNT];
    struct SfxPlayer *mix;
    volatile float volume;
    int q[SFX_FE_QUEUE];
    volatile unsigned q_head;                    /* consumer (audio thread) */
    volatile unsigned q_tail;                    /* producer (game thread) */
    volatile unsigned dropped;
    float since_ms[ROLE_COUNT];                  /* audio-thread only: ms since the role last fired */
    int voice[ROLE_COUNT];                       /* audio-thread only: last handle per role */
};

static int open_bank(SfxBank *b, const char *root, const char *rel) {
    char path[1024];
    if (snprintf(path, sizeof path, "%s/%s", root, rel) >= (int)sizeof path) return 0;
    if (!sfx_bank_open_file(b, path)) { fprintf(stderr, "sfx_fe: %s unavailable: %s\n", rel, b->error); return 0; }
    return 1;
}

SfxFe *sfx_fe_create(const char *dataroot, uint32_t out_rate) {
    if (!dataroot || !out_rate) return NULL;
    SfxFe *m = (SfxFe *)calloc(1, sizeof *m);
    if (!m) return NULL;
    int have[2] = { open_bank(&m->bank[0], dataroot, SFXFE_BANK_FE),
                    open_bank(&m->bank[1], dataroot, SFXFE_BANK_COMMON) };
    for (int r = 0; r < ROLE_COUNT; r++) {
        m->role_ok[r] = have[role_bank[r]] &&
            sfx_clip_from_bank(&m->clip[r], &m->bank[role_bank[r]], role_index[r]);
    }
    if (!m->role_ok[ROLE_NAV] && !m->role_ok[ROLE_ACCEPT] && !m->role_ok[ROLE_BACK] &&
        !m->role_ok[ROLE_DENIED] && !m->role_ok[ROLE_SELECT]) {
        sfx_fe_destroy(m);                                       /* nothing playable: stay silent */
        return NULL;
    }
    m->mix = sfx_player_create(out_rate);
    if (!m->mix) { sfx_fe_destroy(m); return NULL; }
    m->volume = 1.0f;
    for (int r = 0; r < ROLE_COUNT; r++) m->since_ms[r] = 1e9f;  /* every role may fire at once */
    return m;
}

void sfx_fe_destroy(SfxFe *m) {
    if (!m) return;
    if (m->mix) sfx_stop_all(m->mix);                            /* voices before their clips */
    for (int r = 0; r < ROLE_COUNT; r++) sfx_clip_free(&m->clip[r]);
    sfx_bank_close(&m->bank[0]);
    sfx_bank_close(&m->bank[1]);
    sfx_player_destroy(m->mix);
    free(m);
}

void sfx_fe_set_volume(SfxFe *m, float volume) {
    if (!m) return;
    if (!(volume >= 0.0f)) volume = 0.0f;                        /* NaN and negatives -> silent */
    m->volume = volume > 1.0f ? 1.0f : volume;
}

int sfx_fe_event(SfxFe *m, int event) {
    if (!m || event < 0 || event >= SFX_FE_EVENT_COUNT) return 0;
    if (event_role[event] < 0) return 0;                         /* silent events never queue */
    unsigned next = (m->q_tail + 1) % SFX_FE_QUEUE;
    if (next == m->q_head) { m->dropped++; return 0; }           /* full: drop the newest */
    m->q[m->q_tail] = event;
    m->q_tail = next;
    return 1;
}

/* One-shot voices: every role but the loop bed (which no event starts today). */
#define ROLE_ONESHOT(r) ((r) != ROLE_LOOP)

void sfx_fe_render(SfxFe *m, int16_t *out, int n) {
    if (!m || !n) return;
    float frame_ms = 1000.0f * (float)n / 44100.0f;              /* the device rate the mixer got */
    float vol = m->volume;
    if (!(vol > 0.0f)) {                                         /* muted: nothing queues or plays */
        while (m->q_head != m->q_tail) {
            m->q_head = (m->q_head + 1) % SFX_FE_QUEUE;
            m->dropped++;
        }
    } else {
        int oneshots = 0;
        for (int r = 0; r < ROLE_COUNT; r++) {
            if (m->since_ms[r] < 1e8f) m->since_ms[r] += frame_ms;
            if (m->role_ok[r] && ROLE_ONESHOT(r) && m->voice[r] && sfx_active(m->mix, m->voice[r]))
                oneshots++;
        }
        while (m->q_head != m->q_tail) {                         /* single consumer drains all */
            int event = m->q[m->q_head];
            m->q_head = (m->q_head + 1) % SFX_FE_QUEUE;
            int r = event_role[event];
            if (r < 0 || !m->role_ok[r]) continue;
            if (m->since_ms[r] < role_debounce_ms[r]) { m->dropped++; continue; }
            if (oneshots) { m->dropped++; continue; }            /* one sound at a time */
            float g = role_gain[r] * vol;
            int v = sfx_play(m->mix, &m->clip[r], g > 1.0f ? 1.0f : g, 1.0f, !ROLE_ONESHOT(r));
            if (v) { m->voice[r] = v; m->since_ms[r] = 0.0f; if (ROLE_ONESHOT(r)) oneshots++; }
        }
    }
    sfx_mix_s16(m->mix, out, n);
}

unsigned sfx_fe_dropped(const SfxFe *m) { return m ? m->dropped : 0; }
int sfx_fe_voices(const SfxFe *m) {
    if (!m) return 0;
    int n = 0;
    for (int r = 0; r < ROLE_COUNT; r++)
        if (m->role_ok[r] && sfx_active(m->mix, m->voice[r])) n++;
    return n;
}
int sfx_fe_event_voice(const SfxFe *m, int event) {
    if (!m || event < 0 || event >= SFX_FE_EVENT_COUNT) return -1;
    int r = event_role[event];
    return (r >= 0 && m->role_ok[r]) ? role_index[r] : -1;
}
