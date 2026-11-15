/* sfx_bank.c — list and decode the samples of a retail Ginsu sound bank (.abk).
 *
 * Clean-room notes: the container walk, the TLV tags and the EA-XA decoder maths
 * are the same ones already proven by this project's src/audio.c (lines ~91-390),
 * re-derived from the retail data. src/audio.c is NOT modified here; the functions
 * it needs were copied and hardened with bounds checks. speed2.exe was never read.
 *
 * Findings and evidence: docs/parity/M8-SFX-BANKS.md.
 */
#include "sfx_bank.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- ABKC / BNKl container (same fields audio.c uses) ---- */
#define ABK_HDR_LEN   0x28   /* bytes needed to read the bank offset and size */
#define ABK_BANK_OFF  0x18   /* u32 LE: offset of the BNKl bank image */
#define ABK_BANK_SIZE 0x24   /* u32 LE: its length */

#define BNK_HDR_LEN   0x1c   /* "BNKl" + fields, up to the end of index entry 0 */
#define BNK_DATA_END  0x08   /* u32 LE: end of the stream area, bank-relative */

#define PT_HDR        4      /* "PT\0" magic */
#define PT_MAX_SPAN   0x40   /* a PT record never reaches this many bytes */
#define PT_MAX_TLVS   24     /* sanity cap on tags per record */
#define PT_MAX_VLEN   4      /* every retail tag value is <= 4 bytes wide */
#define PT_STRIDE     0x20   /* audio.c steps this far after a hit, then keeps scanning */

#define TAG_END_A     0xFF
#define TAG_END_B     0xFE
#define TAG_NOVALUE   0xFD   /* one-byte entry: no length, no value */
#define TAG_CHAN      0x82
#define TAG_CODEC     0x83
#define TAG_RATE      0x84
#define TAG_FRAMES    0x85
#define TAG_DATA_OFF  0x88

#define DEF_RATE      22050u

/* ---- EA-XA (Ginsu "EAXA") mono ADPCM ---- */
#define EAXA_FRAME    15     /* header byte + 14 payload bytes = 28 nibbles */
#define EAXA_EE_FRAME 61     /* marker + 2 predictor s16be + 28 raw s16be */
#define EAXA_SPS      28     /* samples per frame, either kind */

static const int EAXA_C1[4] = { 0, 240, 460, 392 };
static const int EAXA_C2[4] = { 0, 0, -208, -220 };

static uint32_t rd32(const unsigned char *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int s16be(const unsigned char *p) {
    int v = (p[0] << 8) | p[1];
    return v > 32767 ? v - 65536 : v;
}

static void set_error(SfxBank *b, const char *what) {
    snprintf(b->error, sizeof b->error, "%s", what);
}

/* Every open failure drops the half-built image but keeps the message: close() zeroes
 * the whole struct, so the reason has to be written after it. */
static int open_failed(SfxBank *b, const char *what) {
    sfx_bank_close(b);
    set_error(b, what);
    return 0;
}

/* EA-XA decoder, same maths as audio.c, with every read bounded by `avail`.
 * Returns 0 if the stream area runs out before nsamp samples are produced. */
static int eaxa_decode(const unsigned char *d, size_t avail, int16_t *out, uint32_t nsamp) {
    int prev = 0, prev2 = 0;
    uint32_t n = 0;
    size_t left = avail;

    while (n < nsamp) {
        if (left < 1) return 0;
        if (d[0] == 0xEE) {                       /* uncompressed + predictor reseed */
            uint32_t take = nsamp - n < EAXA_SPS ? nsamp - n : EAXA_SPS;
            size_t need = 5 + 2 * (size_t)take;   /* 5 when take == 0, 61 at the full 28 */
            if (left < need) return 0;
            for (uint32_t k = 0; k < take; k++) out[n + k] = (int16_t)s16be(d + 5 + 2 * k);
            n += take;
            prev  = s16be(d + 1);
            prev2 = s16be(d + 3);
            /* the frame is 61 bytes wide; a bank that ends inside the last one still
               decodes the samples whose bytes are present */
            if (left >= EAXA_EE_FRAME) { d += EAXA_EE_FRAME; left -= EAXA_EE_FRAME; }
            else { d += left; left = 0; }
        } else {
            int ci = d[0] >> 4, shift = 20 - (int)(d[0] & 15);
            if (ci < 0 || ci > 3) return 0;       /* audio.c trusts this; we do not */
            if (left < EAXA_FRAME) return 0;
            for (int b = 1; b < 15 && n < nsamp; b++) {
                for (int hi = 1; hi >= 0 && n < nsamp; hi--) {
                    int nib = hi ? d[b] >> 4 : d[b] & 15;
                    if (nib > 7) nib -= 16;
                    /* shift is 5..20, |nib| <= 8, |prev| <= 32767: the products stay
                       well inside int, so this is plain arithmetic, no shifting UB */
                    int s = ((nib * (1 << shift)) + prev * EAXA_C1[ci]
                                                    + prev2 * EAXA_C2[ci] + 0x80) >> 8;
                    if (s > 32767) s = 32767;
                    if (s < -32768) s = -32768;
                    prev2 = prev;
                    prev  = s;
                    out[n++] = (int16_t)s;
                }
            }
            d += EAXA_FRAME;
            left -= EAXA_FRAME;
        }
    }
    return 1;
}

/* One PT record at bank + p: TLV list (tag u8, length u8, value big-endian) ended by
 * 0xFF/0xFE. 1 when a well-formed record with a sample count and a data offset was read. */
static int pt_read(const unsigned char *bank, size_t len, size_t p, SfxSample *s) {
    size_t q = p + PT_HDR;
    int ntlv = 0;
    int have_frames = 0, have_off = 0;

    if (p + PT_HDR > len || memcmp(bank + p, "PT\0\0", PT_HDR) != 0) return 0;
    s->rate = DEF_RATE;
    s->frames = 0;
    s->channels = 1;
    s->codec = 0;
    s->data_off = 0;
    s->decodable = 0;

    while (q + 2 <= len && q + PT_HDR < p + PT_MAX_SPAN && ntlv < PT_MAX_TLVS) {
        unsigned tag = bank[q++];
        if (tag == TAG_END_A || tag == TAG_END_B) {
            if (!have_frames || !have_off || s->frames == 0) return 0;
            return 1;
        }
        if (tag == TAG_NOVALUE) { ntlv++; continue; }   /* no length, no value */
        unsigned vlen = bank[q++];
        if (vlen > PT_MAX_VLEN || q + vlen > len) return 0;
        uint32_t v = 0;
        for (unsigned k = 0; k < vlen; k++) v = v << 8 | bank[q + k];
        q += vlen;
        ntlv++;
        switch (tag) {
            case TAG_RATE:     s->rate     = v; break;
            case TAG_FRAMES:   s->frames   = v; have_frames = 1; break;
            case TAG_CHAN:     s->channels = v; break;
            case TAG_CODEC:    s->codec    = v; break;
            case TAG_DATA_OFF: s->data_off = v; have_off = 1; break;
            default: break;                            /* 0x06/0x13/0x1a/0x80/0x86/0x87/0x8a: unused here */
        }
    }
    return 0;                                          /* unterminated, or too long */
}

/* Walk the bank for PT records exactly like audio.c: scan byte by byte for the magic,
 * step PT_STRIDE past a hit and keep scanning. Stops at the lowest stream offset seen,
 * because the sample headers always precede the streams they describe. */
static int bank_walk(SfxBank *b, SfxSample *src, int cap) {
    size_t p = 0;
    size_t stream_low = b->bank_len;
    int n = 0;

    while (p + PT_HDR < stream_low && p + PT_HDR <= b->bank_len && n < cap) {
        SfxSample s;
        if (!pt_read(b->bank, b->bank_len, p, &s)) { p++; continue; }
        if (s.data_off < stream_low) stream_low = s.data_off;
        src[n++] = s;
        p += PT_STRIDE;
    }
    return n;
}

static void sample_finish(SfxBank *b, SfxSample *s) {
    /* minimum EA-XA footprint of a stream: 15 bytes per 28 samples */
    uint64_t need = ((uint64_t)s->frames + EAXA_SPS - 1) / EAXA_SPS * EAXA_FRAME;
    uint64_t left = (s->data_off < b->bank_len) ? (uint64_t)b->bank_len - s->data_off : 0;
    s->decodable = (s->channels == 1 && s->codec == 0 && s->frames != 0 && need <= left);
}

int sfx_bank_open_mem(SfxBank *b, const void *data, size_t len) {
    if (!b) return 0;
    memset(b, 0, sizeof *b);
    if (!data || len < ABK_HDR_LEN) { set_error(b, "short file"); return 0; }
    if (memcmp(data, "ABKC", 4) != 0) { set_error(b, "not an ABKC file"); return 0; }

    b->file = (unsigned char *)malloc(len);
    if (!b->file) { set_error(b, "out of memory"); return 0; }
    memcpy(b->file, data, len);
    b->file_len = len;

    uint64_t boff = rd32(b->file + ABK_BANK_OFF);
    uint64_t bsize = rd32(b->file + ABK_BANK_SIZE);
    if (bsize < BNK_HDR_LEN || boff > len || bsize > len - boff)
        return open_failed(b, "bank offset/size out of file");
    if (memcmp(b->file + boff, "BNKl", 4) != 0)
        return open_failed(b, "bank is not BNKl");
    b->bank = b->file + boff;
    b->bank_len = (size_t)bsize;

    size_t cap_sz = b->bank_len / PT_HDR + 1;          /* records cannot be shorter than 4 bytes */
    if (cap_sz > 65536) cap_sz = 65536;
    int cap = (int)cap_sz;
    SfxSample *tmp = (SfxSample *)malloc(sizeof *tmp * cap_sz);
    if (!tmp) return open_failed(b, "out of memory");

    int n = bank_walk(b, tmp, cap);
    if (n == 0) {
        free(tmp);
        return open_failed(b, "no PT sample headers in bank");
    }
    for (int i = 0; i < n; i++) sample_finish(b, &tmp[i]);
    b->samples = tmp;
    b->count = n;
    return 1;
}

int sfx_bank_open_file(SfxBank *b, const char *path) {
    if (!b) return 0;
    memset(b, 0, sizeof *b);
    if (!path || !path[0]) { set_error(b, "no path"); return 0; }

    FILE *f = fopen(path, "rb");
    if (!f) { set_error(b, "cannot open file"); return 0; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); set_error(b, "unseekable file"); return 0; }
    long len = ftell(f);
    if (len < 0) { fclose(f); set_error(b, "unreadable length"); return 0; }
    rewind(f);
    if ((size_t)len < ABK_HDR_LEN) { fclose(f); set_error(b, "short file"); return 0; }

    unsigned char *buf = (unsigned char *)malloc((size_t)len);
    if (!buf) { fclose(f); set_error(b, "out of memory"); return 0; }
    size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) { free(buf); set_error(b, "short read"); return 0; }

    int ok = sfx_bank_open_mem(b, buf, (size_t)len);   /* copies again; the image stays ours */
    free(buf);
    return ok;
}

int sfx_bank_decode(const SfxBank *b, int i, int16_t *out) {
    if (!b || !b->samples || !out || i < 0 || i >= b->count) return 0;
    const SfxSample *s = &b->samples[i];
    if (!s->decodable || s->data_off >= b->bank_len) return 0;
    return eaxa_decode(b->bank + s->data_off, b->bank_len - s->data_off, out, s->frames);
}

/* Number of index entries in the bank's own TOC table at BNKl+0x18: entry k points at
 * bank + 0x18 + 4*k + value(k). Verified against all 176 retail banks; a caller can use
 * it to cross-check sfx_bank_open's walk. 0 when the table is absent or unreadable. */
int sfx_bank_index_count(const SfxBank *b) {
    if (!b || !b->bank || b->bank_len < BNK_HDR_LEN) return 0;
    int n = 0;
    for (size_t off = 0x18; off + 4 <= b->bank_len && n < 65536; off += 4) {
        uint64_t target = (uint64_t)0x18 + 4 * (uint64_t)n + rd32(b->bank + off);
        if (target >= b->bank_len || target < off) break;
        if (memcmp(b->bank + target, "PT\0\0", 4) != 0) break;
        n++;
    }
    return n;
}

void sfx_bank_close(SfxBank *b) {
    if (!b) return;
    free(b->file);
    free(b->samples);
    memset(b, 0, sizeof *b);
}
