/* fe_lot_test -- synthetic checks of the M6 Car Lot (fe_lot.c) and its FeMenu wiring (fe_menu.c):
 * browsing + preview debounce, must_buy Back block, the two-step buy, owned car, roam requests from
 * the lot and from the career Garage. No retail data: the menu runs logic-only (no package).
 *
 * The retail block at the bottom walks the installed-car catalogue and asserts every model the lot
 * can list has a CAR_NAME_<model> key in LANGUAGES/English.bin. fe_lot.c builds that key at run time
 * (snprintf "CAR_NAME_%s" then ug_hash_upper), so no static scan can judge it and a missing key shows
 * the raw folder name ("PEUGOT" instead of "206") instead of failing. With $OPENUG2_DATA unset it
 * prints a SKIP and exits 77, which is not a pass. No retail bytes live in this file. */
#include "fe_lot.h"
#include "fe_menu.h"
#include "fe_screen.h"
#include "resource.h"
#include "ugui/ugui_bundle.h"
#include "ugui/ugui_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CK(c, msg) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

static FemRequest step(FeMenu *m, int frames) {
    FemRequest r; memset(&r, 0, sizeof r);
    for (int k = 0; k < frames; k++) { FemRequest q = fem_update(m, 1.0f / 30); if (q.kind) r = q; }
    return r;
}
static void key(FeMenu *m, SDL_Keycode k) {
    SDL_Event e; memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN; e.key.keysym.sym = k;
    fem_event(m, &e, NULL);
}

static void synthetic_checks(void) {
    FeLotCar cars[4]; memset(cars, 0, sizeof cars);
    const char *names[4] = { "PEUGOT", "FOCUS", "COROLLA", "240SX" };
    for (int i = 0; i < 4; i++) { snprintf(cars[i].name, sizeof cars[i].name, "%s", names[i]); cars[i].starter = 1; }

    /* ---- state machine ---- */
    FeLot l;
    fel_open(&l, cars, 4, 1, 0);
    CK(l.n == 4 && l.sel == 0 && !strcmp(fel_selected(&l), "PEUGOT"), "opens on the first car");
    CK(fel_input(&l, FES_IN_LEFT) == FEL_EV_NONE && l.sel == 0, "left at the first car does not wrap");
    fel_input(&l, FES_IN_RIGHT); fel_input(&l, FES_IN_RIGHT);
    CK(!strcmp(fel_selected(&l), "COROLLA"), "right browses");
    fel_advance(&l, 0.2f); CK(!l.preview[0], "preview debounced");
    fel_advance(&l, 0.2f); CK(!strcmp(l.preview, "COROLLA"), "preview after 0.3 s");
    fel_input(&l, FES_IN_RIGHT); fel_input(&l, FES_IN_RIGHT);
    CK(l.sel == 3 && !l.preview[0], "right stops at the last car, preview reset");
    CK(fel_input(&l, FES_IN_BACK) == FEL_EV_NONE && l.msg == FEL_MSG_MUST_BUY, "must_buy blocks Back");
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_NONE && l.confirm && l.msg == FEL_MSG_CONFIRM, "first Accept asks");
    CK(fel_input(&l, FES_IN_BACK) == FEL_EV_NONE && !l.confirm && l.msg == FEL_MSG_NONE, "Back cancels the question");
    fel_input(&l, FES_IN_ACCEPT);
    CK(fel_input(&l, FES_IN_LEFT) == FEL_EV_NONE && !l.confirm && l.sel == 2, "browsing cancels the question");
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_NONE, "ask again");
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_BUY && !strcmp(fel_selected(&l), "COROLLA"), "second Accept buys");
    fel_open(&l, cars, 4, 0, 1234);
    CK(fel_input(&l, FES_IN_BACK) == FEL_EV_LEAVE && l.bank == 1234, "Back leaves when not must_buy");
    cars[0].owned = 1;
    fel_open(&l, cars, 4, 0, 0);
    CK(l.sel == 1, "opens on the first car not owned");
    fel_input(&l, FES_IN_LEFT);
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_NONE && l.msg == FEL_MSG_SAME_CAR && !l.confirm, "owned car: SAME_CAR");
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_NONE, "owned car never buys");
    cars[0].owned = 0;
    fel_open(&l, NULL, 0, 1, 0);
    CK(fel_input(&l, FES_IN_ACCEPT) == FEL_EV_NONE && !fel_selected(&l)[0], "empty lot");

    /* ---- FeMenu wiring (logic only: no data root) ---- */
    FeMenu *m = fem_create("/nonexistent-openug2-data", NULL, 0);
    CK(m != NULL, "menu");
    if (!m) return;
    fem_open_lot(m, cars, 4, 1, 0);
    CK(fem_is_open(m) && fem_screen(m) == FEM_SCR_CAREER_LOT && !fem_visual_available(m), "lot opens (logic only)");
    key(m, SDLK_RIGHT); step(m, 15);
    CK(!strcmp(fem_preview_car(m), "FOCUS"), "lot preview follows the browsed car");
    key(m, SDLK_ESCAPE); FemRequest r = step(m, 2);
    CK(r.kind == FEM_REQ_NONE && fem_screen(m) == FEM_SCR_CAREER_LOT, "must_buy: Back stays on the lot");
    key(m, SDLK_RETURN); r = step(m, 2);
    CK(r.kind == FEM_REQ_NONE, "first Accept only asks");
    key(m, SDLK_RETURN); r = step(m, 2);
    CK(r.kind == FEM_REQ_LOT_BUY && !strcmp(r.car, "FOCUS"), "second Accept requests the buy");
    key(m, SDLK_RETURN); r = step(m, 2);
    CK(r.kind == FEM_REQ_NONE, "locked after the request");
    fem_open_lot(m, cars, 4, 0, 500);
    key(m, SDLK_ESCAPE); r = step(m, 2);
    CK(r.kind == FEM_REQ_CAREER_ROAM, "leaving the lot requests roam");
    fem_close(m);
    CK(!fem_preview_car(m)[0], "no preview once closed");
    fem_open_career_garage(m);
    CK(fem_screen(m) == FEM_SCR_GARAGE, "career garage opens");
    key(m, SDLK_ESCAPE); r = step(m, 2);
    CK(r.kind == FEM_REQ_CAREER_ROAM, "leaving the career garage requests roam");
    fem_close(m);
    fem_open(m, FEM_SCR_GARAGE);
    key(m, SDLK_ESCAPE); r = step(m, 2);
    CK(r.kind == FEM_REQ_NONE && fem_screen(m) == FEM_SCR_MAIN, "Customize garage still returns to Main");
    fem_destroy(m);
}

/* ---- retail: every catalogue car needs a CAR_NAME_<model> key ------------------------------------ */

/* The 0x00039000 chunk of a language file, found exactly as ugui_assets.c does for the bundle copies
 * and as fe_sms_test.c does for its own retail check. */
typedef struct { const uint8_t *hdr; size_t len; int found; } StrTabSeek;
static int find_strtab(void *user, const UgChunkRef *c) {
    StrTabSeek *w = (StrTabSeek *)user;
    if (w->found) return 0;
    if (c->id == UG_CHUNK_STRINGS) { w->hdr = c->hdr; w->len = (size_t)(c->payload - c->hdr) + c->size; w->found = 1; return 1; }
    return 0;
}

static int load_language(UgStrTab *t, const char *root) {
    char path[1100], err[160] = "";
    UgBuf b = { 0, 0 };
    snprintf(path, sizeof path, "%s/LANGUAGES/English.bin", root);
    if (!ug_file_read(path, &b, err, sizeof err)) { fprintf(stderr, "retail: %s\n", err); return 0; }
    StrTabSeek w = { NULL, 0, 0 };
    if (!ug_chunks_walk(b.data, b.len, find_strtab, &w, err, sizeof err) || !w.found) {
        fprintf(stderr, "retail: no string table in English.bin (%s)\n", err);
        ug_buf_free(&b);
        return 0;
    }
    ug_strtab_init(t);
    if (!ug_strtab_load(t, w.hdr, w.len, err, sizeof err)) {
        fprintf(stderr, "retail: %s\n", err);
        ug_buf_free(&b);
        return 0;
    }
    ug_buf_free(&b);
    return 1;
}

/* [data] game_catalog.c calls a car traffic when CARS/<model>/TEXTURES.BIN is empty or absent, and
 * career_host.c:inst_list installs exactly the non-traffic cars -- which is what ch_lot_list hands the
 * lot, so that is the reachable set of fel_sync's CAR_NAME_<model>. The empty-file test is the
 * catalogue's own rule, deliberately not a name guess: FIRETRUCK looks like traffic and is not, and
 * COUPE does not look like traffic and is. Names stay capped at the 63 characters res_list_cars can
 * produce, so "CAR_NAME_" plus the name always fits fel_sync's 64-byte key buffer. -1 is a listing
 * failure, never a pass. */
#define LOT_NAME_MAX 63
#define LOT_STRIFY(x) #x
#define LOT_MAXSTR(x) LOT_STRIFY(x)
static int catalogue_cars(const char *root, char (*names)[LOT_NAME_MAX + 1], int cap) {
    char (*list)[64] = malloc(128 * sizeof *list);
    if (!list) return -1;
    int sel = 0, n = res_list_cars(root, list, 128, "", &sel);
    int out = 0;
    for (int i = 0; i < n; i++) {
        char p[1300];
        snprintf(p, sizeof p, "%s/CARS/%s/TEXTURES.BIN", root, list[i]);
        FILE *f = fopen(p, "rb");
        if (!f) continue;                                   /* res_list_cars already required GEOMETRY.BIN */
        int empty = fseek(f, 0, SEEK_END) == 0 && ftell(f) == 0;
        fclose(f);
        if (empty) continue;                                /* a traffic body: retail never names it */
        if (strlen(list[i]) > LOT_NAME_MAX) {               /* fel_sync's key buffer would truncate */
            fprintf(stderr, "model name %.64s is longer than the lot key buffer allows\n", list[i]);
            free(list);
            return -1;
        }
        if (out >= cap) { free(list); return -1; }
        snprintf(names[out++], LOT_NAME_MAX + 1, "%s", list[i]);
    }
    free(list);
    return out;
}

static int retail_check(const char *root) {
    char names[128][LOT_NAME_MAX + 1];
    int n = catalogue_cars(root, names, (int)(sizeof names / sizeof names[0]));
    if (n < 0) { fprintf(stderr, "retail: catalogue could not be listed\n"); return 1; }
    if (n == 0) { fprintf(stderr, "retail: no installed car found to check (a vacuous pass)\n"); return 1; }
    UgStrTab lang;
    if (!load_language(&lang, root)) return 1;
    int missing = 0;
    for (int i = 0; i < n; i++) {
        /* The explicit precision keeps the compiler's bound proof as narrow as the cap above; the
         * key is what fel_sync builds, so a longer name would hash differently. */
        char key[80];
        snprintf(key, sizeof key, "CAR_NAME_%." LOT_MAXSTR(LOT_NAME_MAX) "s", names[i]);
        if (!ug_strtab_get(&lang, ug_hash_upper(key))) {
            fprintf(stderr, "retail: %s missing from English.bin: the lot would show \"%.31s\"\n", key, names[i]);
            missing++;
        }
    }
    /* The caption wm_snapshot picks for this screen's map icon, and the crib it returns from.
     * The world map has no fallback text for a caption, so a key that stops resolving paints
     * nothing; nothing else in the build checks these two at run time. */
    static const char *const captions[] = {"MINIMAP_CARLOT", "MINIMAP_GARAGE"};
    for (int i = 0; i < 2; i++) {
        const char *text = ug_strtab_get(&lang, ug_hash_upper(captions[i]));
        CK(text && *text, captions[i]);
    }
    ug_strtab_free(&lang);
    printf("retail: %d installed cars, %d missing CAR_NAME_<model>\n", n, missing);
    return missing ? 1 : 0;
}

int main(void) {
    synthetic_checks();
    const char *root = getenv("OPENUG2_DATA");
    if (!root || !*root) {
        printf("SKIP retail check: OPENUG2_DATA unset (a skip is not a pass)\n");
        printf("fe_lot_test: %s (%d checks, %d failed) + retail SKIPPED\n", fails ? "FAIL" : "PASS", checks, fails);
        return fails ? 1 : 77;
    }
    int bad = retail_check(root);
    checks++;
    if (bad) fails++;
    printf("fe_lot_test: %s (%d checks, %d failed) + retail %s\n", (fails || bad) ? "FAIL" : "PASS",
           checks, fails, bad ? "FAIL" : "ok");
    return (fails || bad) ? 1 : 0;
}
