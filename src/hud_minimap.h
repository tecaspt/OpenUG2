/* OpenUG2 M9 U1: pure, GL-free minimap helpers (placement, window, tints).
 * Screen space is the same as ha_map_pick: output pixels of the frame's
 * width x height viewport (logical 640x480 scaled by min(w/640,h/480)). */
#ifndef HUD_MINIMAP_H
#define HUD_MINIMAP_H
#include <math.h>
#include "hud_asset.h"

typedef struct { float x, y, heading; unsigned char kind, variant; } HudMapIcon;
enum { HMI_RIVAL, HMI_EVENT, HMI_SHOP, HMI_SHOWCASE, HMI_START, HMI_GPS };

/* Inverse of ha_map_pick. ring = clamp radius in pixels around the map-rect
 * centre. 1 inside (out = pixel), 2 clamped to the ring edge, 0 invalid. */
static inline int hud_minimap_place(const HaMapFrame *f, double wx, double wy, double ring, double out[2]) {
    if (!f || !f->enabled || f->width <= 0 || f->height <= 0 || !isfinite(wx) || !isfinite(wy) ||
        !isfinite(ring) || ring < 0 || f->size[0] <= 0 || f->size[1] <= 0 || f->window <= 0 ||
        !isfinite(f->window) || !isfinite(f->heading))
        return 0;
    double scale = fmin(f->width / 640.0, f->height / 480.0);
    double c = cos(f->heading), s = sin(f->heading);
    double a = (wx - f->world[0]) / f->window, b = (f->world[1] - wy) / f->window;
    double u = (c * a + s * b) * f->size[0], v = (-s * a + c * b) * f->size[1];
    double dx = f->basis[0] * u + f->basis[2] * v, dy = f->basis[1] * u + f->basis[3] * v;
    double px = scale * dx, py = scale * dy; /* pixel offset from the centre */
    int r = 1;
    if (!isfinite(px) || !isfinite(py)) return 0;
    double d = sqrt(px * px + py * py);
    if (d > ring) { px *= ring / d; py *= ring / d; r = 2; }
    out[0] = f->width * 0.5 + scale * f->center[0] + px;
    out[1] = f->height * 0.5 + scale * f->center[1] + py;
    return r;
}

/* Piecewise-linear window over ascending (spd, win); clamped; n<=0 -> 500. */
static inline double hud_minimap_window(const double *spd, const double *win, int n, double mps) {
    if (n <= 0 || !spd || !win) return 500.0;
    if (!(mps > spd[0])) return win[0];
    for (int i = 1; i < n; i++)
        if (mps <= spd[i]) {
            double span = spd[i] - spd[i - 1];
            if (!(span > 0)) return win[i];
            return win[i - 1] + (win[i] - win[i - 1]) * (mps - spd[i - 1]) / span;
        }
    return win[n - 1];
}

/* Legend tints. [data] sources: reports/parity/m9/legend.dump
 * (Demo_MiniMap_Legend.fng icon nodes, matched to label rows by screen y) and
 * fr_hud.dump (free-roam HUD).
 *  HMI_RIVAL   0 player car   legend OBJ 41 Minimap_Icon_Car       (48,207,48)
 *              1 opponent car legend OBJ 33 Minimap_Icon_Car       (207,128,48)
 *  HMI_EVENT   0 Circuit Race legend OBJ 32 Minimap_Icon_Circuit   (165,60,233)
 *              1 Short Track  legend OBJ 40 Minimap_Icon_Circuit   (130,190,230)
 *  HMI_SHOP    0 Car Lot      legend OBJ 35 Minimap_Icon_Safe_House (60,185,185)
 *              1 Graphics     legend OBJ 36 Minimap_Icon_Safe_House (120,10,25)
 *              2 Performance  legend OBJ 37 Minimap_Icon_Safe_House (43,100,164)
 *              3 Body Shop    legend OBJ 38 Minimap_Icon_Safe_House (0,128,0)
 *              4 Interiors    legend OBJ 39 Minimap_Icon_Safe_House (128,128,0)
 *  HMI_SHOWCASE 0 Showcase    legend OBJ 34 Minimap_Icon_Cop_star  (210,195,25)
 *  HMI_START   0 start line   fr_hud OBJ 94 Minimap_Icon_Start_Line (192,192,192)
 *  HMI_GPS     0 selector     fr_hud OBJ 4 Minimap_GPS_Selector    (0,128,255) */
static inline int hud_minimap_tint(int kind, int variant, unsigned char rgb[3]) {
    static const unsigned char rival[2][3] = {{48,207,48},{207,128,48}};
    static const unsigned char event[2][3] = {{165,60,233},{130,190,230}};
    static const unsigned char shop[5][3] = {{60,185,185},{120,10,25},{43,100,164},{0,128,0},{128,128,0}};
    static const unsigned char showcase[3] = {210,195,25}, start[3] = {192,192,192}, gps[3] = {0,128,255};
    const unsigned char *p = 0;
    switch (kind) {
    case HMI_RIVAL: if (variant >= 0 && variant < 2) p = rival[variant]; break;
    case HMI_EVENT: if (variant >= 0 && variant < 2) p = event[variant]; break;
    case HMI_SHOP: if (variant >= 0 && variant < 5) p = shop[variant]; break;
    case HMI_SHOWCASE: if (variant == 0) p = showcase; break;
    case HMI_START: if (variant == 0) p = start; break;
    case HMI_GPS: if (variant == 0) p = gps; break;
    }
    if (!p) return 0;
    rgb[0] = p[0]; rgb[1] = p[1]; rgb[2] = p[2];
    return 1;
}
#endif
