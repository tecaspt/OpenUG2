/* input_script.h -- scripted input for deterministic runs (PLAN-M1 U3). Only builds SDL_Event structs; no SDL
 * init needed.
 *
 * Spec: commands separated by commas or whitespace; "@FILE" reads the commands from FILE ('#' starts a comment
 * line tail). A command is "FRAME:what":
 *   F:key=NAME      key down at F, up at F+1 (SDL key name; '_' also stands for a space)
 *   F:pad=BUTTON    controller button down at F, up at F+1 (SDL game controller button name, which=0)
 *   F:click=X:Y     mouse move + left button down at F, up at F+1; which=SDL_TOUCH_MOUSEID. X,Y are
 *                   normalised 0..1 window fractions, scaled with win_w/win_h
 *   F:fdown=ID:X:Y  SDL_FINGERDOWN, finger ID, X,Y normalised 0..1
 *   F:fup=ID        SDL_FINGERUP (position 0,0)
 *   F:tdown=BTN / F:tup=BTN
 *                   touch button press/release (BTN: TcButton number or name: steer_l steer_r gas brake nos
 *                   handbrake camera pause shift_up shift_down enter_race). Emitted as SDL_USEREVENT with
 *                   user.code = 1 (down) / 0 (up) and user.data1 = (void*)(intptr_t)BTN; main resolves it
 *                   through tc_layout into finger events.
 *   F:shot=PATH     screenshot request at frame F (see is_shot)
 *   F:quit          quit request at frame F (see is_quit)
 */
#ifndef OPENUG2_INPUT_SCRIPT_H
#define OPENUG2_INPUT_SCRIPT_H

#include <SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IS_MAX_ITEMS 512
#define IS_PATH_MAX 192

enum { IS_KEY, IS_PAD, IS_CLICK, IS_FDOWN, IS_FUP, IS_TDOWN, IS_TUP, IS_SHOT, IS_QUIT };

typedef struct IsItem {
    long frame;
    int kind;
    int a;               /* key code / button / finger id / touch button */
    int scancode;
    float x, y;
    char path[IS_PATH_MAX];
} IsItem;

typedef struct InputScript { int n; IsItem item[IS_MAX_ITEMS]; } InputScript;

/* Returns 1 on success; 0 with a message in err (n bytes) on bad input. */
int is_parse(InputScript *s, const char *spec_or_at_file, char *err, size_t n);
/* Writes the events due at `frame` (releases of earlier taps first, then new events); returns the count (<= max). */
int is_events(InputScript *s, long frame, SDL_Event *out, int max, int win_w, int win_h);
/* Screenshot path scheduled for this frame, or NULL. */
const char *is_shot(const InputScript *s, long frame);
/* 1 once the quit frame is reached (frame >= quit frame). */
int is_quit(const InputScript *s, long frame);

#ifdef __cplusplus
}
#endif
#endif
