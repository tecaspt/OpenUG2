/* U2PK v1 container layout. See docs/U2PK.md - that document is the contract
 * and the byte offsets in its tables are what the PSP GE itself expects.
 *
 * Shared by the converter (host) and the runtime (PSP). Every multi-byte field
 * lands on a natural boundary despite the packed attribute, because the section
 * it lives in is 16-byte aligned and every element size is a multiple of 4;
 * u2pk_read.c enforces the alignment so the PSP never takes a bus error on a
 * packed float. */
#ifndef OPENUG2_U2PK_H
#define OPENUG2_U2PK_H

#include <stddef.h>
#include <stdint.h>

#define U2PK_MAGIC "U2PK"
#define U2PK_VERSION 1
#define U2PK_ALIGN 16u

/* kinds */
#define U2PK_KIND_CAR 1
#define U2PK_KIND_REGION 2
#define U2PK_KIND_CELL 3

/* section types */
#define U2PK_S_BATCHES 1u
#define U2PK_S_VERTS 2u
#define U2PK_S_INDICES 3u
#define U2PK_S_TEXTURES 4u
#define U2PK_S_CLUTS 5u
#define U2PK_S_CARINFO 6u
#define U2PK_S_COLLIDE 7u  /* M4 */
#define U2PK_S_CELLTAB 8u  /* M4 */

/* Batch.mode */
#define U2PK_MODE_OPAQUE 0u
#define U2PK_MODE_CUTOUT 1u
#define U2PK_MODE_BLEND 2u

/* TexHeader.psm */
#define U2PK_PSM_T8 5u    /* GU_PSM_T8 */
#define U2PK_PSM_DXT1 8u  /* GU_PSM_DXT1 */

#define U2PK_MAX_SECTIONS 8u
#define U2PK_LODS 2

/* World-cell grid side length in metres (M4). Shared so the converter that
 * packs cells and the runtime that maps a pose to a cell agree by construction;
 * the index stores the grid origin but not this, so both sides must use it. */
#define U2PK_CELL_M 96.0f

typedef struct {
	char magic[4];
	uint16_t version;
	uint16_t kind;
	uint32_t section_count;
	uint32_t total_size;
	uint32_t flags;
	uint8_t reserved[12];
} U2pkHeader;

typedef struct {
	uint32_t type;
	uint32_t offset;
	uint32_t size;
	uint32_t count;
} U2pkSection;

typedef struct {
	uint16_t u, v;
	uint16_t color; /* GU_COLOR_5650: 5-5-5, R in the low bits */
	int16_t x, y, z; /* metres = s16/32768 * pos_scale + pos_offset */
} U2pkVertex;

typedef struct {
	uint32_t first_vertex, vertex_count;
	uint32_t first_index, index_count;
	int32_t texture; /* -1 = none */
	uint32_t mode;
	float pos_scale[3];
	float pos_offset[3];
	float uv_scale[2];
	float uv_offset[2];
} U2pkBatch;

typedef struct {
	uint16_t w, h;
	uint16_t psm;
	uint16_t swizzled;
	uint32_t data_offset; /* absolute, multiple of 16 */
	uint32_t data_size;
	int32_t clut; /* index into CLUTS, -1 = none */
	uint32_t reserved[3];
} U2pkTexHeader;

typedef struct {
	uint32_t lod_count;
	uint32_t batch_first[U2PK_LODS];
	uint32_t batch_count[U2PK_LODS];
	uint32_t wheel_batch_first, wheel_batch_count;
	float wheel_pos[4][3]; /* FL, FR, RL, RR */
	float wheel_radius;
	float body_box[6]; /* x0,x1,y0,y1,z0,z1 */
	uint32_t reserved[2];
} U2pkCarInfo;

/* 32-byte header + 8x16 table, and the per-element sizes the geometry flags and
 * the GE's own vertex stride depend on. */
_Static_assert(sizeof(U2pkHeader) == 32, "U2pkHeader must be 32 bytes");
_Static_assert(sizeof(U2pkSection) == 16, "U2pkSection must be 16 bytes");
_Static_assert(sizeof(U2pkVertex) == 12, "U2pkVertex must be 12 bytes");
_Static_assert(sizeof(U2pkBatch) == 64, "U2pkBatch must be 64 bytes");
_Static_assert(sizeof(U2pkTexHeader) == 32, "U2pkTexHeader must be 32 bytes");
_Static_assert(sizeof(U2pkCarInfo) == 112, "U2pkCarInfo must be 112 bytes");

/* Borrowed views into a buffer the caller owns. A null pointer means the file
 * had no such section (allowed for everything except BATCHES/VERTS/INDICES/CARINFO
 * in a car file, which u2pk_open requires). */
typedef struct {
	const uint8_t *buf;
	uint32_t len;
	U2pkHeader hdr; /* copied out so readers never chase unaligned header bytes */
	const U2pkSection *sect; /* hdr.section_count entries */
	const U2pkBatch *batch;
	uint32_t nbatch;
	const U2pkVertex *vert;
	uint32_t nvert;
	const uint16_t *index;
	uint32_t nindex;
	const U2pkTexHeader *tex;
	uint32_t ntex;
	const uint32_t *clut; /* 256 entries per clut */
	uint32_t nclut;
	const U2pkCarInfo *car;
	const char *err; /* set only when u2pk_open returns 0 */
} U2pkView;

/* 1 = accepted, 0 = rejected with view->err set. Never reads out of bounds and
 * never writes to buf. See "Reading rules" in docs/U2PK.md. */
int u2pk_open(const uint8_t *buf, uint32_t len, U2pkView *out);

#endif /* OPENUG2_U2PK_H */
