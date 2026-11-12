/* converter/shim/SDL.h - the minimum SDL surface the upstream world loader's
 * headers name, so converter/region_load.c can compile world.c on the host
 * WITHOUT SDL or GL installed. The upstream render.h includes <SDL.h> only for
 * a few opaque types (SDL_Window, SDL_WindowFlags) and a couple of declarations
 * (SDL_GL_SwapWindow, SDL_Event) that the loader code never calls; only the GL
 * backend (rhi_gl*.c, render.c) uses them for real, and the converter does not
 * link those. This shim is on the converter's include path ONLY - the PSP
 * runtime and the upstream game never see it.
 */
#ifndef OPENUG2_PSP_CONVERTER_SDL_SHIM
#define OPENUG2_PSP_CONVERTER_SDL_SHIM

typedef struct SDL_Window SDL_Window;      /* opaque: only ever used by pointer */
typedef unsigned int SDL_WindowFlags;

/* Referenced by rhi.h / debug.h declarations; never called on the loader path,
 * so a bare prototype that nothing links against is enough. */
int SDL_GL_SwapWindow(SDL_Window *window);

typedef union SDL_Event { int dummy; } SDL_Event;

#endif
