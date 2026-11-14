/* input_script_test -- parser and event generation; no SDL init. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "../src/input_script.h"

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

static InputScript s;
static char err[160];

static int bad(const char *spec) {
    int r;
    err[0] = 0;
    r = is_parse(&s, spec, err, sizeof err);
    return r == 0 && err[0] != 0;
}

int main(int argc, char **argv) {
    SDL_Event ev[8];
    int n;
    const char *at = argc > 1 ? argv[1] : "input_script_test.script";
    char atspec[300];
    FILE *f;

    CHECK(is_parse(&s, "10:key=Return,12:pad=a,20:click=0.5:0.25,30:fdown=2:0.1:0.9,31:fup=2,40:tdown=gas,41:tup=2,50:shot=/tmp/a.png,60:quit", err, sizeof err) == 1);
    CHECK(s.n == 9);

    /* key: down at 10, up at 11 */
    n = is_events(&s, 9, ev, 8, 800, 600); CHECK(n == 0);
    n = is_events(&s, 10, ev, 8, 800, 600);
    CHECK(n == 1 && ev[0].type == SDL_KEYDOWN && ev[0].key.keysym.sym == SDLK_RETURN);
    n = is_events(&s, 11, ev, 8, 800, 600);
    CHECK(n == 1 && ev[0].type == SDL_KEYUP && ev[0].key.keysym.sym == SDLK_RETURN);
    n = is_events(&s, 12, ev, 8, 800, 600);
    CHECK(n == 1 && ev[0].type == SDL_CONTROLLERBUTTONDOWN && ev[0].cbutton.button == SDL_CONTROLLER_BUTTON_A);
    n = is_events(&s, 13, ev, 8, 800, 600);
    CHECK(n == 1 && ev[0].type == SDL_CONTROLLERBUTTONUP && ev[0].cbutton.button == SDL_CONTROLLER_BUTTON_A);

    /* click: mouse events carry SDL_TOUCH_MOUSEID */
    n = is_events(&s, 20, ev, 8, 800, 600);
    CHECK(n == 2 && ev[0].type == SDL_MOUSEMOTION && ev[1].type == SDL_MOUSEBUTTONDOWN);
    CHECK(ev[0].motion.which == SDL_TOUCH_MOUSEID && ev[1].button.which == SDL_TOUCH_MOUSEID);
    CHECK(ev[1].button.x == 400 && ev[1].button.y == 150 && ev[1].button.button == SDL_BUTTON_LEFT);
    n = is_events(&s, 21, ev, 8, 800, 600);
    CHECK(n == 1 && ev[0].type == SDL_MOUSEBUTTONUP && ev[0].button.which == SDL_TOUCH_MOUSEID);
    CHECK(ev[0].button.x == 400 && ev[0].button.y == 150);

    /* fingers */
    n = is_events(&s, 30, ev, 8, 800, 600);
    CHECK(n == 1 && ev[0].type == SDL_FINGERDOWN && ev[0].tfinger.fingerId == 2);
    CHECK(ev[0].tfinger.x > 0.099f && ev[0].tfinger.x < 0.101f && ev[0].tfinger.y > 0.899f && ev[0].tfinger.y < 0.901f);
    n = is_events(&s, 31, ev, 8, 800, 600);
    CHECK(n == 1 && ev[0].type == SDL_FINGERUP && ev[0].tfinger.fingerId == 2);

    /* touch buttons by name and by number */
    n = is_events(&s, 40, ev, 8, 800, 600);
    CHECK(n == 1 && ev[0].type == SDL_USEREVENT && ev[0].user.code == 1 && (intptr_t)ev[0].user.data1 == 2);
    n = is_events(&s, 41, ev, 8, 800, 600);
    CHECK(n == 1 && ev[0].type == SDL_USEREVENT && ev[0].user.code == 0 && (intptr_t)ev[0].user.data1 == 2);

    /* shot / quit */
    CHECK(is_shot(&s, 50) && strcmp(is_shot(&s, 50), "/tmp/a.png") == 0);
    CHECK(is_shot(&s, 49) == NULL && is_shot(&s, 51) == NULL);
    CHECK(!is_quit(&s, 59) && is_quit(&s, 60) && is_quit(&s, 61));
    n = is_events(&s, 50, ev, 8, 800, 600); CHECK(n == 0);

    /* several events in one frame, and max limit */
    CHECK(is_parse(&s, "5:key=A 5:key=B\n5:pad=start", err, sizeof err) == 1);
    n = is_events(&s, 5, ev, 8, 100, 100); CHECK(n == 3);
    n = is_events(&s, 5, ev, 2, 100, 100); CHECK(n == 2);
    n = is_events(&s, 6, ev, 8, 100, 100); CHECK(n == 3 && ev[0].type == SDL_KEYUP);
    CHECK(is_parse(&s, "3:key=Left_Shift", err, sizeof err) == 1);
    n = is_events(&s, 3, ev, 8, 100, 100); CHECK(n == 1 && ev[0].key.keysym.sym == SDLK_LSHIFT);

    /* @file */
    snprintf(atspec, sizeof atspec, "@%s", at);
    f = fopen(at, "w");
    CHECK(f != NULL);
    if (f) {
        fputs("# comment line\n1:key=Up  # trailing comment\n2:shot=/tmp/b.png\n\n3:quit\n", f);
        fclose(f);
        CHECK(is_parse(&s, atspec, err, sizeof err) == 1);
        CHECK(s.n == 3 && is_events(&s, 1, ev, 8, 10, 10) == 1);
        CHECK(is_shot(&s, 2) && is_quit(&s, 3));
        remove(at);
    }
    snprintf(atspec, sizeof atspec, "@%s", "/nonexistent/dir/script.txt");
    CHECK(bad(atspec));

    /* errors */
    CHECK(bad(""));
    CHECK(bad("   "));
    CHECK(bad("key=Return"));
    CHECK(bad("x:key=Return"));
    CHECK(bad("1:key=NoSuchKeyName"));
    CHECK(bad("1:key="));
    CHECK(bad("1:pad=nope"));
    CHECK(bad("1:click=0.5"));
    CHECK(bad("1:click=2:0.5"));
    CHECK(bad("1:fdown=1:0.5"));
    CHECK(bad("1:fup=x"));
    CHECK(bad("1:tdown=nobutton"));
    CHECK(bad("1:frobnicate=3"));
    CHECK(bad("1:shot"));
    CHECK(bad("1:key=A,2:bogus"));
    CHECK(s.n == 0);

    printf("input_script_test: %d checks, %d failures\n", checks, fails);
    printf(fails ? "FAIL\n" : "PASS\n");
    return fails ? 1 : 0;
}
