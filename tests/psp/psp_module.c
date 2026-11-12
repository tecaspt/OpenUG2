/* Module identity + main-thread setup for the M1 upstream test EBOOTs.
 * Upstream sources must stay untouched, so the PSP-only glue lives here and
 * common.mk links it into every test (one definition, one TU). */
#include <pspuser.h>
#include <pspiofilemgr.h>
#include <malloc.h>
#include <errno.h>
#include <stdio.h>

PSP_MODULE_INFO("UG2TEST", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
/* Give the tests nearly the whole partition; they never allocate much, but
 * drive-stage1 runs long physics loops through plain malloc. */
PSP_HEAP_SIZE_KB(-1024);

/* The one and only capture-file state shared by every TU's compat copies
 * (see psp_compat.h). */
int psp_compat_fd = -1;
char psp_compat_obuf[4096];

/* Loud allocation failure (the compat macros route every test TU malloc,
 * calloc and realloc through these). Returns the plain-libc result. */
#undef malloc
#undef calloc
#undef realloc
#undef printf

static void psp_compat_oom(const char *what, unsigned long n)
{
	/* newlib's mallinfo() does not track pspsdk's allocator (it reports a
	 * 1.5 KB arena while megabytes are live), so ask the kernel, which owns
	 * the partition the C heap was carved from, and retry the exact request
	 * to separate a hard limit from a transient one. */
	char msg[128];
	int k;
	void *again = malloc(n);
	errno = 0;
	k = snprintf(msg, sizeof msg,
	             "COMPAT %s FAILED n=%lu errno=%d kfree=%d kmax=%d retry=%p\n",
	             what, n, errno, (int)sceKernelTotalFreeMemSize(),
	             (int)sceKernelMaxFreeMemSize(), again);
	if (again)
		free(again);
	if (k > 0 && psp_compat_fd >= 0)
		sceIoWrite(psp_compat_fd, msg, (SceSize)k);
}

void *psp_compat_malloc(size_t size)
{
	void *p = malloc(size);
	if (!p)
		psp_compat_oom("malloc", (unsigned long)size);
	return p;
}

void *psp_compat_calloc(size_t n, size_t size)
{
	void *p = calloc(n, size);
	if (!p)
		psp_compat_oom("calloc", (unsigned long)(n * size));
	return p;
}

void *psp_compat_realloc(void *p, size_t size)
{
	void *q = realloc(p, size);
	if (!q)
		psp_compat_oom("realloc", (unsigned long)size);
	return q;
}

/* psp_compat.h (force-included) renames the test's main; this TU owns the
 * real one so pending capture IO settles before the PRX crt0 exits. */
#undef main
int psp_compat_test_main(int argc, char **argv);

static void psp_compat_finish(void)
{
	if (psp_compat_fd >= 0) {
		sceIoClose(psp_compat_fd);
		psp_compat_fd = -1;
	}
	/* PPSSPP commits deferred ms0 writes asynchronously; leave it time
	 * while all our buffers (.data) are still alive. */
	sceKernelDelayThread(500000);
	sceKernelExitGame();
}

/* Upstream tests use assert(); the libc __assert_func path aborts without
 * flushing and without leaving the game. This definition wins over libc.a
 * (archives are only consulted for undefined symbols), so a failed assert
 * still ends the run cleanly: the harness then sees a truncated capture
 * instead of a 60 s emulator timeout. */
void __assert_func(const char *file, int line, const char *func,
                   const char *expr)
{
	(void)file; (void)line; (void)func; (void)expr;
	psp_compat_finish();
	for (;;)
		;
}

int main(int argc, char **argv)
{
	int rc;
	char msg[128];
	int k;
	/* Boot-time memory report for the test log (malloc capacity is part of
	 * what M1 measures; see the 4 MB malloc FAILED on map-bake-test). */
	k = snprintf(msg, sizeof msg,
	             "MEM kfree=%d kmax=%d heap_kb=%d\n",
	             (int)sceKernelTotalFreeMemSize(), (int)sceKernelMaxFreeMemSize(),
	             sce_newlib_heap_kb_size);
	if (k > 0 && psp_compat_fd >= 0)
		sceIoWrite(psp_compat_fd, msg, (SceSize)k);
	rc = psp_compat_test_main(argc, argv);
	psp_compat_finish();
	return rc;
}
