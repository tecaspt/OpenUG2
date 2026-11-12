/* Platform glue: module attributes, exit callback, logging, timing.

 * The exit-callback pattern here is taken from pspsdk's
 * samples/gu/common/callbacks.c (BSD licence, see THIRD_PARTY.md).
 */
#include <pspuser.h>
#include <pspiofilemgr.h>
#include <pspctrl.h>
#include <psprtc.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "platform.h"

PSP_MODULE_INFO("OpenUG2", 0, 1, 0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER | THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

volatile int g_quit = 0;

static char g_dir[256] = ".";
static char g_logpath[300];

/* ---- exit callback (pspsdk sample pattern, BSD) ---- */

static int exit_callback(int arg1, int arg2, void *common)
{
	(void)arg1; (void)arg2; (void)common;
	g_quit = 1;
	return 0;
}

static int callback_thread(SceSize args, void *argp)
{
	int cbid;
	(void)args; (void)argp;
	cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);
	sceKernelSleepThreadCB();
	return 0;
}

void plat_setup_callbacks(void)
{
	int thid = sceKernelCreateThread("exit_thread", callback_thread, 0x11, 0xFA0, 0, 0);
	if (thid >= 0)
		sceKernelStartThread(thid, 0, 0);
}

/* ---- paths ---- */

void plat_init(int argc, char **argv)
{
	const char *a0 = (argc > 0 && argv && argv[0]) ? argv[0] : "EBOOT.PBP";
	const char *slash = strrchr(a0, '/');
	int n;

	(void)argv;
	if (slash) {
		n = (int)(slash - a0);
		if (n > (int)sizeof(g_dir) - 1)
			n = (int)sizeof(g_dir) - 1;
		memcpy(g_dir, a0, n);
		g_dir[n] = 0;
	} else {
		strcpy(g_dir, ".");
	}
	plat_path(g_logpath, (int)sizeof(g_logpath), "log.txt");
	/* Start a fresh log for every run; the user reports this file back to us. */
	{
		int fd = sceIoOpen(g_logpath, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
		if (fd >= 0)
			sceIoClose(fd);
	}
}

void plat_path(char *dst, int dstsize, const char *rel)
{
	snprintf(dst, dstsize, "%s/%s", g_dir, rel);
}

const char *plat_dir(void)
{
	return g_dir;
}

int plat_file_exists(const char *rel)
{
	char p[300];
	SceIoStat st;
	plat_path(p, (int)sizeof(p), rel);
	return sceIoGetstat(p, &st) >= 0;
}

/* ---- logging ---- */

void plat_log(const char *fmt, ...)
{
	/* .data, not a stack local: ms0 writes are deferred and commit later out of
	 * the buffer they were handed, so a buffer that dies when this function
	 * returns can arrive as the right length of NULs (docs/PPSSPP.md (d)). One
	 * logging thread only - the callback thread never logs. */
	static char buf[512];
	va_list ap;
	int n, fd;

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	if (n < 0)
		return;
	if (n > (int)sizeof(buf) - 1)
		n = (int)sizeof(buf) - 1;

	buf[n++] = '\n';
	printf("%s", buf);
	fflush(stdout);

	fd = sceIoOpen(g_logpath, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND, 0777);
	if (fd >= 0) {
		sceIoWrite(fd, buf, n);
		sceIoClose(fd);
	}
}

/* ---- time ---- */

uint64_t plat_us(void)
{
	u64 tick = 0;
	u32 res = sceRtcGetTickResolution();

	if (sceRtcGetCurrentTick(&tick) < 0)
		return 0;
	if (res == 0)
		return 0;
	return (uint64_t)tick * 1000000ULL / res;
}
