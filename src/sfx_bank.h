/* sfx_bank.h — read the retail Ginsu sound-effect banks (.abk): list samples, decode to s16.
 *
 * Clean-room: derived only from the retail data files and the project's own
 * src/audio.c. No game executable was read at any point.
 *
 * Container/bank/PT-header layout and the EA-XA codec are documented in
 * docs/parity/M8-SFX-BANKS.md. No SDL, no globals, no allocation surprises:
 * the bank image is owned by SfxBank once open_* succeeds.
 */
#ifndef SFX_BANK_H
#define SFX_BANK_H

#include <stddef.h>
#include <stdint.h>

typedef struct SfxSample {
    uint32_t rate;        /* Hz; 22050 if tag 0x84 is absent */
    uint32_t frames;      /* sample count (tag 0x85) */
    uint32_t channels;    /* tag 0x82 if present, else 1 */
    uint32_t codec;       /* tag 0x83 if present, else 0 (EA-XA) */
    uint32_t level;       /* tag 0x06; default 100, percentage gain [inferred] */
    uint32_t data_off;    /* tag 0x88, relative to the bank */
    int      decodable;   /* 1 if this library can decode it */
} SfxSample;

typedef struct SfxBank {
    unsigned char *file;    /* our copy of the whole .abk file image */
    size_t file_len;
    const unsigned char *bank;   /* points into file at the BNKl offset */
    size_t bank_len;
    int count;
    SfxSample *samples;
    char error[96];
} SfxBank;

/* Copies data; b is fully initialised either way. 1 ok, 0 error (b->error says why). */
int  sfx_bank_open_mem(SfxBank *b, const void *data, size_t len);
int  sfx_bank_open_file(SfxBank *b, const char *path);

/* Decodes sample i to mono s16 (channel 0 if multi-channel). out must hold frames values.
 * Returns 1 ok, 0 error. b is const, so no message is written here: check
 * SfxSample.decodable, and on a 0 return the stream area was too short for frames. */
int  sfx_bank_decode(const SfxBank *b, int i, int16_t *out);
void sfx_bank_close(SfxBank *b);

/* Extra helper, not part of the required API: how many entries the bank's own index
 * table at BNKl+0x18 declares. All 176 retail banks agree with b->count; a caller that
 * sees a difference has found a bank the walk mis-read. 0 if there is no such table. */
int  sfx_bank_index_count(const SfxBank *b);

#endif /* SFX_BANK_H */
