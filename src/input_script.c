/* input_script.c -- see input_script.h. */
#include "input_script.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_err(char *err, size_t n, const char *fmt, const char *tok) {
    if (err && n) snprintf(err, n, fmt, tok ? tok : "");
}

static const char *const TC_NAMES[] = { "steer_l", "steer_r", "gas", "brake", "nos", "handbrake", "camera",
                                        "pause", "shift_up", "shift_down", "enter_race" };

static int parse_long(const char *s, long *v, const char **end) {
    char *e;
    long r;
    if (*s < '0' || *s > '9') return 0;
    r = strtol(s, &e, 10);
    *v = r;
    *end = e;
    return 1;
}

static int parse_float(const char *s, float *v, const char **end) {
    char *e;
    double r = strtod(s, &e);
    if (e == s) return 0;
    *v = (float)r;
    *end = e;
    return 1;
}

static int tc_button(const char *s) {
    unsigned i;
    long v;
    const char *e;
    if (parse_long(s, &v, &e) && !*e) return v < 64 ? (int)v : -1;
    for (i = 0; i < sizeof TC_NAMES / sizeof *TC_NAMES; i++)
        if (strcmp(s, TC_NAMES[i]) == 0) return (int)i;
    return -1;
}

/* One token "F:what" (modified in place). */
static int parse_token(InputScript *s, char *tok, char *err, size_t en) {
    IsItem *it;
    long fr;
    const char *p, *e;
    char *val;
    char name[64];
    if (s->n >= IS_MAX_ITEMS) { set_err(err, en, "too many commands (max %d)", "512"); return 0; }
    if (!parse_long(tok, &fr, &p) || *p != ':') { set_err(err, en, "bad command '%s' (expected FRAME:what)", tok); return 0; }
    p++;
    it = &s->item[s->n];
    memset(it, 0, sizeof *it);
    it->frame = fr;
    if (strcmp(p, "quit") == 0) {
        it->kind = IS_QUIT;
    } else {
        val = strchr((char *)p, '=');
        if (!val) { set_err(err, en, "bad command '%s' (missing '=')", tok); return 0; }
        *val++ = 0;
        if (!*val) { set_err(err, en, "empty value in '%s'", tok); return 0; }
        if (strcmp(p, "key") == 0) {
            SDL_Keycode k;
            size_t i;
            snprintf(name, sizeof name, "%s", val);
            k = SDL_GetKeyFromName(name);
            if (k == SDLK_UNKNOWN) {
                for (i = 0; name[i]; i++) if (name[i] == '_') name[i] = ' ';
                k = SDL_GetKeyFromName(name);
            }
            if (k == SDLK_UNKNOWN) { set_err(err, en, "unknown key name '%s'", val); return 0; }
            it->kind = IS_KEY;
            it->a = (int)k;
            it->scancode = (int)SDL_GetScancodeFromKey(k);
            if (it->scancode == SDL_SCANCODE_UNKNOWN) it->scancode = (int)SDL_GetScancodeFromName(name);
        } else if (strcmp(p, "pad") == 0) {
            SDL_GameControllerButton b = SDL_GameControllerGetButtonFromString(val);
            if (b == SDL_CONTROLLER_BUTTON_INVALID) { set_err(err, en, "unknown pad button '%s'", val); return 0; }
            it->kind = IS_PAD;
            it->a = (int)b;
        } else if (strcmp(p, "click") == 0) {
            if (!parse_float(val, &it->x, &e) || *e != ':' || !parse_float(e + 1, &it->y, &e) || *e) {
                set_err(err, en, "bad click position '%s' (expected X:Y)", val); return 0;
            }
            if (it->x < 0 || it->x > 1 || it->y < 0 || it->y > 1) { set_err(err, en, "click position '%s' out of 0..1", val); return 0; }
            it->kind = IS_CLICK;
        } else if (strcmp(p, "fdown") == 0) {
            long id;
            if (!parse_long(val, &id, &e) || *e != ':' || !parse_float(e + 1, &it->x, &e) || *e != ':' ||
                !parse_float(e + 1, &it->y, &e) || *e) {
                set_err(err, en, "bad fdown '%s' (expected ID:X:Y)", val); return 0;
            }
            if (it->x < 0 || it->x > 1 || it->y < 0 || it->y > 1) { set_err(err, en, "fdown position '%s' out of 0..1", val); return 0; }
            it->kind = IS_FDOWN;
            it->a = (int)id;
        } else if (strcmp(p, "fup") == 0) {
            long id;
            if (!parse_long(val, &id, &e) || *e) { set_err(err, en, "bad fup id '%s'", val); return 0; }
            it->kind = IS_FUP;
            it->a = (int)id;
        } else if (strcmp(p, "tdown") == 0 || strcmp(p, "tup") == 0) {
            int b = tc_button(val);
            if (b < 0) { set_err(err, en, "unknown touch button '%s'", val); return 0; }
            it->kind = p[1] == 'd' ? IS_TDOWN : IS_TUP;
            it->a = b;
        } else if (strcmp(p, "shot") == 0) {
            if (strlen(val) >= IS_PATH_MAX) { set_err(err, en, "shot path too long: '%s'", val); return 0; }
            it->kind = IS_SHOT;
            snprintf(it->path, sizeof it->path, "%s", val);
        } else {
            set_err(err, en, "unknown command '%s'", p);
            return 0;
        }
    }
    s->n++;
    return 1;
}

static int parse_text(InputScript *s, char *text, char *err, size_t en) {
    char *p = text;
    int in_comment = 0;
    while (*p) {
        char *tok;
        if (in_comment) { if (*p == '\n') in_comment = 0; p++; continue; }
        if (*p == '#') { in_comment = 1; p++; continue; }
        if (*p == ',' || *p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { p++; continue; }
        tok = p;
        while (*p && *p != ',' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') p++;
        if (*p) *p++ = 0;
        if (!parse_token(s, tok, err, en)) return 0;
    }
    return 1;
}

int is_parse(InputScript *s, const char *spec, char *err, size_t n) {
    char *buf;
    int ok;
    memset(s, 0, sizeof *s);
    if (err && n) err[0] = 0;
    if (!spec || !*spec) { set_err(err, n, "empty input script%s", ""); return 0; }
    if (spec[0] == '@') {
        FILE *f = fopen(spec + 1, "rb");
        long len;
        size_t got;
        if (!f) { set_err(err, n, "cannot open script file '%s'", spec + 1); return 0; }
        fseek(f, 0, SEEK_END);
        len = ftell(f);
        fseek(f, 0, SEEK_SET);
        if (len < 0 || len > (1 << 20)) { fclose(f); set_err(err, n, "script file '%s' too large or unreadable", spec + 1); return 0; }
        buf = (char *)malloc((size_t)len + 1);
        if (!buf) { fclose(f); set_err(err, n, "out of memory%s", ""); return 0; }
        got = fread(buf, 1, (size_t)len, f);
        fclose(f);
        buf[got] = 0;
    } else {
        buf = (char *)malloc(strlen(spec) + 1);
        if (!buf) { set_err(err, n, "out of memory%s", ""); return 0; }
        strcpy(buf, spec);
    }
    ok = parse_text(s, buf, err, n);
    free(buf);
    if (ok && s->n == 0) { set_err(err, n, "input script has no commands%s", ""); ok = 0; }
    if (!ok) s->n = 0;
    return ok;
}

static int px(float f, int dim) { return (int)(f * (float)dim + 0.5f); }

static int emit(const IsItem *it, int release, SDL_Event *out, int *cnt, int max, int w, int h) {
    SDL_Event e;
    int need = (it->kind == IS_CLICK && !release) ? 2 : 1;
    if (*cnt + need > max) return 0;
    memset(&e, 0, sizeof e);
    switch (it->kind) {
    case IS_KEY:
        e.type = release ? SDL_KEYUP : SDL_KEYDOWN;
        e.key.state = release ? SDL_RELEASED : SDL_PRESSED;
        e.key.keysym.sym = (SDL_Keycode)it->a;
        e.key.keysym.scancode = (SDL_Scancode)it->scancode;
        break;
    case IS_PAD:
        e.type = release ? SDL_CONTROLLERBUTTONUP : SDL_CONTROLLERBUTTONDOWN;
        e.cbutton.which = 0;
        e.cbutton.button = (Uint8)it->a;
        e.cbutton.state = release ? SDL_RELEASED : SDL_PRESSED;
        break;
    case IS_CLICK:
        if (!release) {
            e.type = SDL_MOUSEMOTION;
            e.motion.which = SDL_TOUCH_MOUSEID;
            e.motion.x = px(it->x, w);
            e.motion.y = px(it->y, h);
            out[(*cnt)++] = e;
            memset(&e, 0, sizeof e);
        }
        e.type = release ? SDL_MOUSEBUTTONUP : SDL_MOUSEBUTTONDOWN;
        e.button.which = SDL_TOUCH_MOUSEID;
        e.button.button = SDL_BUTTON_LEFT;
        e.button.state = release ? SDL_RELEASED : SDL_PRESSED;
        e.button.clicks = 1;
        e.button.x = px(it->x, w);
        e.button.y = px(it->y, h);
        break;
    default:
        return 1;
    }
    out[(*cnt)++] = e;
    return 1;
}

int is_events(InputScript *s, long frame, SDL_Event *out, int max, int win_w, int win_h) {
    int cnt = 0, i;
    if (!s || !out || max <= 0) return 0;
    /* releases of taps started on the previous frame */
    for (i = 0; i < s->n; i++) {
        const IsItem *it = &s->item[i];
        if (it->frame + 1 == frame && (it->kind == IS_KEY || it->kind == IS_PAD || it->kind == IS_CLICK))
            if (!emit(it, 1, out, &cnt, max, win_w, win_h)) return cnt;
    }
    for (i = 0; i < s->n; i++) {
        const IsItem *it = &s->item[i];
        SDL_Event e;
        if (it->frame != frame) continue;
        switch (it->kind) {
        case IS_KEY: case IS_PAD: case IS_CLICK:
            if (!emit(it, 0, out, &cnt, max, win_w, win_h)) return cnt;
            break;
        case IS_FDOWN: case IS_FUP:
            if (cnt >= max) return cnt;
            memset(&e, 0, sizeof e);
            e.type = it->kind == IS_FDOWN ? SDL_FINGERDOWN : SDL_FINGERUP;
            e.tfinger.touchId = 1;
            e.tfinger.fingerId = (SDL_FingerID)it->a;
            e.tfinger.x = it->x;
            e.tfinger.y = it->y;
            e.tfinger.pressure = it->kind == IS_FDOWN ? 1.0f : 0.0f;
            out[cnt++] = e;
            break;
        case IS_TDOWN: case IS_TUP:
            if (cnt >= max) return cnt;
            memset(&e, 0, sizeof e);
            e.type = SDL_USEREVENT;
            e.user.code = it->kind == IS_TDOWN ? 1 : 0;
            e.user.data1 = (void *)(intptr_t)it->a;
            out[cnt++] = e;
            break;
        default:
            break;
        }
    }
    return cnt;
}

const char *is_shot(const InputScript *s, long frame) {
    int i;
    for (i = 0; s && i < s->n; i++)
        if (s->item[i].kind == IS_SHOT && s->item[i].frame == frame) return s->item[i].path;
    return NULL;
}

int is_quit(const InputScript *s, long frame) {
    int i;
    for (i = 0; s && i < s->n; i++)
        if (s->item[i].kind == IS_QUIT && frame >= s->item[i].frame) return 1;
    return 0;
}
