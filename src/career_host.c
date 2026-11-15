/* career_host.c -- see career_host.h. */
#include "career_host.h"
#include "settings.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* Same legend mapping as hud_host_event_variant (hud_host_map.h). race.h is not included because its static
 * helpers trip -Wunused-function here; the kind values are N2_RACE_* (race.h): CIRCUIT 1, SPRINT 2, DRAG 3, DRIFT 4,
 * STREETX 5, URL 6. */
static unsigned char ch_variant(int kind)
{
    if (kind == 1) return 0;                 /* circuit */
    if (kind >= 2 && kind <= 6) return (unsigned char)kind;
    return 7;
}

static const GcEvent *ch_event(const CareerHost *ch, int id)
{
    if (!ch->cat) return NULL;
    for (int i = 0; i < ch->cat->nev; i++) if (ch->cat->ev[i].id == id) return &ch->cat->ev[i];
    return NULL;
}

static void zone_centre(const CdZone *z, float *x, float *y)
{
    *x = 0.25f * (z->poly[0][0] + z->poly[1][0] + z->poly[2][0] + z->poly[3][0]);
    *y = 0.25f * (z->poly[0][1] + z->poly[1][1] + z->poly[2][1] + z->poly[3][1]);
}

int ch_init(CareerHost *ch, const char *dataroot, const GameCatalog *cat)
{
    if (!ch) return 0;
    memset(ch, 0, sizeof *ch);
    ch->race = -1; ch->cat = cat;
    if (!dataroot || !cd_load(&ch->cd, dataroot)) {
        snprintf(ch->last, sizeof ch->last, "career data unavailable: %s", ch->cd.error[0] ? ch->cd.error : "no data root");
        return 0;
    }
    ch->loaded = 1;
    snprintf(ch->last, sizeof ch->last, "career data: %d races, %d stages, %d zones", ch->cd.nrace, ch->cd.nstage, ch->cd.nzone);
    return 1;
}

static int ch_save(CareerHost *ch)
{
    int rc = cr_save_file(ch->root, ch->profile, &ch->st);
    if (rc != ST_OK) {
        snprintf(ch->last, sizeof ch->last, "career: save failed (%d) for profile %s", rc, ch->profile);
        fprintf(stderr, "%s\n", ch->last);
    }
    return rc == ST_OK;
}

static void free_roam_launch(const char *car, GfLaunch *out)
{
    memset(out, 0, sizeof *out);
    out->free_run = 1;
    snprintf(out->track, sizeof out->track, "%s", CH_CITY_TRACK);
    snprintf(out->car, sizeof out->car, "%s", car ? car : "");
}

int ch_begin(CareerHost *ch, int new_career, const char *root, const char *profile, const char *car, GfLaunch *out)
{
    if (!ch || !ch->loaded || !root || !profile || !out) return 0;
    snprintf(ch->root, sizeof ch->root, "%s", root);
    snprintf(ch->profile, sizeof ch->profile, "%s", profile);
    ch->race = -1; ch->resolved = 0; ch->spawn_valid = 0;
    if (new_career) {
        cr_new(&ch->st, &ch->cd, car);
        if (!ch_save(ch)) return 0;
    } else {
        int rc = cr_load_file(root, profile, &ch->st);
        if (rc != ST_OK || !ch->st.started) {
            snprintf(ch->last, sizeof ch->last, "career: no saved career to continue (%d)", rc);
            return 0;
        }
        if (ch->st.pose[0] != 0.0f || ch->st.pose[1] != 0.0f) {
            memcpy(ch->spawn, ch->st.pose, sizeof ch->spawn); ch->spawn_valid = 1;
        }
        if (ch->st.car[0]) car = ch->st.car;   /* [inferred] continue in the career car */
    }
    ch->active = 1;
    free_roam_launch(car, out);
    int nav[CD_MAX_RACES];
    snprintf(ch->last, sizeof ch->last, "career: %s stage %d bank %lld rep %u, %d events available",
             new_career ? "new" : "continue", ch->st.stage, (long long)ch->st.bank, ch->st.rep,
             cr_list(&ch->st, &ch->cd, nav, CD_MAX_RACES));
    return 1;
}

int ch_zone(const CareerHost *ch, float x, float y, float kmh)
{
    if (!ch || !ch->active || ch->race >= 0 || fabsf(kmh) > CH_ENGAGE_KMH) return -1;
    return cr_zone_hit(&ch->st, &ch->cd, x, y);
}

int ch_engage(CareerHost *ch, float x, float y, float heading, float kmh, const char *car, GfLaunch *out)
{
    int r = ch_zone(ch, x, y, kmh);
    if (r < 0 || !out) return -1;
    const CdRace *race = &ch->cd.race[r];
    if (race->nheat < 1) return -1;
    const GcEvent *ev = ch_event(ch, race->heat[0].track);
    if (!ev) {
        snprintf(ch->last, sizeof ch->last, "career: event %u of %s is not in the catalogue", race->heat[0].track, race->name);
        return -1;
    }
    memset(out, 0, sizeof *out);
    out->event_id = ev->id;
    snprintf(out->track, sizeof out->track, "STREAM%s", ev->region);
    snprintf(out->car, sizeof out->car, "%s", car ? car : "");
    out->laps = race->heat[0].laps;   /* 0 lets the host keep the event default */
    out->difficulty = 1;              /* [inferred] career difficulty: medium */
    ch->st.pose[0] = x; ch->st.pose[1] = y; ch->st.pose[2] = 0.0f; ch->st.pose[3] = heading;
    if (car && car[0]) snprintf(ch->st.car, sizeof ch->st.car, "%s", car);
    ch->race = r; ch->resolved = 0;
    snprintf(ch->last, sizeof ch->last, "career: engage %s (event %d, %s, %d heat(s))",
             race->name, ev->id, out->track, race->nheat);
    return r;
}

int ch_on_result(CareerHost *ch, int finish_place, int ncars, int failed, int aborted, double seconds)
{
    if (!ch || !ch->active || ch->race < 0 || ch->resolved) return 0;
    CrResult res; memset(&res, 0, sizeof res);
    res.race_hash = ch->cd.race[ch->race].name_hash;
    res.place = finish_place; res.ncars = ncars; res.failed = failed; res.aborted = aborted; res.seconds = seconds;
    memset(&ch->outcome, 0, sizeof ch->outcome);
    if (!cr_apply(&ch->st, &ch->cd, &res, NULL, NULL, &ch->outcome)) return 0;
    ch->resolved = 1;
    ch_save(ch);
    snprintf(ch->last, sizeof ch->last, "career: %s place %d%s +%d cash +%d rep -> bank %lld rep %u stage %d%s",
             ch->cd.race[ch->race].name, finish_place, failed ? " (failed)" : aborted ? " (aborted)" : "",
             ch->outcome.reward.cash, ch->outcome.reward.rep, (long long)ch->st.bank, ch->st.rep, ch->st.stage,
             ch->outcome.stage_after != ch->outcome.stage_before ? " (stage complete)" : "");
    return 1;
}

int ch_return(CareerHost *ch, const char *car, GfLaunch *out)
{
    if (!ch || !ch->active || !out) return 0;
    if (ch->race >= 0 && !ch->resolved) ch_on_result(ch, 0, 0, 0, 1, 0.0);   /* left without a result: aborted */
    ch->race = -1;
    free_roam_launch(car, out);
    memcpy(ch->spawn, ch->st.pose, sizeof ch->spawn);
    ch->spawn_valid = ch->st.pose[0] != 0.0f || ch->st.pose[1] != 0.0f;
    return 1;
}

void ch_suspend(CareerHost *ch, float x, float y, float z, float heading)
{
    if (!ch || !ch->active) return;
    if (ch->race >= 0) {
        if (!ch->resolved) ch_on_result(ch, 0, 0, 0, 1, 0.0);
    } else {
        ch->st.pose[0] = x; ch->st.pose[1] = y; ch->st.pose[2] = z; ch->st.pose[3] = heading;
    }
    ch_save(ch);
    ch->active = 0; ch->race = -1; ch->spawn_valid = 0;
}

int ch_icons(const CareerHost *ch, HudMapIcon *out, int cap)
{
    if (!ch || !ch->active || ch->race >= 0 || !out || cap <= 0) return 0;
    int idx[CD_MAX_RACES], n = cr_list(&ch->st, &ch->cd, idx, CD_MAX_RACES), k = 0;
    for (int i = 0; i < n && k < cap; i++) {
        const CdRace *r = &ch->cd.race[idx[i]];
        int z = cd_find_zone(&ch->cd, r->locator_hash);
        if (z < 0) continue;
        HudMapIcon *o = &out[k++];
        zone_centre(&ch->cd.zone[z], &o->x, &o->y);
        o->heading = 0.0f; o->kind = HMI_EVENT; o->variant = ch_variant(r->kind);
    }
    return k;
}

const char *ch_prompt(const CareerHost *ch, float x, float y, float kmh, char *buf, int cap)
{
    int r = ch_zone(ch, x, y, kmh);
    if (r < 0 || !buf || cap <= 0) return NULL;
    const CdRace *race = &ch->cd.race[r];
    snprintf(buf, (size_t)cap, "ENTER RACE: %s  $%u", race->name, race->purse);   /* [inferred] text; retail ENGAGE_* later */
    return buf;
}

int ch_take_spawn(CareerHost *ch, float spawn[4])
{
    if (!ch || !ch->spawn_valid || !spawn) return 0;
    memcpy(spawn, ch->spawn, sizeof ch->spawn);
    ch->spawn_valid = 0;
    return 1;
}
