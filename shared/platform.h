#ifndef OPENUG2_PLATFORM_H
#define OPENUG2_PLATFORM_H

#include <stdint.h>

/* Set by main() from argv; everything else is loaded relative to it. */
void plat_init(int argc, char **argv);

/* Directory holding EBOOT.PBP, e.g. "ms0:/PSP/GAME/OPENUG2". Never NULL. */
const char *plat_dir(void);

/* Full path helper: writes "<dir>/<rel>" into dst. */
void plat_path(char *dst, int dstsize, const char *rel);

/* printf to stdout (PPSSPP shows it) and append to <dir>/log.txt. */
void plat_log(const char *fmt, ...);

/* Monotonic microseconds. */
uint64_t plat_us(void);

/* Non-zero once the user pressed the PS/home exit button. */
extern volatile int g_quit;

/* Registers the exit callback on a helper thread (pspsdk sample pattern). */
void plat_setup_callbacks(void);

/* 1 when the file exists. */
int plat_file_exists(const char *rel);

#endif
