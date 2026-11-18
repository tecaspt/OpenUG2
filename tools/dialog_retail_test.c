/* dialog_retail_test.c -- TASK-AL: renders screenshots of the three dialog families with the retail
 * resources through the pause adapter's own bind/draw path, and drives keyboard and touch to the
 * resulting answers. usage:
 *   build/dialog_retail_test DATA_DIR [SHOTS_DIR]
 * DATA_DIR is required and must be the install root; without it, or when a retail dialog package
 * does not bind, the test FAILS (it never skips). SHOTS_DIR defaults to build/dialog-shots. The
 * families: GenericDialog_ThreeButton (Delete / No / Cancel rows: focus, keyboard, tap results),
 * HelpDialog_SMALL (title, caption, close on tap and Back), GenericDialog_SMALL (Back still answers
 * the second button). Nothing here reads committed game data. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "rhi.h"
#include <png.h>

#include "pause_menu.h"
#include "pause_menu_layout.h"
#include "gui_dialog.h"

static int g_checks, g_fail;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; \
    fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* Reads back the frame as an RGB PNG (rhi_read_pixels returns top-down rows, as in
 * fe_screens_check). */
static void save_png(const char *path, int w, int h) {
    unsigned char *px = malloc((size_t)w * h * 3);
    if (!px) { g_fail++; return; }
    if (rhi_read_pixels(0, 0, w, h, RHI_READ_RGB8, px, 0) != RHI_OK) {
        free(px);
        g_fail++;
        fprintf(stderr, "FAIL cannot read pixels for %s\n", path);
        return;
    }
    FILE *f = fopen(path, "wb");
    if (!f) { free(px); g_fail++; fprintf(stderr, "FAIL cannot write %s\n", path); return; }
    png_structp p = png_create_write_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
    png_infop i = png_create_info_struct(p);
    png_init_io(p, f);
    png_set_IHDR(p, i, w, h, 8, PNG_COLOR_TYPE_RGB, 0, 0, 0);
    png_write_info(p, i);
    for (int y = 0; y < h; y++) png_write_row(p, px + (size_t)y * w * 3);
    png_write_end(p, i);
    png_destroy_write_struct(&p, &i);
    fclose(f);
    free(px);
    printf("wrote %s\n", path);
}

static PauseMenu *g_menu;
static const char *g_shots;
static int W = 1280, H = 960;
static SDL_Window *g_win;

/* Advances frames, drawing each, and saves the last one. */
static void frames(int n, const char *png) {
    for (int k = 0; k < n; k++) {
        pause_menu_update(g_menu, 1.0f / 60);
        rhi_frame_begin(NULL);
        static const float black[4] = { 0, 0, 0, 1 };
        rhi_cmd_clear_main(black, 1);
        pause_menu_draw(g_menu, W, H);
        if (png && k == n - 1) {
            char path[600];
            snprintf(path, sizeof path, "%s/%s", g_shots, png);
            save_png(path, W, H);
        }
        rhi_frame_end();
    }
}
static void key(SDL_Keycode k) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_KEYDOWN;
    e.key.keysym.sym = k;
    pause_menu_event(g_menu, &e, g_win);
}
/* Clicks (press and release) as window-pixel SDL events, with the window, as the host posts them. */
static void click_px(int px, int py) {
    SDL_Event e;
    memset(&e, 0, sizeof e);
    e.type = SDL_MOUSEBUTTONDOWN;
    e.button.button = SDL_BUTTON_LEFT;
    e.button.x = px;
    e.button.y = py;
    pause_menu_event(g_menu, &e, g_win);
    e.type = SDL_MOUSEBUTTONUP;
    pause_menu_event(g_menu, &e, g_win);
}
/* Clicks the centre of dialog button `button` of `kind`. The virtual space is 640x480 top-left
 * y-down and the dialog space is centred y-up, so a centred point (cx, cy) sits at virtual
 * (cx + 320, 240 + cy) -- pause_menu_to_virtual only divides by the scale. */
static void click_button(GdKind kind, int button) {
    GdSpec spec;
    float e[4];
    gd_spec(&spec, kind, NULL);
    gd_spec_button_extent(&spec, button, e);
    float vx = (e[0] + e[2]) / 2.0f + 320.0f;
    float vy = 240.0f + (e[1] + e[3]) / 2.0f;
    PauseTransform t = pause_menu_transform(W, H);
    int px = (int)(t.ox + t.scale * vx), py = (int)(t.oy + t.scale * vy);
    click_px(px, py);
}
/* Takes the answer once the menu has closed the lone confirmation: updates run until the answer is
 * there (the dialog's own exit script takes ~140 ms), then lets the menu finish its fade so the next
 * pause_menu_confirm can open again. */
static int answer(int expect) {
    int r = pause_menu_take_confirm(g_menu);
    for (int k = 0; k < 60 && r == GD_RES_NONE; k++) {
        frames(1, NULL);
        r = pause_menu_take_confirm(g_menu);
    }
    CHECK(r == expect, "answer is %d, expected %d", r, expect);
    if (r != GD_RES_NONE) frames(24, NULL);                 /* the exit faded out; the menu is closed */
    return r;
}
/* Opens `kind` over the running scene, requires the retail package to bind, then draws and shots it. */
static void open_shot(GdKind kind, const char *png) {
    CHECK(pause_menu_confirm(g_menu, kind), "%s: confirm opens", png ? png : "?");
    CHECK(pause_menu_dialog_prepare(g_menu), "%s: binds the retail package: %s", png ? png : "?", pause_menu_dialog_status(g_menu));
    frames(24, png);
}

/* ---- the three-row dialog: rows, keyboard, touch, Back ------------------------------------------- */
static void test_three_button(void) {
    open_shot(GD_DELETE_PROFILE, "three_button_focus_middle.png");
    CHECK(pause_menu_dialog_visual_available(g_menu), "three-button draws: %s", pause_menu_dialog_status(g_menu));
    answer(GD_RES_NONE);                                   /* idle: the dialog stays open */
    key(SDLK_UP);                                          /* focus moves to the L3 row (top) */
    frames(8, "three_button_focus_top.png");
    answer(GD_RES_NONE);                                   /* Up alone changes focus, not the answer */
    click_button(GD_DELETE_PROFILE, 2);                    /* the top row = Cancel */
    answer(GD_RES_THIRD);
    open_shot(GD_DELETE_PROFILE, NULL);
    key(SDLK_DOWN);                                        /* middle -> L1 (bottom) */
    key(SDLK_RETURN);
    answer(GD_RES_FIRST);
    open_shot(GD_DELETE_PROFILE, NULL);
    key(SDLK_ESCAPE);                                      /* Back = cancel */
    answer(GD_RES_CANCEL);
}

/* ---- a help sheet: the caption row closes it; Back closes it -------------------------------------- */
static void test_help(void) {
    open_shot(GD_HELP, "help_small.png");
    CHECK(pause_menu_dialog_visual_available(g_menu), "the help sheet draws: %s", pause_menu_dialog_status(g_menu));
    click_button(GD_HELP, 0);                              /* the B caption at its authored extent */
    answer(GD_RES_FIRST);
    open_shot(GD_HELP, NULL);
    key(SDLK_ESCAPE);
    answer(GD_RES_CANCEL);
    /* the sheet family grows with the text: this body belongs to MED, not SMALL */
    GdSpec spec;
    gd_spec_help(&spec, 0, NULL, 0,
        "This help text is long enough to need more room than the small sheet offers.", 0, NULL);
    CHECK(!strcmp(spec.package, "HelpDialog_MED.fng"), "a long body selects MED");
}

/* ---- the two-button dialog keeps answering Back with the second button (retail regression) ------ */
static void test_two_button(void) {
    open_shot(GD_DEFAULTS, "two_button.png");
    CHECK(pause_menu_dialog_visual_available(g_menu), "the two-button dialog draws: %s", pause_menu_dialog_status(g_menu));
    key(SDLK_ESCAPE);                                      /* Back = No, as always (6) */
    answer(GD_RES_SECOND);
}

int main(int argc, char **argv) {
    const char *data = argc > 1 && argv[1][0] ? argv[1] : NULL;
    g_shots = argc > 2 && argv[2][0] ? argv[2] : "build/dialog-shots";
    if (!data) { fprintf(stderr, "usage: dialog_retail_test DATA_DIR [SHOTS_DIR] -- the retail data root is required\n"); return 2; }
    mkdir(g_shots, 0777);
    SDL_Init(SDL_INIT_VIDEO);
    rhi_prepare_window(RHI_BACKEND_DEFAULT);
    SDL_Window *win = SDL_CreateWindow("dialog retail check", 0, 0, W, H, rhi_window_flags(RHI_BACKEND_DEFAULT) | SDL_WINDOW_HIDDEN);
    if (!win) { fprintf(stderr, "FAIL window\n"); return 1; }
    g_win = win;
    RhiDeviceDesc dev = { RHI_BACKEND_DEFAULT, 0, win };
    if (rhi_device_create(&dev) != RHI_OK) { fprintf(stderr, "FAIL rhi device\n"); return 1; }
    g_menu = pause_menu_create(data);
    if (!g_menu) { fprintf(stderr, "FAIL pause_menu_create(%s)\n", data); return 1; }
    PauseContext c;
    pe_context_default(&c);
    pause_menu_set_context(g_menu, &c);
    test_three_button();
    test_help();
    test_two_button();
    pause_menu_destroy(g_menu);
    rhi_device_destroy();
    SDL_Quit();
    printf("dialog_retail_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
