/* fe_sms.c -- see fe_sms.h. Package binding follows fe_lot.c: node roles by name hash, the authored
 * sample text stays where the language file has no key, and unimplemented nodes are hidden rather
 * than shown with authored example values. Node identities: docs/parity/SMS-MAILBOX.md 3. */
#include "fe_sms.h"
#include "gui_view.h"

#include <stdio.h>
#include <string.h>

/* [inferred] Header width: the authored list window (the U2_Menu_Box.tga sheet of node 0x22561fee,
 * 562 wide). A longer caption is scaled to it instead of clipped, as fel_sync does for the car lot. */
#define FES_HEADER_W 562.0f

static void sms_copy(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src ? src : "");
}

/* Retail text for one key; NULL when the table is missing or has no such key. */
static const char *sms_key(const LangTable *lang, const char *key) {
    if (!lang || !key) return NULL;
    return ug_strtab_get(lang, ug_hash_upper(key));
}

/* Replaces the first "%s" of `fmt` with `arg` (the fill_arg rule of fe_lot.c). */
static void sms_fill(char *out, size_t cap, const char *fmt, const char *arg) {
    const char *at = strstr(fmt, "%s");
    if (!at) { sms_copy(out, cap, fmt); return; }
    snprintf(out, cap, "%.*s%s%s", (int)(at - fmt), fmt, arg, at + 2);
}

/* Replaces the first "%d" of `fmt` with `value`. */
static void sms_fill_n(char *out, size_t cap, const char *fmt, int value) {
    const char *at = strstr(fmt, "%d");
    if (!at) { sms_copy(out, cap, fmt); return; }
    snprintf(out, cap, "%.*s%d%s", (int)(at - fmt), fmt, value, at + 2);
}

/* The magazine name inside a blurb record name (SMS_MAG_<mag>_SHOW_<n>, SMS_UNIQUE_<x>), for the
 * "%s" of the retail generic blurb lines. NULL when the name carries none. */
static const char *sms_magazine(const char *name) {
    static const char *pre[] = { "SMS_MAG_", "SMS_UNIQUE_", "SMS_SHOWCASE_" };
    for (unsigned k = 0; k < sizeof pre / sizeof pre[0]; k++) {
        size_t n = strlen(pre[k]);
        if (!strncmp(name, pre[k], n) && name[n]) return name + n;
    }
    return NULL;
}

/* Text of one record, written into subject and body; sets *missing when the language file has none.
 * `text` is sized for the longest retail generic line the fill paths can produce. */
static void sms_text(const LangTable *lang, const CdSms *m,
                     char *subject, size_t scap, char *body, size_t bcap, int *missing) {
    char key[64], text[512];
    const char *t;

    subject[0] = 0; body[0] = 0;
    if (missing) *missing = 1;
    if (!m->name[0]) return;                        /* the name offset failed to decode: nothing to key on */

    /* [data] 123 of the 167 retail records have both NAME_SUBJECT and NAME_BODY (SMS-TABLE.md 4). */
    snprintf(key, sizeof key, "%s_SUBJECT", m->name);
    if ((t = sms_key(lang, key))) sms_copy(subject, scap, t);
    snprintf(key, sizeof key, "%s_BODY", m->name);
    if ((t = sms_key(lang, key))) sms_copy(body, bcap, t);
    if (subject[0] || body[0]) { if (missing) *missing = 0; return; }

    /* [data] SMS_CAR_UNLOCK_2..17 carry no text of their own and retail reuses SMS_CAR_UNLOCK_1 for
     * them ("New Inventory in the Car Lot"), so the whole car-lot run reads as one message. */
    if (!strncmp(m->name, "SMS_CAR_UNLOCK_", 15) && strcmp(m->name, "SMS_CAR_UNLOCK_1")) {
        if ((t = sms_key(lang, "SMS_CAR_UNLOCK_1_SUBJECT"))) sms_copy(subject, scap, t);
        if ((t = sms_key(lang, "SMS_CAR_UNLOCK_1_BODY"))) sms_copy(body, bcap, t);
        if (subject[0] || body[0]) { if (missing) *missing = 0; return; }
    }

    /* [data] The 28 magazine / unique blurbs have no text under their own name or any tested suffix.
     * Retail has generic lines with the magazine name as "%s" (SMS_MAG_GENERIC_SUBJ_1/_2 and
     * _BODY_1/_2; SMS_SHOWCASE_GENERIC_* carry the same "%s cover opportunity" text). Which of the
     * pair a record gets is [inferred]: the `flag` byte is the only distinction the record carries,
     * and it splits the blurbs 18/10. The magazine word comes from the record name, so no retail
     * string is invented here - only the pairing is guessed. */
    if (m->kind != CD_SMS_MAG) return;
    {
        const char *sk = m->flag ? "SMS_MAG_GENERIC_SUBJ_1" : "SMS_MAG_GENERIC_SUBJ_2";
        const char *bk = m->flag ? "SMS_MAG_GENERIC_BODY_1" : "SMS_MAG_GENERIC_BODY_2";
        const char *mag = sms_magazine(m->name);
        char word[32];
        const char *show = mag ? strstr(mag, "_SHOW_") : NULL;
        snprintf(word, sizeof word, "%.*s", show ? (int)(show - mag) : (mag ? 31 : 0), mag ? mag : "");
        if (!word[0]) snprintf(word, sizeof word, "%s", "the magazine");   /* [inferred] no name to read */
        if ((t = sms_key(lang, sk)) && *t) {
            char line[FE_SMS_SUBJECT_FILL];
            sms_fill(line, sizeof line, t, word);
            /* The 96-byte subject slot is the contract; the fill buffer is sized to it, so the copy
             * below cannot truncate and gcc can bound it. */
            sms_copy(subject, scap, line);
        }
        if ((t = sms_key(lang, bk)) && *t) {
            sms_fill(text, sizeof text, t, word);
            sms_copy(body, bcap, text);
        }
    }
    if ((subject[0] || body[0]) && missing) *missing = 0;
}

int fe_sms_build(const CareerData *cd, const LangTable *lang, const int *received, int nreceived,
                 FeSmsItem *out, int cap) {
    if (!cd || !out || cap <= 0) return 0;
    if (cap > FE_SMS_MAX) cap = FE_SMS_MAX;
    int n = 0;
    for (int i = 0; i < cd->nsms && n < cap; i++) {
        if (nreceived != FE_SMS_ALL) {
            if (!received || nreceived <= 0) return 0;             /* no receipt list: nothing received */
            int keep = 0;
            for (int k = 0; k < nreceived && !keep; k++) if (received[k] == i) keep = 1;
            if (!keep) continue;
        }
        FeSmsItem *it = &out[n++];
        memset(it, 0, sizeof *it);
        sms_copy(it->name, sizeof it->name, cd->sms[i].name);
        int missing;
        sms_text(lang, &cd->sms[i], it->subject, sizeof it->subject, it->body, sizeof it->body, &missing);
        it->unread = 1;              /* [inferred] a received message is unread until fe_sms_open clears it */
    }
    return n;
}

void fe_sms_open_list(FeSms *s, const FeSmsItem *items, int n) {
    if (!s) return;
    memset(s, 0, sizeof *s);
    if (n < 0 || !items) n = 0;
    if (n > FE_SMS_MAX) n = FE_SMS_MAX;
    int newest = -1;
    for (int i = 0; i < n; i++) {
        s->item[i] = items[i];
        s->item[i].name[sizeof s->item[i].name - 1] = 0;
        s->item[i].subject[sizeof s->item[i].subject - 1] = 0;
        s->item[i].body[sizeof s->item[i].body - 1] = 0;
        if (!s->item[i].subject[0] && !s->item[i].body[0]) s->text_missing++;
        if (s->item[i].unread) { s->unread_total++; newest = i; }
    }
    s->n = n;
    s->opened = -1;
    s->header_scale = 1;
    /* [inferred] Retail opens on the newest message; the table order is the receipt order, so that is
     * the last unread record. With nothing unread the browsed row is the first. */
    s->sel = newest >= 0 ? newest : 0;
}

void fe_sms_select(FeSms *s, int delta) {
    if (!s || !s->n || !delta) return;
    int next = s->sel % s->n + delta;      /* the list wraps at either end, unlike the car lot */
    next %= s->n;
    if (next < 0) next += s->n;
    s->sel = next;
}

int fe_sms_open(FeSms *s) {
    if (!s || !s->n) return -1;
    if (s->item[s->sel].unread) { s->item[s->sel].unread = 0; if (s->unread_total > 0) s->unread_total--; }
    s->opened = s->sel;
    return s->opened;
}

void fe_sms_close(FeSms *s) {
    if (!s) return;
    s->opened = -1;
}

const FeSmsItem *fe_sms_text(const FeSms *s) {
    if (!s || !s->n) return NULL;
    return &s->item[s->opened >= 0 ? s->opened : s->sel];
}

int fe_sms_showing(const FeSms *s) { return s && s->opened >= 0; }
int fe_sms_unread(const FeSms *s) { return s ? s->unread_total : 0; }

/* ---- package binding ---- */

/* [data] Role nodes of UI_SMS_Mailbox.fng by node name hash (the package stores no readable names, so
 * the hash is the only identity; SMS-MAILBOX.md 3 lists the geometry and sample text behind each). */
#define FES_N_HEADER    0x42adb44cu   /* node 98:  x=275 y=-46, sample "Incoming SMS Message" */
#define FES_RETURN      0x001c0cbbu   /* node 109: hint "Return to Gameplay" (key SMS_RETURN) */
#define FES_DELETE      0x56b00632u   /* node 111: hint "Delete Message" (key SMS_DELETE_MESSAGE) */
#define FES_SELECT_CAP  0x001335f0u   /* node 140: hint "Read Message" (key SMS_READ_MESSAGE) */
#define FES_COUNT       0x980822b3u   /* node 113: x=44 y=8 centred, sample "Unread Messages" */
#define FES_ROW_TOP     0x9a3674c8u   /* node 13:  x=-242 y=155, the top subject row */
#define FES_ROW         0x0c6e4198u   /* nodes 14..21 and 58: the nine other subject rows */
#define FES_MAIL_TOP    0xe7afbca7u   /* node 2: Engage_SMS_mail.tga at y=165, the top envelope */
#define FES_MAIL_LO     0x6bde8997u   /* nodes 3..11: envelopes at y=136..-88; the name id steps by */
#define FES_MAIL_HI     0x6bde899fu   /* one per row, so this closed range is exactly the nine */

/* The ten authored rows, ordered top to bottom by authored Y. */
typedef struct FesRows { int subject[FE_SMS_WINDOW]; int mail[FE_SMS_WINDOW]; int n; } FesRows;

static void rows_sort_top_down(GvScreen *s, int *at, int n) {
    for (int a = 0; a < n; a++)
        for (int b = a + 1; b < n; b++)
            if (s->scene.nodes[at[a]].obj->pos[1] < s->scene.nodes[at[b]].obj->pos[1]) {
                int t = at[a]; at[a] = at[b]; at[b] = t;
            }
}

static int fes_rows(const GvScreen *s, FesRows *r) {
    int found[FE_SMS_WINDOW], nf;
    r->n = 0;
    for (int i = 0; i < FE_SMS_WINDOW; i++) { r->subject[i] = -1; r->mail[i] = -1; }
    nf = 0;
    for (int i = 0; i < s->scene.n && nf < FE_SMS_WINDOW; i++) {
        uint32_t g = s->scene.nodes[i].obj->name_hash;
        if (g == FES_ROW_TOP || g == FES_ROW) found[nf++] = i;
    }
    if (nf != FE_SMS_WINDOW) return 0;
    rows_sort_top_down((GvScreen *)s, found, nf);
    for (int i = 0; i < nf; i++) r->subject[i] = found[i];

    nf = 0;
    for (int i = 0; i < s->scene.n && nf < FE_SMS_WINDOW; i++) {
        uint32_t g = s->scene.nodes[i].obj->name_hash;
        if (g == FES_MAIL_TOP || (g >= FES_MAIL_LO && g <= FES_MAIL_HI)) found[nf++] = i;
    }
    if (nf != FE_SMS_WINDOW) return 0;
    rows_sort_top_down((GvScreen *)s, found, nf);
    for (int i = 0; i < nf; i++) r->mail[i] = found[i];
    r->n = FE_SMS_WINDOW;
    return 1;
}

static int fes_has(const GvScreen *s, uint32_t name_hash) {
    for (int i = 0; i < s->scene.n; i++)
        if (s->scene.nodes[i].obj->name_hash == name_hash) return 1;
    return 0;
}

int fe_sms_bind(FeSms *s, GvScreen *scr, const UgAssets *a) {
    if (s) s->header_scale = 1;             /* a fresh scene carries the authored header size */
    if (!scr) return 0;
    if (!gv_screen_load(scr, a, "UI_SMS_Mailbox.fng")) return 0;
    FesRows r;
    if (!fes_rows(scr, &r)) {
        snprintf(scr->status, sizeof scr->status, "SMS mailbox rows missing (need 10 subject rows and 10 envelope icons)");
        return 0;
    }
    static const uint32_t roles[] = { FES_N_HEADER, FES_RETURN, FES_DELETE, FES_SELECT_CAP, FES_COUNT };
    for (unsigned k = 0; k < sizeof roles / sizeof roles[0]; k++)
        if (!fes_has(scr, roles[k])) {
            snprintf(scr->status, sizeof scr->status, "SMS mailbox role node missing (name %08x)", (unsigned)roles[k]);
            return 0;
        }
    return 1;
}

void fe_sms_sync(FeSms *s, GvScreen *scr) {
    if (!s || !scr || !scr->loaded) return;
    const UgAssets *a = scr->assets;
    const LangTable *lang = a && a->have_strings ? &a->strings : NULL;
    const FeSmsItem *shown = fe_sms_text(s);
    int showing = fe_sms_showing(s);

    /* [data] Header keys: SMS_HEADER 0xaaabbd7a "Incoming Mobile Message" over the list, and
     * SMS_MESSAGE_HEADER 0xcc438b1e "Message %d of %d" over the opened one (its unread and stored
     * variants 0xce04c2fc / 0xc9c5976e read "Unread / Stored Messages %d of %d"). */
    if (shown && showing) {
        char fmt[96], line[96];
        sms_copy(fmt, sizeof fmt, ug_strtab_or(lang, 0xcc438b1eu, "Message %d of %d"));
        snprintf(line, sizeof line, "%s", fmt);
        const char *p1 = strstr(line, "%d"), *p2 = p1 ? strstr(p1 + 2, "%d") : NULL;
        if (p1 && p2)
            snprintf(s->header, sizeof s->header, "%.*s%d%s%d", (int)(p1 - line), line,
                     s->sel + 1, p1 + 2, s->n);
        else sms_copy(s->header, sizeof s->header, line);
    } else {
        sms_copy(s->header, sizeof s->header, ug_strtab_or(lang, 0xaaabbd7au, "Incoming Mobile Message"));
    }

    /* [data] SMS_MSG_NUM_UNREAD 0x2ccba545 "%d unread messages" - the count line of the centred title
     * node, whose authored sample is the unkeyed "Unread Messages". */
    {
        char fmt[96];
        sms_copy(fmt, sizeof fmt, ug_strtab_or(lang, 0x2ccba545u, "%d unread messages"));
        sms_fill_n(s->caption, sizeof s->caption, fmt, fe_sms_unread(s));
    }
    snprintf(s->count, sizeof s->count, "%d", fe_sms_unread(s));
    s->sender[0] = 0;
    s->status[0] = 0;
    if (shown && !shown->subject[0] && !shown->body[0])
        snprintf(s->status, sizeof s->status, "no language text for %.39s", shown->name);

    FesRows r;
    if (!fes_rows(scr, &r)) return;
    int first = 0;                     /* [inferred] scroll window start: keeps the browsed row visible */
    if (s->n > FE_SMS_WINDOW) {
        first = s->sel - FE_SMS_WINDOW / 2;
        if (first < 0) first = 0;
        if (first + FE_SMS_WINDOW > s->n) first = s->n - FE_SMS_WINDOW;
    }

    for (int i = 0; i < scr->scene.n; i++) {
        UgNode *n = &scr->scene.nodes[i];
        uint32_t g = n->obj->name_hash;
        if (g == FES_N_HEADER) {
            n->text_override = s->header;
            /* Platform adaptation: scale a long caption to the window width rather than clip it. */
            float tw = 0, th = 0, k = 1;
            const UgFace *was = ug_text_face();
            if (n->face) ug_text_set_face(n->face);
            if (ug_text_face()) ug_text_measure(s->header, 1, 0, 0, UG_GLYPHS_MOUSE, &tw, &th);
            ug_text_set_face(was);
            if (tw > FES_HEADER_W) k = FES_HEADER_W / tw;
            if (k != s->header_scale) {
                float sz[3] = { n->obj->size[0] * k, n->obj->size[1] * k, n->obj->size[2] };
                ug_scene_app_write(&scr->scene, i, UG_P_SIZE, 3, sz);
                s->header_scale = k;
            }
        }
        if (g == FES_COUNT) n->text_override = s->caption;
        /* [inferred] Deleting is not implemented, so its hint is hidden rather than offering an action
         * that does nothing; the read/select caption stays because Accept does open the message. */
        if (g == FES_DELETE) n->app_hidden = 1;
        for (int q = 0; q < r.n; q++) {
            int at = first + q;
            if (i == r.subject[q]) {
                const char *text = NULL;
                if (at < s->n && !showing)
                    text = s->item[at].subject[0] ? s->item[at].subject : s->item[at].name;
                n->text_override = text;
                n->app_hidden = text ? 0 : 1;
            }
            if (i == r.mail[q]) {
                /* [data] The row envelope (Engage_SMS_mail.tga) is the unread marker; read rows and the
                 * opened-message view hide it. */
                n->app_hidden = (at < s->n && !showing && s->item[at].unread) ? 0 : 1;
            }
        }
    }
    ug_scene_relayout(&scr->scene);
}
