/* vinyl_rows: for a car's VINYLS.BIN, how many design rows would the graphics shop actually
 * list, and how many of them are 3-colour families? This is the census behind
 * docs/parity/VINYLS.md and the career_vinyl_shop() filter; it exists because that filter is
 * the difference between a menu of 399 rows and one of ~200, and nothing else checks it.
 *   vinyl_rows <DATAROOT> <CAR>            -> summary + one line per sellable design
 *   vinyl_rows <DATAROOT> <CAR> --all      -> also the slots the shop hides, with the reason
 * Read-only; DATAROOT is the game data root, so the file read is <DATAROOT>/CARS/<CAR>/VINYLS.BIN,
 * the same path the game and vinyl_census use. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "nfsu2.h"
#include "career.h"

#define MAXS 8192
/* 25 bytes, not 24: see the note at the naming loop. */
static char name[MAXS][25];
static uint32_t key[MAXS];
static int nslot;

static int has(const char *tok) {
    for (int i = 0; i < nslot; i++) if (!strcmp(name[i], tok)) return 1;
    return 0;
}

/* The same test career_vinyl_shop() applies: a slot is a compositing part when stripping
 * _MASK or a trailing A/B/C names another slot in the same file. */
static int is_part(const char *tok, char *base, size_t baseliz) {
    size_t tn = strlen(tok);
    if (tn >= 5 && !strcmp(tok + tn - 5, "_MASK")) {
        size_t bl = tn - 5; if (bl > baseliz - 1) bl = baseliz - 1;
        memcpy(base, tok, bl); base[bl] = 0;
        return has(base);
    }
    if (tn > 1 && tok[tn - 1] >= 'A' && tok[tn - 1] <= 'C') {
        size_t bl = tn - 1; if (bl > baseliz - 1) bl = baseliz - 1;
        memcpy(base, tok, bl); base[bl] = 0;
        return has(base);
    }
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: vinyl_rows <DATAROOT> <CAR> [--all]\n"); return 2; }
    int all = argc > 3 && !strcmp(argv[3], "--all");
    char path[1024];
    snprintf(path, sizeof path, "%s/CARS/%s/VINYLS.BIN", argv[1], argv[2]);
    long len = 0;
    unsigned char *d = n2_read_file(path, &len);
    if (!d) { fprintf(stderr, "cannot read %s\n", path); return 1; }
    uint32_t keys[4096];
    int raw = n2_car_tex_keys(d, len, keys, 4096);
    fprintf(stderr, "read %s: %ld bytes, %d slots\n", path, len, raw);
    /* The name table is the whole point of this tool, so it needs 25 bytes per slot:
       n2_car_tex_name_by_key writes out[24] = 0 unconditionally, and refuses outright
       (leaving the slot unnamed) when cap < 25. A 24-byte slot here silently named nothing. */
    for (int i = 0; i < raw && nslot < MAXS; i++) {
        name[nslot][0] = 0;
        if (n2_car_tex_name_by_key(d, len, keys[i], name[nslot], 25)) {
            key[nslot] = keys[i];
            nslot++;
        }
    }
    fprintf(stderr, "named %d slots\n", nslot);
    int sell = 0, three = 0, single = 0, parts = 0, nocat = 0;
    for (int i = 0; i < nslot; i++) {
        char base[24];
        if (is_part(name[i], base, sizeof base)) {
            parts++;
            if (all) printf("HIDE %s part-of=%s\n", name[i], base);
            continue;
        }
        /* The other two gates are the ones the game owns, so they are called rather than
         * restated here: a slot with no known category is not sellable at all, and the
         * colour count is the family default narrowed by the suffix slots this file proves.
         * Restating them is how this tool would drift from the shop it claims to measure. */
        int cat = cr_vinyl_category(name[i], argv[2]);
        int family = cr_vinyl_design_colors(name[i]);
        if (cat < 0 || family < 0) {
            nocat++;
            if (all) printf("HIDE %s no-category\n", name[i]);
            continue;
        }
        int colors = family > CR_VINYL_COLORS ? CR_VINYL_COLORS : family;
        char sub[32];
        size_t tn = strlen(name[i]);
        memcpy(sub, name[i], tn);
        for (int ch = 1; ch < colors; ch++) {
            sub[tn] = (char)('A' + ch); sub[tn + 1] = 0;
            if (!has(sub)) { colors = ch; break; }
        }
        sell++;
        if (colors >= CR_VINYL_COLORS) three++; else single++;
        printf("SELL %s cat=%d colors=%d\n", name[i], cat, colors);
    }
    printf("slots_named=%d sellable=%d three_colour=%d single_colour=%d hidden_parts=%d hidden_no_category=%d\n",
           nslot, sell, three, single, parts, nocat);
    free(d);
    return 0;
}
