/* fe_sms.h -- career SMS mailbox screen (GLOBAL/InGameCommon.lzc -> UI_SMS_Mailbox.fng), task AA.

 * Two layers, like fe_lot.c: the pure state machine (fe_sms_build / fe_sms_select / fe_sms_open /
 * fe_sms_text: which messages the player holds, which one is browsed, which one was opened) and the
 * binding of that state into the authored package (fe_sms_bind / fe_sms_sync). No host wiring lives
 * here - fe_menu.c/main.c own that. The messages themselves are the 0x34A17 career table
 * (career_data.h CdSms, docs/parity/SMS-TABLE.md); their texts are language keys built from the
 * record names, so this module holds no retail text. Field meanings and every [data] / [inferred]
 * tag are in docs/parity/SMS-MAILBOX.md. Pure C99: no SDL, no GL. */
#ifndef OPENUG2_FE_SMS_H
#define OPENUG2_FE_SMS_H

#include "career_data.h"
#include "ugui/ugui_core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The code base has no LangTable type: the language lookup is UgStrTab (ugui/ugui_core.h,
 * ug_strtab_get / ug_strtab_or), so the brief's `LangTable` names that type.
 * docs/parity/SMS-MAILBOX.md 6 records the change. */
#define LangTable UgStrTab

#define FE_SMS_MAX CD_MAX_SMS                /* every possible data record; the authored view still has ten rows */
#define FE_SMS_WINDOW 10             /* [data] UI_SMS_Mailbox.fng authors 10 subject rows (node 0x0c6e4198
                                      * plus 0x9a3674c8 and the 0x949f4946..0x949f494e row groups) */
#define FE_SMS_SUBJECT_FILL 96       /* room for one filled subject line: the size of FeSmsItem.subject,
                                      * so a filled generic line never truncates twice */

typedef struct FeSmsItem { char name[40]; char subject[96]; char body[512]; int unread; } FeSmsItem;

/* The mailbox: the received messages, the browsed row and the opened one. */
typedef struct FeSms {
    FeSmsItem item[FE_SMS_MAX];
    int n, sel;                      /* browsed row (0 when n == 0) */
    int opened;                      /* index of the opened message, or -1 while the list shows */
    int unread_total;                /* items still unread */
    int text_missing;                /* items that came out without any text */
    char status[160];                /* why the shown message has no text ("" when it has) */
    float header_scale;              /* view: scale the header carries (1 = authored size) */
    char header[96], count[64], caption[96], sender[64];
} FeSms;

/* Builds the list from the SMS table. `received` selects the records the player holds: nreceived
 * indices into cd->sms[] (out of range or negative entries are ignored), or, when nreceived is
 * FE_SMS_ALL (-1), every record of the table. Copies at most `cap` items (FE_SMS_MAX at most) and
 * returns how many were written, in receipt order (table order for FE_SMS_ALL). Texts come from the language file, keyed
 * by the record name (NAME_SUBJECT / NAME_BODY, plus the retail generic fall-backs); a record with no
 * text keeps its name and an empty subject and body, so the caller can always show a row. */
#define FE_SMS_ALL (-1)
int fe_sms_build(const CareerData *cd, const LangTable *lang, const int *received, int nreceived,
                 FeSmsItem *out, int cap);
/* (Re)opens the mailbox over `n` built items: the browsed row is the newest unread message (the last
 * unread in receipt order), else the first row. */
void fe_sms_open_list(FeSms *s, const FeSmsItem *items, int n);
/* List navigation with wrap-around: `delta` rows either way (0 and |delta| >= n leave the row alone). */
void fe_sms_select(FeSms *s, int delta);
/* Opens the browsed message: returns its item index, marks it read and shows it (fe_sms_text).
 * -1 and no change when the mailbox is empty. */
int fe_sms_open(FeSms *s);
/* Closes the opened message and returns to the list; no change when the list view is already shown. */
void fe_sms_close(FeSms *s);
/* The message to display: the opened item, or the browsed one while the list shows. NULL when empty. */
const FeSmsItem *fe_sms_text(const FeSms *s);
/* 1 while an opened message is on screen. */
int fe_sms_showing(const FeSms *s);
/* Number of items still unread. */
int fe_sms_unread(const FeSms *s);

/* ---- package binding (gui_view.h) ---- */
struct GvScreen;
struct UgAssets;
/* Loads UI_SMS_Mailbox.fng (GLOBAL/InGameCommon.lzc: the host needs ug_assets_open_ingame) and checks
 * the nodes the sync below writes. The package stores no readable node names (the FNG reader keeps
 * their hashes), so the checked identities are the node name hashes themselves, and they live here
 * rather than in fe_binding.h, which this task does not own (SMS-MAILBOX.md 4).
 * 0 + scr->status when the package, a row or a role node is missing. */
int fe_sms_bind(FeSms *s, struct GvScreen *scr, const struct UgAssets *a);
/* Writes the mailbox into the scene: the header (SMS_HEADER over the list, SMS_MESSAGE_HEADER over an
 * opened message), the unread count, the ten authored subject rows with their unread envelopes, and
 * the hint bar. Hides what is not implemented (the delete hint, read rows' envelopes). */
void fe_sms_sync(FeSms *s, struct GvScreen *scr);

#ifdef __cplusplus
}
#endif
#endif
