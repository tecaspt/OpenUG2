# Frame-time report (perf_report.py)

Turns one per-frame profile CSV into a plain-text answer to "where does the frame time go, and what causes the drops".

## Getting the CSV

- iOS (build 7 and later): the game writes `OpenUG2-perf.csv` on every launch. In the Files app open On My iPhone ->
  OpenUG2 -> `OpenUG2-perf.csv`, then share it by AirDrop or mail. Play the section that stutters, then quit the game
  before copying so the last rows are flushed (the file is flushed every 60 frames).
- Desktop: `nfsu2 DATA_DIR ... --perf-csv FILE.csv`. Add `SDL_VIDEODRIVER=offscreen` for headless runs.
- The writer stops after 36000 rows (about 10 minutes at 60 fps).

## Running

    python3 -I tools/perf_report.py FILE.csv [--fps 60]

Python 3 standard library only. Nothing is written. Columns are found by header name. A truncated last line, CRLF line
endings, empty cells and extra or missing trailing columns are tolerated. An unusable file prints a message on stderr
and exits with status 2.

## Reading the output

1. **Input**: rows read and skipped (with the first reason) and the duration at `--fps` (default 60).
2. **Frame time**: statistics of `cpu_total`, the sum of all CPU sections of one frame. Effective fps is 1000 / mean;
   "1% low" is 1000 / p99. On a vsynced device `cpu_total` is the frame time because it includes the swap wait.
3. **Sections**: mean and p95 per `cpu_*` section and its share of the mean total, largest first. `cpu_swap` is time
   blocked in the buffer swap: a large share means the GPU or vsync is the limit, not the CPU work before it.
4. **Hitches**: frames whose `cpu_total` exceeds max(2 x median, 33.3 ms). They are counted, grouped into episodes of
   consecutive frames, and the 10 worst are listed with the section that grew most versus its own median. That section
   names the cause (streaming, AI spawn, draw, swap).
5. **Load relation**: mean `cpu_total` by quartile of `draws` and of `traffic`. A steep rise with `draws` means cost
   follows scene size; a steep rise with `traffic` means it follows AI count.
6. **GPU timings**: p50 and p95 of `gpu_total` and its three spans. Devices without a GPU timer (iOS GLES, the Pi)
   leave these columns empty and the tool says "no GPU timings in this file".
7. **Verdict**: GPU/vsync-bound (swap share at least 50%), CPU-bound (median over 16.7 ms; names the top two
   non-swap sections), hitch-bound (smooth median but spikes; names the section that causes most of them), or smooth.
   The evidence numbers are printed with the verdict.

## Self-test

    python3 -I tools/perf_report_test.py

Builds synthetic CSVs in a temporary directory and prints `perf_report_test: N checks, 0 failures` and `PASS`.
