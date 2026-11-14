/* ugui_strtab.c -- localisation string table (SPEC 8.3). */
#include "ugui_core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRTAB_CHUNK_ID 0x00039000u

static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint32_t rd16(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }

void ug_strtab_init(UgStrTab *t) { memset(t, 0, sizeof *t); }

void ug_strtab_free(UgStrTab *t) {
    for (int i = 0; i < t->n; i++) free(t->e[i].text);
    free(t->e);
    memset(t, 0, sizeof *t);
}

static int fail(char *err, size_t n, const char *msg) {
    if (err && n) snprintf(err, n, "%s", msg);
    return 0;
}

/* insert keeping the array sorted by hash; replaces an equal hash */
static int insert(UgStrTab *t, uint32_t hash, const char *utf8) {
    int lo = 0, hi = t->n;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (t->e[mid].hash < hash) lo = mid + 1; else hi = mid;
    }
    char *copy = (char *)malloc(strlen(utf8) + 1);
    if (!copy) return 0;
    strcpy(copy, utf8);
    if (lo < t->n && t->e[lo].hash == hash) {
        free(t->e[lo].text);
        t->e[lo].text = copy;
        return 1;
    }
    if (t->n == t->cap) {
        int nc = t->cap ? t->cap * 2 : 16;
        UgStrEntry *ne = (UgStrEntry *)realloc(t->e, sizeof(UgStrEntry) * (size_t)nc);
        if (!ne) { free(copy); return 0; }
        t->e = ne;
        t->cap = nc;
    }
    memmove(&t->e[lo + 1], &t->e[lo], sizeof(UgStrEntry) * (size_t)(t->n - lo));
    t->e[lo].hash = hash;
    t->e[lo].text = copy;
    t->n++;
    return 1;
}

int ug_strtab_add(UgStrTab *t, const char *label, const char *utf8) {
    return insert(t, ug_hash_upper(label), utf8);
}

int ug_strtab_load(UgStrTab *t, const uint8_t *chunk, size_t len,
                   char *err, size_t errlen) {
    if (len < 8) return fail(err, errlen, "string table: truncated chunk header");
    if (rd32(chunk) != STRTAB_CHUNK_ID) return fail(err, errlen, "string table: wrong chunk id");
    uint32_t psize = rd32(chunk + 4);
    if (psize > len - 8) return fail(err, errlen, "string table: chunk runs past buffer");
    const uint8_t *p = chunk + 8;
    if (psize < 16) return fail(err, errlen, "string table: payload too small");
    uint32_t hdr = rd32(p), count = rd32(p + 4), tab = rd32(p + 8), area = rd32(p + 12);
    /* 0x20: header carries a 16-byte table name; 0x10 (PC retail): no name. */
    if (hdr != 0x20 && hdr != 0x10) return fail(err, errlen, "string table: bad header size");
    if (psize < (uint64_t)hdr + 4 + 512) return fail(err, errlen, "string table: payload too small");
    if (rd32(p + hdr) != 256) return fail(err, errlen, "string table: bad code map length");
    if ((uint64_t)tab + (uint64_t)count * 8 > psize) return fail(err, errlen, "string table: entry table out of range");
    if (area > psize) return fail(err, errlen, "string table: string area out of range");

    UgStrTab nt;
    ug_strtab_init(&nt);
    if (hdr == 0x20) {
        memcpy(nt.name, p + 16, 16);
        nt.name[16] = 0;
    }

    uint16_t map[256];
    for (int i = 0; i < 256; i++) map[i] = (uint16_t)rd16(p + hdr + 4 + 2 * i);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t hash = rd32(p + tab + i * 8);
        uint32_t off = rd32(p + tab + i * 8 + 4);
        if ((uint64_t)area + off >= psize) { ug_strtab_free(&nt); return fail(err, errlen, "string table: string offset out of range"); }
        const uint8_t *s = p + area + off;
        const uint8_t *end = p + psize;
        size_t n = 0;
        while (s + n < end && s[n]) n++;
        if (s + n >= end) { ug_strtab_free(&nt); return fail(err, errlen, "string table: unterminated string"); }
        char *buf = (char *)malloc(n * 3 + 1), *o = buf;
        if (!buf) { ug_strtab_free(&nt); return fail(err, errlen, "string table: out of memory"); }
        for (size_t k = 0; k < n; k++) {
            uint32_t cp = s[k];
            if (cp >= 0x80 && map[cp]) cp = map[cp];   /* else Latin-1 */
            o += ug_utf8_encode(cp, o);
        }
        *o = 0;
        int ok = insert(&nt, hash, buf);
        free(buf);
        if (!ok) { ug_strtab_free(&nt); return fail(err, errlen, "string table: out of memory"); }
    }
    ug_strtab_free(t);
    *t = nt;
    return 1;
}

const char *ug_strtab_get(const UgStrTab *t, uint32_t hash) {
    if (!t) return NULL;
    int lo = 0, hi = t->n - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (t->e[mid].hash == hash) return t->e[mid].text;
        if (t->e[mid].hash < hash) lo = mid + 1; else hi = mid - 1;
    }
    return NULL;
}

const char *ug_strtab_or(const UgStrTab *t, uint32_t hash, const char *fallback) {
    const char *s = ug_strtab_get(t, hash);
    return s ? s : fallback;
}
