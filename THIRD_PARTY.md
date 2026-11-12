# Third-party code

Only these sources are used. Retail/EA code is never read, copied or used.

| Source | Licence | Where it is used |
|---|---|---|
| pspsdk `samples/gu/common/callbacks.c` (pspdev, copyright 2005 Jesper Svennevid), from the pspdev v20261101 toolchain | BSD | Exit-callback pattern in `runtime/platform.c` (`exit_callback`, `callback_thread`, `plat_setup_callbacks`). |
| pspsdk GU sample init sequence (`samples/gu/*`), from the same toolchain | BSD | `runtime/gfx.c` follows the standard `sceGuInit`/`sceGuStart`/buffer/viewport/depth sequence. |
| pspsdk headers `psp/sdk/include/*` (`pspextratypes.h` SFO structs, `pspgum.h`, `pspiofilemgr.h`) | BSD | Used to define formats and APIs; no code copied. |
| OpenUG2 `third_party/OpenUG2/src/nfsu2.h`, `src/main.c` | MIT | Parser API used by `converter/` (M3). Code copied from `src/main.c` will be credited here as it is added. |
| PPSSPP v1.20.4 | GPL (test tool only) | Used solely as an emulator to run our test builds. **Never linked into or copied into our code.** Local patch: `docs/ppsspp-local.patch`. |
| pspdev v20261101 toolchain | its own bundled licences (see `toolchain/share/licenses`) | Build tool only; not redistributed in this repo. |

No file from any EA / *Need for Speed: Underground 2* decompilation, recompile, or
derivative (including nfsu2-recompiled, NFSU2-Revived-Android, nfsu2-native,
NFSU2Recomp, anything from `SPEED2.EXE`, or Underground Rivals) was read or used.
