/* sfx_bank_test.c — unit test for src/sfx_bank.c on synthetic data only.
 *
 * Every byte here is produced by the builders below. No byte from a retail file
 * appears in this program; the expected EA-XA output is computed by the reference
 * decoder copied from src/audio.c, which is the oracle for the ADPCM case.
 *
 * build: cc -O2 -std=c99 -Wall -Wextra -Werror -Isrc tools/sfx_bank_test.c src/sfx_bank.c
 */
#include "sfx_bank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int nchecks = 0, nfail = 0;

#define CHECK(cond) do {                                        \
        nchecks++;                                              \
        if (!(cond)) {                                          \
            nfail++;                                            \
            printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond);\
        }                                                       \
    } while (0)

/* ------------------------------------------------------------------ *
 * Reference EA-XA decoder, copied from src/audio.c (the oracle).
 * ------------------------------------------------------------------ */
static const int REF_C1[4] = { 0, 240, 460, 392 };
static const int REF_C2[4] = { 0, 0, -208, -220 };

static int ref_s16be(const unsigned char *p) {
    int v = (p[0] << 8) | p[1];
    return v > 32767 ? v - 65536 : v;
}

static void ref_eaxa_decode(const unsigned char *d, int16_t *out, uint32_t nsamp) {
    int prev = 0, prev2 = 0;
    uint32_t n = 0;
    while (n < nsamp) {
        if (d[0] == 0xEE) {
            uint32_t take = nsamp - n < 28 ? nsamp - n : 28;
            for (uint32_t k = 0; k < take; k++) out[n + k] = (int16_t)ref_s16be(d + 5 + 2 * k);
            n += take;
            prev = ref_s16be(d + 1);
            prev2 = ref_s16be(d + 3);
            d += 61;
        } else {
            int ci = d[0] >> 4, shift = 20 - (int)(d[0] & 15);
            for (int b = 1; b < 15 && n < nsamp; b++) {
                for (int hi = 1; hi >= 0 && n < nsamp; hi--) {
                    int nib = hi ? d[b] >> 4 : d[b] & 15;
                    if (nib > 7) nib -= 16;
                    int s = ((nib * (1 << shift)) + prev * REF_C1[ci]
                                                    + prev2 * REF_C2[ci] + 0x80) >> 8;
                    if (s > 32767) s = 32767;
                    if (s < -32768) s = -32768;
                    prev2 = prev;
                    prev = s;
                    out[n++] = (int16_t)s;
                }
            }
            d += 15;
        }
    }
}

/* ------------------------------------------------------------------ *
 * Tiny big/little-endian write cursor
 * ------------------------------------------------------------------ */
typedef struct { unsigned char *p; size_t at, cap; } Buf;

static void w8(Buf *b, unsigned v)   { if (b->at < b->cap) b->p[b->at] = (unsigned char)v; b->at++; }
static void w32le(Buf *b, uint32_t v) { w8(b, v); w8(b, v >> 8); w8(b, v >> 16); w8(b, v >> 24); }
static void w16be(Buf *b, int v)      { w8(b, (unsigned)v >> 8); w8(b, (unsigned)v & 0xFF); }
static void wpad(Buf *b, size_t to)   { while (b->at < to) w8(b, 0); }

/* One PT record: magic, then the tags the library cares about, then 0xFF, padded so the
 * record ends at end_at (absolute position in the buffer). */
static void wpt(Buf *b, size_t end_at, uint32_t frames, int with_rate, uint32_t rate,
                uint32_t data_off, int with_chan, uint32_t chan, int with_codec, uint32_t codec) {
    b->at += 4;                                            /* caller patches "PT\0\0" */
    w8(b, 0x06); w8(b, 1); w8(b, 90);                      /* authored level */
    w8(b, 0x13); w8(b, 1); w8(b, 127);
    if (with_chan)  { w8(b, 0x82); w8(b, 1); w8(b, (unsigned)chan); }
    if (with_codec) { w8(b, 0x83); w8(b, 1); w8(b, (unsigned)codec); }
    w8(b, 0x85); w8(b, 2); w8(b, frames >> 8); w8(b, frames);
    if (with_rate)  { w8(b, 0x84); w8(b, 2); w8(b, rate >> 8); w8(b, rate); }
    w8(b, 0x80); w8(b, 1); w8(b, 2);                       /* value 2 in every retail record */
    w8(b, 0x8a); w8(b, 4);
    w8(b, 0); w8(b, 0); w8(b, 0); w8(b, 0);                /* big-endian zero */
    w8(b, 0x88); w8(b, 4);
    w8(b, data_off >> 24); w8(b, data_off >> 16); w8(b, data_off >> 8); w8(b, data_off);
    w8(b, 0xFD);                                           /* one-byte entry, no value */
    w8(b, 0xFF);
    wpad(b, end_at);
}

/* ------------------------------------------------------------------ *
 * Synthetic bank: ABKC header + BNKl bank with 2 samples.
 *   sample 0: one 0xEE raw frame (28 samples, decoded exactly)
 *   sample 1: one ADPCM frame   (28 samples, checked against the oracle)
 * Returns the file length; out must hold >= 0x100 bytes.
 * ------------------------------------------------------------------ */
#define BANK_OFF   0x40
#define PT0        0x20                 /* bank-relative record offsets */
#define PT1        0x44
#define DATA_EE    0x68                 /* bank-relative stream offsets */
#define DATA_ADPCM 0xA8
#define BANK_LEN   0xB8

static size_t build_abk(unsigned char *out, size_t cap, int nrec) {
    Buf al = { out, 0, cap };
    size_t pt_off[2] = { BANK_OFF + PT0, BANK_OFF + PT1 };
    size_t data_off[2] = { BANK_OFF + DATA_EE, BANK_OFF + DATA_ADPCM };

    /* ABKC file header */
    memcpy(out, "ABKC", 4);
    al.at = 4;
    w8(&al, 1); w8(&al, 1); w8(&al, 1); w8(&al, 0);         /* version, as in retail */
    w32le(&al, 0x20000); w32le(&al, 0); w32le(&al, 0);
    w32le(&al, BANK_OFF + BANK_LEN);                        /* total file size */
    w32le(&al, BANK_OFF);                                   /* +0x18 bank offset */
    w32le(&al, 0);                                          /* +0x1c: unused by the library */
    w32le(&al, BANK_OFF);
    w32le(&al, BANK_LEN);                                   /* +0x24 bank size */
    wpad(&al, BANK_OFF);

    /* BNKl bank image */
    size_t bank = al.at;
    memcpy(out + bank, "BNKl", 4);
    al.at = bank + 4;
    w8(&al, 5); w8(&al, 0);
    w8(&al, (unsigned)nrec + 1); w8(&al, 0);                /* +6 u16: record count + 1 (retail habit) */
    w32le(&al, DATA_ADPCM + 15);                             /* +8: end of the stream area */
    w32le(&al, 0); w32le(&al, 0);
    /* index table at bank+0x18: entry k = record offset - 0x18 - 4k */
    for (int k = 0; k < nrec; k++) {
        uint32_t v = (uint32_t)(pt_off[k] - bank - 0x18 - 4 * k);
        al.at = bank + 0x18 + 4 * (size_t)k;
        w32le(&al, v);
    }
    /* PT records */
    for (int k = 0; k < nrec; k++) {
        al.at = pt_off[k];
        wpt(&al, pt_off[k] + 0x24, 28, k == 0, 32000,
            (uint32_t)data_off[k] - (uint32_t)bank, 0, 0, 0, 0);
        memcpy(out + pt_off[k], "PT\0\0", 4);
    }
    /* stream 0: one 0xEE frame — marker, 2 predictor s16be, 28 raw s16be */
    al.at = data_off[0];
    w8(&al, 0xEE);
    w16be(&al, -1234);
    w16be(&al, 567);
    for (int k = 0; k < 28; k++) w16be(&al, k * 137 - 1800);
    /* stream 1: one ADPCM frame — coef 2, shift 20-8 = 12, 14 payload bytes */
    al.at = data_off[1];
    w8(&al, 0x28);
    for (int k = 1; k < 15; k++) w8(&al, (unsigned char)((k * 0x9E + 0x31) & 0xFF));
    al.at = BANK_OFF + BANK_LEN;
    return al.at <= cap ? al.at : 0;
}

/* ------------------------------------------------------------------ *
 * checks
 * ------------------------------------------------------------------ */
static void test_two_samples(void) {
    unsigned char file[0x400];
    size_t len = build_abk(file, sizeof file, 2);
    SfxBank b;
    CHECK(len == BANK_OFF + BANK_LEN);
    CHECK(sfx_bank_open_mem(&b, file, len) == 1);
    if (b.count != 2) { printf("  count was %d, error '%s'\n", b.count, b.error); }
    CHECK(b.count == 2);
    if (b.count != 2) { sfx_bank_close(&b); return; }      /* keep a failure from crashing */
    CHECK(b.error[0] == '\0');
    CHECK(b.bank == b.file + BANK_OFF);
    CHECK(b.bank_len == BANK_LEN);
    CHECK(sfx_bank_index_count(&b) == 2);

    /* sample 0 carries an explicit 32000, sample 1 has no 0x84 at all */
    CHECK(b.samples[0].rate == 32000 && b.samples[0].frames == 28);
    CHECK(b.samples[1].rate == 22050 && b.samples[1].frames == 28);
    CHECK(b.samples[0].channels == 1 && b.samples[0].codec == 0);
    CHECK(b.samples[0].level == 90 && b.samples[1].level == 90);
    CHECK(b.samples[0].data_off == DATA_EE && b.samples[1].data_off == DATA_ADPCM);
    CHECK(b.samples[0].decodable == 1 && b.samples[1].decodable == 1);

    int16_t out[64];
    memset(out, 0x5A, sizeof out);
    CHECK(sfx_bank_decode(&b, 0, out) == 1);
    for (int k = 0; k < 28; k++) CHECK(out[k] == (int16_t)(k * 137 - 1800));   /* raw 0xEE frame */
    CHECK(out[28] == (int16_t)0x5A5A);                       /* nothing written past frames */

    memset(out, 0x5A, sizeof out);
    CHECK(sfx_bank_decode(&b, 1, out) == 1);
    int16_t want[64];
    ref_eaxa_decode(file + DATA_ADPCM + BANK_OFF, want, 28); /* oracle copied from audio.c */
    CHECK(memcmp(out, want, 28 * sizeof out[0]) == 0);

    CHECK(sfx_bank_decode(&b, -1, out) == 0);
    CHECK(sfx_bank_decode(&b, 2, out) == 0);
    sfx_bank_close(&b);
    CHECK(b.count == 0 && b.file == NULL && b.samples == NULL);
    /* Unknown tags are skipped, and an absent level defaults to unity. */
    file[BANK_OFF + PT0 + 4] = 0x07;
    CHECK(sfx_bank_open_mem(&b, file, len) == 1);
    CHECK(b.samples[0].level == 100 && b.samples[1].level == 90);
    sfx_bank_close(&b);
}

static void test_tags_that_block_decode(void) {
    unsigned char file[0x400];
    memset(file, 0, sizeof file);
    Buf al = { file, 0, sizeof file };
    /* one record that declares stereo, one that declares an unknown codec.
     * Records are 0x28 wide here: the two extra tags of test 2 would otherwise leave
     * no room for the 0xFD/0xFF terminator inside the record. */
    size_t pt0 = BANK_OFF + 0x20, pt1 = BANK_OFF + 0x48;
    size_t d0 = BANK_OFF + 0x70, d1 = BANK_OFF + 0xB0, d_end = 0xB0 + 61;
    memcpy(file, "ABKC", 4);
    al.at = 4;
    w8(&al, 1); w8(&al, 1); w8(&al, 1); w8(&al, 0);
    w32le(&al, 0x20000); w32le(&al, 0); w32le(&al, 0);
    w32le(&al, (uint32_t)(BANK_OFF + d_end));
    w32le(&al, BANK_OFF); w32le(&al, 0); w32le(&al, BANK_OFF); w32le(&al, (uint32_t)d_end);
    wpad(&al, BANK_OFF);
    size_t bank = al.at;                       /* == BANK_OFF */
    memcpy(file + bank, "BNKl", 4);
    al.at = bank + 4;
    w8(&al, 5); w8(&al, 0); w8(&al, 3); w8(&al, 0);
    w32le(&al, d_end); w32le(&al, 0); w32le(&al, 0);
    al.at = bank + 0x18; w32le(&al, (uint32_t)(pt0 - bank - 0x18));
    al.at = bank + 0x1c; w32le(&al, (uint32_t)(pt1 - bank - 0x18 - 4));
    al.at = pt0; wpt(&al, pt0 + 0x28, 28, 1, 22050, (uint32_t)(d0 - bank), 1, 2, 0, 0);
    memcpy(file + pt0, "PT\0\0", 4);
    al.at = pt1; wpt(&al, pt1 + 0x28, 28, 1, 22050, (uint32_t)(d1 - bank), 0, 0, 1, 1);
    memcpy(file + pt1, "PT\0\0", 4);
    al.at = d0; w8(&al, 0xEE); w16be(&al, 0); w16be(&al, 0);
    for (int k = 0; k < 28; k++) w16be(&al, k);
    al.at = d1; w8(&al, 0xEE); w16be(&al, 0); w16be(&al, 0);
    for (int k = 0; k < 28; k++) w16be(&al, k);
    al.at = BANK_OFF + d_end;

    SfxBank b;
    CHECK(sfx_bank_open_mem(&b, file, al.at) == 1);
    if (b.count != 2) printf("  tags-test count was %d, error '%s'\n", b.count, b.error);
    CHECK(b.count == 2);
    if (b.count != 2) { sfx_bank_close(&b); return; }      /* keep a failure from crashing */
    CHECK(b.samples[0].channels == 2 && b.samples[0].decodable == 0);
    CHECK(b.samples[1].codec == 1 && b.samples[1].decodable == 0);
    int16_t out[64];
    CHECK(sfx_bank_decode(&b, 0, out) == 0);
    CHECK(sfx_bank_decode(&b, 1, out) == 0);
    sfx_bank_close(&b);
}

static void test_rejects_bad_input(void) {
    unsigned char good[0x400];
    size_t len = build_abk(good, sizeof good, 2);
    CHECK(len > 0);

    /* every truncation: either open says no, or decode says no — never a crash */
    int refused = 0, no_msg = 0;
    for (size_t cut = 0; cut < len; cut++) {
        unsigned char *tiny = (unsigned char *)malloc(cut ? cut : 1);
        memcpy(tiny, good, cut);
        SfxBank b;
        if (sfx_bank_open_mem(&b, tiny, cut)) {
            for (int i = 0; i < b.count; i++) {
                size_t nf = b.samples[i].frames;
                if (nf > 1u << 20) nf = 1u << 20;
                int16_t *o = (int16_t *)malloc(nf * sizeof *o + 2);
                if (o) { (void) sfx_bank_decode(&b, i, o); free(o); }
            }
            sfx_bank_close(&b);
        } else {
            refused++;
            if (b.error[0] == '\0') no_msg++;              /* a refusal must say why */
        }
        free(tiny);
    }
    CHECK(refused > 0);
    CHECK(no_msg == 0);

    /* A bank whose declared size is honest but whose stream is cut short: open must
     * accept it, decode must refuse. This is the path prefix-truncation cannot reach. */
    for (size_t bl = 0x60; bl < 0xA6; bl++) {
        size_t fl = BANK_OFF + bl;
        unsigned char *f = (unsigned char *)malloc(fl);
        memcpy(f, good, fl < len ? fl : len);
        f[0x24] = (unsigned char)bl;                       /* ABKC bank size, low byte */
        SfxBank b;
        if (sfx_bank_open_mem(&b, f, BANK_OFF + bl)) {
            int16_t o[64];
            int ok0 = sfx_bank_decode(&b, 0, o);
            if (bl < 0x68 + 61) CHECK(ok0 == 0);           /* EE frame is 61 bytes */
            else CHECK(ok0 == 1);
            sfx_bank_close(&b);
        } else {
            CHECK(bl >= 0x68);                             /* too small to hold the TOC */
        }
        free(f);
    }

    /* garbage of the same length must never be accepted as a bank */
    unsigned char junk[0x100];
    for (size_t i = 0; i < sizeof junk; i++) junk[i] = (unsigned char)(i * 7 + 3);
    SfxBank b;
    CHECK(sfx_bank_open_mem(&b, junk, sizeof junk) == 0);
    CHECK(b.error[0] != '\0');
    junk[0] = 'A'; junk[1] = 'B'; junk[2] = 'K'; junk[3] = 'C';
    CHECK(sfx_bank_open_mem(&b, junk, sizeof junk) == 0);
    CHECK(b.error[0] != '\0');
    /* ABKC that points its bank outside the file */
    memcpy(junk, "ABKC\1\1\1\0", 8);
    memset(junk + 8, 0, 0x30);
    junk[0x18] = 0xF0; junk[0x19] = 0xFF;                 /* bank offset past the end */
    junk[0x24] = 0x10;
    CHECK(sfx_bank_open_mem(&b, junk, sizeof junk) == 0);
    CHECK(b.error[0] != '\0');
    /* ABKC + BNKl but no PT records at all */
    memcpy(junk, "ABKC\1\1\1\0", 8);
    memset(junk + 8, 0, sizeof junk - 8);
    junk[0x18] = 0x40; junk[0x24] = 0x40;                 /* bank = last 0x40 bytes */
    memcpy(junk + 0x40, "BNKl", 4);
    CHECK(sfx_bank_open_mem(&b, junk, sizeof junk) == 0);
    CHECK(strstr(b.error, "PT") != NULL);
    CHECK(sfx_bank_open_mem(&b, NULL, 0) == 0);
    CHECK(sfx_bank_open_file(&b, NULL) == 0);
    CHECK(sfx_bank_open_file(&b, "/nonexistent-dir-for-sfx-test/x.abk") == 0);
    CHECK(sfx_bank_open_mem(&b, good, 4) == 0);            /* magic only, no header */
}

/* One-byte-mutation fuzz: the library must stay inside the buffer every time. */
static void test_mutations(void) {
    unsigned char good[0x400];
    size_t len = build_abk(good, sizeof good, 2);
    CHECK(len > 0);
    unsigned seed = 20261114u;
    int accepted = 0;
    for (int it = 0; it < 4000; it++) {
        unsigned char *f = (unsigned char *)malloc(len);
        memcpy(f, good, len);
        seed = seed * 1103515245u + 12345u;
        size_t nmut = 1 + seed % 3;
        for (size_t m = 0; m < nmut; m++) {
            seed = seed * 1103515245u + 12345u;
            size_t at = seed % len;
            unsigned char v = (unsigned char)(seed >> 11);
            if (at >= 4 || v != good[at]) f[at] = v;       /* keep some ABKC magic alive */
        }
        SfxBank b;
        if (sfx_bank_open_mem(&b, f, len)) {
            accepted++;
            for (int i = 0; i < b.count; i++) {
                size_t nf = b.samples[i].frames;
                if (nf > 1u << 20) nf = 1u << 20;
                int16_t *o = (int16_t *)malloc(nf * sizeof *o + 2);
                if (o) { (void) sfx_bank_decode(&b, i, o); free(o); }
            }
            sfx_bank_close(&b);
        }
        free(f);
    }
    CHECK(accepted > 0);                                   /* mutations are not all fatal */
    CHECK(accepted < 4000);                                /* ...and not all accepted */
}

/* Round trip through the file API, so sfx_bank_open_file is covered too. */
static void test_open_file(void) {
    unsigned char good[0x400];
    size_t len = build_abk(good, sizeof good, 2);
    char path[128];
    snprintf(path, sizeof path, "/tmp/openug2-sfx-bank-test-%ld.tmp", (long)time(NULL));
    FILE *f = fopen(path, "wb");
    if (!f) { printf("  (skipping open_file: cannot write %s)\n", path); return; }
    CHECK(fwrite(good, 1, len, f) == len);
    fclose(f);
    SfxBank b;
    CHECK(sfx_bank_open_file(&b, path) == 1);
    CHECK(b.count == 2);
    int16_t out[64];
    CHECK(sfx_bank_decode(&b, 0, out) == 1);
    CHECK(out[27] == (int16_t)(27 * 137 - 1800));
    sfx_bank_close(&b);
    remove(path);
}

int main(void) {
    test_two_samples();
    test_tags_that_block_decode();
    test_rejects_bad_input();
    test_mutations();
    test_open_file();
    printf("sfx_bank_test: %d checks, %d failures\n", nchecks, nfail);
    printf("%s\n", nfail == 0 ? "PASS" : "FAIL");
    return nfail == 0 ? 0 : 1;
}
