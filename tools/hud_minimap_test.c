#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "hud_minimap.h"
static int N, F;
#define CK(c) do { N++; if (!(c)) { F++; printf("FAIL line %d: %s\n", __LINE__, #c); } } while (0)
static int near(double a, double b, double t) { return fabs(a - b) <= t * fmax(1.0, fmax(fabs(a), fabs(b))); }
int main(void) {
    HaMapFrame f = {{10, -20}, {1, 0, 0, 1}, {100, 80}, {1000, 2000}, 0, 200, 1280, 720, 1};
    double o[2], w[2];
    double heads[] = {0, 0.7, 1.5708, 3.0, -2.2};
    for (int h = 0; h < 5; h++) {
        f.heading = heads[h];
        f.basis[0] = 1.1; f.basis[1] = 0.2; f.basis[2] = -0.1; f.basis[3] = 0.9;
        double pts[][2] = {{1000, 2000}, {1030, 2040}, {950, 1990}, {1010, 2060}};
        for (int i = 0; i < 4; i++) {
            int r = hud_minimap_place(&f, pts[i][0], pts[i][1], 1e6, o);
            CK(r == 1);
            CK(ha_map_pick(&f, o[0], o[1], 1.0, w) == 1);
            CK(near(w[0], pts[i][0], 1e-6) && near(w[1], pts[i][1], 1e-6));
        }
    }
    f.heading = 0; f.basis[0] = f.basis[3] = 1; f.basis[1] = f.basis[2] = 0;
    CK(hud_minimap_place(&f, 1000, 2000, 50, o) == 1);
    CK(near(o[0], 640 + 1.5 * 10, 1e-9) && near(o[1], 360 - 1.5 * 20, 1e-9));
    int r = hud_minimap_place(&f, 1e5, 2000, 50, o);
    CK(r == 2);
    CK(near(o[0], 640 + 15 + 50, 1e-9) && near(o[1], 360 - 30, 1e-9));
    r = hud_minimap_place(&f, 1000, -1e5, 50, o); /* world y down -> screen +y? b>0 means up */
    CK(r == 2 && near(hypot(o[0] - 655, o[1] - 330), 50, 1e-9));
    f.enabled = 0; CK(hud_minimap_place(&f, 1000, 2000, 50, o) == 0);
    f.enabled = 1; f.window = 0; CK(hud_minimap_place(&f, 1000, 2000, 50, o) == 0);
    f.window = 200; CK(hud_minimap_place(NULL, 0, 0, 5, o) == 0);
    CK(hud_minimap_place(&f, NAN, 0, 5, o) == 0);
    double sp[] = {0, 10, 30}, wn[] = {100, 200, 400};
    CK(near(hud_minimap_window(sp, wn, 3, -5), 100, 1e-12));
    CK(near(hud_minimap_window(sp, wn, 3, 0), 100, 1e-12));
    CK(near(hud_minimap_window(sp, wn, 3, 5), 150, 1e-12));
    CK(near(hud_minimap_window(sp, wn, 3, 10), 200, 1e-12));
    CK(near(hud_minimap_window(sp, wn, 3, 20), 300, 1e-12));
    CK(near(hud_minimap_window(sp, wn, 3, 99), 400, 1e-12));
    CK(near(hud_minimap_window(sp, wn, 1, 99), 100, 1e-12));
    CK(near(hud_minimap_window(sp, wn, 0, 5), 500, 1e-12));
    CK(near(hud_minimap_window(NULL, NULL, -1, 5), 500, 1e-12));
    unsigned char c[3];
    CK(hud_minimap_tint(HMI_RIVAL, 0, c) && c[0] == 48 && c[1] == 207 && c[2] == 48);
    CK(hud_minimap_tint(HMI_RIVAL, 1, c) && c[0] == 207 && c[1] == 128 && c[2] == 48);
    CK(hud_minimap_tint(HMI_GPS, 0, c) && c[0] == 0 && c[1] == 128 && c[2] == 255);
    CK(hud_minimap_tint(HMI_EVENT, 0, c) && c[0] == 165);
    CK(hud_minimap_tint(HMI_SHOP, 4, c) && c[0] == 128 && c[2] == 0);
    CK(hud_minimap_tint(HMI_SHOWCASE, 0, c) && c[1] == 195);
    CK(hud_minimap_tint(HMI_START, 0, c) && c[0] == 192);
    CK(!hud_minimap_tint(HMI_RIVAL, 2, c));
    CK(!hud_minimap_tint(HMI_SHOP, 5, c));
    CK(!hud_minimap_tint(HMI_SHOP, -1, c));
    CK(!hud_minimap_tint(99, 0, c));
    printf("hud_minimap_test: %d checks, %d failures\n%s\n", N, F, F ? "FAIL" : "PASS");
    return F ? 1 : 0;
}
