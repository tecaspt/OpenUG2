/* HUD host-map helpers: event icons for the minimap and wrong-way test.
 * Header-only, C99, no GL/SDL. */
#ifndef HUD_HOST_MAP_H
#define HUD_HOST_MAP_H
#include <math.h>
#include "world.h"
#include "hud_minimap.h"

/* N2_RACE_* (race.h) -> HudMapIcon.variant:
 *   CIRCUIT 0; (1 reserved for a short-track/lap-KO kind, none exists in N2_RACE_*)
 *   SPRINT 2, DRAG 3, DRIFT 4, STREETX 5, URL 6, UNKNOWN/other 7. */
static inline unsigned char hud_host_event_variant(int kind)
{
    switch (kind) {
    case N2_RACE_CIRCUIT: return 0;
    case N2_RACE_SPRINT:  return 2;
    case N2_RACE_DRAG:    return 3;
    case N2_RACE_DRIFT:   return 4;
    case N2_RACE_STREETX: return 5;
    case N2_RACE_URL:     return 6;
    default:              return 7;
    }
}

/* One HMI_EVENT per event with an outline (npoly>0; WEvent has no separate
 * "available" flag, a decoded outline is availability). x,y = poly[0], world
 * XY, the same space as the car_x/car_y fed to ha_map (hud.c:732). */
static inline int hud_host_event_icons(HudMapIcon *out, int cap, const WorldCity *city)
{
    int n = 0;
    if (!out || !city || cap <= 0) return 0;
    for (int i = 0; i < city->nev && i < WORLD_MAXEVENT && n < cap; i++) {
        const WEvent *e = &city->ev[i];
        if (e->npoly < 1) continue;
        HudMapIcon *o = &out[n++];
        o->x = e->poly[0][0];
        o->y = e->poly[0][1];
        o->heading = 0.f;
        o->kind = HMI_EVENT;
        o->variant = hud_host_event_variant(e->info.kind);
    }
    return n;
}

/* 1 when heading is more than 110 degrees from route_dir (radians, any range). */
static inline int hud_host_wrong_way(float route_dir, float heading)
{
    const double pi = 3.14159265358979323846;
    double d = fmod((double)heading - (double)route_dir, 2.0 * pi);
    if (d < 0) d = -d;
    if (d > pi) d = 2.0 * pi - d;
    return d > 110.0 * pi / 180.0;
}
#endif
