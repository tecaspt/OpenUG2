/* --in-race for the route checks: the course the game shows, not the
 * world-less ai_race_course decode.
 *
 * Mirrors the race start in src/main.c (race_place_on_grid, then
 * ai_race_prepare on the loaded neighbourhood): the player stands on the
 * supported grid slot nearest gate 0 with the authored grid heading (or the
 * bearing to gate 1), and ai_race_prepare builds the world-supported course
 * and binds the gates to it. The neighbourhood is loaded the way
 * race_events_check loads it (6 km around grid slot 0, the event's own
 * scenery), and the cars have race_events_check's neutral dimensions, since
 * the tools do not know the player's car.
 *
 * Returns 1 with *course (owned by the caller), the bound gates in
 * w->city.race and the player's grid position in start[2]; returns 0 where
 * the game rejects the start: no supported grid slot, a course under two
 * nodes, or a field short of the 3 (5 for URL) opponents outside downhill.
 * The loaded neighbourhood is released before returning. */
#ifndef OPENUG2_IN_RACE_COURSE_H
#define OPENUG2_IN_RACE_COURSE_H
#include "ai.h"
#include "world.h"

static int in_race_course(World *w, const char *troot, int ev, N2Path *course, float start[2]) {
    *course = (N2Path){0};
    WRace *race = &w->city.race;
    const WEvent *e = &w->city.ev[ev];
    if (!race->active || race->ngrid <= 0 || race->ngate <= 0) return 0;
    char region[64]; snprintf(region, sizeof region, "STREAM%s", e->reg);
    WorldNeighborhood nb = {0};
    WLoadOptions opt = {1, race->grid[0][0], race->grid[0][1], 6000, e->id};
    int loaded = world_neighborhood_load(&nb, troot, region, &opt), spawned = 0, grid = -1;
    AiCar player = {.half_length = 2, .half_width = .85f, .height = 1.5f};
    float (*walls)[4] = NULL, (*heights)[2] = NULL; int *sources = NULL;
    if (loaded) {
        world_ground_grid_activate(&nb.grid);
        float best = INFINITY;
        for (int k = 0; k < race->ngrid; k++) {
            const float *g = race->grid[k]; float z = g[2];
            if (!world_race_grid_ground(&nb.scene, e, g, &z)) continue;
            float d = hypotf(g[0]-race->gate[0].x, g[1]-race->gate[0].y);
            if (d < best) { best = d; grid = k; memcpy(player.pos, g, sizeof player.pos); player.pos[2] = z; }
        }
    }
    if (grid >= 0) {
        const WGate *g0 = &race->gate[0];
        player.head = race->grid_named ? race->grid_heading
                    : race->ngate > 1 ? atan2f(race->gate[1].y - g0->y, race->gate[1].x - g0->x)
                    : atan2f(g0->dy, g0->dx);
        walls = calloc(nb.scene.count ? nb.scene.count : 1, sizeof *walls);
        heights = calloc(nb.scene.count ? nb.scene.count : 1, sizeof *heights);
        sources = calloc(nb.scene.count ? nb.scene.count : 1, sizeof *sources);
    }
    if (walls && heights && sources) {
        int n = phys_collect_walls(&nb.scene, walls, sources, heights, nb.scene.count);
        phys_wall_index_build(walls, heights, n);
        AiTrafficWorld world = {.scene = &nb.scene, .obst = walls, .obstz = heights, .obstsrc = sources, .nobst = n,
                                .race_source = NULL, .race_source_choice = -1, .race_difficulty = 1};
        AiCar cars[N_RACE_AI+1] = {0}; AiRace drivers[N_RACE_AI+1] = {0};
        for (int k = 0; k < N_RACE_AI; k++) cars[k] = (AiCar){.half_length = 2, .half_width = .85f, .height = 1.5f, .mass = 1.4f};
        spawned = ai_race_prepare(troot, e, race, &world, course, cars, drivers, &player);
    }
    int wanted = e->info.kind == N2_RACE_URL ? 5 : 3;   /* main.c's acceptance rule */
    int ok = course->n >= 2 && (e->info.downhill || spawned == wanted);
    if (ok) { start[0] = player.pos[0]; start[1] = player.pos[1]; }
    else { free(course->xy); *course = (N2Path){0}; }
    free(walls); free(heights); free(sources);
    phys_wall_index_build(NULL, NULL, 0); world_ground_grid_activate(NULL);
    world_neighborhood_free(&nb); world_props_reset();
    return ok;
}
#endif
