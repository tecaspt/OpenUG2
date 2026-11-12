# OpenUG2 PSP - progress

One section per milestone from `../HANDOFF_PROMPT.md`. Every acceptance item is
recorded as PASS, FAIL or NOT RUN with the evidence that decided it, plus the
`dist/M<n>/` folder the user can put on a real PSP-1000. Nothing here is a
claim about hardware that has not been held in hand: PPSSPP proves the build
and the numbers, the user's PSP proves the hardware.

Reproduce any section with the commands under it. Emulator runs serialise on
`/home/nike/openug2-dev/.heavy.lock`; the toolchain comes from
`/home/nike/openug2-dev/psp/env-psp.sh`.

---

## M0 - hello GU on a PSP-1000

**Status: complete in PPSSPP. Hardware check pending: `dist/M0/`.**

### Files this milestone added

| Path | What it is |
| --- | --- |
| `runtime/Makefile` | pspsdk build.mak project, `PSP_LARGE_MEMORY=0`, `BUILD_PRX=1`, `PSP_FW_VERSION=500` |
| `runtime/main.c` | boot, the M0 scene (checker triangle + DXT1 quad), autotest frame loop and screenshot trigger |
| `runtime/platform.c` | module info, exit callback, `plat_dir`/`plat_log`/`plat_us` |
| `runtime/pool.c` | one 17 MB `memalign` arena carved into the D8 bump pools |
| `runtime/gfx.c` | GU setup in the pspsdk sample sequence, `g_draw` tracking across `sceGuSwapBuffers` |
| `runtime/shot.c` | framebuffer read (uncached alias), unswizzle, PPM writer |
| `shared/{gfx,pool,shot,platform,dxt,swizzle}.h`, `shared/{dxt,swizzle}.c` | interfaces plus DXT1 decode and texture unswizzle |
| `tools/sfo_check.py` | parses `PARAM.SFO` out of an `EBOOT.PBP` and checks `MEMSIZE` |
| `tools/ppm_cmp.c` | PPM comparison with an MSE limit, for the screenshot gate |
| `tools/peakrss.py` | host peak-RSS measurement, for the milestone that compares memory use |
| `tests/psp/run.sh` | stage an EBOOT into an ms0 image, run PPSSPPHeadless under the lock, collect log/screenshot |
| `tests/psp/m0.sh` | the M0 gate below, end to end |
| `docs/PPSSPP.md`, `docs/ppsspp-local.patch`, `THIRD_PARTY.md` | emulator facts that cost hours to learn, and the local patch to PPSSPP |

### Acceptance

| # | Item | Verdict | Evidence |
| --- | --- | --- | --- |
| 1 | `sfo_check.py` exits 0 | PASS | `MEMSIZE int 0`, `BOOTABLE 1`, `CATEGORY MG`, `TITLE OpenUG2`; "sfo_check: OK (MEMSIZE = 0)" |
| 2 | Heap within [20 MB, 24 MB] | PASS with a re-pointed instrument | `M0 OK arena_top=0x09a36e50`; `tests/psp/m0.sh` requires `0x08800000 < top <= 0x0A000000`. See the note below. |
| 3 | Screenshot matches the golden | PASS | `ppm_cmp: 480x272 samples=391680 mse=0.000000 limit=2.000000`, `ppm_cmp: PASS`, golden `out/goldens/m0.ppm` |
| 4 | DXT1 quad shows the red/blue checker | PASS | Sampling the golden every other pixel: 2064 red-dominant and 2870 blue-dominant samples out of 32640, in one quad-sized block |
| 5 | `psp-size openug2.elf`: text+data+bss <= 2.5 MB | PASS | `157344 + 3308 + 1094448 = 1255100` bytes = 1.20 MB; the 1.04 MB of bss is the 256 K-entry GU display list |

Reproduce: `tests/psp/m0.sh` (builds, checks the SFO, runs the emulator, checks
memory and the screenshot). Output captured in `out/m1/m0-final.log`.

### Why item 2 does not read the partition size

Written as "heap is within [20 MB, 24 MB]", the check cannot discriminate
models the way it looks like it does. `PSP_HEAP_SIZE_KB(-1024)` makes the loader
hand almost the whole user partition to the C heap before `main` runs, so
`sceKernelTotalFreeMemSize()` at entry reads about 782 KB on every PSP model -
it measures what the loader left over, not how much RAM the machine has. The
first version of this gate therefore failed on a build that was otherwise fine
(`782336 not in [20000000, 25165824]`).

What does discriminate is where the 17 MB arena physically lands. The runtime
logs `pool_arena_top_phys()`, the last byte of the arena: `0x09a36e50` is inside
the PSP-1000's 24 MB HOME window (`0x08800000..0x0A000000`), and anything at or
above `0x0A000000` could only have come from extended memory, which `MEMSIZE=0`
(item 1) already forbids. The gate now asserts that, plus the "17 MB exists at
all" half, which is `memalign(16, 17*1024*1024)` succeeding (`pool: arena=...
carve=11796480 of 17825792 bytes`).

### User test

`dist/M0/README.txt`: copy `PSP/` to the memory stick, start OpenUG2, expect a
checker triangle plus a red/blue square, buttons do nothing. It also says what
`log.txt` should contain and that `data/autotest.cfg` must NOT be shipped, since
that makes the program quit after N frames for the emulator.

---

## M1 - OpenUG2 GL-free tests on the PSP CPU

**Status: complete in PPSSPP; all nine tests pass and agree with the host.
Hardware check pending: `dist/M1/`.**

### Files this milestone added

| Path | What it is |
| --- | --- |
| `tests/psp/m1/common.mk` | shared cross-build: VPATH into the submodule, `-DPSP -include ../../psp_compat.h`, `LIBS=-lm`, links `psp_module.o` |
| `tests/psp/m1/<test>/Makefile` | one per test; OBJS mirror the upstream Makefile rule for that test |
| `tests/psp/psp_module.c` | the single `PSP_MODULE_INFO`, the real `main` (boot memory line, call the renamed test main, clean exit), loud allocation failure, `__assert_func` that still exits |
| `tests/psp/psp_compat.h` | the only PSP-only change to what the tests see: capture stdout, rename `main`, allocation and `strlen` shims (details below) |
| `tests/psp/m1/host.sh` | host arm: builds all nine with gcc from the OpenUG2 root and saves `out/m1/<test>.host.txt` |
| `tests/psp/m1/psp.sh` | PSP arm: cross-build, stage EBOOT + the 2 MB of cleanroom vectors, run in PPSSPP, save `out/m1/<test>.psp.txt`, grade identical, print the table with `psp-size` |
| `tests/psp/run.sh` | also collects the guest's `stdout.txt` now |
| `scripts/mkdist.sh` | builds `dist/M0` and `dist/M1` (EBOOTs cannot be committed; the guard rejects `*.PBP`), README text included |
| `docs/PPSSPP.md` | section (d): the three guest-IO traps this milestone found |

### The table (acceptance M1)

Both arms rebuilt from the same submodule commit `02e306a`, in this order:
`tests/psp/m1/host.sh` then `tests/psp/m1/psp.sh` (last full run:
`out/m1/host-run2.log`, `out/m1/psp-final.log`, both exit 0).

| Test | Host result | PSP result | Identical? | KB (text+data+bss) |
| --- | --- | --- | --- | --- |
| camera-test | `camera_test: 4735 checks, 0 failures` | same | yes | 269 |
| map-bake-test | `map_bake_test: PASS` (37 checks) | same | yes\* (5) | 150 |
| drivetrain-test | `PASS: 3825 checks, 0 failed` | same | yes | 242 |
| braking-test | `Stage 3: 479 checks, 0 failures` | same | yes | 200 |
| resistance-test | `Stage 2: 353 checks, 0 failures` | same | yes | 222 |
| lateral-test | `Stage4: 208 checks, 0 failures` | same | yes | 183 |
| contact-test | `Stage5: 77 vectors + 29 surfaces, 503 checks, 0 failures` | same | yes | 202 |
| body-response-test | `body-response: 63 vectors, 0 failures` | same | yes | 198 |
| drive-stage1-test | `Stage 1 gameplay adapter tests passed` | same | yes | 317 |

`identical` means the guest capture equals the host capture byte for byte after
CR removal and after dropping the harness's own `MEM` boot line, which has no
host counterpart. Nothing was hand-edited on either side. No test needed the
LIBM-ULP allowance: not one printed number differed in its last digit, which is
worth knowing before M2 starts timing this arithmetic.

`yes*` is graded by `psp.sh` itself, not by hand: the test's own lines are
byte-identical and the only extra lines are the harness's `COMPAT` allocation
diagnostics. `psp.sh` prints the count and still says `no` for any other kind of
difference.

### \* map-bake-test: the one machine limit, and why it is not a numeric difference

`map_bake.h:392` calls `mb_fill_seams()` while roughly 20 MB is already live
(`map_bake_compose` holds ~15 MB at s=1024 and the test holds `road`, `second`
and `out`, another 5 MB). The seam fill needs a `n*sizeof(int)` = 4 MB BFS queue
plus a 1 MB `seen` bitmap: about 25 MB at peak. A PSP-1000 gives a game a 24 MB
user partition and `PSP_HEAP_SIZE_KB(-1024)` asks that heap for everything but
1 MB, so 25 MB cannot be satisfied however the allocator behaves. Measured, not
assumed: the same EBOOT reports `MEM kfree=786432 kmax=524288`, and at each
failure the harness retries the identical 4 MB request (`retry=0x0`, five times)
while the kernel's free total never moves - a stable capacity limit, not
fragmentation and not a transient.

Upstream guards that one step with `if (queue && seen)` and does not fold it
into its `ok` result, so the fill is skipped and the bake returns normally.
Because all 37 checks still match the host capture exactly, the comparison is
itself the evidence that the skipped step changes nothing for these fixtures.
It also does not affect the port: the map bake is work for `psp_convert`, which
runs on the host, where 25 MB is nothing.

### The two shims in `psp_compat.h` (allowed by the "IF IT FAILS" rule)

1. **`strlen` capped at 8192.** `test_json.h` sizes every JSON string with
   `malloc(strlen(jp)+1)` - the length of the rest of the *file*, not the string.
   A host malloc shrugs that off (stage4's 44 KB file costs ~34 MB of slack);
   on a PSP it exhausts the heap, and `test_json.h` then builds a broken DOM out
   of NULLs, which surfaced as 72 bogus `unhandled (null)` FAILs in lateral-test
   before the run died on a stray pointer. Nothing re-reads the length, the
   longest legitimate token or line in the vectors is 711 bytes, and every real
   large buffer in these nine tests is sized by `ftell`/`fread`/`calloc` rather
   than `strlen` (audited). Without the cap, four of the nine tests cannot run
   on any PSP.
2. **Allocation failure is loud.** Every `malloc`/`calloc`/`realloc` in a test TU
   goes through a wrapper that writes `COMPAT malloc FAILED n=...` to the capture
   before returning NULL. This is what turned "72 mysterious failures" into
   "one 4 MB allocation the machine cannot satisfy", and it is why map-bake's
   limit is visible in the table instead of silently changing what runs.

Both live only behind `-include psp_compat.h`, which is PSP-only; the submodule
is untouched and pinned at `02e306a`. No `patches/upstream/*.patch` was needed:
no test disagreed with the host about a number.

### Deviations from the milestone text

- Step 5 says to reach `sceKernelExitGame()` through `atexit` in
  `psp_compat.h`. A PRX's crt0 does not run `atexit` or stdio cleanup on exit, so
  that path never executes; `psp_module.c` owns the real `main` instead, renames
  the upstream one to `psp_compat_test_main`, and finishes the capture itself.
- Capturing guest stdout needed more than a file write, and the reason is in
  `docs/PPSSPP.md` (d): headless PPSSPP drops guest stdout, and ms0 writes are
  deferred and commit from the *caller's* buffer - formatting into a stack
  temporary and exiting writes the right number of NUL bytes. Output is
  formatted into module-scope `.data`, written with `sceIoWrite`, then given
  500 ms before leaving.
- `main` is renamed by macro because the tests each define their own `main`; the
  harness needs to run code before and after them.

### Skipped tests, with the reason

- `ground-motion-test`, `race-ai-test`: both compile against `src/render.c` and
  the generated shaders, which need GL. The PSP build has no GL and will not get
  one; their subject matter (ground motion, race AI presentation) is covered on
  this port by the GU path, and the physics they depend on is covered by
  drive-stage1-test here and by M2.
- Scope note: the upstream Makefile at `02e306a` defines 84 test targets, of
  which the milestone names 11. The nine in the table are those 11 minus the two
  that need GL. The other 73 targets are out of scope for this milestone and
  untested here - some of them look GL-free (hud, settings, gui, world), so if
  the plan ever wants broader upstream coverage on the device it should be a
  stated milestone rather than a quiet expansion of this one.

### Findings worth carrying forward

- PPSSPP headless guest IO: see `docs/PPSSPP.md` (d). The 500 ms delay before
  `sceKernelExitGame` is load-bearing; `tests/psp/m1/psp.sh` needs no retries.
- `mallinfo()` from pspsdk's newlib reports a 1536-byte arena while megabytes
  are live. Do not use it for memory measurements on this target; use
  `sceKernelTotalFreeMemSize`/`MaxFreeMemSize` and the arena arithmetic in
  `runtime/pool.c`.
- The libc allocator prototypes must be parsed before the compat macros exist:
  `psp_compat.h` includes `<malloc.h>` (among others) for exactly that reason,
  since it is force-included at the top of every TU.
- A PRX link needs exactly one `PSP_MODULE_INFO`, so it lives in `psp_module.c`
  and `common.mk` links that object into every test.
- The upstream tests read their default vector directory from the working
  directory, which PSP firmware sets to the game folder. Staging
  `docs/cleanroom-camera` and `docs/cleanroom-driving-physics` (2 MB) next to the
  EBOOT makes them work with no argument changes; the rest of `docs/` (38 MB of
  prose and retail-comparison material) is not opened by any of the nine.

### User test

`dist/M1/README.txt`: nine games (`UG2CAM`, `UG2MAP`, `UG2DRIV`, `UG2BRK`,
`UG2RES`, `UG2LAT`, `UG2CON`, `UG2BODY`, `UG2ST1`), each writing `stdout.txt`
beside its own EBOOT, with `expected/*.txt` from the host build to compare
against, and the map-bake limit explained so the five `COMPAT` lines do not read
as a failure. Build it with `scripts/mkdist.sh` after `tests/psp/m1/psp.sh`.

### Loose ends

- The repo has an untracked junk file named `-c` at its root: the `PARAM.SFO` of
  the throwaway write-probe from this milestone's debugging (title "Wrprobe").
  Safe to delete; left alone rather than removing something unasked.

---

## M2 - physics cost

**Status: item 1 PASS, item 2 FAIL. Stopped, per the milestone's own
"IF IT FAILS" rule, which says to report the numbers and to leave `double`
alone. See `../reports/REPORT.md`.**

### Files this milestone added

| Path | What it is |
| --- | --- |
| `runtime/bench_phys.c` | the benchmark: upstream car set-up plus the stepping loop, four cars, three modes (1000-substep report, `mode=long` for wall-time differencing, and the double-op counter readout) |
| `runtime/bench/Makefile` | builds `bench` as its own EBOOT with `runtime/Makefile`'s exact flags (`-Os` included), so the figure describes the shipped build |
| `runtime/bench/dfcount.c` | `make DFCOUNT=1`: linker `--wrap` counters for every software double-precision helper the physics can call |
| `tests/psp/m2.sh` | both arms, and the final-state comparison that is this milestone's gate |
| `tests/psp/m2-budget.sh` | the budget measurement: same EBOOT run twice, `rounds=0` and `rounds=N`, timed from outside the emulator |
| `runtime/platform.c` | `plat_log`'s buffer moved from the stack to `.data` - `ms0:` writes commit after the call returns, and a stack buffer arrives as NULs (see `docs/PPSSPP.md` (d)) |

### Acceptance

| # | Item | Verdict | Evidence |
| --- | --- | --- | --- |
| 1 | Final states identical to the host's, or the differences explained | PASS | `diff` of the four `PHYS car` lines between `out/m2/bench.host.txt` and `out/m2/bench.psp.txt`: no differences. States print at `%.9g`, an exact float32 round-trip, so this is bit-level identity, not a tolerance. `out/m2/m2-final.txt` |
| 2 | 4 cars x 8 substeps x `us_per_substep` <= 8000 us | **FAIL, 4-10x over** | the budget is 250 us per car-substep; the measurement says about 2.6 ms. Both methods below |
| 3 | Substeps per frame are ceil(33.3/4.4) = 8 | PASS with the deviation noted | `third_party/OpenUG2/src/physics.h:14` is `PHYS_TICKRATE 60.0f` - a tick is 16.7 ms, not 4.4 ms - and line 22 caps a frame at `8.0/PHYS_TICKRATE`, so 8 is the stall cap and 2 is what a steady 30 fps frame runs. Graded on 8 as written; the bench prints both (`frame_us_worst8`, `frame_us_steady2`) |

Reproduce: `tests/psp/m2.sh` (both arms plus the state comparison),
`tests/psp/m2-budget.sh` (the wall-time method),
`make -C runtime/bench -B DFCOUNT=1 EBOOT.PBP` then a `mode=long` run (the
captured result is `out/m2/dfcount.txt`).

### Why item 2 fails, in one paragraph

The physics does its arithmetic in `double`, and this toolchain has no
double-precision FPU path for the PSP, so every double operation compiles to a
call into libgcc. Counting those calls inside the PSP build - not estimating
them - gives **8131 software double operations per car per substep**:

| helper | calls per car-substep | instructions in the linked ELF | instructions per car-substep |
| --- | --- | --- | --- |
| `__muldf3` | 1923.4 | 173 | 332,748 |
| `__adddf3` | 881.1 | 254 | 223,799 |
| `__subdf3` | 728.5 | 253 | 184,311 |
| `__extendsfdf2` | 2491.8 | 16 | 39,869 |
| `__divdf3` | 398.1 | 127 | 50,559 |
| comparisons (`__ltdf2` and friends) | 942.4 | 30 | 28,272 |
| `__truncdfsf2` | 570.7 | 24 | 13,697 |
| `__floatsidf` | 123.0 | 42 | 5,166 |
| `__fixdfsi` | 65.4 | 55 | 3,597 |
| `cosf` | 7.0 | not measured | not measured |
| **total** | **8131.4** | | **about 882,000** |

The PSP's CPU is in-order and single-issue, so 882,000 instructions is at least
882,000 cycles: **2.65 ms per car per substep at 333 MHz**. Dependency chains and
cache misses inside the helper code make IPC below 1 likelier than above, so the
error runs in the direction of worse, not better. The budget allows
8000 us / 32 car-substeps = 250 us. The physics alone is therefore 85 ms per
worst-case frame (4 cars x 8 substeps), 21 ms per steady frame (4 cars x 2), and
5.3 ms even for one car at the steady rate. The budget is missed in every
configuration with more than one car.

### The second method, which agrees

`tests/psp/m2-budget.sh` runs the same EBOOT twice, `mode=long rounds=0` and
`rounds=200`, and times each from the host process; the subtraction cancels
emulator start-up, module load, the log flush and the frame count, leaving the
guest's physics in real seconds. 41 ms versus 6171 ms: a difference of 6130 ms
over 200 frames = **30,650 us per frame = 958 us per car-substep**, with the code
JIT-translated onto this machine's 2.4 GHz Cortex-A76. Run the instruction count
backwards, 882,000 instructions in 958 us is IPC 0.92 at 2.4 GHz, which is what
dependent integer helper code should score. The two methods do not disagree: the
emulator was not inflating anything, it was running genuinely expensive code on a
fast CPU. Captured in `out/m2/budget.txt`.

### Why the host number is 1500x better, and why neither printed figure measures a PSP

`tests/psp/m2.sh` prints 5.729 us per car-substep for the host arm and
7799.867 us for the PSP arm. Neither is a PSP figure.

- **Host 5.729 us.** This machine is 4x Cortex-A76 at 2.4 GHz and does double
  arithmetic in hardware - one instruction where the PSP needs about a hundred.
  Same source, different machine. It is not a predictor and must not be quoted
  as one.
- **PSP arm 7799.867 us.** `sceRtcGetCurrentTick` is the only clock user code can
  read, and under headless PPSSPP it does not track wall time: the same 12 ms of
  physics reported ~7800 us per substep in a JIT run that took 11 s of real time
  and in an interpreter run that took 261 s (`out/m2/rtc-jit.txt`,
  `out/m2/rtc-int.txt`). Reading the R4000 Count register instead - the honest
  in-emulator instrument - was tried and is worse: CP0 is privileged, a user-mode
  read takes a reserved-instruction exception, and the EBOOT printed nothing at
  all (`out/m2/bench-cp0.EBOOT.PBP`). That code path is gone; it must never reach
  hardware.

The user's PSP-1000 has a real RTC, so the same EBOOT in `dist/M2/` prints the
definitive on-hardware figure into `log.txt`. The instruction bound above needs no
clock at all, and it already says the budget is missed by an order of magnitude.

### What the fix would be, and why it is not mine to make

The milestone says "Do NOT change double to float (planner decision D5)", and every
fix to this cost is some version of that, so it is the planner's call.

First, what the cost is *not*: it is not stray literals promoting float maths into
double. A first guess said that, and `scripts/float_literals.py` (a conservative
transform that suffixes only literals whose line has no `double` in it, no double
variable, no explicit cast and no accuracy-sensitive call) disproved it: across all
nine physics units and their headers it finds **six** literals to suffix. An
earlier grep said 1035 unsuffixed literals; that count was comments, which the
script excludes.

The double arithmetic is structural. The physics API is double on purpose:

```
double drv_curve(const float *s, int q, double a, double b, double x);
double drv_engine_torque_at(const DrvCar*, const DrvTune*, int, int, double rpm, double u);
double drv_clutch_torque(const DrvCar*, double omega_e, double omega_w, double engage, float*);
void   drv_axle_split(double wa, double wb, int, double *share_a, double *share_b);
static void select_gear(Drivetrain*, int cmd, double rpm_e, float frame_dt);
void   drv_frame_begin(Drivetrain*, const DrvInput*, float frame_dt, double rpm_override);
static void dist(Drivetrain*, double T);          static double psi(double x);
static double clamp(double, double, double);      static double side_combine(const Drivetrain*, const double v[4], int);
float  ct_bump(float x, float y, float, float);   /* computes in double, returns float */
static float noise(double i, double j);           /* fmod on doubles, per contact */
float  lat_decode(uint16_t);                      /* (double)u*(360.0/65536) */
```

That is the float-state / double-maths boundary the physics is written on, and it
is why 2492 of the calls per substep are `__extendsfdf2` and 571 are
`__truncdfsf2`: every one of those functions converts in and back out.

It is also not a toolchain flag. The toolchain is configured
`--with-float=hard -mhard-float -msingle-float`, and asking for double-precision
instructions gets them refused by the assembler:

```
$ psp-gcc -O2 -mdouble-float -c dbl.c
/tmp/cc0lH3ss.s:24: Error: opcode not supported on this processor: allegrex (mips2) `mul.d $f0,$f12,$f14'
```

The PSP's CPU is Allegrex and its FPU does single precision only, so `float`
compiles to one instruction (`mul.s`) and `double` to a libgcc call. There is no
build setting that changes this. The options are therefore:

1. Make the physics hot path float, including the signatures above. This is a D5
   exception and it changes results: unlike the literal case, moving
   `drv_engine_torque_at` or `ct_bump` into single precision changes rounding, so
   the M1 and M2 bit-identity evidence has to be re-baselined and reviewed rather
   than assumed. Expected effect: the 8131 helper calls per substep go to about
   zero, so roughly two orders of magnitude.
2. Fewer substeps per frame or fewer cars - a gameplay decision. Even one car at
   the steady 2 substeps costs ~5.3 ms, so this only narrows the miss.
3. Hand-written VFPU for the hot functions. The VFPU is single-precision SIMD, so
   this is option 1 for those functions with the extra work of writing them, and
   it is a milestone of its own.

Knock-on, read from the milestone text rather than assumed: M3, M4 and M5 measure
`frame_us avg <= 33333` and each drives **one** car (M3 is an orbit camera on a
parked car, M4 is static, M5 is one car with a replay script). One car at the
steady two substeps costs about 2-5.4 ms of the 33 ms frame, so those milestones
are not blocked by this measurement - M2's gate is the one that budgets four cars.
Four cars arrive in M6 (AI racers), which is past the "After M5, STOP" line and
needs planner review regardless, and that is where the double-precision cost has
to be settled. Two caveats stand either way: the physics cost here is derived from
an instruction count, not measured on a PSP, and `frame_us` in M3-M5 is read from
`plat_us()`, the same RTC that is not a clock under headless PPSSPP.

### Deviation from the milestone text

- The milestone's substep arithmetic assumes a 4.4 ms tick; upstream is
  `PHYS_TICKRATE 60.0f` (16.7 ms), with 8 substeps as a per-frame cap rather than
  the steady rate. Graded on 8 as written, steady figure printed alongside. The
  verdict is unchanged at 8, at 2, and for a single car.

### User test

`dist/M2/OPENUG2/` is the bench EBOOT. On hardware it writes `log.txt` beside
itself with real RTC timings - the number this milestone could not get honestly
from the emulator. Expected: the four `PHYS car` lines matching
`out/m2/bench.host.txt` exactly, and `us_per_substep_per_car` in the
hundreds-of-microseconds-to-milliseconds range the instruction count predicts. If
hardware says far below ~1 ms per car-substep, the instruction-count model above is
wrong and item 2 should be re-graded from the hardware number.

---

## M3 - converter for cars, and a car on screen

**Status: PASS - all seven acceptance items met (verified by `tests/psp/m3.sh`,
which prints each verdict with its evidence).** One milestone-internal
contradiction in step 6 was resolved by a planner-authorized change of a single
constant, and the runtime stage then surfaced two genuine converter bugs, both
fixed. `double`/`float` was never touched (D5 stands).

Work continued into M3 after the M2 stop by the user's explicit choice ("Continue to
M3"). M3 step 1 (U2PK v1 spec, its reader, and the probe that measured the GE's
fixed-point encodings) is committed as `e44242e`. This section is steps 2-7: the
converter core plus the runtime draw. The converter loads a retail car through the
upstream loader, bakes vertex colour, batches by (texture, mode), clusters to the
LOD budgets, encodes s16 positions / u16 UVs / 5650 colours / T8 or DXT1 textures
with swizzled CLUTs, writes a validated U2PK v1 file; the runtime loads that file and
draws it at both LODs through `sceGu`.

### Resolving step 6's cluster-key / budget contradiction (planner-authorized)

Step 6's cluster key includes `u` and `v` rounded to `1/16`, and step 6 says to grow
the position cell to 0.5 m then "FAIL the car." Implemented literally, the `1/16`
UV term is the binding constraint: a large textured panel (one texture, one
continuous UV gradient) is welded vertex-by-vertex, so the AI LOD could not decimate
to the 1000-tri LOD1 budget - it floored at 1154-1537 tris - and the shared wheel set
stayed over 400. Measured with only the UV rounding weakened (position term
unchanged):

| car | UV 1/16 (brief literal) | **UV 1/8 (chosen)** | UV 1/4 |
| --- | --- | --- | --- |
| MIATA  | 1295 over | **891 ok**  | 620 |
| 350Z   | 1537 over | **996 ok**  | 870 |
| 240SX  | 1154 over | **977 ok**  | 709 |
| A3     | 1467 over | **999 ok**  | 697 |
| 3000GT | 1276 over | **851 ok**  | 771 |

This was the one point where a number stated in the milestone had to change, so I
asked; the user delegated the call ("do whatever you think is best"). I chose
`1/16 -> 1/8` because it is a single constant, it is the smallest change that makes
the cars actually *fit* the budgets as written (raising the budgets instead would
leave the printed budget table failing the `<= 1000` gate that item 3 grades), and it
keeps every budget figure verbatim. The deviation is commented in `converter/geom.c`
on the `quv()` constant; it touches only the clustering key, never the stored per-batch
UVs. `1/8` leaves three cars within ~25 tris of the ceiling - no headroom - so a car
outside this test set may need revisiting, which is noted here rather than hidden.

### Two real bugs the first on-screen render exposed

Rendering the first converted car made it obvious the geometry and UVs were right but
the car was grey and featureless. Chasing that through MODULATE/DECAL/REPLACE and a
host-side texture decode found two converter defects (neither a spec issue, both
fixed), and the golden only after them:

1. **Body paint never baked on untextured skins.** The red "fleet default" paint was
   applied only inside the `if (m->texkey)` branch, but NFSU2 stores the CARSKIN
   paint layer, for most cars, as an *untextured* solid mesh whose colour IS the paint
   (`tex=0`, e.g. MIATA's 400-tri and 342-tri body slices). So the body fell through
   to the `0x808080` untextured grey. Fix: bake `0xC82828` for any `N2_CAR_BODY` +
   `N2_MAT_CARSKIN` mesh regardless of `texkey`. The car is red now. (M3 still has no
   per-car paint table; every car is the fleet red - see the dist README.)
2. **CLUT byte order.** The palette was written as `0xAARRGGBB`, but the GE reads a
   `GU_PSM_8888` CLUT as `0xAABBGGRR` (blue in bits 16..23, red low byte), so every
   coloured texel would have had red and blue swapped. Invisible on MIATA's mostly
   greyscale diffuse maps but wrong for any coloured car. Fix: swap R and B on the way
   out to `t.clut[]`. Verified against the earlier `runtime/vfmt` GE measurement.

### A real PSP-1000 exposed two bugs the emulator hides (2026-11-12)

Everything above was graded in PPSSPP's software renderer. The user then ran the
`dist/M3` build on a real PSP-1000 and reported a **blue background and an almost
completely distorted, floating car** that still rotated and exited cleanly on START.
That is exactly the emulator/hardware split this project is careful about: the build
and the numbers prove out in PPSSPP, but PPSSPP is not the ALLEGREX GE, so M3's item-4
"correct car" verdict was an **emulator** pass, not a hardware one. Two real bugs, both
in the render path (not the converter, not the budgets), and both fixed:

1. **Background colour (blue).** `gfx.c` cleared with `0xff302010`, chosen to look like
   a dark warm tone. The GE clear-colour register is `0xAABBGGRR`, so that value is
   R=0x10,G=0x20,B=0x30 = dark **blue** - and PPSSPP drew it blue too (the old golden's
   corners read `[16,32,48]`), so I had simply mislabeled my own golden. The intended
   tone RGB(0x30,0x20,0x10) is `0xff102030`; the fog colour was fixed to match. Same
   `0xAABBGGRR` rule the CLUT fix established - clear colour and fog were the two 8888
   literals left in `0xRRGGBB`.
2. **Geometry (distorted).** The car drew with `GU_VERTEX_16BIT`. PPSSPP's software GE
   renders that rarely-used fixed format one way, the real GE another - fine in the
   emulator, shredded on hardware. Fix: re-cut the s16 positions into **GE float
   vertices** (`GU_VERTEX_32BITF`) on load in `car.c`. Verified in the emulator that the
   float path reproduces the previously-reviewed car *exactly* (same screen bbox
   `x[171..301] y[115..189]`, car-pixel mean `[91,64.8,62.6]` vs the old golden's
   `[91,64.8,62.4]`), the only change being the intentional background.

A float position must sit on a 4-byte boundary, so the interleaved vertex is
`uv(u16,u16) colour(u32 8888) pos(float3)` = 20 B (`GU_COLOR_8888` forces the bytes
before the float to a multiple of 4; a 2-byte 5650 colour leaves the float misaligned
at offset 6 and reads as garbage *even in PPSSPP* - caught by the emulator regression,
which is the whole value of keeping the golden). The 5650->8888 expansion decodes R
from the **low** bits, because that is how the GE reads `GU_COLOR_5650` (decoding R from
the high bits swapped R/B - also caught by the emulator). `carpool_high` rose from
396800 to 545600 B (532 KB) because both loaded LODs now carry a float-vertex copy;
still well under the 1536 KB Cars-pool cap.

Carried into M4/M5: they draw through the same `sceGu` path, so the world renderer uses
float vertices and `0xAABBGGRR` colours from the start, and the emulator golden is a
*regression* baseline, never proof of a correct picture - only the user's PSP is that.

### Files this milestone added

| Path | What it is |
| --- | --- |
| `converter/psp_convert.c` | the `car` CLI: `psp_convert car <NFSU2_DIR> <CAR> <out.u2pk>`; loads, converts, prints the LOD budget table, then re-opens the file with `u2pk_open` and validates before exiting 0 (deletes the file on any mismatch) |
| `converter/car_conv.c` / `.h` | the pipeline: mesh selection -> base-colour bake (skin red on `N2_MAT_CARSKIN`, textured or not) -> per-LOD texture build (LOD1 halved to <=64 px) -> batch by (texture, mode) -> `fit_group` clusters each of the player / AI / wheel groups against its budget -> write; holds the two fixes above |
| `converter/car_load.c` / `.h` | loads one car's GEOM/TPK/GLOBALB with the upstream `nfsu2.h` header-only loader, exactly the sequence the desktop `main.c` uses, so the source geometry matches what the reference build renders |
| `converter/geom.c` / `.h` | the `Geo` triangle soup, `u2pk_color5650`, the s16/u16 quantisers, and `geo_cluster` (open-addressed hash on the cluster key; `quv()` carries the authorized `1/8` deviation) |
| `converter/quant.c` / `.h` | box-filter halving for the LOD1 textures, and median-cut to the palette budget |
| `converter/Makefile` | host `gcc -std=c99 -O2` build plus an `asan` target (`-fsanitize=address,undefined`) |
| `shared/u2pk_read.c` | **bug fix** (committed `e44242e`): `u2pk_open` never stored `v.buf`/`v.len`, so its bounds check saw length 0 and rejected every real file |
| `runtime/car.c` / `.h` | loads a car file into the Cars pool (`u2pk_open` after a dcache writeback), draws batches opaque->cutout->blend with per-batch CLUT + UV-scaled texture, and the four shared wheel slices at `wheel_pos` (FR/RR mirrored) |
| `runtime/main.c` | rewritten for M3: loads `data/car.u2pk` twice (player LOD0 + AI LOD1, resident together for item 5), orbits a camera, fog, takes an autotest screenshot, logs `frame_us` and `carpool_high` |
| `runtime/Makefile` | adds `car.o` and `../shared/u2pk_read.o` |
| `tests/host/` | five host tests (t_swizzle, t_dxt, t_quant, t_vertex, t_reader) with a `test` and an `asan` target |
| `tests/psp/m3.sh` | the milestone script; grades all seven items and prints each verdict with evidence |
| `tests/psp/run.sh` | gains an optional `data/` payload copy so a milestone can ship a runtime asset (guarded; M0-M2 unaffected) |
| `scripts/mkdist.sh` | gains the `dist/M3` build (EBOOT + native `psp_convert` + README) |

### Acceptance

| # | Item | Verdict | Evidence |
| --- | --- | --- | --- |
| 1 | All 5 host tests pass, and pass under ASan | **PASS** | `make -C tests/host test` and `... asan`: swizzle round-trip, DXT1 round-trip, median-cut exact-palette, s16/u16 encode-decode bounds (pos worst 0.000091 m, uv worst 3.9e-5), reader accepts a valid file and rejects 7 corruption classes plus every truncation |
| 2 | Converter runs under ASan+UBSan with no reports on the 5 cars | **PASS** | `make -C converter asan` then `psp_convert.asan` on MIATA/350Z/240SX/A3/3000GT: zero ASan/LSan/UBSan reports, all "VALIDATED". (Earlier this milestone UBSan caught `batch_append` passing a null source to a zero-length `memcpy` on a fresh batch; guarded and fixed.) |
| 3 | Every car meets the LOD budgets; print a table | **PASS** | table below: LOD0 2415-2886 (<=3000), LOD1 851-999 (<=1000), wheels 245-383 (<=400), file 191-199 KB (<=600 KB). At the `1/8` setting above |
| 4 | Screenshot shows a correct car and matches the golden | **PASS (emulator)** | the golden is a red body with black wheels at the base, dark glass/cabin and bright fender highlights, coherent closed geometry, seen from the front-left and above - the described picture. Golden `out/goldens/m3_miata.ppm` (gitignored); a re-run matches it at `mse=0.000000`. Verified by direct pixel analysis (the session's image-transcription bridge returned a fabricated description, so the picture was judged from the pixels, and the description above is what the pixels show). **Caveat: this is an emulator pass only.** The first real PSP-1000 run (see the section above) showed a distorted car and blue background; those are now fixed with float vertices + correct clear colour, and the emulator render is byte-faithful to this golden. The corrected `dist/M3` build is with the user for a hardware re-check, which is the real proof. |
| 5 | `carpool_high` <= 1.5 MB with player and AI LOD both loaded | **PASS** | one file loaded twice into the Cars pool (player LOD0 + AI LOD1) reports `carpool_high=545600` (532 KB) <= 1536 KB; the number rose from 396800 when both LODs gained a float-vertex copy (render-path fix above) |
| 6 | `frame_us` avg <= 33333 | **PASS** | 2540-3876 us/frame by differenced host wall clock (see the instrument note). The in-guest `frame_us avg=199` is NOT used: `sceRtcGetCurrentTick` under headless PPSSPP is a fixed tick step, not a clock (same finding as M2) |
| 7 | `sfo_check` and heap check still pass | **PASS** | `sfo_check: OK (MEMSIZE = 0)`; the arena (`0x8936150`) sits inside the PSP-1000 `0x08800000..0x0A000000` HOME window; pool high-water 387 KB of the 17 MB carve |

### LOD budget table (ASan build, `tests/psp/m3.sh`)

| car | LOD0 (<=3000) | LOD1 (<=1000) | wheels (<=400) | file (<=600 KB) |
| --- | --- | --- | --- | --- |
| MIATA  | 2705 | 891 | 342 | 193 KB |
| 350Z   | 2596 | 996 | 383 | 193 KB |
| 240SX  | 2886 | 977 | 289 | 196 KB |
| A3     | 2469 | 999 | 245 | 199 KB |
| 3000GT | 2415 | 851 | 266 | 191 KB |

### Instrument note for item 6

The guest cannot time itself (see item 6's cell). The gate is the emulator's own wall
time, differenced between two runs that differ only in `frames=` so startup, module
load and exit cancel. On this Pi under PPSSPP's software renderer the scene costs
~2.5-3.9 ms/frame - well under the 33 ms budget - but that is a bound on whether the
scene is cheap enough, measured on the emulator's software GE, not an emulation of a
333 MHz PSP-1000 with a hardware GE. The real per-frame number is a user test on
hardware.

### User test

`dist/M3/` is ready to test on a real PSP-1000. Copy `dist/M3/PSP` to the stick (the
EBOOT lands at `ms0:/PSP/GAME/OPENUG2/EBOOT.PBP`); build the car file with the bundled
native tool - `./psp_convert car <NFSU2_DIR> MIATA car.u2pk` - and drop it at
`ms0:/PSP/GAME/OPENUG2/data/car.u2pk`. Start "OpenUG2"; expect a slowly orbiting red
car with four wheels. What to look for, and the meaning of each failure mode, is in
`dist/M3/README.txt`.

---

## M4 — one static world slice

### M4a: the upstream region loader, GL/SDL-free, on the host (2026-11-12)

M4 step 1 says load each region with the loader `--instance-audit` uses, without
the GL renderer. Traced the path in `third_party/OpenUG2/src/main.c`:
`--instance-audit` → `world_load_ex(&world, troot, trackname, &opts)` where
`troot = <DATA>/TRACKS`, `trackname = "STREAM<REGION>"`, and the focus comes
from `winst_default_focus(companion, stream)` (companion = the stem, e.g.
`L4RH.BUN`). `world.c` names no GL/SDL symbol for real — the only two are
`rhi_texture_destroy` / `upload_tex_keyed`, reached only from
`world_bind_textures`, which the converter never calls.

`world.h` → `render.h` does `#include <SDL.h>` for three opaque types
(`SDL_Window`, `SDL_WindowFlags`) and one declaration (`SDL_GL_SwapWindow`);
SDL2 is not installed here. Solution: `converter/shim/SDL.h`, a 20-line stand-in
on the **converter's include path only** — the PSP runtime and the game never see
it. `converter/region_load.c` drives the public loader API and stubs the two GL
symbols. The complete host link closure is just `world.c physics.c resource.c
world_instance.c` + those two stubs (found by `nm -u`, then confirmed by link).

`converter/region_load.[ch]` exposes `region_open(dataroot, "STREAML4RH")` →
the world-space `N2Scene` plus the ROAD/TERRAIN bounding box (the all-mesh bbox
is useless — props are authored across the whole city; the ground defines the
slice). `region_conv.c` builds the 96 m cell grid, splits ROAD/TERRAIN to an
8 m max edge, and assigns each triangle to the cell of its centroid (M4 steps
2-3), then prints the budget table.

**Bug found and fixed while building it:** the first longest-edge bisection
always emitted children `(a,mid,c)+(mid,b,c)` regardless of which edge was the
longest. That is only correct when the split edge is AB; for BC/CA it built
wrong, overlapping triangles that never shrank, inflating L4RH 67× (10424 →
703444) and blowing the window budget 10×. The correct children are `(r,p,mid)`
and `(r,mid,q)` for split edge `pq` opposite vertex `r`.

Survey after the fix (`psp_convert region <DATA> STREAML4RH`):

| region | grid | ground tris in→out | worst cell | worst 3×3 window | budget |
| --- | --- | --- | --- | --- | --- |
| L4RH | 13×5 (65) | 10424 → 51903 | 1382 | 10046 tris / 3 batches | **PASS** (15000/250) |
| L4RA | 85×105 (8925) | 993478 → 3696628 | 8608 | 57745 tris / 182 batches | over |

L4RH is a small, low-rise region — a 1200×468 m ROAD/TERRAIN strip with **0
BUILDING/STRUCT meshes** (a real data fact, not a bug: its scenery is terrain,
938 props and 187 walls). It meets the window budget at 8 m comfortably. L4RA
(the whole airport city, 8×10 km, 5329 buildings, giant overlapping terrain base
planes up to 1577 m edges) puts 57745 tris in its densest 3×3 window. The M4
escalation ladder (prop-volume cutoff, then texture caps) only sheds props and
texture bytes, not ground+building triangles, so it cannot bring that window
under 15000. L4RA is therefore carried as a **budget conflict for planner
review**, not a code defect; the runtime only loads the 3×3 around a chosen
start pose, and L4RH (the spec's primary region) passes outright. Building the
converter to completion on L4RH first; L4RA follows for the screenshot pass.

Milestone gate: the loader runs (0 s for L4RH, 2 s for L4RA), geometry is
correct, budgets are computed and printed. Files: `converter/shim/SDL.h`,
`converter/region_load.[ch]`, `converter/region_conv.[ch]`, `converter/Makefile`,
`converter/psp_convert.c`.

### M4b: the region -> cell converter writes valid U2PK slices (2026-11-12)

`converter/region_convert.c` turns a region into three files under an output dir:

- **index.u2pk** (kind REGION): one CELLTAB section of `ncell` entries
  `{i32 cx, i32 cy, u32 off, u32 size, f32 zmin, f32 zmax}` (24 B each). The
  grid `{u16 gw, u16 gh, f32 x0, f32 y0}` rides in the header's 12 reserved
  bytes, so the runtime maps a pose to a cell without a new section type.
- **cells.u2pk**: `ncell` complete kind-CELL U2PK blobs at 16-byte aligned
  offsets, each with BATCHES/VERTS/INDICES/COLLIDE. No in-file TEXTURES: the
  batches index the shared sidecar.
- **tex.u2pk** (kind REGION): TEXTURES, one entry per distinct source key. In
  M4b each is a 4×4 white DXT1 block (a real encoder arrives in M4c); the
  indices are already wired, so swapping the bytes in changes nothing else.

Geometry: every drawn triangle carries its uv and colour (from `vcol`, else
white) through the 8 m longest-edge split (midpoints interpolate uv and colour),
lands in the centroid cell, and is quantised against the **cell box** (x/y from
the cell, z from the cell's accumulated zmin/zmax) with the same `q16_pos` /
`u2pk_color5650` the cars use. COLLIDE is ROAD/TERRAIN triangles as f32 xyz
triples plus a 10 m centroid grid (`u16 start[ng+1]` + `u16 tri[]`) for M5's
ground query.

Two reader/writer fixes fell out of building it:
- the reader rejected a CELL whose batches reference the shared sidecar
  (`b->texture >= v.ntex` when the cell carries no TEXTURES section); relaxed to
  allow that only for `kind==CELL && ntex==0`, cars unchanged.
- ASan caught `bpad` feeding a 16-byte static as the source of a large memcpy
  (global-buffer-overflow; the files only looked right because the over-read
  bytes were zero). Rewritten to grow-then-`memset` the destination.

**Validation** (throwaway host validator using the real `u2pk_open`):
`region convert <DATA> STREAML4RH` → 63 cells, 126 batches, 3 textures;
**all 63 cell blobs round-trip `u2pk_open` (ok=63 bad=0)**; worst cell blob
111 KB (< 256 KB), worst 3×3 window 10046 tris (< 15000), CELLTAB 1560 B
(< 256 KB). `make asan` is now overflow-free and leak-free in converter code;
the one remaining leak (1536 B) is the vendored loader's `n2_tpk_open` inside
`world_instance_build_for_event`, not freed on `world_city_free`/
`world_neighborhood_free` — a one-shot host-tool leak, not touched. Files:
`converter/region_convert.c`, `region_conv.h`, `region_load.c`, `Makefile`,
`psp_convert.c`, `shared/u2pk_read.c`.

### M4c: the runtime draws a static world slice (2026-11-12)

`runtime/world.[ch]` streams a slice: `world_open` reads `index.u2pk` (CELLTAB +
the grid in the reserved header bytes) and parks it at the base of POOL_WORLD
(`world_floor`), then slurps the shared `tex.u2pk` into POOL_TEXTURES.
`world_load_window(x,y)` resets POOL_WORLD to `world_floor`, seeks the 3×3 cells
around the pose in `cells.u2pk`, reads each blob into the pool and re-cuts the
s16 verts into the 20-byte float vertex the car path already uses
(uv u16×2 + colour u32 + float×3). Whole world in one pool, reload-reset-to-floor,
so no per-frame fragmentation (D8-compliant). `world_draw` walks batches
OPAQUE→BLEND, model matrix per batch = translate(pos_offset)·scale(pos_scale),
shared `tex.u2pk` CLUTs, and `sceGumDrawArray`.

**Cull winding (the spec's "try GU_CCW if roads disappear"):** with
`sceGuFrontFace(GU_CW)` the entire ground top-face culled — an almost-clear
frame. Disabling culling filled the lower half to the horizon (geometry and
placement proven correct), so the ground triangles wind CCW; **GU_CCW is
correct** for this slice and is what ships (recorded in a `world.c` comment).

The camera is `set_camera_world` (eye at the pose, looking horizontally along
`yaw`, far plane 160 m), fog 60→150 m, per spec. `main.c` gained a mode dispatch:
`data/world.cfg`'s **presence** selects world mode (region + up to 8
`pose=x y z yaw` lines); with no such file it runs the M3 car mode unchanged.
`tests/psp/run.sh` stages the payload recursively so `data/L4RH/` lands.

### M4 step 4: real T8 ground art (2026-11-12)

`converter/region_tex.[ch]` replaces the white 4×4 placeholders. The first cut
decoded from GLOBAL/GLOBALB.BUN and got **0/3 keys** — GLOBALB carries *car* art.
Traced the real lookup in `main.c` (per-mesh track-texture resolution): a ground
key resolves through **the packs the loaded region already holds in memory** —
region STREAM TPKs → LOC4DYNTEX → master → common — and `world_bind_textures_step`
is the only thing that frees those buffers, which the converter never calls. So
`region_tex_write(WorldNeighborhood*, …)` decodes each key in exactly that order;
`region_convert` hands it `&reg.w->neighborhood`.

Each key is capped to 128 px (road/terrain) / 64 px (else), box-filtered,
median-cut to a 256-colour T8 + CLUT (GU_PSM_8888 = 0xAABBGGRR) and PSO-swizzled
— the same path `car_conv.c` uses, which the M4 spec permits ("PSP DXT1 … or
T8"). CLUTs are deduped by content; a key no pack decodes falls back to a 16×8
white T8 (white modulates to the baked vertex colour) and is logged, matching the
engine's own shared-fallback behaviour. **L4RH: 2/3 keys carry real art**; the
third (`0x120caccb`) has no record in any pack and white-falls-back — the engine
would fall back on it too.

`make asan` re-run on the full `region convert STREAML4RH` after the encoder
landed: **no new AddressSanitizer/UBSan findings** — the sole report is still the
one pre-existing 1536 B / 1-allocation vendored-loader leak (`n2_tpk_open` ←
`world_instance_build_for_event` ← `world_neighborhood_load`, reached from
`region_convert`→`region_open`, freed by neither `world_neighborhood_free` nor
`world_city_free`). The total did not grow, confirming `region_tex.c` frees every
allocation it makes (`th`, the dedup `clutdata`, each `tex[i].data`, and the
`N2Tex` rgb/alpha/dxt planes) — a one-shot host-tool leak in MIT code, left as is.

### M4 poses helper (2026-11-12)

`psp_convert region poses <DATA> STREAM<REG> <cfg> [n]` writes `data/world.cfg`.
The spec named `ROUTES<REG>/Paths*.bin` route nodes via `n2_load_path`, but
L4RH's `PathsFreeRoam.bin` is a route *graph* (chunk `0x34147`/`0x3414a`), not the
coarse `0x34148` centreline `n2_load_path` parses, and its points are in the
game's absolute frame, not the focus-recentred world. **Deviation:** poses are
taken from the geometry that will be rendered — sample `N2_ROAD` triangle
centroids (guaranteed on-road, in-grid), greedy farthest-point selection seeded
on the centroid farthest from the mean (deterministic), camera 2 m above the
road looking along each triangle's longest edge. Same "2 m above a road, looking
along it" the spec asks for.

### M4 acceptance (L4RH — emulator PASS; only a real PSP proves the picture)

`tests/psp/m4.sh` grades all four items for L4RH and prints a report-only L4RA
survey. Run 2026-11-12:

| # | criterion | result |
| --- | --- | --- |
| 1 | budgets (cell ≤256 KB, index ≤256 KB, 3×3 ≤15000 tris) | **PASS** — worst 3×3 10046 tris, largest cell 111106 B, index 1608 B, `within_budget=YES` |
| 2 | screenshots show roads+ground, no missing ground | **PASS (emulator)** — all 3 poses: clear-colour 0 %, ground fills the lower frame, 45–155 distinct colours (roads read light-grey, fog horizon bluish). `m4_p*.ppm` |
| 3 | total pool high-water ≤17 MB + heap window | **PASS** — high-water total 812080 B (793 KB); arena `0x8937a50` inside `0x08800000..0x0A000000`, heap free 782336 |
| 4 | emulated frame_us avg ≤33333 at all 3 poses | **PASS** — 4311 µs/frame (5f/65f differenced host wall clock over 180 render frames) |

Item 4 uses differenced host wall clock, not the EBOOT's `frame_us avg=132` line
(a fixed tick step under headless PPSSPP, not a clock) — same rule as M3.
Goldens `out/goldens/m4_l4rh_p{0,1,2}.ppm` are **local regression baselines
only**; the emulator's software renderer drew them. **`dist/M4/` is ready for a
real PSP-1000 run** — on hardware the run writes `m4_p*.ppm` to the stick, and
those are the pictures that actually count.

**L4RA** (report only): worst 3×3 window **57745 tris** (> 15000) and its worst
single cell (~8608 tris) exceeds the 256 KB blob cap. Both are ground+building
*triangle density*; the spec's escalation (prop cutoff 1→2→4→8 m³, then texture
caps 128→64) sheds props and texture bytes, not ground/building triangles, so it
cannot close the gap. This is a **fixed-window budget conflict for planner
review**, not a code defect — L4RH, the spec's primary region, passes outright.

Files: `runtime/world.[ch]`, `runtime/main.c`, `runtime/Makefile`,
`shared/u2pk.h` (`U2PK_CELL_M`), `converter/region_tex.[ch]`,
`converter/region_convert.c`, `converter/region_conv.h`, `converter/psp_convert.c`,
`tests/psp/m4.sh`, `tests/psp/run.sh`, `tools/u2pk_cell_max.py`,
`tools/ppm_world_check.py`, `scripts/mkdist.sh`.
