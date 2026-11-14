# Task B integration checkpoint (partial; not ready for M1 merge)

Implemented additive API: `void fem_show_error(FeMenu *, const char *message)` in fe_menu.h.
At the `GF_CMD_ERROR` marker use `fem_show_error(fem, cmd.message)` (substitute the actual local
GfCommand variable name). This opens QR_TRACK and stores the message in `fem_status`.
QR_TRACK remains unavailable at this checkpoint; there is no rendered error text yet.
Free Run requests now carry `track="STREAML4RA"` and the current car.

No SRC additions yet. Do not wire postrace: it is not implemented.
Pending: U5 track/options and generic list API, U6 car selection/preview, U7 postrace,
all eight main.c postrace markers, real request selection, full integration gate and captures.
The existing temporary first-event shortcut remains and must not be mistaken for completed U5/U6.

## New test targets (exact recipes)

Run from the worktree after sourcing env.sh; every compile/run under the heavy lock with nice -n 10.

```sh
cc -std=c99 -Wall -Wextra -Werror -Isrc tools/fe_binding_test.c -o build/fe_binding_test
build/fe_binding_test
cc -std=c99 -DN2_GLES -Wall -Wextra -Werror -Isrc tools/fe_binding_test.c -o build/fe_binding_test_gles
build/fe_binding_test_gles
cc -std=c99 -D_DEFAULT_SOURCE -Wall -Wextra -Isrc tools/fe_binding_check.c \
 src/ugui/ugui_bundle.c src/ugui/ugui_codec.c src/ugui/ugui_fng.c \
 src/ugui/ugui_util.c src/ugui/ugui_anim.c -lm -o build/fe_binding_check
build/fe_binding_check
```

Suggested targets: `fe-binding-test` (synthetic) and `fe-binding-check` (runtime retail).
The runtime checker returns 77 if OPENUG2_DATA is unset; report SKIP, never PASS.
