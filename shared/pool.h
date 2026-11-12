#ifndef OPENUG2_POOL_H
#define OPENUG2_POOL_H

#include <stddef.h>
#include <stdint.h>

/* D8 budget: total runtime use must stay at or under 17 MB on a PSP-1000. */
#define POOL_TOTAL (17u * 1024u * 1024u)

typedef struct {
	const char *name;
	uint8_t *base;
	size_t cap, used, high;
} Pool;

/* Indices into the single carve-out; sizes follow the D8 table. */
enum {
	POOL_STACKS = 0,
	POOL_WORLD,
	POOL_TEXTURES,
	POOL_CARS,
	POOL_PHYSICS,
	POOL_AUDIO,
	POOL_HUD,
	POOL_COUNT
};

/* One memalign for everything; returns NULL if the heap cannot give 17 MB. */
int pool_init_all(void);

/* Partition free at boot, kept for the log. NOT a model discriminator: the
 * loader pre-allocates nearly all of it into the C heap, so it reads ~1 MB
 * on every model (see runtime/pool.c). */
size_t pool_boot_total_free(void);
size_t pool_boot_max_free(void);

/* Top physical (uncached) address covered by the 17 MB arena; 0 before
 * pool_init_all(). On a PSP-1000 the HOME partition ends at 0x0A000000, so
 * this staying below that bound is the user-mode extended-memory check. */
unsigned int pool_arena_top_phys(void);

Pool *pool_get(int which);
void *pool_alloc(Pool *p, size_t size, size_t align);
void pool_reset(Pool *p);
void pool_report(void);

#endif
