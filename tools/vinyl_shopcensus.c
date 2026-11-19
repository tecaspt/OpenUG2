/* Replicates career_vinyl_shop()'s filters exactly, at the array bounds the game uses, so a
 * mismatch between the census (tools/vinyl_rows, 25-byte names, no 32-byte table) and the shop's
 * own count is attributable rather than a mystery. Run: vinyl_shopcensus <DATAROOT> <CAR> */
#include "nfsu2.h"
#include "career.h"
#include <stdio.h>
#include <string.h>

#define N 4096
static char nm[N][32];
static int n;

static int idx(const char *t) {
    for (int i = 0; i < n; i++) if (!strcmp(nm[i], t)) return i + 1;
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: vinyl_shopcensus <DATAROOT> <CAR>\n"); return 2; }
    char path[1024];
    snprintf(path, sizeof path, "%s/CARS/%s/VINYLS.BIN", argv[1], argv[2]);
    long len = 0;
    unsigned char *d = n2_read_file(path, &len);
    if (!d) { fprintf(stderr, "cannot read %s\n", path); return 1; }
    uint32_t k[N];
    int raw = n2_car_tex_keys(d, len, k, N);
    for (int i = 0; i < raw && n < N; i++) {
        nm[n][0] = 0;
        if (n2_car_tex_name_by_key(d, len, k[i], nm[n], sizeof nm[n])) n++;
    }
    int sell = 0, parts = 0, nocat = 0, capped = 0, narrowed = 0;
    for (int i = 0; i < n; i++) {
        size_t tn = strlen(nm[i]);
        char base[32];
        int part = 0;
        int colors = cr_vinyl_design_colors(nm[i]);
        if (colors < 1) colors = 1;
        if (tn >= 5 && !strcmp(nm[i] + tn - 5, "_MASK")) {
            size_t bl = tn - 5; if (bl >= sizeof base) bl = sizeof base - 1;
            memcpy(base, nm[i], bl); base[bl] = 0; part = idx(base) != 0;
        } else if (tn > 1 && nm[i][tn - 1] >= 'A' && nm[i][tn - 1] <= 'C') {
            size_t bl = tn - 1; if (bl >= sizeof base) bl = sizeof base - 1;
            memcpy(base, nm[i], bl); base[bl] = 0; part = idx(base) != 0;
        }
        if (part) { parts++; continue; }
        /* The game's own part test clamps the stripped base to 23 characters (`char base[24]` in
           career_vinyl_shop()); the name field holds 24 characters plus NUL, so a design whose
           base is that long loses its colour slots in-game. Reproduced here, not fixed, so the
           census stays a mirror; vinyl_rows reports how many designs are affected. */
        if (cr_vinyl_category(nm[i], argv[2]) < 0) { nocat++; continue; }
        /* The third filter, copied from career_vinyl_shop(): narrow the design to the colour
         * channels its own file proves. Counting what it costs is the point of this tool. */
        if (colors > 1) {
            int present = 1;
            char sub[40];
            size_t bl = tn; if (bl + 2 > sizeof sub) bl = sizeof sub - 2;
            memcpy(sub, nm[i], bl);
            for (int ch = 1; ch < colors; ch++) { sub[bl] = (char)('A' + ch); sub[bl+1] = 0; if (idx(sub)) present = ch + 1; }
            if (present < colors) narrowed++;
            colors = present;
        }
        if (sell >= 512) { capped++; continue; }   /* FEV_MAX */
        sell++;
    }
    printf("%s: named=%d parts=%d no_category=%d narrowed_by_channels=%d sell=%d capped=%d\n",
           argv[2], n, parts, nocat, narrowed, sell, capped);
    free(d);
    return 0;
}
