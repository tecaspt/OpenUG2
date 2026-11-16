# s11_drag.spec - the drag gear-shift gate: a shift must reach the HUD, with words on it.
#   python3 -I tools/log_assert.py tools/log_specs/s11_drag.spec <gate>/s11.log
# Three rules together mean "at least three shifts, and every one of them captioned": enough
# captioned lines to show the feature works, and no shift line that is uncaptioned or captioned
# with an empty string. The caption text itself is never matched, only that there is some.
expect >=3 /drag shift: .* caption "[^"]+"/
forbid /drag shift:[^\n]*caption ""/
forbid /drag shift: (?!.*caption ")/
