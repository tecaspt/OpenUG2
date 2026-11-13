/* controls_sdl_test -- the SDL side of the Controls feature (src/controls_sdl.c) against an SDL virtual
 * joystick: device discovery and defaults, frame samples for the capture, the driving lookups, and
 * persistence across cx_shutdown/cx_init. Headless (joystick subsystem only). If this SDL has no virtual
 * joystick support the test skips. User directories are redirected to build/controls_sdl_tmp.
 * Exit code is nonzero if any check fails. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#endif

#include <SDL.h>

#include "controls_sdl.h"
#include "controls_store.h"

static int g_checks, g_fail;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_fail++; \
    if (g_fail <= 60) { printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } } while (0)

/* Binding index SDL's mapping gives a button, as the engine reads it (raw button < 16, or one hat direction), or -1. */
static int sdl_index(SDL_GameController *gc, SDL_GameControllerButton b) {
    SDL_GameControllerButtonBind k = SDL_GameControllerGetBindForButton(gc, b);
    if (k.bindType == SDL_CONTROLLER_BINDTYPE_BUTTON) return k.value.button < CB_BIND_BUTTONS ? k.value.button : -1;
    if (k.bindType == SDL_CONTROLLER_BINDTYPE_HAT && k.value.hat.hat < CB_BIND_POVS) {
        int d = k.value.hat.hat_mask == SDL_HAT_DOWN ? CB_DIR_DOWN : k.value.hat.hat_mask == SDL_HAT_UP ? CB_DIR_UP :
                k.value.hat.hat_mask == SDL_HAT_LEFT ? CB_DIR_LEFT : k.value.hat.hat_mask == SDL_HAT_RIGHT ? CB_DIR_RIGHT : -1;
        return d < 0 ? -1 : CB_BIND_BUTTONS + 4 * k.value.hat.hat + d;
    }
    return -1;
}

static int find_virtual(const CxInput *c) {
    for (int i = 0; i < CB_MAX_PADS; i++) {
        if (!c->joy[i]) continue;
        for (int di = 0; di < SDL_NumJoysticks(); di++)
            if (SDL_JoystickIsVirtual(di) && SDL_JoystickGetDeviceInstanceID(di) == SDL_JoystickInstanceID(c->joy[i])) return i;
    }
    return -1;
}

int main(void) {
    setenv("HOME", "build/controls_sdl_tmp", 1);
    setenv("XDG_CONFIG_HOME", "/tmp/claude-1000/controls_sdl_cfg_unused", 0);
    unsetenv("XDG_CONFIG_HOME");
    unsetenv("XDG_DATA_HOME");
    if (SDL_Init(SDL_INIT_JOYSTICK) != 0) { printf("controls_sdl_test: SKIP (%s)\n", SDL_GetError()); return 0; }
    int vi = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 12, 1);
    if (vi < 0) { printf("controls_sdl_test: SKIP (no virtual joystick: %s)\n", SDL_GetError()); SDL_Quit(); return 0; }
    if (system("rm -rf build/controls_sdl_tmp")) return 1;

    CxInput *c = (CxInput *)calloc(1, sizeof *c);
    cx_init(c);
    int vp = find_virtual(c);                       /* the machine may have real devices too: find ours */
    CHECK(vp >= 0 && c->b.pad[vp].present, "the virtual pad is among the pads");
    if (vp < 0) { printf("controls_sdl_test: %d checks, %d failed\n", g_checks, g_fail); return 1; }
    for (int i = 0; i < CB_MAX_PADS; i++) if (i != vp) c->b.pad[i].present = 0;   /* other devices must not colour the lookups */
    CHECK(c->b.pad[vp].info.buttons == 12 && c->b.pad[vp].info.axes_mask == 0x3F && !c->b.pad[vp].info.wheel, "pad info: buttons %d mask %x",
          c->b.pad[vp].info.buttons, c->b.pad[vp].info.axes_mask);
    const StInputDevice *d = &c->b.pad[vp].dev;
    /* the standard layout: every element SDL maps resolves to its action, through whatever raw index the mapping gives */
    static const int want[CB_STD_COUNT] = { CB_ACT_THROTTLE, CB_ACT_LOOK_BACK, CB_ACT_BRAKE, CB_ACT_CAMERA, CB_ACT_RESET_CAR, CB_ACT_NONE,
        CB_ACT_HYD_PRESSURE, CB_ACT_HYD_BOUNCE, CB_ACT_NITROUS, CB_ACT_HANDBRAKE, CB_ACT_SHIFT_DOWN, CB_ACT_SHIFT_UP,
        CB_ACT_ENTER_RACE, CB_ACT_WORLD_MAP, CB_ACT_STEER_LEFT, CB_ACT_STEER_RIGHT };
    static const SDL_GameControllerButton sb[CB_STD_COUNT] = { SDL_CONTROLLER_BUTTON_A, SDL_CONTROLLER_BUTTON_B, SDL_CONTROLLER_BUTTON_X, SDL_CONTROLLER_BUTTON_Y,
        SDL_CONTROLLER_BUTTON_BACK, SDL_CONTROLLER_BUTTON_START, SDL_CONTROLLER_BUTTON_LEFTSTICK, SDL_CONTROLLER_BUTTON_RIGHTSTICK,
        SDL_CONTROLLER_BUTTON_LEFTSHOULDER, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, SDL_CONTROLLER_BUTTON_INVALID, SDL_CONTROLLER_BUTTON_INVALID,
        SDL_CONTROLLER_BUTTON_DPAD_UP, SDL_CONTROLLER_BUTTON_DPAD_DOWN, SDL_CONTROLLER_BUTTON_DPAD_LEFT, SDL_CONTROLLER_BUTTON_DPAD_RIGHT };
    const CbPadInfo *pi = &c->b.pad[vp].info;
    CHECK(c->gc[vp] && pi->std, "the virtual controller has a mapping: the standard layout is on");
    if (!c->gc[vp] || !pi->std) { printf("controls_sdl_test: %d checks, %d failed\n", g_checks, g_fail + 1); return 1; }
    int nbound = 0;
    for (int e = 0; e < CB_STD_COUNT; e++) {
        int idx = sb[e] == SDL_CONTROLLER_BUTTON_INVALID ? -1 : sdl_index(c->gc[vp], sb[e]);
        SDL_GameControllerButtonBind tb = SDL_GameControllerGetBindForAxis(c->gc[vp], e == CB_STD_L2 ? SDL_CONTROLLER_AXIS_TRIGGERLEFT : SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        if (e == CB_STD_L2 || e == CB_STD_R2) idx = tb.bindType == SDL_CONTROLLER_BINDTYPE_AXIS ? CB_VBTN_FIRST + (e == CB_STD_R2) : sdl_index(c->gc[vp], SDL_CONTROLLER_BUTTON_INVALID);
        CHECK(pi->std_ctl[e] == idx, "std element %d resolves to control %d (SDL says %d)", e, pi->std_ctl[e], idx);
        if (idx >= 0) { CHECK(d->binding[idx] == want[e], "std element %d (control %d) -> action %d, got %d", e, idx, want[e], d->binding[idx]); if (want[e]) nbound++; }
    }
    CHECK(nbound >= 12, "at least the 12 buttons and triggers the virtual mapping covers are bound (%d)", nbound);
    CHECK(pi->std_ctl[CB_STD_START] < 0 || d->binding[pi->std_ctl[CB_STD_START]] == CB_ACT_NONE, "Start is unbound");
    CHECK(pi->vbtn_axis[0] == 4 && pi->vbtn_axis[1] == 5 && pi->std_ctl[CB_STD_R2] == 15, "triggers are axes 4 and 5, virtual buttons 14 and 15");
    CHECK(d->assigned[CB_ROLE_STEER] && d->axis[CB_ROLE_STEER] == 0 && d->assigned[CB_ROLE_COMBINED] && d->axis[CB_ROLE_COMBINED] == 4 * pi->std_pedal_axis &&
          pi->std_pedal_axis == 3 && d->combined_mode && d->dir[CB_ROLE_COMBINED] == 1, "left stick X steers, right stick Y is the combined pedals");
    int south = pi->std_ctl[CB_STD_SOUTH];
    d = &c->b.pad[vp].dev;

    /* names: no platform name for a raw axis without a controller mapping -> NULL */
    CmPlatform plat;
    cx_platform(c, &plat);
    CHECK(plat.key_name(plat.user, SDL_SCANCODE_W) && !strcmp(plat.key_name(plat.user, SDL_SCANCODE_W), "W"), "SDL key name");
    CbCtl axis = { CB_CTL_AXIS, 0 };
    (void)plat.ctl_name(plat.user, vp, axis);        /* must not crash whatever the mapping says */

    /* samples and lookups */
    SDL_Joystick *j = c->joy[vp];
    SDL_JoystickSetVirtualButton(j, south, 1);      /* A: Throttle */
    SDL_JoystickUpdate();
    CbPadSample s;
    cx_sample(c, vp, &s);
    CHECK((s.buttons & (1u << south)) && s.pov[0] == -1, "sample: button A down, hat centred");
    CHECK(cx_action_down(c, NULL, CB_ACT_THROTTLE) && !cx_action_down(c, NULL, CB_ACT_BRAKE), "pad button -> Throttle");
    CHECK(cx_throttle(c, NULL) == 1.0f, "digital throttle");
    SDL_JoystickSetVirtualButton(j, south, 0);
    c->b.pad[vp].dev.binding[CB_BIND_BUTTONS + CB_DIR_LEFT] = CB_ACT_STEER_LEFT;    /* the virtual mapping has no D-pad hat: bind the POV by hand */
    c->b.pad[vp].dev.binding[CB_BIND_BUTTONS + CB_DIR_RIGHT] = CB_ACT_STEER_RIGHT;
    SDL_JoystickSetVirtualHat(j, 0, SDL_HAT_LEFT);  /* Steering (Left) */
    SDL_JoystickUpdate();
    cx_sample(c, vp, &s);
    CHECK(s.pov[0] == 27000, "hat left is 27000 centidegrees");
    CHECK(cx_steer(c, NULL) == 1.0f, "POV left steers left (+1)");
    SDL_JoystickSetVirtualHat(j, 0, SDL_HAT_RIGHTUP);
    SDL_JoystickUpdate();
    cx_sample(c, vp, &s);
    CHECK(s.pov[0] == 4500 && cb_pov_direction(s.pov[0]) == CB_DIR_RIGHT, "a diagonal reads as right");
    CHECK(cx_steer(c, NULL) == -1.0f, "POV right steers right (-1)");
    SDL_JoystickSetVirtualHat(j, 0, SDL_HAT_CENTERED);
    /* axes: steering X with direction +1: positive = right */
    SDL_JoystickSetVirtualAxis(j, 0, 32767);
    SDL_JoystickUpdate();
    CHECK(cx_steer(c, NULL) < -0.99f, "X full right steers -1");
    SDL_JoystickSetVirtualAxis(j, 0, -32768);
    SDL_JoystickUpdate();
    CHECK(cx_steer(c, NULL) > 0.99f, "X full left steers +1");
    SDL_JoystickSetVirtualAxis(j, 0, 1000);
    SDL_JoystickUpdate();
    CHECK(cx_steer(c, NULL) == 0.0f, "inside the dead zone");
    /* combined pedals on the right stick Y (axis 3), direction +1: negative output = throttle (stick up) */
    SDL_JoystickSetVirtualAxis(j, 3, -32768);
    SDL_JoystickUpdate();
    CHECK(cx_throttle(c, NULL) > 0.99f, "combined axis pushed negative: throttle");
    SDL_JoystickSetVirtualAxis(j, 3, 32767);
    SDL_JoystickUpdate();
    CHECK(cx_throttle(c, NULL) < -0.99f, "combined axis pushed positive: brake");
    SDL_JoystickSetVirtualAxis(j, 3, 0);
    SDL_JoystickSetVirtualAxis(j, 0, 0);
    SDL_JoystickUpdate();
    /* the right trigger is virtual button 15: Shift Up, one edge per press */
    CHECK(!cx_action_down(c, NULL, CB_ACT_SHIFT_UP), "trigger released: no Shift Up");
    SDL_JoystickSetVirtualAxis(j, 5, 32767);
    SDL_JoystickUpdate();
    CHECK(cx_action_down(c, NULL, CB_ACT_SHIFT_UP) && !cx_action_down(c, NULL, CB_ACT_SHIFT_DOWN), "right trigger past the threshold: Shift Up held");
    cx_sample(c, vp, &s);
    CHECK((s.buttons & (1u << 15)) && !(s.buttons & (1u << 14)), "cx_sample reports virtual button 15 only");
    {
        SDL_Event te;
        memset(&te, 0, sizeof te);
        te.type = SDL_JOYAXISMOTION; te.jaxis.which = SDL_JoystickInstanceID(j); te.jaxis.axis = 5; te.jaxis.value = 20000;
        CHECK(cx_joy_edges(c, &te) == (1u << CB_ACT_SHIFT_UP), "trigger press: exactly one Shift Up edge");
        te.jaxis.value = 32767;
        CHECK(cx_joy_edges(c, &te) == 0, "trigger still held and moving: no repeat");
        te.jaxis.value = 0;
        CHECK(cx_joy_edges(c, &te) == 0, "trigger released: nothing");
        te.jaxis.value = 30000;
        CHECK(cx_joy_edges(c, &te) == (1u << CB_ACT_SHIFT_UP), "pressed again: a second edge");
        te.jaxis.value = 0;
        (void)cx_joy_edges(c, &te);
        te.jaxis.axis = 3; te.jaxis.value = 30000;
        CHECK(cx_joy_edges(c, &te) == 0, "a stick axis is not a trigger");
    }
    SDL_JoystickSetVirtualAxis(j, 5, 0);
    SDL_JoystickSetVirtualAxis(j, 4, 32767);
    SDL_JoystickUpdate();
    cx_sample(c, vp, &s);
    CHECK(cx_action_down(c, NULL, CB_ACT_SHIFT_DOWN) && (s.buttons & (1u << 14)), "left trigger: Shift Down, virtual button 14");
    SDL_JoystickSetVirtualAxis(j, 4, 0);
    SDL_JoystickUpdate();
    CHECK(!cx_action_down(c, NULL, CB_ACT_SHIFT_DOWN), "left trigger released");
    /* keyboard table */
    Uint8 keys[SDL_NUM_SCANCODES];
    memset(keys, 0, sizeof keys);
    keys[SDL_SCANCODE_W] = 1;
    CHECK(cx_action_down(c, keys, CB_ACT_THROTTLE) && cx_throttle(c, keys) == 1.0f, "W is the Throttle secondary");
    keys[SDL_SCANCODE_W] = 0;
    keys[SDL_SCANCODE_DOWN] = 1; keys[SDL_SCANCODE_LEFT] = 1;
    CHECK(cx_throttle(c, keys) == -1.0f && cx_steer(c, keys) == 1.0f, "Down brakes, Left steers left");
    CHECK(cx_key_is(c, SDL_SCANCODE_ESCAPE, CB_ACT_PAUSE) && cx_key_is(c, SDL_SCANCODE_P, CB_ACT_PAUSE) && !cx_key_is(c, SDL_SCANCODE_Q, CB_ACT_PAUSE),
          "Pause is Escape and P");
    CHECK(cx_key_bound(c, SDL_SCANCODE_K) && cx_key_bound(c, SDL_SCANCODE_PERIOD) && !cx_key_bound(c, SDL_SCANCODE_G), "developer keys: K and . are bound, G is not");
    CHECK(cx_joy_button_is(c, SDL_JoystickInstanceID(j), south, CB_ACT_THROTTLE) && !cx_joy_button_is(c, SDL_JoystickInstanceID(j), south, CB_ACT_PAUSE), "button event lookup");

    /* rebind, save, restart */
    cb_kb_capture(&c->b.kb, CB_ACT_NITROUS, 0, SDL_SCANCODE_N);
    cb_pad_assign_control(&c->b.pad[vp].dev, CB_ACT_NITROUS, 0, 1);
    for (int i = 0; i < CB_MAX_PADS; i++) if (c->joy[i] && i != vp) c->b.pad[i].present = 0;
    CHECK(cx_save(c) == 0, "save");
    cx_shutdown(c);
    memset(c, 0, sizeof *c);
    cx_init(c);
    vp = find_virtual(c);
    CHECK(vp >= 0, "the pad is found again");
    if (vp < 0) vp = 0;
    CHECK(cx_key_is(c, SDL_SCANCODE_N, CB_ACT_NITROUS) && !cx_key_is(c, SDL_SCANCODE_LALT, CB_ACT_NITROUS), "keyboard binding survives a restart");
    CHECK(c->b.pad[vp].present && c->b.pad[vp].dev.binding[1] == CB_ACT_NITROUS && c->b.pad[vp].dev.binding[7] == CB_ACT_HYD_PRESSURE, "pad record survives a restart");

    /* review 1: the controls store and a longer-lived store on the same INI (the graphics store) must not
     * overwrite each other's values */
    char cfg[1024];
    StRegStore gs, fs;
    CHECK(st_default_config_path(cfg, sizeof cfg) == ST_OK && st_reg_open_ini(&gs, cfg) == ST_OK, "open the long-lived store first");
    cb_kb_capture(&c->b.kb, CB_ACT_CAMERA, 0, SDL_SCANCODE_Y);
    CHECK(cx_save(c) == 0, "controls save after the other store was opened");
    CHECK(st_reg_set_dword(&gs, "TestGraphicsValue", 7) == ST_OK, "the older store writes afterwards (display Back)");
    st_reg_close(&gs);
    CbBindings *lb = (CbBindings *)calloc(1, sizeof *lb);
    cb_bindings_defaults(lb);
    uint32_t gv = 0;
    CHECK(st_reg_open_ini(&fs, cfg) == ST_OK && cs_kb_load(&fs, &lb->kb) && st_reg_get_dword(&fs, "TestGraphicsValue", &gv) && gv == 7,
          "both stores' values are in the file");
    st_reg_close(&fs);
    CHECK(cx_key_is(c, SDL_SCANCODE_Y, CB_ACT_CAMERA), "camera key in memory");
    int kk[2];
    CHECK(cb_kb_keys(&lb->kb, CB_ACT_CAMERA, kk) >= 1 && kk[0] == SDL_SCANCODE_Y, "the keyboard binding survived the older store's write");
    CHECK(cb_kb_keys(&lb->kb, CB_ACT_NITROUS, kk) >= 1 && kk[0] == SDL_SCANCODE_N, "the earlier binding survived too");
    free(lb);

    /* review 2: a profile that exists but cannot be loaded is never overwritten; an absent one is created */
    char ppath[1100];
    snprintf(ppath, sizeof ppath, "%s/OPENUG2/OPENUG2", c->profile_root);
    cx_shutdown(c);
    memset(c, 0, sizeof *c);
    {
        FILE *pf = fopen(ppath, "wb");
        CHECK(pf != NULL, "profile file path");
        if (pf) { fwrite("not a profile file at all, kept as is", 1, 37, pf); fclose(pf); }
    }
    cx_init(c);
    CHECK(!c->profile_writable, "a corrupt profile is not writable");
    cb_kb_capture(&c->b.kb, CB_ACT_HUD, 0, SDL_SCANCODE_U);
    CHECK(cx_save(c) != 0, "pad bindings could not be saved");
    {
        char buf[64] = { 0 };
        FILE *pf = fopen(ppath, "rb");
        size_t n = pf ? fread(buf, 1, sizeof buf, pf) : 0;
        if (pf) fclose(pf);
        CHECK(n == 37 && !memcmp(buf, "not a profile file at all, kept as is", 37), "the corrupt profile's bytes are unchanged");
    }
    StRegStore ks;
    CbBindings *kb2 = (CbBindings *)calloc(1, sizeof *kb2);
    cb_bindings_defaults(kb2);
    CHECK(st_reg_open_ini(&ks, cfg) == ST_OK && cs_kb_load(&ks, &kb2->kb) && cb_kb_keys(&kb2->kb, CB_ACT_HUD, kk) >= 1 && kk[0] == SDL_SCANCODE_U,
          "the keyboard bindings are still saved");
    st_reg_close(&ks);
    free(kb2);
    cx_shutdown(c);
    memset(c, 0, sizeof *c);
    CHECK(remove(ppath) == 0, "remove the garbage profile");
    cx_init(c);
    CHECK(c->profile_writable, "an absent profile may be created");
    CHECK(cx_save(c) == 0, "and is created by a save");
    {
        FILE *pf = fopen(ppath, "rb");
        CHECK(pf != NULL, "profile file now exists");
        if (pf) fclose(pf);
    }
    vp = find_virtual(c);
    if (vp < 0) vp = 0;
    j = c->joy[vp];

#ifndef _WIN32
    /* A transient directory read failure must not turn an existing profile into a new one. */
    c->profile->career_difficulty = 1;
    CHECK(cx_save(c) == 0, "save non-default profile before permission failure");
    char pdir[1100];
    snprintf(pdir, sizeof pdir, "%s/OPENUG2", c->profile_root);
    static unsigned char before[ST_PROFILE_FILE_SIZE], after[ST_PROFILE_FILE_SIZE];
    FILE *pf = fopen(ppath, "rb");
    CHECK(pf && fread(before, 1, ST_PROFILE_FILE_SIZE, pf) == ST_PROFILE_FILE_SIZE,
          "snapshot the valid profile bytes");
    if (pf) fclose(pf);
    cx_shutdown(c);
    CHECK(chmod(pdir, 0300) == 0, "temporarily deny directory enumeration");
    cx_init(c);
    int writable = c->profile_writable;
    int status = st_profile_status(c->profile_root, CS_PROFILE_NAME);
    CHECK(chmod(pdir, 0700) == 0, "restore profile directory access");
    CHECK(!writable && status == ST_E_IO, "unreadable existing profile must not be writable or absent");
    cb_kb_capture(&c->b.kb, CB_ACT_HUD, 0, SDL_SCANCODE_I);
    CHECK(cx_save(c) != 0, "save reports unavailable profile after access returns");
    pf = fopen(ppath, "rb");
    CHECK(pf && fread(after, 1, ST_PROFILE_FILE_SIZE, pf) == ST_PROFILE_FILE_SIZE &&
          !memcmp(before, after, ST_PROFILE_FILE_SIZE), "valid profile bytes survive the failed load");
    if (pf) fclose(pf);
    cb_bindings_defaults(&c->b);
    CHECK(st_reg_open_ini(&ks, cfg) == ST_OK && cs_kb_load(&ks, &c->b.kb) &&
          cb_kb_keys(&c->b.kb, CB_ACT_HUD, kk) >= 1 && kk[0] == SDL_SCANCODE_I,
          "keyboard save remains independent of profile failure");
    st_reg_close(&ks);
    cx_shutdown(c);
    cx_init(c);
    CHECK(c->profile->career_difficulty == 1, "non-default career value survives restart");
    vp = find_virtual(c);
    if (vp < 0) vp = 0;
    j = c->joy[vp];
#endif

    /* review 3: pad presses of once-per-press actions, one edge per press */
    cb_pad_assign_control(&c->b.pad[vp].dev, CB_ACT_CAMERA, 0, 4);
    cb_pad_assign_control(&c->b.pad[vp].dev, CB_ACT_SHIFT_UP, 0, 5);
    SDL_Event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = SDL_JOYBUTTONDOWN; ev.jbutton.which = SDL_JoystickInstanceID(j); ev.jbutton.button = 4;
    CHECK(cx_joy_edges(c, &ev) == (1u << CB_ACT_CAMERA), "camera button press: one camera edge");
    ev.type = SDL_JOYBUTTONUP;
    CHECK(cx_joy_edges(c, &ev) == 0, "release: nothing");
    ev.type = SDL_JOYBUTTONDOWN;
    CHECK(cx_joy_edges(c, &ev) == (1u << CB_ACT_CAMERA), "repress: another edge");
    ev.jbutton.button = 5;
    CHECK(cx_joy_edges(c, &ev) == (1u << CB_ACT_SHIFT_UP), "shift up button");
    ev.jbutton.which = SDL_JoystickInstanceID(j) + 1000;
    CHECK(cx_joy_edges(c, &ev) == 0, "another device's button: nothing");
    memset(&ev, 0, sizeof ev);
    ev.type = SDL_JOYHATMOTION; ev.jhat.which = SDL_JoystickInstanceID(j); ev.jhat.hat = 0;
    c->b.pad[vp].dev.binding[CB_BIND_BUTTONS + CB_DIR_UP] = CB_ACT_CAMERA;
    c->b.pad[vp].dev.binding[CB_BIND_BUTTONS + CB_DIR_LEFT] = CB_ACT_STEER_LEFT;     /* the virtual mapping has no D-pad hat */
    c->b.pad[vp].dev.binding[CB_BIND_BUTTONS + CB_DIR_RIGHT] = CB_ACT_STEER_RIGHT;
    ev.jhat.value = SDL_HAT_UP;
    CHECK(cx_joy_edges(c, &ev) == (1u << CB_ACT_CAMERA), "POV up pressed: one edge");
    CHECK(cx_joy_edges(c, &ev) == 0, "POV still held: no repeat");
    ev.jhat.value = SDL_HAT_RIGHTUP;
    CHECK(cx_joy_edges(c, &ev) == (1u << CB_ACT_STEER_RIGHT), "adding a direction: only the new one is an edge");
    ev.jhat.value = SDL_HAT_CENTERED;
    CHECK(cx_joy_edges(c, &ev) == 0, "POV released: nothing");
    ev.jhat.value = SDL_HAT_UP;
    CHECK(cx_joy_edges(c, &ev) == (1u << CB_ACT_CAMERA), "POV repressed: another edge");
    ev.jhat.value = SDL_HAT_LEFT;
    CHECK(cx_joy_edges(c, &ev) == (1u << CB_ACT_STEER_LEFT), "POV left: the steering action (lane change in drag)");

    /* review 4: a key bound to Camera or a drag shift is not an overlay key */
    CHECK(!cx_key_bound(c, SDL_SCANCODE_1), "1 is free by default");
    cb_kb_capture(&c->b.kb, CB_ACT_CAMERA, 0, SDL_SCANCODE_1);
    CHECK(cx_key_bound(c, SDL_SCANCODE_1) && cx_key_is(c, SDL_SCANCODE_1, CB_ACT_CAMERA), "1 bound to Camera is reported bound");
    cb_kb_capture(&c->b.kb, CB_ACT_SHIFT_UP, 0, SDL_SCANCODE_1);
    CHECK(cx_key_bound(c, SDL_SCANCODE_1) && cx_key_is(c, SDL_SCANCODE_1, CB_ACT_SHIFT_UP), "1 bound to Shift Up is reported bound");
    cx_shutdown(c);
    free(c);
    SDL_JoystickDetachVirtual(vi);
    SDL_Quit();
    printf("controls_sdl_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
