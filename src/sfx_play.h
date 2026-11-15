/* sfx_play.h — small standalone mixer that plays samples decoded by sfx_bank.h.
 *
 * Output model: mono signed 16-bit at a fixed output rate (44100 in the game).
 * The caller owns the output buffer; sfx_mix_s16 ADDS into it and never
 * overwrites, so it can sit on top of whatever else fills the device buffer.
 *
 * Threading: there is no internal lock and no thread here. sfx_mix_s16 runs on
 * the audio thread (from the device callback); sfx_play/sfx_set/sfx_stop/
 * sfx_stop_all are mutators and must be serialised against it by the caller —
 * the intended mechanism is SDL_LockAudioDevice around each mutator call, or
 * calling them only from inside the callback. sfx_active is read-only.
 *
 * Clip lifetime: a voice only *references* the clip's pcm — nothing is copied
 * and nothing is allocated on the play or mix path. The caller must therefore
 * keep every SfxClip (and its pcm) alive until every voice started from it has
 * finished or been stopped. Freeing a clip while a voice still uses it is a
 * use-after-free; sfx_stop_all does not help, since the caller frees when it
 * likes. Banks must outlive their clips for the same reason.
 *
 * No SDL, no globals, no threads. C99.
 */
#ifndef SFX_PLAY_H
#define SFX_PLAY_H

#include "sfx_bank.h"

#define SFX_MAX_VOICES 24

/* Gain/pitch changes made by sfx_set are ramped over this many milliseconds so
 * a parameter tweak does not click. sfx_play applies its gain immediately —
 * the attack of a percussive effect must not be smeared over 5 ms. The constant
 * is public so a test (or a host) can predict the per-frame slew limit. */
#define SFX_GAIN_RAMP_MS 5

typedef struct SfxClip {
    int16_t *pcm;      /* owned by this struct after sfx_clip_from_bank */
    uint32_t frames;
    uint32_t rate;     /* Hz of the clip, 0 unless set */
} SfxClip;

typedef struct SfxPlayer SfxPlayer;   /* opaque */

/* Decodes bank sample `index` and takes ownership of the buffer. 1 ok, 0 error
 * (bad bank/index, sample not decodable, zero frames, or out of memory). The
 * bank is const, so no message is recorded anywhere: check the return value,
 * and SfxBank.error / SfxSample.decodable for the reason. On 0, c is empty and
 * still safe to pass to sfx_clip_free. */
int  sfx_clip_from_bank(SfxClip *c, const SfxBank *b, int index);
void sfx_clip_free(SfxClip *c);

/* out_rate is the device rate, e.g. 44100. NULL if out_rate is 0 or on an
 * allocation failure. Nothing else in this file ever allocates. */
SfxPlayer *sfx_player_create(uint32_t out_rate);
void sfx_player_destroy(SfxPlayer *p);

/* Start a clip. Returns a voice handle > 0, or 0 if no voice is free (then
 * steal the quietest one-shot, never a loop). A handle is slot + generation:
 * once its voice ends, is stopped, or is stolen, the handle is dead for good
 * and sfx_set/sfx_stop/sfx_active on it do nothing.
 * gain is clamped to 0..1 (a NaN becomes 0), pitch to 0.25..4 (a NaN or a
 * non-positive pitch becomes 0.25). loop != 0 loops until sfx_stop. */
int  sfx_play(SfxPlayer *p, const SfxClip *c, float gain, float pitch, int loop);
void sfx_set(SfxPlayer *p, int voice, float gain, float pitch);   /* stale handle = no-op */
void sfx_stop(SfxPlayer *p, int voice);                          /* stale handle = no-op */
void sfx_stop_all(SfxPlayer *p);
int  sfx_active(const SfxPlayer *p, int voice);                  /* 1 while playing */

/* ADD the mix of all voices into out[0..n) (mono s16), with saturation, never overwrite. */
void sfx_mix_s16(SfxPlayer *p, int16_t *out, int n);

#endif /* SFX_PLAY_H */
