/* sfx_groups.h — which retail bank backs which in-game effect group.
 *
 * A lookup table, nothing else: no code, no state. Paths are relative to the
 * data root (the directory that holds SOUND/), so the host joins
 * "<data root>/<path>" and hands it to sfx_bank_open_file().
 *
 * Tagging (the same convention as docs/parity/M8-SFX-BANKS.md):
 *   [data]      the path exists on disk and sfx_bank opens it with every sample
 *               decodable — verified over all 176 retail banks.
 *   [inferred]  what the bank *contains*, guessed from the retail file and
 *               directory names only. The banks carry no sample names, so no
 *               row is more than a guess, and the sample order inside a bank is
 *               unverified. See the doc for the counts behind each row.
 *
 * The retail names are quoted exactly, misspellings and all — "Stich" in the
 * collision bank versus "Stitch" in the whoosh bank is on disk, not a typo
 * here; "correcting" it makes the bank unopenable.
 *
 * Include this only where it is used: the arrays are file-scope statics.
 */
#ifndef SFX_GROUPS_H
#define SFX_GROUPS_H

typedef struct SfxGroup {
    const char *group;   /* effect group, as the game thinks about it */
    const char *path;    /* bank path relative to the data root [data] */
} SfxGroup;

/* Every row is [data] for the path; the group column is [inferred]. */
static const SfxGroup SFX_GROUPS[] = {
    /* front-end menu: 68 samples at 44100 Hz — the only group that needs no
     * resampling for a 44100 output. [inferred] index 0..67 = menu moves,
     * accepts, backs, page flips. */
    { "menu",        "SOUND/FE/FE_MB.abk" },
    { "menu",        "SOUND/GLOBAL/FE_COMMON_MB.abk" },

    /* collisions / impacts: 94 samples at 36000 Hz — the biggest single impact
     * set, one bank for bodywork hits, scrapes and wall hits. [inferred] */
    { "collision",   "SOUND/IG_GLOBAL/MB_Stich_Collision.abk" },

    /* skids: 4 banks of 5 samples at 32000/36000 Hz; PAV vs PAV2 and DRIFT vs
     * DRIFT2 read as two surfaces times two variants. [inferred] */
    { "skid",        "SOUND/SKIDS/SKID_PAV_MB.abk" },
    { "skid",        "SOUND/SKIDS/SKID_PAV2_MB.abk" },
    { "skid",        "SOUND/SKIDS/SKIDS_DRIFT_MB.abk" },
    { "skid",        "SOUND/SKIDS/SKIDS_DRIFT2_MB.abk" },

    /* nitrous / NOS: 6 + 6 + 7 samples; Nitrous_00 vs _01 is likely two
     * mixes of the same event, Hydraulics is valve/solenoid chatter.
     * [inferred] */
    { "nos",         "SOUND/NOS/Nitrous_00_MB.abk" },
    { "nos",         "SOUND/NOS/Nitrous_01_MB.abk" },
    { "nos",         "SOUND/NOS/Hydraulics_00_MB.abk" },

    /* gearbox: one bank per (size, level) — LRG/MED/SML/TK look like engine
     * size classes and Base/Lev1/Lev2/Lev3 like upgrade stages. Each bank has
     * 2 samples at 32000 Hz, probably shift-up and shift-down. [inferred] */
    { "shifting",    "SOUND/SHIFTING/GEAR_SML_Base.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_SML_Lev1.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_SML_Lev2.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_SML_Lev3.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_MED_Base.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_MED_Lev1.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_MED_Lev2.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_MED_Lev3.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_LRG_Base.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_LRG_Lev1.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_LRG_Lev2.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_LRG_Lev3.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_TK_Base.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_TK_Lev1.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_TK_Lev2.abk" },
    { "shifting",    "SOUND/SHIFTING/GEAR_TK_Lev3.abk" },

    /* turbo: 5 samples per bank at 22050 Hz, BIG/MED/SML1/SML2/TRUCK by
     * engine size and _0/_1/_2 probably a boost stage. A loop candidate, not a
     * one-shot: same shape as the engine banks. [inferred] */
    { "turbo",       "SOUND/TURBO/TURBO_SML1_0_MB.abk" },
    { "turbo",       "SOUND/TURBO/TURBO_SML2_0_MB.abk" },
    { "turbo",       "SOUND/TURBO/TURBO_MED_0_MB.abk" },
    { "turbo",       "SOUND/TURBO/TURBO_BIG_0_MB.abk" },
    { "turbo",       "SOUND/TURBO/TURBO_TRUCK_0_MB.abk" },

    /* wind: 4 + 3 samples; the pair looks like two speeds or two
     * environments rather than a stereo split (every retail sample is
     * mono). [inferred] */
    { "wind",        "SOUND/IG_GLOBAL/WIND_00_MB.abk" },
    { "wind",        "SOUND/IG_GLOBAL/WIND_01_MB.abk" },

    /* rain: 2 samples, 32000 and 24000 Hz — one loop, one
     * variant. [inferred] */
    { "rain",        "SOUND/IG_GLOBAL/FX_Rain_MB.abk" },

    /* road noise: 14 samples at 22050 Hz, likely per surface. [inferred] */
    { "roadnoise",   "SOUND/IG_GLOBAL/ROADNOISE_00_MB.abk" },

    /* Not in the requested list, but the same IG_GLOBAL bag holds these and a
     * host will want them: pass-by whoosh (16), traffic (2), general
     * environment (8), misc foley (2), and a 4-sample FX bank. [inferred] */
    { "whoosh",      "SOUND/IG_GLOBAL/MB_Stitch_Whoosh.abk" },
    { "traffic",     "SOUND/IG_GLOBAL/TRAFFIC_MB.abk" },
    { "environment", "SOUND/IG_GLOBAL/ENV_COMMON_MB.abk" },
    { "misc",        "SOUND/IG_GLOBAL/FX_Misc_MB.abk" },
    { "fx",          "SOUND/IG_GLOBAL/FX_MAIN_MEM_MB.abk" }
};

static const unsigned SFX_GROUP_COUNT =
    (unsigned)(sizeof(SFX_GROUPS) / sizeof(SFX_GROUPS[0]));

#endif /* SFX_GROUPS_H */
