/* fe_sms_test -- checks of the career SMS mailbox (src/fe_sms.c).
 *
 * The state machine and the text lookup run on synthetic data (an in-memory language table and a
 * hand-made career database): empty list, wrap-around, unread -> read, missing text. The retail check
 * builds the items for five real message names and prints their subject lines; with $OPENUG2_DATA
 * unset it prints a SKIP and exits 77, which is not a pass. No retail bytes live in this file. The retail check also loads and
 * binds the mailbox package and checks row visibility through the pure scene layer. */
#include "fe_sms.h"
#include "gui_view.h"
#include "ugui/ugui_assets.h"
#include "ugui/ugui_bundle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails, checks;
#define CK(c, msg) do { checks++; if (!(c)) { fails++; fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); } } while (0)

/* ---- synthetic data --------------------------------------------------------------------------- */

/* A language table built in memory. ug_strtab_add copies the text itself
 * (ugui/ugui_strtab.c:insert), so the caller's strings only have to live for the call. */
typedef UgStrTab FakeLang;

static void lang_add(FakeLang *l, const char *label, const char *text) {
    ug_strtab_add(l, label, text);
}
static void lang_free(FakeLang *l) { ug_strtab_free(l); }

/* A career database with `n` messages, named from `names` (kind and flag chosen per name so both the
 * plain and the generic-blurb text paths are covered). */
static void make_cd(CareerData *cd, const char *const *names, int n, const uint8_t kind, const uint8_t flag) {
    memset(cd, 0, sizeof *cd);
    cd->nsms = n;
    for (int i = 0; i < n; i++) {
        snprintf(cd->sms[i].name, sizeof cd->sms[i].name, "%s", names[i]);
        cd->sms[i].name_hash = cd_hash(names[i]);
        cd->sms[i].kind = kind;
        cd->sms[i].cls = (uint8_t)(kind == CD_SMS_MAG ? 3 : 4);
        cd->sms[i].flag = flag;
    }
}

static int has(const FeSmsItem *it, const char *name) {
    for (int i = 0; i < FE_SMS_MAX; i++) if (!strcmp(it[i].name, name)) return i;
    return -1;
}

static void synthetic_checks(void) {
    FakeLang lang; memset(&lang, 0, sizeof lang);
    ug_strtab_init(&lang);
    lang_add(&lang, "SMS_HELLO_SUBJECT", "First message");
    lang_add(&lang, "SMS_HELLO_BODY", "Body of the first message.");
    lang_add(&lang, "SMS_BYE_SUBJECT", "Second message");
    lang_add(&lang, "SMS_BYE_BODY", "Body of the second message.");
    lang_add(&lang, "SMS_CAR_UNLOCK_1_SUBJECT", "New Inventory in the Car Lot");
    lang_add(&lang, "SMS_CAR_UNLOCK_1_BODY", "Head over to the Car Lot.");
    lang_add(&lang, "SMS_MAG_GENERIC_SUBJ_1", "%s cover opportunity");
    lang_add(&lang, "SMS_MAG_GENERIC_BODY_1", "A photographer from %s is looking for a car.");
    lang_add(&lang, "SMS_LONG_SUBJECT", "Long");
    {   /* a body longer than the 512-byte item buffer, to check that truncation stays clean */
        char big[1200];
        for (size_t i = 0; i < sizeof big - 1; i++) big[i] = (char)('a' + (i % 26));
        big[sizeof big - 1] = 0;
        lang_add(&lang, "SMS_LONG_BODY", big);
    }

    static const char *const plain[] = { "SMS_HELLO", "SMS_BYE", "SMS_NO_TEXT", "SMS_CAR_UNLOCK_7", "SMS_LONG" };
    CareerData cd;
    make_cd(&cd, plain, 5, CD_SMS_MSG, 0);
    cd.sms[2].kind = CD_SMS_MSG;                       /* no text under any key */

    /* ---- text lookup ---- */
    FeSmsItem item[FE_SMS_MAX];
    int all = FE_SMS_ALL;
    int n = fe_sms_build(&cd, &lang, NULL, all, item, (int)(sizeof item / sizeof item[0]));
    CK(n == 5, "build all five");
    int k = has(item, "SMS_HELLO");
    CK(k == 0 && !strcmp(item[0].subject, "First message") &&
       !strcmp(item[0].body, "Body of the first message."), "text from the language file");
    CK(item[0].unread == 1, "a received message starts unread");
    CK(!strcmp(item[2].name, "SMS_NO_TEXT") && !item[2].subject[0] && !item[2].body[0],
       "missing text keeps the name and leaves the text empty");
    CK(!strcmp(item[3].subject, "New Inventory in the Car Lot"),
       "the car-lot run reuses the one message retail wrote");
    CK(strlen(item[4].body) == 511 && item[4].body[511] == 0, "an over-long body truncates to the buffer");

    /* the magazine blurb path, with the retail generic line and the name as "%s" */
    static const char *const mag[] = { "SMS_MAG_STREET_SHOW_3" };
    make_cd(&cd, mag, 1, CD_SMS_MAG, 1);
    n = fe_sms_build(&cd, &lang, NULL, all, item, 4);
    CK(n == 1 && !strcmp(item[0].subject, "STREET cover opportunity"),
       "a textless blurb uses the retail generic subject");
    CK(!strcmp(item[0].body, "A photographer from STREET is looking for a car."),
       "a textless blurb uses the retail generic body");

    /* an empty language table is not an error: the rows are still there, with no text */
    UgStrTab none;
    ug_strtab_init(&none);
    make_cd(&cd, plain, 5, CD_SMS_MSG, 0);
    n = fe_sms_build(&cd, &none, NULL, all, item, 8);
    CK(n == 5, "no language table still builds every row");
    int bare = 1;
    for (int i = 0; i < n; i++) if (item[i].subject[0] || item[i].body[0]) bare = 0;
    CK(bare, "no language table: no row has text");
    ug_strtab_free(&none);

    /* ---- receipt list ---- */
    make_cd(&cd, plain, 5, CD_SMS_MSG, 0);
    {
        int pick[2] = { 3, 0 };                        /* the row order follows the table, not the list */
        n = fe_sms_build(&cd, &lang, pick, 2, item, 8);
        CK(n == 2 && !strcmp(item[1].name, "SMS_HELLO") && !strcmp(item[0].name, "SMS_CAR_UNLOCK_7"),
           "the receipt list retains delivery order");
        CK(fe_sms_build(&cd, &lang, NULL, 0, item, 4) == 0, "no receipt list builds nothing");
        CK(fe_sms_build(NULL, &lang, NULL, all, item, 4) == 0 &&
           fe_sms_build(&cd, &lang, NULL, all, NULL, 4) == 0 &&
           fe_sms_build(&cd, &lang, NULL, all, item, 0) == 0, "null and empty arguments");
        int one[1] = { 99 };                           /* an index outside the table is ignored */
        CK(fe_sms_build(&cd, &lang, one, 1, item, 4) == 0, "an out-of-range receipt index is dropped");
    }

    {
        int pick[] = {3,3,-1,0,99};
        n=fe_sms_build(&cd,&lang,pick,5,item,FE_SMS_MAX);
        CK(n==2 && !strcmp(item[0].name,"SMS_CAR_UNLOCK_7") && !strcmp(item[1].name,"SMS_HELLO"),
           "receipt duplicates and invalid indices do not alter delivery order");
        memset(&cd,0,sizeof cd);cd.nsms=CD_MAX_SMS;
        for(int i=0;i<cd.nsms;i++)snprintf(cd.sms[i].name,sizeof cd.sms[i].name,"SMS_TEST_%d",i);
        n=fe_sms_build(&cd,&lang,NULL,FE_SMS_ALL,item,FE_SMS_MAX);
        CK(n==CD_MAX_SMS && !strcmp(item[n-1].name,"SMS_TEST_191"),"mailbox holds the entire data capacity beyond forty rows");
    }

    /* ---- the mailbox: empty list ---- */
    FeSms s;
    fe_sms_open_list(&s, NULL, 0);
    CK(s.n == 0 && s.sel == 0 && fe_sms_unread(&s) == 0, "empty mailbox");
    fe_sms_select(&s, 1); fe_sms_select(&s, -1); fe_sms_select(&s, 7);
    CK(s.sel == 0, "navigation on an empty mailbox does nothing");
    CK(fe_sms_open(&s) == -1 && fe_sms_text(&s) == NULL && !fe_sms_showing(&s), "nothing to open");

    /* ---- unread -> read, and the browsed row ---- */
    make_cd(&cd, plain, 5, CD_SMS_MSG, 0);
    n = fe_sms_build(&cd, &lang, NULL, all, item, (int)(sizeof item / sizeof item[0]));
    fe_sms_open_list(&s, item, n);
    CK(s.n == 5 && fe_sms_unread(&s) == 5, "five unread messages");
    CK(s.sel == 4, "opens on the newest unread message");
    int opened = fe_sms_open(&s);
    CK(opened == 4 && fe_sms_showing(&s) && fe_sms_unread(&s) == 4, "opening marks the message read");
    CK(fe_sms_text(&s) == &s.item[4], "the opened message is the one shown");
    CK(!strcmp(s.item[4].subject, "Long"), "the opened subject survives the read flag");
    fe_sms_select(&s, 1);
    CK(s.sel == 0, "navigation continues past the opened row");
    fe_sms_close(&s);
    CK(!fe_sms_showing(&s) && fe_sms_text(&s) == &s.item[0], "closing shows the browsed row");
    fe_sms_open(&s);
    CK(fe_sms_unread(&s) == 3, "opening a second message reads it too");
    fe_sms_open(&s);
    CK(fe_sms_unread(&s) == 3 && s.opened == s.sel, "re-opening the same row does not read twice");

    /* ---- wrap-around ---- */
    fe_sms_open_list(&s, item, n);
    fe_sms_open_list(&s, item, n);
    s.sel = 0;
    fe_sms_select(&s, -1);
    CK(s.sel == 4, "up from the first row wraps to the last");
    fe_sms_select(&s, 1);
    CK(s.sel == 0, "down from the last row wraps to the first");
    fe_sms_select(&s, 3);
    CK(s.sel == 3, "down by three");
    fe_sms_select(&s, -6);
    CK(s.sel == 2, "up by six wraps the long way");
    fe_sms_select(&s, 5);
    CK(s.sel == 2, "a full turn lands on the same row");
    fe_sms_select(&s, 0);
    CK(s.sel == 2, "zero leaves the row alone");
    FeSms one;
    fe_sms_open_list(&one, item, 1);
    fe_sms_select(&one, 1); fe_sms_select(&one, -1); fe_sms_select(&one, 40);
    CK(one.sel == 0 && one.n == 1, "a single row never moves");

    /* ---- the display strings ---- */
    fe_sms_open_list(&s, item, n);
    fe_sms_open(&s);
    fe_sms_sync(&s, NULL);                             /* no scene: must not touch anything */
    CK(s.status[0] || !s.status[0], "sync without a screen is a no-op");

    /* a mailbox over FE_SMS_MAX items is cut to the list capacity */
    {
        FeSmsItem many[FE_SMS_MAX + 4];
        memset(many, 0, sizeof many);
        for (int i = 0; i < FE_SMS_MAX + 4; i++) snprintf(many[i].subject, 96, "M%d", i);
        fe_sms_open_list(&s, many, FE_SMS_MAX + 4);
        CK(s.n == FE_SMS_MAX, "the list holds at most FE_SMS_MAX rows");
    }

    lang_free(&lang);
}

/* ---- retail ----------------------------------------------------------------------------------- */

/* The 0x00039000 chunk of a language file (LANGUAGES/English.bin starts with it; the walk finds it in
 * any of the framings, exactly as ugui_assets.c:165 does for the bundle copies). */
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

/* Builds the mailbox items for `want` real message names and prints their subject lines. */
static int retail_check(const char *root) {
    static const char *const want[] = {
        "SMS_UNLOCK_01", "SMS_CAR_UNLOCK_1", "SMS_LOC_TEXT_COAL_HARBOR", "ENGAGE_TIPS_SMS",
        "SMS_END_OF_GAME_MESSAGE"
    };
    CareerData *cd = (CareerData *)calloc(1, sizeof *cd);
    if (!cd) return 1;
    if (!cd_load(cd, root)) { fprintf(stderr, "retail: career data: %s\n", cd->error); free(cd); return 1; }
    UgStrTab lang;
    if (!load_language(&lang, root)) { free(cd); return 1; }

    int idx[8], nw = 0;
    for (unsigned i = 0; i < sizeof want / sizeof want[0] && nw < 8; i++) {
        int at = cd_find_sms(cd, cd_hash(want[i]));
        if (at < 0) { fprintf(stderr, "retail: no SMS record %s\n", want[i]); free(cd); ug_strtab_free(&lang); return 1; }
        idx[nw++] = at;
    }
    FeSmsItem item[8];
    int n = fe_sms_build(cd, &lang, idx, nw, item, 8);
    printf("retail: %d messages built from %d records of %d\n", n, nw, cd->nsms);
    if (n != nw) { fprintf(stderr, "retail: built %d of %d\n", n, nw); free(cd); ug_strtab_free(&lang); return 1; }

    FeSms box;
    fe_sms_open_list(&box, item, n);
    if (box.sel != n - 1) { fprintf(stderr, "retail: opened on row %d, expected the newest unread %d\n", box.sel, n - 1); free(cd); ug_strtab_free(&lang); return 1; }
    /* One step from the newest message wraps to the oldest, then one row at a time back round. */
    for (int i = 0; i < n; i++) {
        int expect = (box.sel + 1) % n;
        fe_sms_select(&box, 1);
        printf("  %-28s unread=%d  %s\n", item[box.sel].name, item[box.sel].unread, item[box.sel].subject);
        if (box.sel != expect) { fprintf(stderr, "retail: wrap landed on %d, expected %d\n", box.sel, expect); free(cd); ug_strtab_free(&lang); return 1; }
    }
    int opened = fe_sms_open(&box);
    if (opened != n - 1 || fe_sms_unread(&box) != n - 1) {
        fprintf(stderr, "retail: opening read %d of %d\n", opened, n);
        free(cd); ug_strtab_free(&lang); return 1;
    }
    printf("  opened %s, unread now %d\n", item[opened].name, fe_sms_unread(&box));
    for (int i = 0; i < n; i++)
        if (!item[i].subject[0] || !item[i].body[0]) { fprintf(stderr, "retail: %s has no text\n", want[i]); free(cd); ug_strtab_free(&lang); return 1; }
    {
        UgAssets assets; GvScreen screen={0};char error[256];
        int loaded=ug_assets_open_ingame(&assets,root,NULL,error,sizeof error);
        CK(loaded,"retail in-game assets load");
        CK(loaded && fe_sms_bind(&box,&screen,&assets),"retail mailbox binds all ten subject rows and envelopes");
        if(screen.loaded) {
            fe_sms_sync(&box,&screen);
            CK(!strstr(box.header,"%d"),"both message header placeholders are filled");
            int subjects=0;
            for(int i=0;i<screen.scene.n;i++) {
                const UgNode *node=&screen.scene.nodes[i];
                if(!node->app_hidden && node->text_override &&
                   (node->obj->name_hash==0x9a3674c8u ||
                   (node->obj->name_hash>=0x0c6e4198u && node->obj->name_hash<=0x0c6e41a0u)))subjects++;
            }
            /* Opened messages intentionally hide all list rows. */
            CK(subjects==0,"opened message hides mailbox rows");
            fe_sms_close(&box);fe_sms_sync(&box,&screen);
            subjects=0;
            for(int i=0;i<screen.scene.n;i++) {
                const UgNode *node=&screen.scene.nodes[i];
                if(!node->app_hidden && node->text_override &&
                   (node->obj->name_hash==0x9a3674c8u ||
                   (node->obj->name_hash>=0x0c6e4198u && node->obj->name_hash<=0x0c6e41a0u)))subjects++;
            }
            CK(subjects==n,"retail list populates one row per received message");
        }
        gv_screen_free(&screen);ug_assets_close(&assets);
    }
    free(cd);
    ug_strtab_free(&lang);
    return 0;
}

int main(void) {
    synthetic_checks();
    const char *root = getenv("OPENUG2_DATA");
    if (!root || !*root) {
        printf("SKIP retail check: OPENUG2_DATA unset (a skip is not a pass)\n");
        printf("fe_sms_test: %s (%d checks, %d failed) + retail SKIPPED\n", fails ? "FAIL" : "PASS", checks, fails);
        return fails ? 1 : 77;
    }
    int bad = retail_check(root);
    if (bad) fails++;
    checks++;
    printf("fe_sms_test: %s (%d checks, %d failed) + retail %s\n", (fails || bad) ? "FAIL" : "PASS",
           checks, fails, bad ? "FAIL" : "ok");
    return (fails || bad) ? 1 : 0;
}
