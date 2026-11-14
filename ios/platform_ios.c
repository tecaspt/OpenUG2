/* platform_ios.c - iOS/iPadOS startup and lifecycle hooks (see src/platform_ios.h).
 * C99 + SDL + POSIX only, so it also compiles on Linux for the ios-host check. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#include "../src/platform_ios.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

#define IOS_PATH_MAX 1024
#define IOS_MAX_ARGS 64
#define IOS_MAX_DIRS 64

static const char *const k_required[] = {
    "TRACKS/STREAML4RA.BUN", "TRACKS/L4RA.BUN", "GLOBAL/GLOBALB.BUN",
    "GLOBAL/InGameCommon.bun", "CARS/HUMMER/GEOMETRY.BIN",
};
#define N_REQUIRED ((int)(sizeof k_required / sizeof k_required[0]))

static SDL_atomic_t g_background;

static int ios_event_watch(void *ud, SDL_Event *e) {
    (void)ud;
    switch (e->type) {
    case SDL_APP_WILLENTERBACKGROUND:
    case SDL_APP_DIDENTERBACKGROUND:
        SDL_AtomicSet(&g_background, 1);
        break;
    case SDL_APP_DIDENTERFOREGROUND:
        SDL_AtomicSet(&g_background, 0);
        break;
    default:
        break;
    }
    return 1;
}

void n2_ios_frame_gate(void) {
    while (SDL_AtomicGet(&g_background)) { SDL_PumpEvents(); SDL_Delay(50); }
}

static int copy_str(char *dst, size_t cap, const char *src) {
    size_t n = strlen(src);
    if (n >= cap) return 0;
    memcpy(dst, src, n + 1);
    return 1;
}

static int is_file(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int is_dir(const char *path) {
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

/* Exact-case check of every required file under root. */
static int root_valid(const char *root) {
    char p[IOS_PATH_MAX];
    for (int i = 0; i < N_REQUIRED; i++) {
        if (snprintf(p, sizeof p, "%s/%s", root, k_required[i]) >= (int)sizeof p) return 0;
        if (!is_file(p)) return 0;
    }
    return 1;
}

/* Resolve rel under root ignoring case, one path part at a time; writes the
 * on-disk spelling (relative to root) to out. Returns 1 if found. */
static int resolve_ci(const char *root, const char *rel, char *out, size_t cap) {
    char cur[IOS_PATH_MAX], part[256];
    out[0] = 0;
    if (!copy_str(cur, sizeof cur, root)) return 0;
    const char *s = rel;
    while (*s) {
        size_t n = strcspn(s, "/");
        if (n == 0 || n >= sizeof part) return 0;
        memcpy(part, s, n); part[n] = 0;
        DIR *d = opendir(cur);
        if (!d) return 0;
        char found[256] = "";
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (!strcasecmp(de->d_name, part)) {
                if (!found[0] || !strcmp(de->d_name, part)) snprintf(found, sizeof found, "%s", de->d_name);
            }
        }
        closedir(d);
        if (!found[0]) return 0;
        size_t l = strlen(cur);
        if (l + 1 + strlen(found) >= sizeof cur) return 0;
        snprintf(cur + l, sizeof cur - l, "/%s", found);
        l = strlen(out);
        snprintf(out + l, cap - l, "%s%s", l ? "/" : "", found);
        s += n;
        if (*s == '/') s++;
    }
    return 1;
}

static int cmp_str(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* Candidate roots: Documents/NFSU2, Documents, then each visible subfolder. */
static int collect_candidates(const char *docs, char cand[][IOS_PATH_MAX], int max) {
    int n = 0;
    if (snprintf(cand[n], IOS_PATH_MAX, "%s/NFSU2", docs) < IOS_PATH_MAX) n++;
    if (snprintf(cand[n], IOS_PATH_MAX, "%s", docs) < IOS_PATH_MAX) n++;
    char *names[IOS_MAX_DIRS];
    int nn = 0;
    DIR *d = opendir(docs);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL && nn < IOS_MAX_DIRS) {
            if (de->d_name[0] == '.' || !strcmp(de->d_name, "Inbox")) continue;
            char p[IOS_PATH_MAX];
            if (snprintf(p, sizeof p, "%s/%s", docs, de->d_name) >= (int)sizeof p || !is_dir(p)) continue;
            names[nn] = strdup(de->d_name);
            if (names[nn]) nn++;
        }
        closedir(d);
    }
    qsort(names, (size_t)nn, sizeof names[0], cmp_str);
    for (int i = 0; i < nn; i++) {
        if (n < max && snprintf(cand[n], IOS_PATH_MAX, "%s/%s", docs, names[i]) < IOS_PATH_MAX) n++;
        free(names[i]);
    }
    return n;
}

static void mkdir_p(const char *path) {
    char tmp[IOS_PATH_MAX];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = 0; mkdir(tmp, 0755); *p = '/';
    }
    mkdir(tmp, 0755);
}

static void ensure_home(void) {
    const char *h = getenv("HOME");
    if (h && h[0]) return;
    char *pref = SDL_GetPrefPath("OpenUG2", "OpenUG2");
    if (!pref) return;
    char *lib = strstr(pref, "/Library/");
    if (lib) { *lib = 0; setenv("HOME", pref, 1); }
    SDL_free(pref);
}

static void write_readme(const char *docs) {
    char p[IOS_PATH_MAX];
    if (snprintf(p, sizeof p, "%s/Copy your NFSU2 folder here.txt", docs) >= (int)sizeof p) return;
    FILE *f = fopen(p, "w");
    if (!f) return;
    fputs("OpenUG2 needs the game data from your own PC copy of Need for Speed: Underground 2.\n\n"
          "1. Take the PC game's install folder, the one that contains the folders TRACKS, CARS and GLOBAL.\n"
          "2. Rename it to NFSU2.\n"
          "3. In the Files app open On My iPhone (or On My iPad), then the OpenUG2 folder,\n"
          "   and copy the NFSU2 folder into it.\n"
          "4. Return to OpenUG2 and tap Check again.\n", f);
    fclose(f);
}

int n2_ios_startup(int *pargc, char ***pargv) {
    static char *new_argv[IOS_MAX_ARGS + 4];
    static char root_buf[IOS_PATH_MAX];
    static char path_opt[] = "--path";

    SDL_SetHint(SDL_HINT_ACCELEROMETER_AS_JOYSTICK, "0");
    SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight");
    SDL_SetHint(SDL_HINT_IOS_HIDE_HOME_INDICATOR, "2");
    if (SDL_InitSubSystem(SDL_INIT_EVENTS) == 0) SDL_AddEventWatch(ios_event_watch, NULL);

    ensure_home();
    const char *home = getenv("HOME");
    if (!home || !home[0]) {
        fprintf(stderr, "OpenUG2: cannot find the app's home folder.\n");
        return 1;
    }
    char cfg[IOS_PATH_MAX], docs[IOS_PATH_MAX];
    if (snprintf(cfg, sizeof cfg, "%s/Library/Application Support/OpenUG2", home) < (int)sizeof cfg) {
        mkdir_p(cfg);
        setenv("OPENUG2_CONFIG_DIR", cfg, 0);
    }
    if (snprintf(docs, sizeof docs, "%s/Documents", home) >= (int)sizeof docs) return 1;

    static char cand[IOS_MAX_DIRS + 2][IOS_PATH_MAX];
    char hint[1024];
    for (int attempt = 0;; attempt++) {
        int n = collect_candidates(docs, cand, IOS_MAX_DIRS + 2);
        const char *root = NULL;
        for (int i = 0; i < n && !root; i++) if (root_valid(cand[i])) root = cand[i];
        if (root) {
            if (!copy_str(root_buf, sizeof root_buf, root)) return 1;
            int argc = *pargc, k = 0;
            char **argv = *pargv;
            new_argv[k++] = argc > 0 ? argv[0] : path_opt;
            new_argv[k++] = path_opt;
            new_argv[k++] = root_buf;
            for (int i = 1; i < argc && k < IOS_MAX_ARGS + 3; i++) new_argv[k++] = argv[i];
            new_argv[k] = NULL;
            *pargc = k; *pargv = new_argv;
            return 0;
        }
        /* Look for the right files with the wrong capitalisation. */
        hint[0] = 0;
        for (int i = 0; i < n && !hint[0]; i++) {
            for (int j = 0; j < N_REQUIRED; j++) {
                char got[IOS_PATH_MAX];
                if (!resolve_ci(cand[i], k_required[j], got, sizeof got)) continue;
                if (strcmp(got, k_required[j]) == 0) continue;
                const char *rel = cand[i] + strlen(docs);
                snprintf(hint, sizeof hint, "\n\nFound \"%s%s%s\" but the game needs \"%s\" (capital letters matter).",
                         rel + (rel[0] == '/'), rel[0] ? "/" : "", got, k_required[j]);
                break;
            }
        }
        write_readme(docs);
        char text[2048];
        snprintf(text, sizeof text,
                 "OpenUG2 needs the game data from your own PC copy of Need for Speed: Underground 2.\n\n"
                 "Take the PC game's install folder (the one containing TRACKS, CARS and GLOBAL) and rename it NFSU2. "
                 "In the Files app open On My iPhone or On My iPad, then the OpenUG2 folder, and copy NFSU2 into it. "
                 "Then tap Check again.%s", hint);
        if (attempt == 0) fprintf(stderr, "OpenUG2: no game data found in %s\n%s\n", docs, text);
        SDL_MessageBoxButtonData btn = { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT |
                                         SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Check again" };
        SDL_MessageBoxData mb;
        memset(&mb, 0, sizeof mb);
        mb.flags = SDL_MESSAGEBOX_INFORMATION;
        mb.title = "OpenUG2: game data needed";
        mb.message = text;
        mb.numbuttons = 1;
        mb.buttons = &btn;
        int which = 0;
        if (SDL_ShowMessageBox(&mb, &which) < 0) return 1;
    }
}
