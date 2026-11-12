# PPSSPPHeadless as used by this repo

Built from PPSSPP v1.20.4 (`git clone --depth 1 --branch v1.20.4`), configured as:

```
. /home/nike/openug2-dev/env.sh
cmake .. -DHEADLESS=ON -DUSING_GLES2=ON -DUSING_X11_VULKAN=OFF -DCMAKE_BUILD_TYPE=Release
make -j2 PPSSPPHeadless
```

Configure notes for this Debian 13 aarch64 host:
- `USING_GLES2=ON` is needed because `find_package(OpenGL REQUIRED)` under
  `CMP0072 NEW` demands the GLVND `libOpenGL.so`, which this mesa layout does not
  ship. With `USING_GLES2=ON`, PPSSPP sets `OPENGL_LIBRARIES=GLESv2;EGL` and skips
  that lookup; the headers and import libraries come from the user-space sysroot
  in `/home/nike/openug2-dev/env.sh` (which must be sourced: CMake's `find_path`
  ignores `C_INCLUDE_PATH`, so sourcing alone does not help — the option does).
- `USING_X11_VULKAN=OFF` is needed because `find_package(X11)` fails here and the
  resulting `X11_Xlib_INCLUDE_PATH-NOTFOUND` string leaks into the include
  directories of `ext/zstd-build`, breaking the generate step.
- PPSSPP is a **test tool only** (GPL). Nothing of it is linked into or copied
  into our code.

## (a) Command-line flags we use

From `headless/Headless.cpp` usage text and its argument parser:

| Purpose | Flag |
|---|---|
| timeout | `--timeout=SECONDS` (aborts the test if it takes longer) |
| graphics backend | `--graphics=BACKEND` (`gles`, `software`, `directx9`, ...); we use `software` |
| screenshot compare | `--screenshot=FILE` plus `--max-mse=NUMBER` |
| full logging | `-l`, `--log` (otherwise only emulated printfs) |
| mount ISO / host0 | `-m, --mount`, `-r, --root some/path` |
| compare stdout | `-c, --compare` against `file.expected` |
| cpu | `-i` interpreter, `-j` jit (default), `--ir` |

We do **not** use PPSSPP's screenshot comparison; our runtime writes its own PPM
via `sceGuSync` and `tools/ppm_cmp` compares it, so the pixels come from the GE's
own framebuffer rather than a host-side capture.

Invocation used by `tests/psp/run.sh`:

```
PPSSPPHeadless <ms0>/PSP/GAME/OPENUG2/EBOOT.PBP --graphics=software --timeout=60
```

## (b) How ms0: is mapped

There is **no command-line flag** for the memory-stick directory. Upstream, on
Linux, `Headless.cpp` hard-codes:

```cpp
g_Config.memStickDirectory = Path(std::string(getenv("HOME"))) / ".ppsspp";
```

Per the plan's decision for this case, we patch it (see
`docs/ppsspp-local.patch`) so ms0: is
`/home/nike/openug2-dev/psp/openug2-psp/out/ms0`, with an `OPENUG2_MS0`
environment override. Homebrew writes `log.txt` and screenshots relative to the
game folder, so they land on the host filesystem inside `out/ms0/...` where the
tests can read them.

## (c) PSP model selection

`Headless.cpp` has no model flag but hard-codes a model:

```cpp
g_Config.iPSPModel = PSP_MODEL_SLIM;   /* upstream */
```

`PSP_MODEL_FAT = 0`, `PSP_MODEL_SLIM = 1`. Since a PSP-1000 is what we target, we
patch this to `PSP_MODEL_FAT` (also in `docs/ppsspp-local.patch`), so the emulated
user partition is the 24 MB one. Our runtime additionally never requests extended
memory (`PSP_LARGE_MEMORY = 0`, so `MEMSIZE = 0` in PARAM.SFO, checked by
`tools/sfo_check.py`), which is what keeps a real PSP-1000 in its normal partition.

**All numbers produced by this tool are emulator numbers, not hardware results.**

## (d) Guest stdout and file writes (measured here, cost hours to rediscover)

1. **Guest stdout is dropped.** `printf` from the emulated program reaches
   nowhere in headless mode (the console path is not wired to the host
   terminal). Homebrew output must be written to files.
2. **ms0 file writes are deferred and commit from the caller's buffer.**
   `sceIoWrite` returns success immediately; the bytes are copied out of
   guest memory later. Anything written from a *stack* temporary shortly
   before `sceKernelExitGame` commits onto freed memory: the host file ends
   up the **right size and all NUL**. `tests/psp/psp_compat.h +
   psp_module.c` therefore format into a module-scope `.data` buffer, write
   via `sceIoWrite` directly (stdio cannot be trusted for this), and wait
   500 ms before exiting the game.
3. **A PRX's crt0 does not run atexit or stdio cleanup.** When `main`
   returns, `crt0_prx` calls `sceKernelExitGame` directly; any buffered
   stdio output is lost. The test harness wraps the test's `main`
   (`-include psp_compat.h` renames it) and flushes/closes/waits itself.

A verification tool for all three: `out/wrtprobe.c` history lives in the
M1 commits; the pattern to copy is any `tests/psp/m1/*/EBOOT.PBP`.
