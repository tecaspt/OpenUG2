/* sfx_fe.h -- front-end menu sounds from the retail FE banks (OpenUG2 task AQ).
 *
 * The menu (gui_view and the FE hosts) names navigation events; this module owns the two retail
 * front-end banks and plays one short sample per event, on the audio thread, through sfx_play.
 * The game thread never touches the mixer: it pushes at most SFX_FE_QUEUE events into a lock-free
 * single-producer/single-consumer ring of plain ints, and the host's audio callback drains it once
 * per callback with sfx_fe_render, which starts voices and adds the mix into the device buffer
 * (the sfx_game volatile-globals pattern, with a queue because menu events are discrete and two
 * can fire inside one 1024-frame callback).
 *
 * Which retail sample means what is [inferred] from measurable sample traits (tools/sfx_fe_traits.py;
 * the table and evidence in docs/parity/AUDIO.md). An event with no defensible sample stays silent;
 * a bank that failed to open keeps every event silent. Pure C99, no SDL. */
#ifndef OPENUG2_SFX_FE_H
#define OPENUG2_SFX_FE_H

#include <stdint.h>

typedef struct SfxFe SfxFe;

/* Navigation events, the vocabulary gui_view emits. Values are the ring's payload: stable across
 * translation units, never renumbered while a build is live. */
enum {
    SFX_FE_MOVE = 0,       /* focus / carousel selection moved, any screen */
    SFX_FE_SCROLL,         /* the list scrolled while focus stayed put */
    SFX_FE_ACCEPT,         /* confirm / activate the focused entry */
    SFX_FE_BACK,           /* back / exit a screen */
    SFX_FE_ERROR,          /* an input the screen refused (locked row, bad entry) */
    SFX_FE_TAB,            /* category / tab / device-selector change */
    SFX_FE_SCREEN_IN,      /* a screen finished entering */
    SFX_FE_SCREEN_OUT,     /* a screen began exiting */
    SFX_FE_DIALOG_OPEN,
    SFX_FE_DIALOG_CLOSE,
    SFX_FE_EVENT_COUNT
};

#define SFX_FE_QUEUE 16    /* pending events; overflow drops the NEWEST (a stuck key loses a blip, not audio) */

/* Opens SOUND/FE/FE_MB.abk and SOUND/GLOBAL/FE_COMMON_MB.abk under dataroot and decodes the
 * mapped clips. NULL when neither bank opens (the host keeps silent menus, like sfg_create). */
SfxFe *sfx_fe_create(const char *dataroot, uint32_t out_rate);
void sfx_fe_destroy(SfxFe *m);

/* The host publishes the volume here (it owns the settings): the value played is
 * gain = voice_table_gain * volume, clamped to 0..1. `volume` is the product of the profile's
 * master and SFX sliders (0..1); negative or NaN becomes 0, above 1 clamps. Volatile float,
 * same benign race as g_sfx_master. */
void sfx_fe_set_volume(SfxFe *m, float volume);

/* Game thread: push one event. 1 when queued, 0 when the ring is full (dropped). Thread-safe
 * against the audio thread (producer = this thread only). */
int sfx_fe_event(SfxFe *m, int event);

/* Audio thread (from the device callback): drains the ring, starting the mapped voice per event
 * with three gates -- the role's own debounce window, the one-shot gate (one one-shot sound at a
 * time, like the retail menus; a loop bed would not silence one-shots, but no event starts the
 * loop today), and volume (at 0 the ring is drained and dropped with sfx_fe_dropped counting, so
 * the queue never backs up) -- then ADDS the mix into out[0..n) (mono s16, saturating). n is
 * frames at the rate given to sfx_fe_create (the debounce clock advances by n/44100 seconds). */
void sfx_fe_render(SfxFe *m, int16_t *out, int n);

unsigned sfx_fe_dropped(const SfxFe *m);    /* ring-full + debounce drops (tests/telemetry) */
int sfx_fe_voices(const SfxFe *m);          /* voices currently playing (tests) */
int sfx_fe_event_voice(const SfxFe *m, int event);  /* mapped clip index, -1 = silent event */

#endif
