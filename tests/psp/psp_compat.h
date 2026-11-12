/* Included into every upstream source when it is cross-compiled for the PSP
 * (CFLAGS carries -include ../../psp_compat.h). Empty by intent: add ONLY what
 * actually fails to compile or run on the PSP, nothing speculative.
 */
#ifndef OPENUG2_PSP_COMPAT_H
#define OPENUG2_PSP_COMPAT_H

#ifdef PSP

#include <pspuser.h>
#include <pspiofilemgr.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Pull in every libc allocator prototype BEFORE the malloc/calloc/realloc
 * macros below exist: this header is force-included at the top of each TU, so
 * a libc header included later would otherwise have its declarations macro-
 * expanded (malloc.h declares calloc/realloc, hence a hard parse error). */
#include <malloc.h>

/* PPSSPP headless drops guest stdout, and ms0 file writes are DEFERRED:
 * they commit later, straight out of the caller's buffer. Anything flushed
 * near sceKernelExitGame lands on freed stack/heap and arrives as NULs of
 * the right length. stdio (even unbuffered) writes from stack temporaries,
 * so it cannot be trusted here. Instead printf/puts/fputs/fprintf are
 * rerouted through sceIoWrite with the formatting done in module-scope
 * .data, which outlives the guest thread; psp_module.c still waits half a
 * second before leaving so pending IO commits. */
extern int psp_compat_fd;
extern char psp_compat_obuf[4096];

static void psp_compat_open(void)
{
	if (psp_compat_fd < 0)
		psp_compat_fd = sceIoOpen("stdout.txt",
		                          PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC,
		                          0644);
}

static void psp_compat_raw_write(const char *p, int n)
{
	while (n > 0 && psp_compat_fd >= 0) {
		int w = sceIoWrite(psp_compat_fd, p, (SceSize)n);
		if (w <= 0)
			return;
		p += w;
		n -= w;
	}
}

static int psp_compat_vout(const char *fmt, va_list ap)
{
	int n = vsnprintf(psp_compat_obuf, sizeof psp_compat_obuf, fmt, ap);
	if (n < 0) {
		/* vsnprintf failure must not vanish silently (see M1 debug
		 * history in docs/PPSSPP.md). */
		static const char msg[] = "COMPAT vsnprintf FAILED\n";
		psp_compat_raw_write(msg, (int)sizeof msg - 1);
		return n;
	}
	if (n >= (int)sizeof psp_compat_obuf)
		n = (int)sizeof psp_compat_obuf - 1; /* no test line comes close */
	psp_compat_raw_write(psp_compat_obuf, n);
	return n;
}

static int psp_compat_printf(const char *fmt, ...)
{
	va_list ap;
	int n;
	va_start(ap, fmt);
	n = psp_compat_vout(fmt, ap);
	va_end(ap);
	return n;
}

static int psp_compat_puts(const char *s)
{
	int n = psp_compat_printf("%s\n", s);
	return n < 0 ? EOF : n;
}

/* Runs before main in every TU; the first copy opens the capture file. */
__attribute__((constructor)) static void psp_compat_ctor(void)
{
	psp_compat_open();
}

/* printf/puts go to the capture. fprintf and fputs are NEVER used for
 * console output by these nine tests (audited: camera writes its settings
 * file with them) — redirecting those would corrupt the tests' own file
 * IO, which is exactly the M1 "5 bogus camera FAILs". */
#define printf(...) psp_compat_printf(__VA_ARGS__)
#define puts(s) psp_compat_puts(s)

/* Allocation failure must be LOUD: test_json.h happily builds a broken DOM
 * out of NULL results, which surfaces as 72 bogus "unhandled (null)" FAILs
 * before a stray pointer crash. psp_module.c defines the real functions. */
extern void *psp_compat_malloc(size_t size);
extern void *psp_compat_calloc(size_t n, size_t size);
extern void *psp_compat_realloc(void *p, size_t size);
#define malloc(s) psp_compat_malloc(s)
#define calloc(n, s) psp_compat_calloc((n), (s))
#define realloc(p, s) psp_compat_realloc((p), (s))

/* test_json.h sizes every JSON string with `malloc(strlen(jp) + 1)` — the
 * whole REST OF THE FILE, not the string. Host mallocs shrug that off (~34 MB
 * live for stage4's 44 KB file); it exhausts a PSP heap and the test then
 * fails with junk output. A 24 MB machine cannot host that pattern, so
 * strlen is capped: no string in the test data or the tested code comes
 * anywhere near 8 KB, and the oversized allocations were only ever used as
 * slack — nothing re-reads the length. (All legitimate >8 KB buffers in the
 * nine tests are sized by ftell/fread counts or calloc, not strlen: audited
 * 02:20 2026-11-11. The function is defined BEFORE the macro on purpose.) */
#define PSP_COMPAT_STRLEN_CAP 8192u
static size_t psp_compat_strlen(const char *s)
{
	size_t n = strlen(s);
	return n > PSP_COMPAT_STRLEN_CAP ? PSP_COMPAT_STRLEN_CAP : n;
}
#define strlen(s) psp_compat_strlen(s)

/* Rename the upstream test's main; the wrapper in psp_module.c calls it. */
#define main psp_compat_test_main

#endif /* PSP */
#endif /* OPENUG2_PSP_COMPAT_H */
