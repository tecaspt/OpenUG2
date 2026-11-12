/* One static carve-out of the whole 17 MB budget, handed out by bump allocation.
 * Nothing else in the runtime calls malloc after boot.
 */
#include <pspuser.h>
#include <stdlib.h>
#include <malloc.h>   /* memalign lives here in this libc */
#include <string.h>
#include "platform.h"
#include "pool.h"

static const size_t g_pool_cap[POOL_COUNT] = {
	256u * 1024u,          /* POOL_STACKS   0.25 MB */
	3u * 1024u * 1024u,    /* POOL_WORLD    3.00 MB */
	4u * 1024u * 1024u,    /* POOL_TEXTURES 4.00 MB */
	1536u * 1024u,         /* POOL_CARS     1.50 MB */
	1024u * 1024u,         /* POOL_PHYSICS  1.00 MB */
	1024u * 1024u,         /* POOL_AUDIO    1.00 MB */
	512u * 1024u,          /* POOL_HUD      0.50 MB */
};
static const char *g_pool_name[POOL_COUNT] = {
	"stacks", "world", "textures", "cars", "physics", "audio", "hud"
};

static uint8_t *g_arena;
static Pool g_pools[POOL_COUNT];
static size_t g_boot_total_free, g_boot_max_free;

size_t pool_boot_total_free(void) { return g_boot_total_free; }
size_t pool_boot_max_free(void) { return g_boot_max_free; }

/* End of the 17 MB arena as an uncached physical address (0x0........). */
unsigned int pool_arena_top_phys(void)
{
	if (!g_arena)
		return 0;
	/* malloc hands out uncached 0x0....... views; normalise anyway so a
	 * cached 0x8....... pointer cannot sneak past the 0x0A000000 bound. */
	return ((unsigned int)g_arena & 0x7FFFFFFFu) + (unsigned int)POOL_TOTAL;
}

int pool_init_all(void)
{
	size_t carve = 0;
	int i;

	/* Partition free at main() entry is NOT a model discriminator: the loader
	 * has already moved nearly all of it into the C heap (PSP_HEAP_SIZE_KB
	 * (-1024)), so it reads ~1 MB on every model. The discriminator that does
	 * work from user mode is the physical address the loader hands us: a
	 * PSP-1000 (24 MB HOME) lives entirely below 0x0A000000; anything
	 * allocated at or above that came from extended memory. */
	g_boot_total_free = (size_t)sceKernelTotalFreeMemSize();
	g_boot_max_free = (size_t)sceKernelMaxFreeMemSize();
	plat_log("pool: boot part_free=%d part_max=%d",
	         sceKernelTotalFreeMemSize(), sceKernelMaxFreeMemSize());

	for (i = 0; i < POOL_COUNT; i++)
		carve += g_pool_cap[i];
	if (carve > POOL_TOTAL)
		return -1;

	g_arena = (uint8_t *)memalign(16, POOL_TOTAL);
	if (!g_arena) {
		plat_log("POOL INIT FAILED: memalign(%u) (max_free=%d)",
		         (unsigned)POOL_TOTAL, sceKernelMaxFreeMemSize());
		return -1;
	}

	{
		uint8_t *b = g_arena;
		for (i = 0; i < POOL_COUNT; i++) {
			g_pools[i].name = g_pool_name[i];
			g_pools[i].base = b;
			g_pools[i].cap = g_pool_cap[i];
			g_pools[i].used = 0;
			g_pools[i].high = 0;
			b += g_pool_cap[i];
		}
	}
	plat_log("pool: arena=%p carve=%u of %u bytes", (void *)g_arena,
	         (unsigned)carve, (unsigned)POOL_TOTAL);
	return 0;
}

Pool *pool_get(int which)
{
	if (which < 0 || which >= POOL_COUNT)
		return NULL;
	return &g_pools[which];
}

void *pool_alloc(Pool *p, size_t size, size_t align)
{
	size_t off, aligned;
	void *ptr;

	if (!p)
		return NULL;
	if (align < 16)
		align = 16;
	off = p->used;
	aligned = (off + (align - 1)) & ~(align - 1);
	if (aligned + size > p->cap) {
		plat_log("POOL FULL %s (want %u align %u, used %u cap %u)", p->name,
		         (unsigned)size, (unsigned)align, (unsigned)p->used, (unsigned)p->cap);
		return NULL;
	}
	ptr = p->base + aligned;
	p->used = aligned + size;
	if (p->used > p->high)
		p->high = p->used;
	return ptr;
}

void pool_reset(Pool *p)
{
	if (p)
		p->used = 0;
}

void pool_report(void)
{
	int i;
	size_t high_total = 0;

	for (i = 0; i < POOL_COUNT; i++) {
		plat_log("pool %-8s cap=%-8u high=%-8u", g_pools[i].name,
		         (unsigned)g_pools[i].cap, (unsigned)g_pools[i].high);
		high_total += g_pools[i].high;
	}
	plat_log("pool high-water total=%u arena=%u free_total=%d free_max=%d",
	         (unsigned)high_total, (unsigned)POOL_TOTAL,
	         sceKernelTotalFreeMemSize(), sceKernelMaxFreeMemSize());
}
