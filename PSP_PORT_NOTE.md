OpenUG2 PSP-1000 port — branch snapshot
========================================

This branch is a working snapshot of an **independent port of NFS: Underground 2
to the PSP-1000 (sceGu/libgu)** built on top of OpenUG2. It is a separate project
from the OpenUG2 engine itself: it adds a Linux asset converter (`psp_convert`,
retail NFSU2 -> a container called U2PK) and a PSP runtime (`EBOOT.PBP`) that
draws the result through the PSP's Geometry Engine.

Milestones M0-M5 are complete and tested in the PPSSPP software renderer; a real
PSP-1000 is the final gate and is being checked by hand.

  M0  boot a PSP homebrew, get pixels on screen
  M1  load real car data from retail archives (converter)
  M2  measure one physics substep on the target (upstream drive/physics)
  M3  a car on screen (U2PK car container + GE draw)
  M4  a static world slice (region -> 96 m cell grid, streamed 3x3 window,
      textured roads/terrain). L4RH passes; L4RA (the whole airport) does not
      fit the fixed 3x3 / 96 m budget on ground+building density alone and is
      flagged for planner review - see PROGRESS.md "M4".
  M5  drive in the slice. A shared, IO-free drive spine (upstream arcade physics
      + the sprung-ride integrator, ground fed only through PhysRideState.
      contact_mask from each cell's COLLIDE section) steps one car through the
      streamed world. The host and PSP arms run the same spine and land on the
      replayed final pose BIT-IDENTICALLY (|dXY|=0.000 m), replay is
      deterministic, pools and per-tick cost are in budget. Numeric spine green;
      the on-screen picture is inherited-open from the M3 hardware render defect
      (see PROGRESS.md "M3 hardware follow-up") pending one probe run.

OpenUG2 dependency
------------------
The full repo carries OpenUG2 as a git submodule at `third_party/OpenUG2`, pinned
to commit `02e306a9092a208ba361aa128f40b64850dd7f37` (the `development` line of
this OpenUG2 project). It is deliberately **not** recorded as a submodule pointer
on this snapshot branch (that pointer referenced a local private mirror). To build:

  git clone <this repo> -b psp-port
  git clone https://github.com/OpenUG2/OpenUG2.git third_party/OpenUG2
  git -C third_party/OpenUG2 checkout 02e306a

Layout
------
  converter/   host (Linux C99) asset converter: psp_convert (cars + world regions)
  runtime/     PSP runtime (psp-gcc), builds EBOOT.PBP (+ a replay EBOOT, M5)
  shared/      code shared by both (U2PK reader/writer, swizzle, DXT, pools,
               platform, ground query, the IO-free drive spine)
  tests/       host unit tests + PSP/PPSSPP milestone scripts (tests/psp/m3.sh, m4.sh, m5.sh)
  tools/       sfo_check.py, ppm_cmp.c, u2pk_cell_max.py, ppm_world_check.py,
               u2pk_batches.py, gen_replay.py, guard
  docs/        U2PK format spec, PPSSPP harness notes
  scripts/     build-the-user-facing dist/ builder (mkdist.sh)
  PROGRESS.md  per-milestone acceptance evidence

Build
-----
  source <pspdev env>
  make -C converter     # psp_convert
  make -C runtime       # EBOOT.PBP
  tests/psp/m3.sh       # M3 acceptance in PPSSPPHeadless (a car on screen)
  tests/psp/m4.sh       # M4 acceptance in PPSSPPHeadless (a world slice)
  tests/psp/m5.sh       # M5 acceptance: host vs PSP drive replay agree + budgets

To see the world slice on hardware, build region data from your own NFS Underground
2 PC folder first:

  psp_convert region convert <NFSU2_DIR> STREAML4RH data/L4RH
  psp_convert region poses   <NFSU2_DIR> STREAML4RH data/world.cfg 3

then copy data/L4RH/*.u2pk and data/world.cfg next to the EBOOT on the stick.

This snapshot contains no retail game data and no built binaries: *.u2pk, *.ppm,
*.PBP, PARAM.SFO and dist/ are all gitignored.
