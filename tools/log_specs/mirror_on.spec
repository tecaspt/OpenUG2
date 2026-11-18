# mirror_on.spec - STREAML4RA with --rear-mirror on: the flag must not break the run.
#   python3 -I tools/log_assert.py tools/log_specs/mirror_on.spec <gate>/mirror_on.log
# The mirror draws into its own scene target right before the frame is presented, so a regression
# there shows up as a crash or a missing screenshot, not as a log line. Nothing here asserts that
# the mirror itself rendered - the offscreen backend may not offer a scene target at all, and the
# inv-l4ra/RB steps already pin the pixels with the mirror off.
expect /wrote mirror-on-STREAML4RA\.png \(640x480\) after driving to/
forbid /Segmentation|ASan|AddressSanitizer|LeakSanitizer|abort|Segmentation fault/
