/* Engine recording map regression.
 *
 * Checks that every mapped car resolves to recordings that exist and parse, that
 * their RPM envelopes are sane, that an unmodelled model still falls back to the
 * CAR_nn bank, and that GOLF keeps its road GTi pair. Then renders each car
 * through the production dummy-device callback at 1000/3000/6000 RPM exactly as
 * the AE evidence batch did (same 44 x 1024-frame pulls, same Hann power
 * spectrum with DC removed) and prints the RMS / >2 kHz table for
 * docs/parity/AUDIO.md. --dump-pcm also writes the raw renders so a baseline run
 * can be diffed against the updated one. --skip-map-checks keeps only the GOLF
 * pair check and the renders, so a binary built from the pre-map source can
 * produce the matching "before" column for the fallback cars.
 *
 *   make engine-map-test
 *   ./build/engine_map_test DATAROOT
 *   ./build/engine_map_test DATAROOT --render GOLF SKYLINE [--dump-pcm DIR]
 *
 * Exit 0 all checks pass, 1 a check failed, 2 bad usage, 3 no audio device.
 */
#include "../src/audio.c"
#include "../src/sfx_game.c"

#include <sys/stat.h>

static int PASS = 0, FAIL = 0;
static void chk(const char *what, int ok) {
    printf("    %-62s %s\n", what, ok ? "PASS" : "FAIL");
    ok ? PASS++ : FAIL++;
}

/* The map is the deliverable, so the checks read it directly rather than only
 * through the loader; ENG_RECORDINGS has internal linkage, hence this file's
 * access through the translation unit that includes src/audio.c. A run that
 * renders the pre-map source defines EMAP_NO_MAP (the table does not exist
 * there) and keeps only the GOLF pair check plus the renders, which is how the
 * "before" column of the AUDIO.md table comes from the previous selection. */
#ifdef EMAP_NO_MAP
#define MAP_ROWS 0
#else
#define MAP_ROWS ((int)(sizeof ENG_RECORDINGS / sizeof ENG_RECORDINGS[0]))
#endif

/* The Gnsu20 header is parsed here too, so a mapping typo surfaces as a missing
 * or short file instead of a silently empty sweep. */
typedef struct {
    float rpm_min, rpm_max;
    uint32_t rate, total;
    int descending;
} GinHead;

static int gin_head(const char *path, GinHead *h) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int n_rpm = 0;
    unsigned char d[0x20];
    int ok = fread(d, 1, sizeof d, f) == sizeof d && memcmp(d, "Gnsu20", 6) == 0;
    if (ok) {
        memcpy(&h->rpm_min, d + 8, 4);
        memcpy(&h->rpm_max, d + 12, 4);
        n_rpm = (int)rd32(d + 0x10);
        h->total = rd32(d + 0x18);
        h->rate = rd32(d + 0x1c);
        unsigned char ends[8];
        ok = n_rpm >= 2 && fseek(f, 0x20, SEEK_SET) == 0 &&
             fread(ends, 1, 4, f) == 4 &&
             fseek(f, 0x20 + 4L * n_rpm, SEEK_SET) == 0 &&
             fread(ends + 4, 1, 4, f) == 4;
        if (ok) h->descending = (int)rd32(ends) > (int)rd32(ends + 4);
    }
    fclose(f);
    return ok;
}

static int sweep_matches(const GinsuSweep *sw, const GinHead *h) {
    return sw->samples && sw->total_samples == h->total && sw->sample_rate == h->rate &&
           fabsf(sw->rpm_min - h->rpm_min) < 0.5f && fabsf(sw->rpm_max - h->rpm_max) < 0.5f;
}

static void reset_gin(void) {
    if (g_engine.gin.has_gin_data) {
        if (!g_engine.gin.decel_is_alias) free_sweep(&g_engine.gin.decel_sweep);
        free_sweep(&g_engine.gin.accel_sweep);
    }
    memset(&g_engine.gin, 0, sizeof g_engine.gin);
}

static void sweep_path(char *out, size_t n, const char *root, const char *file) {
    snprintf(out, n, "%s/SOUND/ENGINE/%s", root, file);
}

static void check_table(const char *root) {
    char pa[600], pd[600];
    (void)root; (void)pa; (void)pd;
#ifndef EMAP_NO_MAP
    for (int i = 0; i < MAP_ROWS; i++) {
        const char *car = ENG_RECORDINGS[i].car;
        const char *wa = ENG_RECORDINGS[i].rec.accel, *wd = ENG_RECORDINGS[i].rec.decel;
        char what[160];
        const EngineRecordings *rec = audio_engine_recordings(car);
        snprintf(what, sizeof what, "%-6s resolves through the map", car);
        chk(what, rec && !strcmp(rec->accel, wa) && !strcmp(rec->decel, wd));

        GinHead ha, hd;
        sweep_path(pa, sizeof pa, root, wa);
        sweep_path(pd, sizeof pd, root, wd[0] ? wd : wa);
        int ok_a = gin_head(pa, &ha), ok_d = gin_head(pd, &hd);
        snprintf(what, sizeof what, "%-6s acceleration recording parses", car);
        chk(what, ok_a);
        snprintf(what, sizeof what, "%-6s deceleration recording parses", car);
        chk(what, ok_d);
        if (!ok_a || !ok_d) continue;

        snprintf(what, sizeof what, "%-6s acceleration sweeps up, coast sweeps down", car);
        chk(what, !ha.descending && (wd[0] ? hd.descending : !ha.descending));
        /* The RPM floor is engine-family dependent and the GLOBALB powertrain
         * row for each model is the reference: a big-displacement truck or SUV
         * revs to 5500-6000, so its recording legitimately stops lower. */
        float hi_floor = strcmp(car, "HUMMER") ? 5500.0f : 4500.0f;
        snprintf(what, sizeof what, "%-6s acceleration RPM band is plausible", car);
        chk(what, ha.rpm_min > 800.0f && ha.rpm_min < 2300.0f &&
                  ha.rpm_max > hi_floor && ha.rpm_max < 9500.0f &&
                  ha.rpm_max > ha.rpm_min * 2.0f);
        snprintf(what, sizeof what, "%-6s coast RPM band is plausible", car);
        chk(what, hd.rpm_min > 800.0f && hd.rpm_max > hi_floor - 1000.0f &&
                  hd.rpm_max < 9500.0f && hd.rpm_max > hd.rpm_min);
        snprintf(what, sizeof what, "%-6s recording rates are 20-36 kHz", car);
        chk(what, ha.rate >= 20000 && ha.rate <= 36000 &&
                  hd.rate >= 20000 && hd.rate <= 36000);

        int want = wd[0] ? 2 : 1;
        int n = audio_load_ginsu_sweeps(root, car);
        snprintf(what, sizeof what, "%-6s loads %d sweep(s)", car, want);
        chk(what, n == want);
        snprintf(what, sizeof what, "%-6s loader kept the named recordings", car);
        chk(what, sweep_matches(&g_engine.gin.accel_sweep, &ha) &&
                  sweep_matches(&g_engine.gin.decel_sweep, wd[0] ? &hd : &ha));
        snprintf(what, sizeof what, "%-6s coast %s aliased to acceleration", car, want == 2 ? "not" : "");
        chk(what, (g_engine.gin.decel_is_alias != 0) == (want == 1));
        reset_gin();
    }
#endif
}

static void check_golf_unchanged(const char *root) {
#ifndef EMAP_NO_MAP
    const EngineRecordings *rec = audio_engine_recordings("GOLF");
    chk("GOLF maps to the road GTi pair",
        rec && !strcmp(rec->accel, "GIN_GTi_Pull_01b_shrt.gin") &&
        !strcmp(rec->decel, "GIN_GTi_01b_DCL.gin"));
    chk("GOLF never maps to the drag GTi recording",
        rec && !strstr(rec->accel, "Drag") && !strstr(rec->decel, "Drag"));
    struct stat st;
    char p[600];
    sweep_path(p, sizeof p, root, "GIN_GTi_Pull_01b_shrt.gin");
    chk("GOLF acceleration recording is on disk", 0 == stat(p, &st));
#endif
    chk("GOLF loads two sweeps", audio_load_ginsu_sweeps(root, "GOLF") == 2);
    GinsuSweep a = g_engine.gin.accel_sweep, d = g_engine.gin.decel_sweep;
    chk("GOLF keeps its authored RPM envelopes",
        fabsf(a.rpm_min - 2062.22f) < 1.0f && fabsf(a.rpm_max - 7349.32f) < 1.0f &&
        fabsf(d.rpm_min - 2709.58f) < 1.0f && fabsf(d.rpm_max - 6420.79f) < 1.0f);
    chk("GOLF keeps its recording rates", a.sample_rate == 32000 && d.sample_rate == 24000);
    chk("GOLF keeps its sample counts", a.total_samples == 138358 && d.total_samples == 41430);
    chk("GOLF coast sweep is not aliased", !g_engine.gin.decel_is_alias);
    reset_gin();
}

static void check_lookup(const char *root) {
#ifdef EMAP_NO_MAP
    (void)root;
#else
    chk("model lookup ignores case",
        audio_engine_recordings("golf") == audio_engine_recordings("GOLF"));
    chk("a partial model name is not a match",
        !audio_engine_recordings("RX") && !audio_engine_recordings("350") &&
        !audio_engine_recordings("GOLF2"));
    /* Cars the brief expects to keep the CAR_nn fallback: no authored recording
     * plausibly belongs to them (see docs/parity/AUDIO.md). */
    static const char *unmodelled[] = { "SUPRA", "CELICA", "COROLLA", "MUSTANGGT",
                                        "GTO", "3000GT", "IS300", "TIBURON", "SENTRA",
                                        "A3", "PEUGOT", "LANCEREVO8", "IMPREZAWRX",
                                        "AMBULANCE", "BUS", "TAXI", "SUV", "FIRETRUCK" };
    for (unsigned i = 0; i < sizeof unmodelled / sizeof unmodelled[0]; i++) {
        char what[128];
        snprintf(what, sizeof what, "%-12s keeps the bank fallback", unmodelled[i]);
        chk(what, !audio_engine_recordings(unmodelled[i]) &&
                  !audio_load_ginsu_sweeps(root, unmodelled[i]));
    }
    chk("an unknown model gets no sweep", !audio_load_ginsu_sweeps(root, "NO_SUCH_CAR"));
    reset_gin();
#endif
}

/* ---- callback renders: the AE measurement method ----------------------- */

typedef struct { double rms, dc, hi; long frames; } Render;

/* Hann power spectrum, DC removed; >2 kHz share of total power. The 44 x 1024
 * frames pulled below are exactly one period of the 4096-point transform. */
static void fwd(int n, double *re, double *im) {
    for (int i = 1, j = 0; i < n; i++) {
        int k = n >> 1;
        while (j & k) { j &= ~k; k >>= 1; }
        j |= k;
        if (i < j) { double t = re[i]; re[i] = re[j]; re[j] = t;
                     t = im[i]; im[i] = im[j]; im[j] = t; }
    }
    for (int len = 2; len <= n; len <<= 1) {
        double ang = -2.0 * M_PI / (double)len;
        double wr = cos(ang), wi = sin(ang);
        for (int i = 0; i < n; i += len) {
            double cr = 1, ci = 0;
            for (int k = 0; k < len / 2; k++) {
                double ur = re[i+k], ui = im[i+k];
                double vr = re[i+k+len/2] * cr - im[i+k+len/2] * ci;
                double vi = re[i+k+len/2] * ci + im[i+k+len/2] * cr;
                re[i+k] = ur + vr; im[i+k] = ui + vi;
                re[i+k+len/2] = ur - vr; im[i+k+len/2] = ui - vi;
                double ncr = cr * wr - ci * wi;
                ci = cr * wi + ci * wr; cr = ncr;
            }
        }
    }
}

static Render measure(const int16_t *pcm, size_t n, int nfft) {
    Render r = { 0, 0, 0, (long)n };
    if (!n) return r;
    double *re = (double *)malloc(sizeof(double) * (size_t)nfft);
    double *im = (double *)malloc(sizeof(double) * (size_t)nfft);
    if (!re || !im) { free(re); free(im); return r; }
    double sum = 0;
    for (size_t i = 0; i < n; i++) sum += (double)pcm[i];
    r.dc = sum / (double)n;
    for (size_t i = 0; i < n; i++) r.rms += (double)pcm[i] * (double)pcm[i];
    r.rms = sqrt(r.rms / (double)n);
    for (int i = 0; i < nfft; i++) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * (double)i / (double)(nfft - 1));
        re[i] = ((double)pcm[i] - r.dc) * w;
        im[i] = 0;
    }
    fwd(nfft, re, im);
    double total = 0, above = 0;
    for (int k = 0; k <= nfft / 2; k++) {
        double p = re[k] * re[k] + im[k] * im[k];
        total += p;
        if ((double)k * 44100.0 / (double)nfft > 2000.0) above += p;
    }
    r.hi = total > 0 ? 100.0 * above / total : 0;
    free(re); free(im);
    return r;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: engine_map_test DATAROOT [--render car ...] [--dump-pcm DIR]\n");
        return 2;
    }
    const char *root = argv[1];
    const char *dumpdir = 0;
    const char **cars = 0;
    int map_checks = 1;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--dump-pcm") && i + 1 < argc) dumpdir = argv[++i];
        else if (!strcmp(argv[i], "--render") && i + 1 < argc) cars = (const char **)(argv + i + 1);
        else if (!strcmp(argv[i], "--skip-map-checks")) map_checks = 0;
    }

    printf("engine recording map\n");
    if (map_checks) check_table(root);
    check_golf_unchanged(root);
    if (map_checks) check_lookup(root);

    static const char *mapped[] = { "240SX", "350Z", "ECLIPSE", "FOCUS", "G35", "GOLF",
                                    "HUMMER", "MIATA", "RX7", "RX8", "SKYLINE", "TT", 0 };
    if (!cars) cars = mapped;
    if (dumpdir) mkdir(dumpdir, 0777);

    if (SDL_Init(SDL_INIT_AUDIO)) { printf("  SDL: %s\n", SDL_GetError()); return 3; }
    SDL_AudioDeviceID dev = audio_init();
    if (!dev) { SDL_Quit(); return 3; }
    SDL_PauseAudioDevice(dev, 1);
    /* The device starts before pausing; reset state so scheduling cannot reach
     * an offline render (same guard the AE capture harness uses). */
    g_rng = 22222; memset(&g_aux, 0, sizeof g_aux);

    printf("\nfull-throttle renders at 44.1 kHz, 45056 frames per row\n");
    printf("%-10s %6s %8s %10s %10s\n", "car", "rpm", "source", "RMS", ">2 kHz %");
    static const int rpms[3] = { 1000, 3000, 6000 };
    for (int c = 0; cars[c]; c++) {
        const char *car = cars[c];
        static int16_t pcm[44 * 1024];
        for (int r = 0; r < 3; r++) {
            int rpm = rpms[r];
            /* Mirrors the game's engine setup per car load: native recordings
             * when the data has them for this model, otherwise the hashed
             * CAR_nn bank. Reloaded for every RPM so no render can inherit a
             * cursor or grain window left behind by the previous one. */
            reset_gin();
            int sweeps = audio_load_ginsu_sweeps(root, car);
            int bank = sweeps ? 0 : audio_load_engine_bank(root, audio_bank_for_car(car));
            if (!sweeps && !bank) break;
            g_engine.current_rpm = g_engine.target_rpm = (float)rpm;
            g_engine.current_load = g_engine.load = 1;
            g_engine.master_volume = 1;
            g_road_vol = 0; g_hit = 0; g_skid = 0; g_sfx_game = 0;
            for (int k = 0; k < 44; k++) audio_cb(NULL, (Uint8 *)pcm, sizeof pcm);
            Render m = measure(pcm, sizeof pcm / sizeof pcm[0], 4096);
            printf("%-10s %6d %8s %10.2f %10.5f\n", car, rpm, sweeps ? "sweeps" : "bank",
                   m.rms, m.hi);
            if (dumpdir) {
                char p[600];
                snprintf(p, sizeof p, "%s/%s-%d.pcm", dumpdir, car, rpm);
                FILE *f = fopen(p, "wb");
                if (f) { fwrite(pcm, sizeof pcm[0], sizeof pcm / sizeof pcm[0], f); fclose(f); }
            }
        }
    }
    reset_gin();
    SDL_CloseAudioDevice(dev);
    SDL_Quit();

    printf("\nengine_map_test: %d checks, %d failures\n", PASS + FAIL, FAIL);
    return FAIL ? 1 : 0;
}
