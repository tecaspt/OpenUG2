#include <stdio.h>
#include <stdlib.h>
#include "hud_host_map.h"
static int checks, fails;
#define CHECK(c) do { checks++; if (!(c)) { fails++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
#define R(d) ((float)((d) * 3.14159265358979323846 / 180.0))
static void ev(WorldCity *c, int i, int kind, int npoly, float x, float y)
{
    c->ev[i].info.kind = kind; c->ev[i].npoly = npoly;
    c->ev[i].poly[0][0] = x; c->ev[i].poly[0][1] = y;
}
int main(void)
{
    WorldCity *c = calloc(1, sizeof *c);
    HudMapIcon out[16];
    if (!c) return 2;
    ev(c, 0, N2_RACE_CIRCUIT, 5, 1, 2);
    ev(c, 1, N2_RACE_SPRINT, 0, 9, 9);   /* no outline: skipped */
    ev(c, 2, N2_RACE_DRAG, 4, 3, 4);
    ev(c, 3, N2_RACE_DRIFT, 4, 5, 6);
    ev(c, 4, N2_RACE_STREETX, 4, 7, 8);
    ev(c, 5, N2_RACE_URL, 4, 9, 10);
    ev(c, 6, N2_RACE_UNKNOWN, 4, 11, 12);
    ev(c, 7, N2_RACE_SPRINT, 4, 13, 14);
    c->nev = 8;
    CHECK(hud_host_event_icons(out, 16, c) == 7);
    CHECK(out[0].kind == HMI_EVENT && out[0].variant == 0 && out[0].x == 1 && out[0].y == 2 && out[0].heading == 0);
    CHECK(out[1].variant == 3 && out[1].x == 3);
    CHECK(out[2].variant == 4);
    CHECK(out[3].variant == 5);
    CHECK(out[4].variant == 6);
    CHECK(out[5].variant == 7);
    CHECK(out[6].variant == 2 && out[6].x == 13 && out[6].y == 14);
    CHECK(hud_host_event_icons(out, 3, c) == 3);
    CHECK(hud_host_event_icons(out, 0, c) == 0);
    c->nev = 0;
    CHECK(hud_host_event_icons(out, 16, c) == 0);
    CHECK(hud_host_wrong_way(0, R(0)) == 0);
    CHECK(hud_host_wrong_way(0, R(90)) == 0);
    CHECK(hud_host_wrong_way(0, R(109)) == 0);
    CHECK(hud_host_wrong_way(0, R(111)) == 1);
    CHECK(hud_host_wrong_way(0, R(180)) == 1);
    CHECK(hud_host_wrong_way(0, R(-179)) == 1);
    CHECK(hud_host_wrong_way(0, R(-90)) == 0);
    CHECK(hud_host_wrong_way(0, R(-111)) == 1);
    CHECK(hud_host_wrong_way(0, R(360)) == 0);
    CHECK(hud_host_wrong_way(0, R(360 + 111)) == 1);
    CHECK(hud_host_wrong_way(R(170), R(-170)) == 0);
    CHECK(hud_host_wrong_way(R(-100), R(100)) == 1);
    free(c);
    printf("hud_host_map_test: %d checks, %d failures\n%s\n", checks, fails, fails ? "FAIL" : "PASS");
    return fails != 0;
}
