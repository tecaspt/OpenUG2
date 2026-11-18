/* Pause menu adapter: the retail authored packages (UI_pause.fng, UI_PauseOptionsMain.fng,
 * UI_PauseOptions.fng, UI_PC_Customize_Options.fng and the two-button dialogs), evaluated at run time
 * over the host's paused driving scene. SPEC_GUI_ART 1-8.
 *
 * The state machines (pause_screen.c, options_menu.c, gui_dialog.c, gui_carousel.c) are GL-free and
 * own all behaviour; gui_view.c writes their state into the authored objects. This file maps devices
 * to inputs, advances time, switches packages at exit-complete and renders. If a package, art or font
 * is missing the affected screen is not drawn (no native fallback): input and results keep working. */
#include "pause_menu.h"
#include "pause_menu_layout.h"
#include "pause_menu_binding.h"
#include "gui_timing.h"
#include "gui_view.h"
#include "pause_screen.h"
#include "controls_sdl.h"
#include "ugui/ugui_gl.h"
#include "ugui/ugui_assets.h"
#include "ugui/ugui_ui.h"
#include "ugui/ugui_scene.h"
#include "ugui/ugui_tuning.h"
#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <stdio.h>

typedef enum { PM_NONE = 0, PM_PAUSE, PM_HUB, PM_LIST, PM_CONTROLS } PmView;
typedef enum { PMK_NONE = 0, PMK_LEFT, PMK_RIGHT, PMK_UP, PMK_DOWN, PMK_ACCEPT, PMK_BACK, PMK_START, PMK_RESET } PmKey;

#define PAUSE_HEADING_KEY 0x6C839FBEu      /* "Pause" */
#define PAUSE_ROOT_NAME 0x8D7E0C53u        /* presentation group of UI_pause.fng */
#define PAUSE_HINT_GUID 0x000E24A1u        /* hint group, never shown */

struct PauseMenu {
    PauseScreen ps;
    PauseContext ctx;            /* host-described situation, applied at the next open */
    int ctx_set;
    int world_map_enabled;   /* host opt-in; career free roam only */
    int open;
    GuiDialog confirm;           /* M6: a host confirmation shown alone, without the pause screen */
    int confirm_only;
    GdResult confirm_result;
    UgAssets assets;
    UgSkin skin;
    UgGL *gl;
    UgCanvas canvas;
    UgCtx ui;
    int assets_open, compatible;
    float ui_ms;
    /* authored views: one screen at a time plus an optional dialog over it */
    GvScreen scr;
    PmView view;
    OmScreen view_screen;
    GvCarousel car_v;
    GvList list_v;
    GvControls ctl_v;            /* UI_Options_PC_Controller.fng */
    int visual;                  /* the current screen was built and can be drawn */
    int pause_root;
    unsigned seen_ps_exit, seen_ps_dialog, seen_hub_exit, seen_ctl_dialog;
    GvDialog dlg_v;
    int dlg_loaded, dlg_failed;
    unsigned icon_warned;        /* entries whose missing icon was reported */
    /* PC Back / Continue pills of the pause screen */
    int pill_hover, pill_initialized, press_pill;
    UgAnim pill_pulse, pill_glow;
    OptionsMenu options;         /* hub, Display and Customize, SPEC_GUI */
    StGraphics *opt_gfx; const StGfxCaps *opt_caps; StRegStore *opt_store;
    int options_enabled;         /* the host lent the settings: the Options entry exists */
    int *mouse_look;
    CxInput *cx;                 /* bindings and joysticks, lent by the host (Controls screen) */
    int controls_checked;        /* the Controls resources were verified (once, at creation) */
    int capture_actions, capture_last_action;
    char status[256];
    char dlg_status[220];
    char caption_status[160];
};

/* ---- views ------------------------------------------------------------------------------------ */

static void pause_drop_dialog(PauseMenu *p) {
    if (p->dlg_loaded) gv_dialog_close(&p->dlg_v);
    p->dlg_loaded = 0;
}
static void pause_drop_view(PauseMenu *p) {
    pause_drop_dialog(p);
    if (p->scr.loaded) gv_screen_free(&p->scr);
    p->view = PM_NONE;
    p->visual = 0;
}

/* The reviewed identities of UI_pause.fng (BINDING): every role and pool object must resolve uniquely. */
static int pause_verify_identities(const UgScene *s) {
    for (int k = 0; k < PAUSE_ROLE_COUNT; k++) if (pause_identity_find(s, &pause_identities[k]) < 0) return 0;
    for (int k = 0; k < PAUSE_POOL_COUNT; k++) if (pause_identity_find(s, &pause_pool_identities[k]) < 0) return 0;
    return 1;
}

static const char *pause_package_for(const PauseMenu *p, PmView v, OmScreen scr) {
    if (v == PM_PAUSE) return "UI_pause.fng";
    return om_package_name(p->options.cfg.ctx, scr);
}

static void pause_switch_view(PauseMenu *p, PmView want, OmScreen scr) {
    pause_drop_view(p);
    p->view = want;
    p->view_screen = scr;
    if (!p->assets_open || !p->compatible) return;
    const char *name = pause_package_for(p, want, scr);
    if (!name) return;
    int ok = gv_screen_load(&p->scr, &p->assets, name);
    if (ok) {
        if (want == PM_PAUSE) {
            ok = gv_carousel_bind(&p->car_v, &p->scr, &p->ps.car);
            if (ok && !pause_verify_identities(&p->scr.scene)) {
                snprintf(p->scr.status, sizeof p->scr.status, "%s: reviewed object identities not uniquely resolved", name);
                ok = 0;
            }
            if (ok) {
                int hint = ug_scene_find_guid(&p->scr.scene, PAUSE_HINT_GUID);
                if (hint >= 0) p->scr.scene.nodes[hint].app_hidden = 1;
                p->pause_root = ug_scene_find_name(&p->scr.scene, PAUSE_ROOT_NAME);
                p->seen_ps_exit = p->ps.exit_serial;
                p->seen_ps_dialog = p->ps.dialog_serial;
            }
        } else if (want == PM_HUB) {
            ok = gv_carousel_bind(&p->car_v, &p->scr, &p->options.hub);
            p->seen_hub_exit = p->options.exit_serial;
        } else if (want == PM_CONTROLS) {
            ok = gv_controls_bind(&p->ctl_v, &p->scr, &p->options.cm);
            p->seen_ctl_dialog = p->options.dialog_serial;
        } else {
            ok = gv_list_bind(&p->list_v, &p->scr, &p->options);
        }
        if (!ok) { snprintf(p->status, sizeof p->status, "%s", p->scr.status); gv_screen_free(&p->scr); }
    } else snprintf(p->status, sizeof p->status, "%s", p->scr.status);
    p->visual = ok;
    if (!ok) fprintf(stderr, "pause: %s; screen not drawn (no native fallback)\n", p->status);
}

static const GuiDialog *pause_current_dialog(const PauseMenu *p) {
    if (p->confirm_only) return p->confirm.open ? &p->confirm : NULL;
    if (om_is_open(&p->options)) return p->options.dialog.open ? &p->options.dialog : NULL;
    return p->ps.dialog.open ? &p->ps.dialog : NULL;
}

static void pause_sync_dialog(PauseMenu *p, float ms) {
    const GuiDialog *d = pause_current_dialog(p);
    if (!d) { pause_drop_dialog(p); p->dlg_failed = 0; return; }
    if (!p->dlg_loaded && !p->dlg_failed) {
        if (!p->assets_open || !p->compatible || !gv_dialog_open(&p->dlg_v, &p->assets, d)) {
            p->dlg_failed = 1;
            snprintf(p->dlg_status, sizeof p->dlg_status, "%s", p->assets_open ? p->dlg_v.scr.status : "dialog unavailable: pause resources unavailable");
            fprintf(stderr, "pause: %s; input handling continues without the picture\n", p->dlg_status);
        } else {
            p->dlg_loaded = 1;
            p->dlg_status[0] = 0;
        }
    }
    if (p->dlg_loaded) gv_dialog_sync(&p->dlg_v, d, ms);
}

static void pause_entries_to_view(PauseMenu *p, GvEntry out[PE_MAX]) {
    for (int i = 0; i < p->ps.n; i++) {
        out[i].tex = gv_texture(&p->assets, p->ps.entries[i].icon_key);
        out[i].label = gv_text(&p->assets, p->ps.entries[i].label_key, p->ps.entries[i].label);
        if (!out[i].tex && p->assets_open && !(p->icon_warned & (1u << i))) {          /* omitted, never replaced */
            p->icon_warned |= 1u << i;
            fprintf(stderr, "pause: icon texture 0x%08X of \"%s\" unavailable; the entry is not drawn\n", p->ps.entries[i].icon_key, p->ps.entries[i].label);
        }
    }
}

/* Applies the state machines to the authored objects and advances the scenes. */
static void pause_views_update(PauseMenu *p, float ms) {
    PmView want = !om_is_open(&p->options) ? PM_PAUSE : om_screen(&p->options) == OM_SCREEN_HUB ? PM_HUB :
                  om_screen(&p->options) == OM_SCREEN_CONTROLS ? PM_CONTROLS : PM_LIST;
    OmScreen scr = want == PM_PAUSE ? OM_SCREEN_CLOSED : om_screen(&p->options);
    if (want != p->view || scr != p->view_screen) pause_switch_view(p, want, scr);
    if (p->visual && p->scr.loaded) {
        gv_screen_update(&p->scr, ms);
        if (p->view == PM_PAUSE) {
            if (p->ps.dialog_serial != p->seen_ps_dialog) {          /* dialog over the screen: DIM / UNDIM (1.4) */
                p->seen_ps_dialog = p->ps.dialog_serial;
                gv_screen_dim(&p->scr, p->ps.dialog.open);
            }
            if (p->ps.exit_serial != p->seen_ps_exit) {              /* the authored exit script (320 ms) */
                p->seen_ps_exit = p->ps.exit_serial;
                gv_screen_exit(&p->scr);
            }
            GvEntry e[PE_MAX];
            pause_entries_to_view(p, e);
            gv_carousel_apply(&p->car_v, &p->ps.car, e, p->ps.n, gv_text(&p->assets, PAUSE_HEADING_KEY, "Pause"),
                              p->ps.move_serial, p->ps.move_dir);
        } else if (p->view == PM_HUB) {
            if (p->options.exit_serial != p->seen_hub_exit) {
                p->seen_hub_exit = p->options.exit_serial;
                gv_screen_exit(&p->scr);
            }
            GvEntry e[OM_HUB_MAX];
            int nh = om_hub_count(&p->options);
            for (int i = 0; i < nh; i++) {
                const OmHubEntry *h = om_hub_entry_of(&p->options, i);
                e[i].tex = gv_texture(&p->assets, h->icon_key);
                e[i].label = gv_text(&p->assets, h->label_key, h->label);
            }
            uint32_t key;
            const char *hd = om_header(p->options.cfg.ctx, OM_SCREEN_HUB, 0, &key);
            gv_carousel_apply(&p->car_v, &p->options.hub, e, nh, gv_text(&p->assets, key, hd),
                              p->options.blink_serial, p->options.blink_dir);
        } else if (p->view == PM_CONTROLS) {
            if (p->options.dialog_serial != p->seen_ctl_dialog) {      /* the reset dialog dims the screen (1.4) */
                p->seen_ctl_dialog = p->options.dialog_serial;
                gv_screen_dim(&p->scr, om_dialog_is_open(&p->options));
            }
            gv_controls_sync(&p->ctl_v, &p->options.cm, ms);
        } else {
            gv_list_sync(&p->list_v, &p->options, ms);
        }
    }
    pause_sync_dialog(p, ms);
}

/* ---- creation --------------------------------------------------------------------------------- */

PauseMenu *pause_menu_create(const char *dataroot) {
    PauseMenu *p = calloc(1, sizeof *p);
    if (!p) return NULL;
    ug_cv_init(&p->canvas);
    ug_ctx_init(&p->ui);
    p->assets.main_face = -1;
    p->pill_hover = -1;
    p->assets_open = ug_assets_open_ingame(&p->assets, dataroot, NULL, p->status, sizeof p->status);
    if (p->assets_open) {
        ug_skin_resolve(&p->skin, &p->assets);
        const UgFace *face = ug_assets_main_face(&p->assets);
        p->compatible = face && face->tex > 0 && p->skin.pill && p->skin.pill_glow;
        if (!p->compatible)
            snprintf(p->status, sizeof p->status, "required genuine PC pill/glow art or font unavailable");
        else {
            p->gl = ug_gl_create();
            if (p->gl) ug_gl_set_texlib(p->gl, &p->assets.tex);
            if (p->assets.strings.n <= 0)
                snprintf(p->caption_status, sizeof p->caption_status, "language table unavailable: English captions are used");
        }
    }
    if (!p->compatible || !p->gl)
        fprintf(stderr, "pause: compatibility resources unavailable; authored presentation omitted (%s)\n", p->status);
    return p;
}

void pause_menu_destroy(PauseMenu *p) {
    if (!p) return;
    pause_drop_view(p);
    ug_gl_destroy(p->gl);
    ug_cv_free(&p->canvas);
    if (p->pill_initialized) { ug_anim_free(&p->pill_pulse); ug_anim_free(&p->pill_glow); }
    ug_assets_close(&p->assets);
    free(p);
}

void pause_menu_set_context(PauseMenu *p, const PauseContext *c) {
    if (!p) return;
    p->ctx_set = c != NULL;
    if (c) p->ctx = *c;
}

void pause_menu_set_world_map_enabled(PauseMenu *p, int enabled) {
    if (p) p->world_map_enabled = enabled != 0;
}

static void pause_start_pause_screen(PauseMenu *p) {
    PauseContext c;
    if (p->ctx_set) c = p->ctx; else pe_context_default(&c);
    c.options_ok = c.options_ok && p->options_enabled;       /* the Options entry needs the settings the host lends */
    if (p->world_map_enabled && c.mode == PE_MODE_CAREER && c.race == PE_RACE_NONE) {
        PauseEntry entries[PE_MAX], candidates[PE_MAX];
        int n = pe_build(&c, entries);
        PauseContext map_context = c;
        map_context.race = 1;
        map_context.from_map = 1;
        int count = pe_build(&map_context, candidates), already=0;
        for (int i=0;i<n;i++)if(entries[i].id==PE_WORLD_MAP)already=1;
        for (int i = 0; i < count && n < PE_MAX && !already; i++) if (candidates[i].id == PE_WORLD_MAP) {
            int at = n;
            for (int k = 0; k < n; k++) if (entries[k].id == PE_QUIT_MAIN) { at = k; break; }
            memmove(entries + at + 1, entries + at, (size_t)(n - at) * sizeof entries[0]);
            entries[at] = candidates[i];
            n++;
            break;
        }
        /* [inferred] Reuse the retail race-return row and its confirmation while exploring. */
        ps_open_entries(&p->ps, &c, entries, n);
    } else ps_open(&p->ps, &c);
}

void pause_menu_open(PauseMenu *p) {
    if (!p) return;
    om_close(&p->options);
    om_reset_session(&p->options);
    pause_start_pause_screen(p);
    p->open = 1;
    p->ui_ms = 0;
    p->press_pill = 0;
    p->dlg_failed = 0;
    if (p->pill_initialized) { ug_anim_free(&p->pill_pulse); ug_anim_free(&p->pill_glow); }
    ug_anim_init(&p->pill_pulse, UG_WHITE, 0, 0);
    ug_anim_init(&p->pill_glow, UG_WHITE, 0, 0);
    p->pill_initialized = 1;
    p->pill_hover = -1;
    p->status[0] = 0;
    p->icon_warned = 0;
    pause_drop_view(p);
    pause_views_update(p, 0);                                /* builds UI_pause.fng */
}
int pause_menu_is_open(const PauseMenu *p) { return p && p->open; }
void pause_menu_set_options(PauseMenu *p, StGraphics *gfx, const StGfxCaps *caps, StRegStore *store) {
    if (!p) return;
    p->opt_gfx = gfx; p->opt_caps = caps; p->opt_store = store;
    p->options_enabled = gfx && caps;
}
void pause_menu_set_mouse_look(PauseMenu *p,int *enabled) { if(p)p->mouse_look=enabled; }
/* Every original resource the Controls screen needs, checked once (CONTROLS_SPEC R1-R3): the package and its
 * eight textures, the hub icon, the dialog package and every language text it shows. Nothing is substituted: if
 * one is missing the hub simply has no Controls entry. */
static int controls_resources_ok(PauseMenu *p) {
    static const uint32_t texts[] = {
        0x04DC6DB5u, 0x5CF08EB7u, 0xF91FBA4Du, 0x14D4BD3Bu, 0x1B15F53Cu, 0x37E740E0u, 0x8BAB66EFu, 0x23CF49AAu, 0xFED0D3DFu,
        0x2BBCCA6Bu, 0x00000639u, 0x541389E5u,
        0x153E7337u, 0xBD7BA1AAu, 0x409607E2u, 0x82EBCC98u, 0x3EB318CEu, 0xB7D33921u, 0xB3D1EA35u, 0xDF5AEF26u, 0x419CA63Au,
        0x35B9F571u, 0x4A1341B3u, 0x1E7E3391u, 0x58BAEBBAu, 0x735B2E6Cu, 0x6FC04E6Fu, 0xA081F0CCu, 0x64730AACu, 0x6F99F62Au
    };
    static const uint32_t textures[] = {
        0xA04A7B26u,                                           /* OPTIONS_ICON_CONTROLS */
        0xA67C3FE2u, 0xC8C5F8A6u, 0x367B3E04u, 0x9C7C3C73u, 0xDA10B4A1u, 0x014245E2u, 0xD253B8F3u, 0xAD749E02u
    };
    if (!p->assets_open || !p->compatible || !p->gl || !p->assets.have_strings) return 0;
    for (size_t i = 0; i < sizeof texts / sizeof texts[0]; i++)
        if (!ug_strtab_get(&p->assets.strings, texts[i])) return 0;
    for (size_t i = 0; i < sizeof textures / sizeof textures[0]; i++)
        if (!gv_texture(&p->assets, textures[i])) return 0;
    if (!ug_assets_package(&p->assets, om_dialog_package_name())) return 0;
    GvScreen scr;
    GvControls cv;
    ControlsMenu cm;
    CbBindings *b = calloc(1, sizeof *b);
    if (!b) return 0;
    cb_bindings_defaults(b);
    memset(&cm, 0, sizeof cm);
    cm_open(&cm, b, NULL);
    int ok = gv_screen_load(&scr, &p->assets, om_package_name(OM_CTX_INGAME, OM_SCREEN_CONTROLS)) &&
             gv_controls_bind(&cv, &scr, &cm);
    if (!ok) fprintf(stderr, "pause: Controls screen unavailable (%s)\n", scr.status);
    gv_screen_free(&scr);
    free(b);
    return ok;
}

void pause_menu_set_controls(PauseMenu *p, CxInput *cx) {
    if (!p) return;
    p->cx = cx;
    CmPlatform plat;
    memset(&plat, 0, sizeof plat);
    if (cx) cx_platform(cx, &plat);
    om_set_controls(&p->options, cx ? &cx->b : NULL, &plat, cx && controls_resources_ok(p));
}
int pause_menu_options_open(const PauseMenu *p) { return p && om_is_open(&p->options); }
OptionsMenu *pause_menu_options(PauseMenu *p) { return p ? &p->options : NULL; }
int pause_menu_visual_available(const PauseMenu *p) { return p && p->open && p->visual && p->compatible && p->gl; }
const char *pause_menu_status(const PauseMenu *p) { return p ? p->status : "pause menu allocation unavailable"; }
int pause_menu_dialog_open(const PauseMenu *p) { return p && pause_current_dialog(p) != NULL; }
int pause_menu_dialog_visual_available(const PauseMenu *p) { return p && p->dlg_loaded && p->dlg_v.scr.draw_ok && pause_current_dialog(p); }
const char *pause_menu_dialog_status(const PauseMenu *p) { return p ? p->dlg_status : "pause menu allocation unavailable"; }
const char *pause_menu_caption_status(const PauseMenu *p) { return p ? p->caption_status : "pause menu allocation unavailable"; }
const char *pause_menu_caption(const PauseMenu *p) {
    if (!p) return "";
    if (om_is_open(&p->options)) {
        if (om_screen(&p->options) == OM_SCREEN_HUB) {
            const OmHubEntry *h = om_hub_entry_of(&p->options, om_hub_focus(&p->options));
            return h && h->label ? h->label : "";
        }
        return "";
    }
    return ps_caption(&p->ps);
}
const PauseScreen *pause_menu_screen(const PauseMenu *p) { return p ? &p->ps : NULL; }

/* ---- input ------------------------------------------------------------------------------------- */

static PsInput ps_of(PmKey k) {
    switch (k) {
    case PMK_LEFT: return PS_IN_LEFT;
    case PMK_RIGHT: return PS_IN_RIGHT;
    case PMK_UP: return PS_IN_UP;                              /* dialog row navigation (TASK-AL) */
    case PMK_DOWN: return PS_IN_DOWN;
    case PMK_ACCEPT: return PS_IN_ACCEPT;
    case PMK_BACK: return PS_IN_BACK;
    case PMK_START: return PS_IN_START;
    default: return PS_IN_NONE;                              /* Up/Down do nothing on the carousel */
    }
}
static OmInput om_of(PmKey k) {
    switch (k) {
    case PMK_LEFT: return OM_IN_LEFT;
    case PMK_RIGHT: return OM_IN_RIGHT;
    case PMK_UP: return OM_IN_UP;
    case PMK_DOWN: return OM_IN_DOWN;
    case PMK_ACCEPT: return OM_IN_ACCEPT;
    case PMK_BACK: return OM_IN_BACK;
    case PMK_START: return OM_IN_START;
    case PMK_RESET: return OM_IN_RESET;
    default: return OM_IN_NONE;
    }
}
static void pause_pill_clear(PauseMenu *p) {
    p->pill_hover = -1;
    if (p->pill_initialized) { ug_anim_stop(&p->pill_pulse); ug_anim_stop(&p->pill_glow); }
}
static void pause_key(PauseMenu *p, PmKey k) {
    if (!p->open || k == PMK_NONE) return;
    if (p->confirm_only) {
        GdInput in = k == PMK_LEFT ? GD_IN_LEFT : k == PMK_RIGHT ? GD_IN_RIGHT : k == PMK_ACCEPT ? GD_IN_ACCEPT :
                     k == PMK_BACK ? GD_IN_BACK : GD_IN_NONE;
        if (in != GD_IN_NONE) gd_input(&p->confirm, in);
        return;
    }
    if (om_is_open(&p->options)) { om_input(&p->options, om_of(k)); return; }
    PsInput in = ps_of(k);
    if (in != PS_IN_NONE) ps_input(&p->ps, in);
}

/* Pointer input in top-left virtual units (640x480). The PC Back/Continue pills act on press; the
 * carousel arrows step on press and the selection slot activates on release only when the press began
 * on it (SPEC_GUI_ART 4.6 style). Option screens take a complete click on press. */
static int pause_pills_visible(const PauseMenu *p) {
    return p->view == PM_PAUSE && !om_is_open(&p->options) && !p->ps.dialog.open;
}
static void pause_pointer_press(PauseMenu *p, float x, float y) {
    if (!p->open) return;
    p->press_pill = 0;
    if (x < 0 || x >= 640 || y < 0 || y >= 480) return;
    if (p->confirm_only) { gd_pointer_click(&p->confirm, x - 320, y - 240); return; }
    if (om_is_open(&p->options)) { om_pointer_click(&p->options, x - 320, y - 240); return; }
    if (p->ps.exiting) return;
    if (!p->ps.dialog.open) {
        if (ug_rect_hit(pause_menu_bounds(PAUSE_BACK), x, y)) { ps_input(&p->ps, PS_IN_BACK); p->press_pill = 1; return; }
        if (ug_rect_hit(pause_menu_bounds(PAUSE_CONTINUE), x, y)) { ps_input(&p->ps, PS_IN_ACCEPT); p->press_pill = 1; return; }
    }
    ps_pointer_press(&p->ps, x - 320, y - 240);
}
static void pause_pointer_release(PauseMenu *p, float x, float y) {
    if (!p->open) return;
    int pill = p->press_pill;
    p->press_pill = 0;
    if (om_is_open(&p->options) || pill || p->confirm_only) return;
    ps_pointer_release(&p->ps, x - 320, y - 240);
}
/* Complete primary click at a window point (press and release together). */
void pause_menu_pointer(PauseMenu *p, float px, float py, int w, int h) {
    if (!pause_menu_is_open(p) || w <= 0 || h <= 0) return;
    float x, y;
    pause_menu_to_virtual(pause_menu_transform(w, h), px, py, &x, &y);
    pause_pointer_press(p, x, y);
    pause_pointer_release(p, x, y);
}

/* PC pills use the existing shared focus/glow animations. */
static void pause_pill_hover(PauseMenu *p, float x, float y) {
    int hover = -1;
    if (pause_pills_visible(p)) {
        if (ug_rect_hit(pause_menu_bounds(PAUSE_BACK), x, y)) hover = 0;
        else if (ug_rect_hit(pause_menu_bounds(PAUSE_CONTINUE), x, y)) hover = 1;
    }
    if (hover == p->pill_hover) return;
    p->pill_hover = hover;
    if (hover >= 0) { ug_anim_play(&p->pill_pulse, UG_ANIM_PILL_FOCUS, 0); ug_anim_play(&p->pill_glow, UG_ANIM_PILL_GLOW, 0); }
    else { ug_anim_stop(&p->pill_pulse); ug_anim_stop(&p->pill_glow); }
}

void pause_menu_event(PauseMenu *p, const SDL_Event *e, SDL_Window *win) {
    if (!pause_menu_is_open(p)) return;
    PmKey k = PMK_NONE;
    if (e->type == SDL_KEYDOWN && !e->key.repeat && om_controls_capturing(&p->options)) {
        om_key_down(&p->options, (int)e->key.keysym.scancode);     /* a capture takes the raw key: Escape cancels */
        return;
    }
    if (e->type == SDL_KEYDOWN && !e->key.repeat) {
        switch (e->key.keysym.sym) {
        case SDLK_DELETE: k = PMK_RESET; break;                    /* Reset Keys To Default (the help-bar pill is not drawn) */
        case SDLK_LEFT: k = PMK_LEFT; break;
        case SDLK_RIGHT: k = PMK_RIGHT; break;
        case SDLK_UP: k = PMK_UP; break;
        case SDLK_DOWN: k = PMK_DOWN; break;
        case SDLK_RETURN: case SDLK_KP_ENTER: k = PMK_ACCEPT; break;
        case SDLK_ESCAPE: k = PMK_BACK; break;
        default: break;
        }
    } else if (e->type == SDL_CONTROLLERBUTTONDOWN) {
        switch (e->cbutton.button) {
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: k = PMK_LEFT; break;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: k = PMK_RIGHT; break;
        case SDL_CONTROLLER_BUTTON_DPAD_UP: k = PMK_UP; break;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: k = PMK_DOWN; break;
        case SDL_CONTROLLER_BUTTON_A: k = PMK_ACCEPT; break;
        case SDL_CONTROLLER_BUTTON_B: case SDL_CONTROLLER_BUTTON_BACK: k = PMK_BACK; break;
        case SDL_CONTROLLER_BUTTON_START: k = PMK_START; break;
        case SDL_CONTROLLER_BUTTON_Y: k = PMK_RESET; break;
        default: break;
        }
    } else if (e->type == SDL_MOUSEBUTTONDOWN && e->button.button == SDL_BUTTON_RIGHT) k = PMK_BACK;
    else if (e->type == SDL_MOUSEWHEEL && e->wheel.y) {
        if (om_is_open(&p->options) && om_screen(&p->options) != OM_SCREEN_HUB) return;   /* list wheel: unspecified (SPEC_GUI 11.2) */
        int d = e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -e->wheel.y : e->wheel.y;
        k = d > 0 ? PMK_LEFT : PMK_RIGHT;
    } else if ((e->type == SDL_MOUSEBUTTONDOWN || e->type == SDL_MOUSEBUTTONUP) && e->button.button == SDL_BUTTON_LEFT && win) {
        int w, h;
        SDL_GetWindowSize(win, &w, &h);
        if (w <= 0 || h <= 0) return;
        float x, y;
        pause_menu_to_virtual(pause_menu_transform(w, h), e->button.x, e->button.y, &x, &y);
        if (e->type == SDL_MOUSEBUTTONDOWN) pause_pointer_press(p, x, y); else pause_pointer_release(p, x, y);
        return;
    } else if (e->type == SDL_MOUSEMOTION && win) {
        int w, h;
        SDL_GetWindowSize(win, &w, &h);
        if (w > 0 && h > 0) {
            float x, y;
            pause_menu_to_virtual(pause_menu_transform(w, h), e->motion.x, e->motion.y, &x, &y);
            pause_pill_hover(p, x, y);                       /* hover never changes the selection (unspecified, SPEC_GUI_ART 10) */
            om_pointer_move(&p->options, x - 320, y - 240);  /* Controls: hover focuses a cell or the selector (4.4) */
        }
        return;
    }
    pause_key(p, k);
}

/* ---- update ----------------------------------------------------------------------------------- */

static FeAction pause_action_of(PsEvent e) {
    switch (e) {
    case PS_EV_RESUME: return FE_ACTION_RESUME;
    case PS_EV_QUIT: return FE_ACTION_QUIT;
    case PS_EV_REPLAY: return FE_ACTION_REPLAY;
    case PS_EV_RESTART: return FE_ACTION_RESTART;
    case PS_EV_EXPLORE: return FE_ACTION_EXPLORE;
    case PS_EV_WORLD_MAP: return FE_ACTION_WORLD_MAP;
    case PS_EV_GARAGE:
        /* This never-retail action has no host command; it must not select World Map. */
        fprintf(stderr, "pause: Jump to Garage is unavailable; resuming\n");
        return FE_ACTION_RESUME;
    default: return FE_ACTION_NONE;
    }
}

static void pause_open_hub(PauseMenu *p) {
    OmConfig cfg;
    om_config_default(&cfg, OM_CTX_INGAME);
    cfg.mouse_look=p->mouse_look;
    om_open_hub(&p->options, &cfg, p->opt_gfx, p->opt_caps, p->opt_store);
}

FeAction pause_menu_update(PauseMenu *p, float seconds) {
    if (!pause_menu_is_open(p)) return FE_ACTION_NONE;
    float ms = seconds > 0 ? seconds * 1000.0f : 0.0f;
    p->ui_ms += ms;
    p->ui.time_ms = p->ui_ms;
    if (p->pill_initialized) { ug_anim_update(&p->pill_pulse, ms); ug_anim_update(&p->pill_glow, ms); }
    FeAction a = FE_ACTION_NONE;
    if (p->confirm_only) {                                               /* M6: a lone confirmation */
        gd_update(&p->confirm, seconds);
        pause_sync_dialog(p, ms);
        GdResult r = gd_poll(&p->confirm);
        if (r == GD_RES_NONE) return FE_ACTION_NONE;
        p->confirm_result = r;
        p->confirm_only = 0; p->open = 0; p->press_pill = 0;
        pause_drop_dialog(p); p->dlg_failed = 0;
        return FE_ACTION_RESUME;                                          /* the host reads the answer */
    }
    if (om_is_open(&p->options) || p->options.nevents) {
        if (p->cx && om_controls_capturing(&p->options)) {          /* a joystick capture reads the device each frame */
            int pad = p->options.cm.dev[p->options.cm.cur].pad;
            CbPadSample sample;
            if (pad >= 0) { cx_sample(p->cx, pad, &sample); om_pad_frame(&p->options, &sample); }
        }
        om_update(&p->options, seconds);
        OmEvent oe;
        while ((oe = om_poll_event(&p->options)) != OM_EV_NONE) {
            if (oe == OM_EV_RESUME_GAME) a = FE_ACTION_RESUME;           /* Start: nothing is written (SPEC_GUI 5.2) */
            else if (oe == OM_EV_CONTROLS_CHANGED) {                     /* Controls left with edited bindings (D4) */
                if (p->cx && cx_save(p->cx)) fprintf(stderr, "controls: could not save the bindings\n");
            } else if (oe == OM_EV_LEAVE_TO_PAUSE) {                       /* hub Back: the pause screen is built afresh */
                pause_start_pause_screen(p);
                pause_pill_clear(p);
            }
        }
    } else {
        ps_update(&p->ps, seconds);
        PsEvent e;
        while ((e = ps_poll(&p->ps)) != PS_EV_NONE) {
            if (e == PS_EV_OPTIONS) {                                    /* exit-complete: switch to the options hub */
                if (p->options_enabled) pause_open_hub(p);
                else pause_start_pause_screen(p);                        /* cannot happen: the entry needs the settings */
            } else if (a == FE_ACTION_NONE) a = pause_action_of(e);
        }
    }
    pause_views_update(p, ms);
    if (a != FE_ACTION_NONE) {                                           /* the pause screen closes with the action */
        p->open = 0;
        p->press_pill = 0;
        pause_drop_view(p);
    }
    return a;
}

/* ---- drawing ---------------------------------------------------------------------------------- */

typedef struct { float x0, y0, x1, y1; int count; const UgFace *face; const uint8_t *pixels; int width, height; } PauseInk;
static void ink_extent(void *user, const UgQuad *q) {
    PauseInk *b = user;
    const UgFGlyph *g = ug_face_glyph(b->face, q->cp);
    if (!g || !b->pixels) return;
    int x0 = g->w, y0 = g->h, x1 = 0, y1 = 0;
    for (int y = 0; y < g->h; y++) for (int x = 0; x < g->w; x++) {
        int ax = g->x + x, ay = g->y + y;
        if (ax >= b->width || ay >= b->height || !b->pixels[((size_t)ay * b->width + ax) * 4 + 3]) continue;
        if (x < x0) x0 = x;
        if (y < y0) y0 = y;
        if (x + 1 > x1) x1 = x + 1;
        if (y + 1 > y1) y1 = y + 1;
    }
    if (x1 <= x0 || y1 <= y0) return;
    float left = q->x0 + x0, top = q->y0 + y0, right = q->x0 + x1, bottom = q->y0 + y1;
    if (!b->count++) { b->x0 = left; b->y0 = top; b->x1 = right; b->y1 = bottom; return; }
    b->x0 = fminf(b->x0, left); b->y0 = fminf(b->y0, top);
    b->x1 = fmaxf(b->x1, right); b->y1 = fmaxf(b->y1, bottom);
}
/* Pill label fitted by its visible ink into the measured region (pause SPEC 2). */
static void native_text(PauseMenu *p, const char *s, UgRect region, float max_scale, UgColor col, unsigned just) {
    UgCanvas *cv = &p->canvas;
    PauseInk b = {0};
    const UgFace *face = ug_text_face();
    if (!face || face->tex <= 0 || face->tex > p->assets.tex.n) { cv->visual_status = UG_VIS_UNAVAILABLE; return; }
    const UgTexture *atlas = &p->assets.tex.t[face->tex - 1];
    b.face = face; b.width = atlas->w; b.height = atlas->h;
    b.pixels = ug_texlib_pixels(&p->assets.tex, face->tex - 1);
    ug_text_quads(face, s, 1, ink_extent, &b, NULL);
    if (!b.count) { cv->visual_status = UG_VIS_UNAVAILABLE; return; }
    float tw = b.x1 - b.x0, th = b.y1 - b.y0;
    float scale = max_scale;
    if (tw * scale > region.w && tw > 0) scale = region.w / tw;
    if (th * scale > region.h && th > 0) scale = region.h / th;
    float x = region.x - b.x0 * scale;
    if (just & UG_J_CENTER) x += (region.w - tw * scale) / 2;
    float y = region.y + (region.h - th * scale) / 2 - b.y0 * scale;
    ug_text_draw_ink(cv, s, x, y, scale, col, 0, UG_GLYPHS_KEYS);
}
static void pause_draw_pills(PauseMenu *p) {
    if (!pause_pills_visible(p) || !p->scr.loaded) return;
    float alpha = p->pause_root >= 0 ? p->scr.scene.nodes[p->pause_root].wcolor.a / 255.0f : 1.0f;
    for (int k = 0; k < 2; k++) {
        UgRect r = pause_menu_bounds(k ? PAUSE_CONTINUE : PAUSE_BACK);
        UgColor tint = k == p->pill_hover ? ug_anim_color(&p->pill_pulse) : UG_RGBA(255, 255, 255, 150);
        if (k == p->pill_hover) ug_kit_pill_glow(&p->canvas, r, ug_color_alpha(ug_anim_color(&p->pill_glow), alpha));
        ug_kit_pill_pc(&p->canvas, r, ug_color_alpha(tint, alpha));
        native_text(p, k ? "Continue" : "Back", pause_menu_label_ink(k), .45f,
                    ug_color_alpha(k == p->pill_hover ? UG_ACCENT : UG_WHITE, alpha), UG_J_CENTER);
    }
}

/* Composes the current screen, the dialog over it and the pause pills into the canvas. */
static void pause_compose(PauseMenu *p) {
    UgCanvas *cv = &p->canvas;
    ug_cv_clear(cv);
    if (p->confirm_only) {                                               /* M6: the dialog over the running scene */
        if (!p->dlg_loaded || !gv_screen_draw(&p->dlg_v.scr, &p->ui, cv)) { ug_cv_clear(cv); cv->visual_status = UG_VIS_UNAVAILABLE; }
        return;
    }
    if (!p->visual || !p->scr.loaded) { cv->visual_status = UG_VIS_UNAVAILABLE; return; }
    if (!gv_screen_draw(&p->scr, &p->ui, cv)) {
        p->visual = 0;
        snprintf(p->status, sizeof p->status, "%s", p->scr.status);
        fprintf(stderr, "pause: %s\n", p->status);
        ug_cv_clear(cv);
        cv->visual_status = UG_VIS_UNAVAILABLE;
        return;
    }
    if (p->dlg_loaded && pause_current_dialog(p) && !gv_screen_draw(&p->dlg_v.scr, &p->ui, cv)) {
        snprintf(p->dlg_status, sizeof p->dlg_status, "%s", p->dlg_v.scr.status);
        fprintf(stderr, "pause: %s\n", p->dlg_status);
    }
    ug_cv_translate(cv, -320, -240);
    pause_draw_pills(p);
}

void pause_menu_draw(PauseMenu *p, int width, int height) {
    if (!pause_menu_is_open(p) || !p->gl || !p->compatible || width <= 0 || height <= 0) return;
    const UgFace *face = ug_text_face();
    const UgSkin *old = ug_kit_skin();
    UgSkin saved;
    if (old) saved = *old;
    ug_text_set_face(ug_assets_main_face(&p->assets));
    ug_kit_set_skin(&p->skin);
    pause_compose(p);
    if (ug_cv_visual_status(&p->canvas) == UG_VIS_OK) ug_gl_render_overlay(p->gl, &p->canvas, width, height);
    ug_text_set_face(face);
    ug_kit_set_skin(old ? &saved : NULL);
}

/* ---- verification adapter ----------------------------------------------------------------------- */
/* Advances only UI time, never host scene/camera state. Replay syntax is comma-separated absolute
 * times: 400:Right,480:Enter. Names: open Left Right Up Down Enter Continue Back Escape Start
 * right_click, and the pointer events pointer/press/release/hover x:y in top-left virtual units
 * (rail_* are accepted and inert). Up/Down/Start matter on the option screens. */
int pause_menu_capture_duration_valid(float elapsed_ms) {
    return isfinite(elapsed_ms) && elapsed_ms >= 0 && elapsed_ms <= PAUSE_MENU_CAPTURE_MAX_MS;
}
static void pause_capture_tick(PauseMenu *p, float seconds) {
    FeAction a = pause_menu_update(p, seconds);
    if (a != FE_ACTION_NONE) { p->capture_actions++; p->capture_last_action = a; }
}
static void pause_capture_advance(PauseMenu *p, float ms) {
    while (ms > 0) { float step = fminf(ms, 10); pause_capture_tick(p, step * .001f); ms -= step; }
}
int pause_menu_capture_replay(PauseMenu *p, const char *events, float elapsed_ms, int w, int h) {
    if (!p || !pause_menu_capture_duration_valid(elapsed_ms) || w <= 0 || h <= 0) return 0;
    float previous = 0;
    p->capture_actions = 0;
    p->capture_last_action = FE_ACTION_NONE;
    char *copy = events ? strdup(events) : NULL, *save = NULL;
    if (events && !copy) return 0;
    for (char *item = copy ? strtok_r(copy, ",", &save) : NULL; item; item = strtok_r(NULL, ",", &save)) {
        float at, x = 0, y = 0;
        char name[32] = {0};
        int fields = sscanf(item, "%f:%31[^:]:%f:%f", &at, name, &x, &y);
        if (fields < 2 || !isfinite(at) || at < previous || at > elapsed_ms) { free(copy); return 0; }
        pause_capture_advance(p, at - previous);
        previous = at;
        if (!strcmp(name, "open")) pause_menu_open(p);
        else if (!strcmp(name, "Right")) pause_key(p, PMK_RIGHT);
        else if (!strcmp(name, "Left")) pause_key(p, PMK_LEFT);
        else if (!strcmp(name, "Up")) pause_key(p, PMK_UP);
        else if (!strcmp(name, "Down")) pause_key(p, PMK_DOWN);
        else if (!strcmp(name, "Enter") || !strcmp(name, "Continue")) pause_key(p, PMK_ACCEPT);
        else if (!strcmp(name, "Back") || !strcmp(name, "Escape") || !strcmp(name, "right_click")) pause_key(p, PMK_BACK);
        else if (!strcmp(name, "Start")) pause_key(p, PMK_START);
        else if (!strcmp(name, "Reset")) pause_key(p, PMK_RESET);
        else if (fields >= 3 && !strcmp(name, "key")) { if (om_controls_capturing(&p->options)) om_key_down(&p->options, (int)x); }   /* key:SDL scancode */
        else if (!strcmp(name, "rail_up") || (fields >= 3 && (!strcmp(name, "rail_down") || !strcmp(name, "rail_move")))) {}
        else if (fields == 4 && !strcmp(name, "pointer")) {
            PauseTransform t = pause_menu_transform(w, h);
            pause_menu_pointer(p, (float)(t.ox + x * t.scale), (float)(t.oy + y * t.scale), w, h);
        } else if (fields == 4 && !strcmp(name, "press")) pause_pointer_press(p, x, y);
        else if (fields == 4 && !strcmp(name, "release")) pause_pointer_release(p, x, y);
        else if (fields == 4 && !strcmp(name, "hover")) pause_pill_hover(p, x, y);
        else { free(copy); return 0; }
        pause_capture_tick(p, 0);
    }
    free(copy);
    pause_capture_advance(p, elapsed_ms - previous);
    fprintf(stderr, "pause capture: elapsed=%.3f ui=%.3f screen=%s selected=%d open=%d dialog=%d actions=%d last_action=%d caption=%s status=%s\n",
            elapsed_ms, p->ui_ms, om_is_open(&p->options) ? (om_screen(&p->options) == OM_SCREEN_HUB ? "hub" :
            om_screen(&p->options) == OM_SCREEN_DISPLAY ? "display" :
            om_screen(&p->options) == OM_SCREEN_GAMEPLAY ? "gameplay" :
            om_screen(&p->options) == OM_SCREEN_CONTROLS ? "controls" : "customize") : "pause",
            om_is_open(&p->options) ? (om_screen(&p->options) == OM_SCREEN_HUB ? om_hub_focus(&p->options) : om_focus(&p->options)) : ps_selected(&p->ps),
            p->open, pause_menu_dialog_open(p), p->capture_actions, p->capture_last_action, pause_menu_caption(p), p->status);
    return 1;
}
/* The populated reference of --pause-reference: the same replay on the production adapter, then the
 * frame. (Before the authored-art rewrite this was a separately coded evaluator; both now evaluate the
 * authored packages through the same view code, so a second path no longer adds independence.) */
int pause_menu_draw_reference(PauseMenu *p, int width, int height, float elapsed_ms, const char *events) {
    if (!p || !p->gl || !pause_menu_capture_duration_valid(elapsed_ms) || width <= 0 || height <= 0) return 0;
    pause_menu_open(p);
    if (!pause_menu_capture_replay(p, events, elapsed_ms, width, height)) return 0;
    if (!p->open) return 1;
    pause_menu_draw(p, width, height);
    return pause_menu_visual_available(p) && (!pause_menu_dialog_open(p) || pause_menu_dialog_visual_available(p));
}

const char *pause_menu_text(const PauseMenu *p, uint32_t key, const char *fallback) {
    return p && p->assets_open && key ? gv_text(&p->assets, key, fallback) : fallback;
}

int pause_menu_confirm(PauseMenu *p, int kind) {
    if (!p || p->open) return 0;
    GdSpec spec;
    gd_spec(&spec, (GdKind)kind, NULL);
    pause_drop_dialog(p); p->dlg_failed = 0;
    gd_open(&p->confirm, &spec);
    p->confirm_only = 1; p->confirm_result = GD_RES_NONE; p->open = 1; p->press_pill = 0;
    pause_sync_dialog(p, 0);
    return 1;
}
int pause_menu_take_confirm(PauseMenu *p) {
    if (!p) return GD_RES_NONE;
    int r = p->confirm_result;
    p->confirm_result = GD_RES_NONE;
    return r;
}
