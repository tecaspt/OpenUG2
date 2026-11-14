/* fe_entries.c -- see fe_entries.h. */
#include "fe_entries.h"

#include "race.h"
#include "ugui/ugui_core.h"

typedef struct Proto { int id; const char *icon, *label_name, *label; } Proto;

static const Proto k_main[] = {
    { FE_MAIN_CAREER,     "MAIN_ICON_CAREER",        "UI_MAIN_CAREER",     "Career" },
    { FE_MAIN_QUICK_RACE, "MAIN_ICON_QUICK_RACE",    "UI_MAIN_QUICK_RACE", "Quick Race" },
    { FE_MAIN_GO_ONLINE,  "MAIN_ICON_GO_ONLINE",     "UI_MAIN_ONLINE",     "Online" },
    { FE_MAIN_LAN,        "MAIN_ICON_LAN",           "UI_MAIN_ONLINE_LAN", "Online LAN" },
    { FE_MAIN_CUSTOMIZE,  "MAIN_ICON_CUSTOMIZE_CAR", "UI_MAIN_CUSTOMIZE",  "Customize" },
    /* [data] the language table has no key for this caption: the English fallback is shown */
    { FE_MAIN_PROFILE,    "MAIN_ICON_PROFILE_MANAG", "UI_MAIN_PROFILE",    "Profile Management" },
    { FE_MAIN_OPTIONS,    "MAIN_ICON_OPTIONS",       "UI_MAIN_OPTIONS",    "Options" },
};
static const Proto k_mode[] = {
    { FE_MODE_CIRCUIT,  "TRACK_ICON_CIRCUIT",      "QR_CIRCUIT",      "Circuit" },
    { FE_MODE_SPRINT,   "TRACK_ICON_SPRINT",       "QR_SPRINT",       "Sprint" },
    { FE_MODE_DRAG,     "TRACK_ICON_DRAG",         "QR_DRAG",         "Drag" },
    { FE_MODE_DRIFT,    "TRACK_ICON_DRIFT",        "QR_DRIFT",        "Drift" },
    { FE_MODE_STREET_X, "TRACK_ICON_SHORT_TRACK",  "QR_STREET_CROSS", "Street X" },
    { FE_MODE_URL,      "TRACK_ICON_U_RACING_LEA", "QR_URL",          "URL Race" },
    { FE_MODE_FREE_RUN, "TRACK_ICON_FREE_RUN",     "QR_FREE_RUN",     "Free Run" },
};

static void fill(FeEntry *e, const Proto *p, int disabled) {
    e->id = p->id;
    e->icon_key = ug_hash_upper(p->icon);
    e->label_key = ug_hash_upper(p->label_name);
    e->icon_name = p->icon;
    e->label = p->label;
    e->disabled = disabled;
}

int fe_main_entries(FeEntry out[GC_MAX], unsigned enabled_mask) {
    int n = (int)(sizeof k_main / sizeof k_main[0]);
    if (!out) return 0;
    for (int i = 0; i < n; i++) {
        int on = (enabled_mask >> k_main[i].id & 1u) && k_main[i].id != FE_MAIN_GO_ONLINE && k_main[i].id != FE_MAIN_LAN;
        fill(&out[i], &k_main[i], !on);
    }
    return n;
}

int fe_mode_entries(FeEntry out[GC_MAX]) {
    int n = (int)(sizeof k_mode / sizeof k_mode[0]);
    if (!out) return 0;
    for (int i = 0; i < n; i++) fill(&out[i], &k_mode[i], 0);
    return n;
}

int fe_mode_race_kind(int mode) {
    switch (mode) {
    case FE_MODE_CIRCUIT: return N2_RACE_CIRCUIT;
    case FE_MODE_SPRINT: return N2_RACE_SPRINT;
    case FE_MODE_DRAG: return N2_RACE_DRAG;
    case FE_MODE_DRIFT: return N2_RACE_DRIFT;
    case FE_MODE_STREET_X: return N2_RACE_STREETX;
    case FE_MODE_URL: return N2_RACE_URL;
    default: return -1;
    }
}
