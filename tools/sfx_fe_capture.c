/* sfx_fe_capture.c -- task AQ evidence: a scripted menu walk through the real device callback.
 *
 * Usage: sfx_fe_capture DATAROOT SCRIPT OUT.pcm
 *   SCRIPT: "event[:hold_ms],event,..." with event a role word (nav accept back denied select
 *   drive tick) or an sfx_fe event word (move scroll error tab screen_in screen_out
 *   dialog_open dialog_close); hold_ms is the silence before the event (default 500,
 *   max 4000). A line of stderr documents every event and the callback blocks that observed it.
 *
 * The audio callback is src/audio.c's own (included below, the audio-ae-capture pattern), so the
 * walk travels the shipped path: g_sfx_fe ring -> sfx_fe_render gates -> sfx_mix_s16 ADDING onto
 * the engine/road callback output. SDL runs on the dummy driver; the callback is pulled by hand,
 * 1024 frames at a time, so the walk is deterministic. OUT.pcm is raw mono s16 at 44100 Hz.
 * Write it OUTSIDE any git work tree; never commit audio. */
#define _GNU_SOURCE
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* sfx_game.c and sfx_fe.c each keep a static open_bank(); this single TU needs distinct names.
 * sfg_open_bank is sfg_create (audio.c uses it); the _fe copy is sfx_fe.c's own. */
#define open_bank sfg_open_bank
#include "../src/audio.c"
#include "../src/sfx_game.c"
#undef open_bank
#define open_bank sfxfe_open_bank
#include "../src/sfx_fe.c"
#undef open_bank

static const struct { const char *name; int ev; } k_events[] = {
    { "move", SFX_FE_MOVE }, { "scroll", SFX_FE_SCROLL }, { "accept", SFX_FE_ACCEPT },
    { "back", SFX_FE_BACK }, { "error", SFX_FE_ERROR }, { "tab", SFX_FE_TAB },
    { "screen_in", SFX_FE_SCREEN_IN }, { "screen_out", SFX_FE_SCREEN_OUT },
    { "dialog_open", SFX_FE_DIALOG_OPEN }, { "dialog_close", SFX_FE_DIALOG_CLOSE },
};
static const struct { const char *name; int role; } k_roles[] = {
    { "nav", ROLE_NAV }, { "accept", ROLE_ACCEPT }, { "back", ROLE_BACK },
    { "denied", ROLE_DENIED }, { "select", ROLE_SELECT }, { "loop", ROLE_LOOP },
    { "drive", ROLE_DRIVE }, { "tick", ROLE_TICK },
};

static int event_of(const char *w, size_t n, int *ev) {
    for (size_t i = 0; i < sizeof k_events / sizeof k_events[0]; i++)
        if (strlen(k_events[i].name) == n && !strncmp(k_events[i].name, w, n)) { *ev = k_events[i].ev; return 1; }
    for (size_t i = 0; i < sizeof k_roles / sizeof k_roles[0]; i++)
        if (strlen(k_roles[i].name) == n && !strncmp(k_roles[i].name, w, n)) { *ev = k_roles[i].role; return 2; }
    return 0;
}

int main(int argc, char **argv) {
    if (argc != 4) { fprintf(stderr, "usage: sfx_fe_capture DATAROOT SCRIPT OUT.pcm\n"); return 2; }
    if (SDL_Init(SDL_INIT_AUDIO)) return 3;
    SDL_AudioDeviceID dev = audio_init();
    if (!dev) return 4;
    SDL_PauseAudioDevice(dev, 1);                 /* callback never runs on its own */
    g_rng = 22222; memset(&g_aux, 0, sizeof g_aux);   /* deterministic filler like audio-ae-capture */
    g_sfx_game = sfg_create(argv[1], 44100);      /* the real menu-over-engine bus, kept quiet below */
    g_sfx_fe = sfx_fe_create(argv[1], 44100);
    if (!g_sfx_fe) { fprintf(stderr, "no FE banks under %s\n", argv[1]); return 5; }
    g_engine.master_volume = 0;                   /* silence the engine filler; the FE mix is the signal */
    g_road_vol = 0; g_skid = 0; g_scrape = 0; g_hit = 0; g_sfx_master = 0;
    sfx_fe_set_volume(g_sfx_fe, 1.0f);

    char *script = strdup(argv[2]);
    FILE *f = fopen(argv[3], "wb");
    if (!f) return 7;
    int16_t pcm[1024];
    unsigned total_blocks = 0;
    char *save = NULL;
    for (char *tok = strtok_r(script, ",", &save); tok; tok = strtok_r(NULL, ",", &save)) {
        char *colon = strchr(tok, ':');
        size_t nlen = colon ? (size_t)(colon - tok) : strlen(tok);
        while (*tok == ' ') tok++, nlen--;
        int ev = 0, kind = event_of(tok, nlen, &ev);
        if (!kind) { fprintf(stderr, "unknown event '%s'\n", tok); return 2; }
        int hold_ms = colon ? atoi(colon + 1) : 500;
        if (hold_ms < 0) hold_ms = 0;
        if (hold_ms > 4000) hold_ms = 4000;
        unsigned pre = total_blocks;
        for (int b = 0; b < hold_ms * 44100 / 1000 / 1024; b++) {           /* the silence before */
            audio_cb(NULL, (Uint8 *)pcm, sizeof pcm);
            if (fwrite(pcm, 2, 1024, f) != 1024) return 8;
            total_blocks++;
        }
        if (kind == 1) sfx_fe_event(g_sfx_fe, ev);
        else {                                                            /* a role word: its event */
            int fake = SFX_FE_EVENT_COUNT;                                /* none of the ten maps to LOOP today */
            for (int e = 0; e < SFX_FE_EVENT_COUNT && fake == SFX_FE_EVENT_COUNT; e++)
                if (sfx_fe_event_voice(g_sfx_fe, e) == role_index[ev]) fake = e;
            if (fake == SFX_FE_EVENT_COUNT) { fprintf(stderr, "role '%s' has no mapped event\n", tok); return 2; }
            sfx_fe_event(g_sfx_fe, fake);
        }
        audio_cb(NULL, (Uint8 *)pcm, sizeof pcm);                         /* the callback that starts it */
        if (fwrite(pcm, 2, 1024, f) != 1024) return 8;
        total_blocks++;
        fprintf(stderr, "event=%s at_block=%u pushed_into_block=%u\n", tok, pre, total_blocks - 1);
    }
    for (int b = 0; b < 44100 / 1024; b++) {                              /* a second of tail */
        audio_cb(NULL, (Uint8 *)pcm, sizeof pcm);
        if (fwrite(pcm, 2, 1024, f) != 1024) return 8;
        total_blocks++;
    }
    fprintf(stderr, "total_frames=%u dropped=%u\n", total_blocks * 1024u, sfx_fe_dropped(g_sfx_fe));
    fclose(f);
    sfx_fe_destroy(g_sfx_fe); g_sfx_fe = NULL;
    sfg_destroy(g_sfx_game); g_sfx_game = NULL;
    SDL_CloseAudioDevice(dev);
    SDL_Quit();
    return 0;
}
