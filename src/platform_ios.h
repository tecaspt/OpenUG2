/* platform_ios.h - hooks for the iOS/iPadOS build (N2_IOS). On every other
 * build the macros below compile to nothing. Implementation: ios/platform_ios.c. */
#ifndef PLATFORM_IOS_H
#define PLATFORM_IOS_H

#if defined(N2_IOS) && !defined(N2_GLES)
#error "N2_IOS requires N2_GLES"
#endif

#ifdef N2_IOS
#include <SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
int  n2_ios_startup(int *pargc, char ***pargv);   /* nonzero: exit with failure */
void n2_ios_frame_gate(void);                     /* blocks while backgrounded */
/* Lowers the GL view's backing scale so the drawable stays within N2_IOS_PIXEL_BUDGET pixels (never below 1x,
 * never above the screen's native scale). Call once after the GL context exists. No-op off Apple platforms. */
void n2_ios_apply_render_scale(SDL_Window *win);
#define N2_IOS_PIXEL_BUDGET 2100000.0   /* about 1920x1080: 3x iPhones drop to ~2.2x, 2x iPad Pros to ~1.2x */
/* The scale chosen for a view of w x h points at `native` scale (pure; also used by the host check). */
static inline double n2_ios_render_scale(double w, double h, double native, double budget) {
    if (!(w > 0) || !(h > 0) || !(native > 0)) return native > 0 ? native : 1.0;
    double s = native;
    while (s > 1.0 && w * s * h * s > budget) s -= 0.05;   /* simple, monotonic, no libm needed */
    return s < 1.0 ? 1.0 : s;
}
#ifdef __cplusplus
}
#endif
#define N2_IOS_STARTUP_OR_FAIL(pa, pv) { if (n2_ios_startup((pa), (pv))) return 1; }   /* nothing elsewhere */
#define N2_IOS_FRAME_GATE()     n2_ios_frame_gate()
#define N2_IOS_WINDOW_FLAGS     (SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI)   /* pixel resolution; capped by n2_ios_apply_render_scale */
#define N2_IOS_AFTER_GL(win)    n2_ios_apply_render_scale(win)
#else
#define N2_IOS_STARTUP_OR_FAIL(pa, pv)
#define N2_IOS_FRAME_GATE()     ((void)0)
#define N2_IOS_WINDOW_FLAGS     0
#define N2_IOS_AFTER_GL(win)    ((void)(win))
#endif

#endif
