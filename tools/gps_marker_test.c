/* gps_marker_test -- M9 U4: the 3D GPS arrow (src/gps_marker.h).
 *
 * Pose math is checked with synthetic numbers and always runs. The solid
 * check needs the game data: with $OPENUG2_DATA unset the test exits 77
 * (SKIP) after the pose checks. No retail bytes live in this file. */
#define GPS_MARKER_NO_GPU
#include "../src/gps_marker.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); \
    printf(__VA_ARGS__); printf("\n"); } } while (0)
static int near(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* world = M * (x, y, z, 1), column-major */
static void xform(const float m[16], const float p[3], float o[3]) {
    for (int r = 0; r < 3; r++) o[r] = m[r] * p[0] + m[4 + r] * p[1] + m[8 + r] * p[2] + m[12 + r];
}

static void pose_checks(void) {
    const float car[3] = {100.0f, 200.0f, 10.0f};
    float pos[3], yaw, m[16];

    /* bearing 0 = +X; t = 0 -> no bob */
    gps_marker_pose(car, 0.0f, 0, pos, &yaw);
    CHECK(near(pos[0], 100.0f + GPS_MARKER_AHEAD_M, 1e-4f) && near(pos[1], 200.0f, 1e-4f) &&
          near(pos[2], 10.0f + GPS_MARKER_HEIGHT_M, 1e-4f) && near(yaw, 0.0f, 1e-6f),
          "bearing 0 pose (%.3f %.3f %.3f) yaw %.3f", pos[0], pos[1], pos[2], yaw);

    /* bearing +pi/2 = +Y (counter-clockwise, HudState.nav_bearing convention) */
    gps_marker_pose(car, 1.5707963f, 0, pos, &yaw);
    CHECK(near(pos[0], 100.0f, 1e-3f) && near(pos[1], 200.0f + GPS_MARKER_AHEAD_M, 1e-3f),
          "bearing pi/2 pose (%.3f %.3f)", pos[0], pos[1]);

    /* bob: a quarter period is the crest, three quarters the trough */
    gps_marker_pose(car, 0.0f, GPS_MARKER_BOB_PERIOD_MS / 4, pos, &yaw);
    CHECK(near(pos[2], 10.0f + GPS_MARKER_HEIGHT_M + GPS_MARKER_BOB_M, 1e-3f), "bob crest %.4f", pos[2]);
    gps_marker_pose(car, 0.0f, 3 * GPS_MARKER_BOB_PERIOD_MS / 4 + 5 * GPS_MARKER_BOB_PERIOD_MS, pos, &yaw);
    CHECK(near(pos[2], 10.0f + GPS_MARKER_HEIGHT_M - GPS_MARKER_BOB_M, 1e-3f), "bob trough %.4f", pos[2]);

    /* model matrix: the authored tip (-Z) leads along the bearing, +Y is up,
       +X is the right of travel, and the rotation is proper (no mirror). */
    const float bearings[] = {0.0f, 0.7f, 2.5f, -1.2f};
    for (int i = 0; i < 4; i++) {
        float b = bearings[i], at[3] = {5, -3, 1}, tip[3], up[3], right[3], ctr[3];
        gps_marker_model(at, b, m);
        const float mt[3] = {0, 0, -1}, mu[3] = {0, 1, 0}, mr[3] = {1, 0, 0}, mo[3] = {0, 0, 0};
        xform(m, mt, tip); xform(m, mu, up); xform(m, mr, right); xform(m, mo, ctr);
        for (int a = 0; a < 3; a++) { tip[a] -= at[a]; up[a] -= at[a]; right[a] -= at[a]; }
        CHECK(near(ctr[0], 5, 1e-5f) && near(ctr[1], -3, 1e-5f) && near(ctr[2], 1, 1e-5f), "origin b=%.2f", b);
        CHECK(near(tip[0], cosf(b), 1e-5f) && near(tip[1], sinf(b), 1e-5f) && near(tip[2], 0, 1e-6f),
              "tip b=%.2f -> (%.3f %.3f %.3f)", b, tip[0], tip[1], tip[2]);
        CHECK(near(up[0], 0, 1e-6f) && near(up[1], 0, 1e-6f) && near(up[2], 1, 1e-6f), "up b=%.2f", b);
        CHECK(near(right[0], sinf(b), 1e-5f) && near(right[1], -cosf(b), 1e-5f), "right b=%.2f", b);
        float det = m[0] * (m[5] * m[10] - m[9] * m[6]) - m[4] * (m[1] * m[10] - m[9] * m[2]) +
                    m[8] * (m[1] * m[6] - m[5] * m[2]);
        CHECK(near(det, GPS_MARKER_SCALE * GPS_MARKER_SCALE * GPS_MARKER_SCALE, 1e-4f), "det %.4f", det);
    }
    printf("pose: %s\n", fails ? "FAIL" : "ok");
}

int main(void) {
    pose_checks();
    const char *root = getenv("OPENUG2_DATA");
    if (!root || !*root) {
        printf("SKIP solid check: OPENUG2_DATA unset\n");
        return fails ? 1 : 77;
    }
    GpsMarkerGeom g;
    int tris = gps_marker_geom_load(root, &g, NULL, NULL);
    printf("%s: %d ranges, %d triangles, texkey %08x\n", GPS_MARKER_SOLID, g.scene.count, tris, g.texkey);
    printf("bbox (%.3f %.3f %.3f) .. (%.3f %.3f %.3f)\n", g.bb_min[0], g.bb_min[1], g.bb_min[2],
           g.bb_max[0], g.bb_max[1], g.bb_max[2]);
    CHECK(tris > 0, "no %s triangles", GPS_MARKER_SOLID);
    if (tris > 0) {
        float span[3], c[3] = {0, 0, 0};
        for (int a = 0; a < 3; a++) span[a] = g.bb_max[a] - g.bb_min[a];
        /* sane: a metres-scale object, every axis 0.1..10 m, longest = the
           authored arrow axis Z (the pose relies on it) */
        for (int a = 0; a < 3; a++) CHECK(span[a] > 0.1f && span[a] < 10.0f, "span[%d] %.3f", a, span[a]);
        CHECK(span[2] > span[0] && span[2] > span[1], "longest axis is not Z");
        /* recentred: the drawn bounds straddle the origin */
        float lo[3] = {1e30f, 1e30f, 1e30f}, hi[3] = {-1e30f, -1e30f, -1e30f};
        for (int i = 0; i < g.scene.count; i++) {
            const N2Mesh *m = &g.scene.meshes[i];
            for (int k = 0; k < m->nidx; k++)
                for (int a = 0; a < 3; a++) {
                    float v = m->verts[(size_t)m->idx[k] * 5 + a];
                    if (v < lo[a]) lo[a] = v;
                    if (v > hi[a]) hi[a] = v;
                }
            CHECK(m->vcol != NULL, "range %d has no vertex colour", i);
        }
        for (int a = 0; a < 3; a++) { c[a] = 0.5f * (lo[a] + hi[a]); CHECK(near(c[a], 0, 1e-4f), "centre[%d] %.4f", a, c[a]); }
        /* the tip (narrowest end) is on -Z: the vertices within 0.2 m of each
           Z end, widest |x| at that end -- the -Z end must be the narrow one */
        float wneg = 0, wpos = 0;
        for (int i = 0; i < g.scene.count; i++) {
            const N2Mesh *m = &g.scene.meshes[i];
            for (int k = 0; k < m->nidx; k++) {
                const float *p = m->verts + (size_t)m->idx[k] * 5;
                if (p[2] < lo[2] + 0.2f && fabsf(p[0]) > wneg) wneg = fabsf(p[0]);
                if (p[2] > hi[2] - 0.2f && fabsf(p[0]) > wpos) wpos = fabsf(p[0]);
            }
        }
        printf("tip check: half-width at -Z end %.3f, at +Z end %.3f\n", wneg, wpos);
        CHECK(wneg < wpos, "-Z end is not the tip");
    }
    gps_marker_geom_free(&g);
    printf("%s\n", fails ? "gps_marker_test: FAIL" : "gps_marker_test: ok");
    return fails ? 1 : 0;
}
