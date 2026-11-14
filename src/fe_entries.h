/* fe_entries.h -- entry catalogues of the retail front-end carousels (UI_Main.fng, UI_QRModeSelect.fng).
 *
 * Pure data and rules, no GL/SDL. Icon and label keys are data lookup keys (ug_hash_upper of the stored
 * texture / language names) into the user's install. Main-menu order: tools/ugui_demo_orig.c (SPEC 6.4 /
 * 15.9, PC build); the Quick Race mode order is [inferred] until observed. */
#ifndef OPENUG2_FE_ENTRIES_H
#define OPENUG2_FE_ENTRIES_H

#include <stdint.h>

#include "gui_carousel.h"          /* GC_MAX */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FeEntry { int id; uint32_t icon_key, label_key; const char *icon_name, *label; int disabled; } FeEntry;

enum { FE_MAIN_CAREER, FE_MAIN_QUICK_RACE, FE_MAIN_GO_ONLINE, FE_MAIN_LAN, FE_MAIN_CUSTOMIZE, FE_MAIN_PROFILE, FE_MAIN_OPTIONS };
enum { FE_MODE_CIRCUIT, FE_MODE_SPRINT, FE_MODE_DRAG, FE_MODE_DRIFT, FE_MODE_STREET_X, FE_MODE_URL, FE_MODE_FREE_RUN };

/* Main menu: 7 entries in list order. Bit k of `enabled_mask` enables entry FE_MAIN_k; Go Online and
 * LAN are always disabled. Returns the entry count. */
int fe_main_entries(FeEntry out[GC_MAX], unsigned enabled_mask);
/* Quick Race mode select: 7 entries, all enabled. */
int fe_mode_entries(FeEntry out[GC_MAX]);
/* N2_RACE_* (race.h) of a FE_MODE_*; -1 for Free Run (and for an unknown mode). */
int fe_mode_race_kind(int mode);

#ifdef __cplusplus
}
#endif
#endif
