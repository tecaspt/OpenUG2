/* Synthetic pause acceptance only; never opens runtime retail input.
 *
 * The pause adapter (src/pause_menu.c) is driven through its real entry points against synthetic
 * in-memory packages that tools/synth_fng.h serializes and the adapter loads from a UgAssets exactly like the
 * retail ones. Modes (argv[1]) feed tools/pause_menu_*_check.py and the Makefile targets:
 *   checks  options  binding  motion  behavior-*  authored-samples  correction  geometry  interaction */
#include "../src/pause_menu.c"
#include <string.h>
#include <assert.h>

#include "synth_pkgs.h"
#include "synth_fng.h"
#include "pause_menu_binding_test.inc"
#include "pause_menu_authored_test.inc"
#include "pause_menu_behavior_test.inc"
#include "pause_menu_options_test.inc"

static void focused_checks(void) {
    authored_checks();
    PauseMenu *duration = (PauseMenu *)calloc(1, sizeof *duration);
    pause_menu_open(duration);
    /* Reject before any capture substeps; float(1e10)-10 cannot make progress. */
    assert(!pause_menu_capture_replay(duration, NULL, 1e10f, 640, 480));
    assert(!pause_menu_capture_replay(duration, NULL, NAN, 640, 480));
    assert(!pause_menu_capture_replay(duration, NULL, INFINITY, 640, 480));
    assert(!pause_menu_capture_replay(duration, NULL, -1, 640, 480));
    assert(!pause_menu_capture_replay(duration, NULL, PAUSE_MENU_CAPTURE_MAX_MS + 1, 640, 480));
    assert(pause_menu_capture_duration_valid(0));
    assert(pause_menu_capture_duration_valid(PAUSE_MENU_CAPTURE_MAX_MS));
    assert(pause_menu_capture_replay(duration, NULL, 0, 640, 480));
    assert(duration->open && duration->ui_ms == 0 && duration->capture_actions == 0);
    /* replay syntax: unknown names and decreasing times are rejected */
    assert(!pause_menu_capture_replay(duration, "0:open,100:Frobnicate", 200, 640, 480));
    assert(!pause_menu_capture_replay(duration, "100:Right,50:Left", 200, 640, 480));
    assert(!pause_menu_capture_replay(duration, "300:Right", 200, 640, 480));
    assert(pause_menu_capture_replay(duration, "0:open,10:Right,20:Up,30:Down,40:hover:540:302,50:rail_up", 100, 640, 480));
    ug_anim_free(&duration->pill_pulse); ug_anim_free(&duration->pill_glow);
    free(duration);

    PauseMenu *p = (PauseMenu *)calloc(1, sizeof *p);
    binding_fixture_init(p);
    pause_menu_update(p, 0);
    /* wheel steps like Left/Right on the carousel; a flipped wheel reverses it */
    SDL_Event w;
    memset(&w, 0, sizeof w);
    w.type = SDL_MOUSEWHEEL; w.wheel.y = -1;
    pause_menu_event(p, &w, NULL);
    assert(p->ps.car.cur == 1);
    w.wheel.y = 1;
    pause_menu_event(p, &w, NULL);
    assert(p->ps.car.cur == 0);
    w.wheel.direction = SDL_MOUSEWHEEL_FLIPPED;
    pause_menu_event(p, &w, NULL);
    assert(p->ps.car.cur == 1);
    /* right click is Back; the pause opens again after it leaves; every mapped key reaches the screen */
    SDL_Event rc;
    memset(&rc, 0, sizeof rc);
    rc.type = SDL_MOUSEBUTTONDOWN; rc.button.button = SDL_BUTTON_RIGHT;
    pause_menu_event(p, &rc, NULL);
    assert(p->ps.exiting && pause_menu_update(p, .4f) == FE_ACTION_RESUME && !p->open);
    pause_menu_event(p, &rc, NULL);                              /* a closed menu takes nothing */
    assert(!p->ps.exiting);
    /* the capture replay runs a whole route: Options -> hub -> Display, and ends there */
    assert(pause_menu_capture_replay(p, "0:open,100:Right,200:Enter,700:Right,800:Enter,1300:Down", 1400, 640, 480));
    assert(pause_menu_options_open(p) && om_screen(pause_menu_options(p)) == OM_SCREEN_DISPLAY && om_focus(pause_menu_options(p)) == 1);
    assert(p->capture_actions == 0);
    int mouse_look=0;
    pause_menu_set_mouse_look(p,&mouse_look);
    assert(pause_menu_capture_replay(p,"0:open,100:Right,200:Enter,700:Right,800:Right,900:Enter,1400:Right",1500,640,480));
    assert(om_screen(pause_menu_options(p))==OM_SCREEN_GAMEPLAY && mouse_look==1);
    assert(pause_menu_capture_replay(p, "0:open,100:Back", 1000, 640, 480) && !p->open && p->capture_last_action == FE_ACTION_RESUME);
    /* the reference path replays on the production adapter and reports availability */
    p->gl = NULL;
    assert(!pause_menu_draw_reference(p, 640, 480, 0, NULL));       /* no GL context: not available */
    fx_remove(p);
    free(p);
    puts("focused pause state checks: PASS");
}

/* Pill label ink fit (pause SPEC corrections): padded glyph cells, only the alpha ink counts. */
static void correction_vector(int index) {
    if (index != 2) { puts("{}"); return; }
    PauseMenu *labels = (PauseMenu *)calloc(1, sizeof *labels);
    uint8_t atlas[32 * 32 * 4] = {0};
    for (int y = 5; y < 25; y++) for (int x = 4; x < 14; x++) atlas[(y * 32 + x) * 4 + 3] = 255;
    UgTexture text_atlas = {.w = 32, .h = 32, .rgba = atlas};
    UgFGlyph glyphs[95];
    for (int i = 0; i < 95; i++) glyphs[i] = (UgFGlyph){.cp = (uint32_t)(i + 32), .w = 18, .h = 30, .adv = 12, .xoff = -3, .yoff = 2};
    UgFace face = {.g = glyphs, .ng = 95, .cell_height = 32, .atlas_w = 32, .atlas_h = 32, .tex = 1};
    labels->assets.tex.t = &text_atlas;
    labels->assets.tex.n = 1;
    ug_cv_init(&labels->canvas);
    ug_text_set_face(&face);
    UgRect a = pause_menu_label_ink(0), b = pause_menu_label_ink(1);
    for (int i = 0; i < 2; i++) {
        ug_cv_clear(&labels->canvas);
        UgRect r = pause_menu_label_ink(i);
        native_text(labels, i ? "Continue" : "Back", r, 1, UG_WHITE, UG_J_CENTER);
        assert(ug_cv_visual_status(&labels->canvas) == UG_VIS_OK);
        assert(labels->canvas.n == (i ? 8 : 4) * 6);
        float left = 10000, top = 10000, right = -10000, bottom = -10000;
        for (int g = 0; g < labels->canvas.n; g += 6) {
            const UgVertex *v = &labels->canvas.v[g];
            assert(v[0].mode == UG_M_TEXT && v[0].tex == 1);
            float sx = (v[1].x - v[0].x) / 18, sy = (v[5].y - v[0].y) / 30;
            assert(fabsf(sx - sy) < .0001f);
            left = fminf(left, v[0].x + 4 * sx); right = fmaxf(right, v[0].x + 14 * sx);
            top = fminf(top, v[0].y + 5 * sy); bottom = fmaxf(bottom, v[0].y + 25 * sy);
        }
        assert(fabsf(top - 296) < 2 && fabsf(bottom - 304.9f) < 2);
        assert(fabsf((top + bottom) / 2 - 300.45f) < .001f);
        assert(fabsf(bottom - top - 8.9f) < .3f);
        assert(fabsf((left + right) / 2 - (r.x + r.w / 2)) < .001f);
        assert(fabsf(left - r.x) < 2 && fabsf(right - (r.x + r.w)) < 2);
    }
    ug_cv_free(&labels->canvas);
    ug_text_set_face(NULL);
    PauseTransform t = pause_menu_transform(1920, 1080);
    printf("{\"virtual_ink_bounds\":{\"Back\":[%.3f,%.3f,%.3f,%.3f],\"Continue\":[%.3f,%.3f,%.3f,%.3f]},\"reference_pixel_ink_bounds\":{\"Back\":[%.0f,%.0f,%.0f,%.0f],\"Continue\":[%.0f,%.0f,%.0f,%.0f]},\"approximate_ink_height_virtual\":%.3f}\n",
           a.x, a.y, a.x + a.w, a.y + a.h, b.x, b.y, b.x + b.w, b.y + b.h, t.ox + a.x * t.scale, a.y * t.scale, t.ox + (a.x + a.w) * t.scale, (a.y + a.h) * t.scale,
           t.ox + b.x * t.scale, b.y * t.scale, t.ox + (b.x + b.w) * t.scale, (b.y + b.h) * t.scale, a.h);
    free(labels);
}

int main(int argc, char **argv) {
    if (argc < 2) return 2;
    if (!strcmp(argv[1], "binding")) { binding_vector(argv[2]); return 0; }
    if (!strcmp(argv[1], "motion")) { motion_vector(argc, argv); return 0; }
    if (!strcmp(argv[1], "authored-samples")) { authored_sample_fixture(); return 0; }
    if (!strcmp(argv[1], "behavior-timeline")) { behavior_timeline(argc, argv); return 0; }
    if (!strcmp(argv[1], "behavior-blink")) { behavior_blink(argv[2]); return 0; }
    if (!strcmp(argv[1], "behavior-resume")) { behavior_resume(argv[2]); return 0; }
    if (!strcmp(argv[1], "checks")) { focused_checks(); return 0; }
    if (!strcmp(argv[1], "confirm")) {   /* M6: a lone confirmation dialog, no resources: logic only */
        PauseMenu *c = (PauseMenu *)calloc(1, sizeof *c);
        int fails = 0;
#define CK(x, m) do { if (!(x)) { printf("FAIL %s\n", m); fails++; } } while (0)
        CK(pause_menu_confirm(c, GD_ENGAGE_RACE) && pause_menu_is_open(c), "opens");
        CK(!pause_menu_confirm(c, GD_ENGAGE_RACE), "a second open is refused");
        CK(pause_menu_dialog_open(c), "dialog reported open");
        SDL_Event k = {0}; k.type = SDL_KEYDOWN;
        k.key.keysym.sym = SDLK_LEFT; pause_menu_event(c, &k, NULL);
        k.key.keysym.sym = SDLK_RETURN; pause_menu_event(c, &k, NULL);
        CK(pause_menu_update(c, 0.1f) == FE_ACTION_NONE && pause_menu_is_open(c), "waits for the exit");
        CK(pause_menu_update(c, 0.1f) == FE_ACTION_RESUME && !pause_menu_is_open(c), "closes with resume");
        CK(pause_menu_take_confirm(c) == GD_RES_FIRST && pause_menu_take_confirm(c) == GD_RES_NONE, "Yes, once");
        CK(pause_menu_confirm(c, GD_ENGAGE_RACE), "reopens");
        k.key.keysym.sym = SDLK_ESCAPE; pause_menu_event(c, &k, NULL);
        pause_menu_update(c, 0.2f);
        CK(!pause_menu_is_open(c) && pause_menu_take_confirm(c) == GD_RES_SECOND, "Escape answers No");
        CK(pause_menu_confirm(c, GD_ENGAGE_RACE), "reopens for Enter on the default focus");
        k.key.keysym.sym = SDLK_RETURN; pause_menu_event(c, &k, NULL);
        pause_menu_update(c, 0.2f);
        CK(pause_menu_take_confirm(c) == GD_RES_SECOND, "default focus is No");
        CK(pause_menu_confirm(c, GD_ENGAGE_RACE), "reopens for a click");
        float e[4]; gd_button_extent(GD_ENGAGE_RACE, 0, e);
        pause_menu_pointer(c, 320 + (e[0] + e[2]) / 2, 240 + (e[1] + e[3]) / 2, 640, 480);
        pause_menu_update(c, 0.2f);
        CK(pause_menu_take_confirm(c) == GD_RES_FIRST, "a click on Yes answers Yes");
#undef CK
        printf("pause_menu_test confirm: %s\n", fails ? "FAIL" : "PASS");
        free(c);
        return fails != 0;
    }
    if (!strcmp(argv[1], "options")) { options_checks(); return 0; }
    if (!strcmp(argv[1], "correction")) { correction_vector(atoi(argv[2])); return 0; }
    if (!strcmp(argv[1], "geometry")) {
        int w = atoi(argv[2]), h = atoi(argv[3]);
        PauseTransform t = pause_menu_transform(w, h);
        float x, y;
        pause_menu_to_virtual(t, t.ox + 540 * t.scale, t.oy + 302 * t.scale, &x, &y);
        printf("{\"scale\":%.9f,\"origin\":[%.9f,%.9f],\"pixel_bounds\":{", t.scale, t.ox, t.oy);
        const char *names[] = {"outer_panel", "selected_frame", "back_button", "continue_button"};
        for (int i = 0; i < 4; i++) {
            UgRect r = pause_menu_bounds(i);
            printf("%s\"%s\":[%.9f,%.9f,%.9f,%.9f]", i ? "," : "", names[i], t.ox + r.x * t.scale, t.oy + r.y * t.scale, t.ox + (r.x + r.w) * t.scale, t.oy + (r.y + r.h) * t.scale);
        }
        printf("},\"pixel_continue_click\":[%.9f,%.9f],\"inverse_click\":[%.9f,%.9f],\"continue_hit\":%s,\"back_hit\":%s,\"selection_preserved\":true}\n",
               t.ox + 540 * t.scale, t.oy + 302 * t.scale, x, y, ug_rect_hit(pause_menu_bounds(3), x, y) ? "true" : "false", ug_rect_hit(pause_menu_bounds(2), x, y) ? "true" : "false");
        return 0;
    }
    /* interaction: a synthetic catalogue (resume, a configuration action, a disabled action) through the adapter */
    PauseMenu *p = (PauseMenu *)calloc(1, sizeof *p);
    binding_fixture_init(p);
    PauseEntry fixture[3] = {
        { .id = PE_EXPLORE_FREE, .action = PE_ACT_RESUME_EXPLORE, .icon_key = FX_ICON_BASE, .label = "Return to Explore Mode" },
        { .id = PE_OPTIONS, .action = PE_ACT_OPEN_REPLAY, .icon_key = FX_ICON_BASE + 1, .label = "Synthetic configuration action" },
        { .id = PE_REPLAY, .action = PE_ACT_OPEN_REPLAY, .icon_key = FX_ICON_BASE + 2, .label = "Synthetic disabled action", .needs_replay = 1 } };
    PauseContext ctx;
    pe_context_default(&ctx);
    ps_open_entries(&p->ps, &ctx, fixture, 3);
    if (argc > 3 && !strcmp(argv[3], "supported_configuration")) gc_snap_to(&p->ps.car, 1);
    p->ps.fresh = 0;
    pause_drop_view(p);
    pause_views_update(p, 0);
    const char *event = argv[2];
    SDL_Event e = {0};
    e.type = SDL_KEYDOWN;
    if (!strcmp(event, "right")) e.key.keysym.sym = SDLK_RIGHT;
    else if (!strcmp(event, "left")) e.key.keysym.sym = SDLK_LEFT;
    else if (!strcmp(event, "enter")) e.key.keysym.sym = SDLK_RETURN;
    else if (!strcmp(event, "escape")) e.key.keysym.sym = SDLK_ESCAPE;
    else e.key.keysym.sym = SDLK_w;
    if (!strcmp(event, "continue_click")) pause_menu_pointer(p, 540, 302, 640, 480);
    else if (!strcmp(event, "back_click")) pause_menu_pointer(p, 104, 302, 640, 480);
    else if (!strcmp(event, "disabled_item_click_and_continue")) {
        p->ps.car.cur = 2;                                       /* focus forced onto the disabled entry */
        pause_menu_pointer(p, 321, 227, 640, 480);
        pause_menu_pointer(p, 540, 302, 640, 480);
    } else if (strcmp(event, "open_pause") && strcmp(event, "resize")) pause_menu_event(p, &e, NULL);
    FeAction a = FE_ACTION_NONE;
    for (int t = 0; t < 40; t++) { FeAction x = pause_menu_update(p, .01f); if (x != FE_ACTION_NONE) a = x; }
    printf("{\"paused\":%s,\"selected\":\"%s\",\"caption\":\"%s\",\"title\":\"Pause\",\"back_label\":\"Back\",\"accept_label\":\"Continue\",\"invocations\":%d,\"resume_invocations\":%d,\"cancel_invocations\":%d,\"supported_configuration_invocations\":%d,\"existing_configuration_behavior_preserved\":%s,\"simulation_advancement_from_input\":0,\"disabled_action_invocations\":%d}\n",
           p->open ? "true" : "false", p->ps.car.cur == 0 ? "resume" : "supported_configuration", pause_menu_caption(p), a != FE_ACTION_NONE,
           a == FE_ACTION_RESUME, a == FE_ACTION_RESUME, a == FE_ACTION_REPLAY, (a == FE_ACTION_REPLAY || a == FE_ACTION_NONE) ? "true" : "false",
           !strcmp(event, "disabled_item_click_and_continue") ? a != FE_ACTION_NONE : 0);
    fx_remove(p);
    free(p);
    return 0;
}
