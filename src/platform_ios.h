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
int  n2_ios_pause_event(const SDL_Event *e);      /* two-finger tap opens pause */
#ifdef __cplusplus
}
#endif
#define N2_IOS_STARTUP_OR_FAIL(pa, pv) { if (n2_ios_startup((pa), (pv))) return 1; }   /* nothing elsewhere */
#define N2_IOS_FRAME_GATE()     n2_ios_frame_gate()
#define N2_IOS_PAUSE_EVENT(pe)  n2_ios_pause_event(pe)
#define N2_IOS_WINDOW_FLAGS     (SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALLOW_HIGHDPI)   /* full pixel resolution, not points */
#else
#define N2_IOS_STARTUP_OR_FAIL(pa, pv)
#define N2_IOS_FRAME_GATE()     ((void)0)
#define N2_IOS_PAUSE_EVENT(pe)  0
#define N2_IOS_WINDOW_FLAGS     0
#endif

#endif
