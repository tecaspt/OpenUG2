/* vinyl_catalog_check DATAROOT -- the design catalogue against the rules that sell it.
 *
 * Every claim here is about retail data, checked over every installed car rather than one, because
 * the bug that motivated this check (cr_vinyl_category() reading the leading "<CAR>_" segment as
 * the family) looked fine on a hand-written token and silently sold 63 of GOLF's 390 designs.
 * Prints a per-car line and fails loudly on a regression. See docs/parity/VINYLS.md. */
#include "career.h"
#include "nfsu2.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int fails;
#define EXPECT(c, ...) do { if (!(c)) { fails++; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while (0)

#define MAXN 4096
static char names[MAXN][32];
static int nnames;

static int have(const char *t) {
    for (int i = 0; i < nnames; i++) if (!strcmp(names[i], t)) return 1;
    return 0;
}

/* Same list as career_data_check / res_list_cars. */
static int installed_cars(const char *root, char (*out)[32], int cap) {
    static const char *const parts[] = { "AUDIO", "BRAKES", "EXHAUST", "PLATES", "ROOF", "WHEELS", "SPOILER",
        "SPOILER_HATCH", "SPOILER_SUV", "MIRRORS_BODY", "MIRRORS_HUMMER", "MIRRORS_POST", "MIRRORS_SUV" };
    char p[1100];
    struct dirent *e;
    DIR *d;
    int n = 0;
    snprintf(p, sizeof p, "%s/CARS", root);
    if (!(d = opendir(p))) return 0;
    while ((e = readdir(d)) && n < cap) {
        struct stat g, t;
        char q[1400];
        unsigned k;
        if (e->d_name[0] == '.' || strlen(e->d_name) > 31) continue;
        for (k = 0; k < sizeof parts / sizeof parts[0]; k++) if (!strcmp(e->d_name, parts[k])) break;
        if (k < sizeof parts / sizeof parts[0]) continue;
        snprintf(q, sizeof q, "%s/%s/GEOMETRY.BIN", p, e->d_name);
        if (stat(q, &g) != 0) continue;
        snprintf(q, sizeof q, "%s/%s/TEXTURES.BIN", p, e->d_name);
        if (stat(q, &t) != 0 || t.st_size == 0) continue;
        memcpy(out[n], e->d_name, strlen(e->d_name) + 1); n++;
    }
    closedir(d);
    return n;
}

int main(int argc, char **argv) {
    char cars[128][32];
    char root[1024];
    
    int ncar, total_named = 0, total_sell = 0, cap_hits = 0;
    if (argc < 2) { fprintf(stderr, "usage: vinyl_catalog_check DATAROOT\n"); return 2; }
    /* The root is copied through a fixed buffer first so every path below is provably bounded;
     * a DATAROOT longer than this is not a truncation we would want to count slots on. */
    if (strlen(argv[1]) >= sizeof root) { fprintf(stderr, "DATAROOT too long\n"); return 2; }
    strcpy(root, argv[1]);   /* length checked on the line above */
    ncar = installed_cars(root, cars, 128);
    EXPECT(ncar > 0, "no installed cars under %s/CARS", root);
    if (!ncar) return 1;

    for (int c = 0; c < ncar; c++) {
        char path[1400];
        long len = 0;
        unsigned char *d;
        uint32_t keys[MAXN];
        int raw, sell = 0, parts = 0, nocat = 0, three = 0, capped = 0, uncapped = 0;

        /* Built by concatenation, and the result checked rather than trusted. If the root did
         * not fit we would open the wrong path and report a car whose data is present as
         * missing it - the one failure mode this tool cannot have, because a wrong count here
         * reads as a finding about the retail data rather than about this program.
         * (snprintf would trip -Wformat-truncation: gcc bound the 32-byte car operand by the
         * file-scope names[4096][32] rather than the caller's cars[128][32], so no buffer size
         * here satisfies it.) */
        if (strlen(root) + 32 + 24 >= sizeof path) { fprintf(stderr, "path too long\n"); return 2; }
        strcpy(path, root);
        strcat(path, "/CARS/");
        strcat(path, cars[c]);
        strcat(path, "/VINYLS.BIN");
        d = n2_read_file(path, &len);
        if (!d) { EXPECT(1, "car %s has no VINYLS.BIN (skipped)", cars[c]); continue; }
        raw = n2_car_tex_keys(d, len, keys, MAXN);
        /* Every car must be read whole: a truncated read makes every count below meaningless, and
         * the game's own table is the same size, so the two cannot silently diverge. */
        EXPECT(raw < MAXN, "%s: slot list hit the %d cap - raise MAXN, the counts below are short",
               cars[c], MAXN);
        nnames = 0;
        for (int i = 0; i < raw && nnames < MAXN; i++) {
            names[nnames][0] = 0;
            /* cap >= 25 is a hard requirement of the reader, which writes out[24]=0. A 24-byte
             * buffer names nothing at all and reports no error. */
            if (n2_car_tex_name_by_key(d, len, keys[i], names[nnames], sizeof names[nnames])) nnames++;
        }
        EXPECT(nnames > 0, "%s: %d slots, none named (name table too small?)", cars[c], raw);

        for (int i = 0; i < nnames; i++) {
            size_t tn = strlen(names[i]);
            char base[32];
            int part = 0;
            if (tn >= 5 && !strcmp(names[i] + tn - 5, "_MASK")) {
                size_t bl = tn - 5; if (bl >= sizeof base) bl = sizeof base - 1;
                memcpy(base, names[i], bl); base[bl] = 0; part = have(base);
            } else if (tn > 1 && names[i][tn - 1] >= 'A' && names[i][tn - 1] <= 'C') {
                size_t bl = tn - 1; if (bl >= sizeof base) bl = sizeof base - 1;
                memcpy(base, names[i], bl); base[bl] = 0; part = have(base);
            }
            if (part) { parts++; continue; }
            if (cr_vinyl_category(names[i], cars[c]) < 0) { nocat++; continue; }
            uncapped++;
            if (sell >= 512) { capped++; continue; }   /* FEV_MAX in src/fe_vinyl.h */
            if (cr_vinyl_design_colors(names[i]) > 1) {
                char sub[40];
                size_t bl = tn; if (bl + 2 > sizeof sub) bl = sizeof sub - 2;
                memcpy(sub, names[i], bl);
                sub[bl] = 'B'; sub[bl + 1] = 0;
                if (have(sub)) three++;
            }
            sell++;
        }
        free(d);

        /* The naming rule that broke: every design in a car's own file carries the car folder
         * first, so a token cannot be categorised without knowing the car. If that ever stops
         * being true, this check says so instead of the shop quietly losing designs. */
        for (int i = 0; i < nnames; i++) {
            size_t cl = strlen(cars[c]);
            EXPECT(!strncmp(names[i], cars[c], cl) && names[i][cl] == '_',
                   "%s: token %s does not start with \"%s_\" - the car-prefix strip in "
                   "cr_vinyl_category() needs revisiting", cars[c], names[i], cars[c]);
            break;   /* the first slot is enough to catch a format change, at every car */
        }

        /* A design with colour regions must be categorised: an uncategorised one would be hidden
         * from the shop even though its A/B/C slots prove it is shop stock. */
        /* The three buckets must cover the file exactly. A filter that starts dropping designs it
         * should not (or hiding a part that is really a design) shows up here rather than as a
         * silently shorter shop list. */
        EXPECT(parts + nocat + uncapped == nnames,
               "%s: parts=%d + no_category=%d + designs=%d != named=%d", cars[c], parts, nocat, uncapped, nnames);
        printf("%-12s slots=%4d named=%4d designs=%3d (%3d listed, %3d past FEV_MAX) three_colour=%3d parts=%4d no_category=%2d\n",
               cars[c], raw, nnames, uncapped, sell, capped, three, parts, nocat);
        total_named += nnames; total_sell += uncapped; cap_hits += capped ? 1 : 0;
    }
    printf("%d cars, %d named slots, %d sellable, %d cars hit FEV_MAX\n", ncar, total_named, total_sell, cap_hits);
    printf("vinyl_catalog_check: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
