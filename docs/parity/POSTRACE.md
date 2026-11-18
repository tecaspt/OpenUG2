# Post-race screen family

[data] Retail layout evidence comes from `tools/fng_dump.py` on
`GLOBAL/InGameRace.bun`: `UI_PostRaceResults.fng`, `UI_PostRace_TournResults.fng`,
`UI_PostRace_TournStandings.fng`, `UI_PostRaceReward.fng`, `UI_PostRaceStats.fng`
and `UI_PostRaceWorldMap.fng`. The existing action carousel is `UI_postrace.fng`.
All driving HUD layers (retail instruments, map, race labels/banners, diagnostic
fallbacks, rear mirror and loading overlays) are suppressed while any post-race
page is open. The 3D scene and HUD state sampling remain available.
The five new identity sets cover every object (440 total), not only dynamic text.
`fe-binding-check` verifies hashes, GUIDs, parent GUIDs, object types and resources.

[inferred] Flow is results → reward (career credited outcome) → stats → world map
(career) → action menu. URL tournaments replace results with tournament round
results → cumulative tournament standings. Next labels are authored on results,
reward and standings; stats has Back, and the map has Accept. These captions do
not establish an executable transition graph: cross-package order and the
forward stats caption are inferred. An unknown race kind preserves the previous
results/menu path for existing callers; all six supported kinds include stats.

[data] Keyboard Return and controller A advance; Escape/controller B go to the
previous page (or quit on the first page/menu). R/controller X restart. The menu
uses Left/Right or controller D-pad to select Restart, Continue or Quit. Touch
uses bottom-left Back and bottom-right Next/Accept, with carousel arrow/centre
taps on the menu. Restart remains available on the original results caption.
Restart/Continue/Quit are returned to the host only when selected; intermediate
Next actions never launch the next tournament round or credit a second reward.

[data] `PostRaceInfo` is copied on opening. Career cash and reputation come from
`g_career.outcome.reward` after `ch_on_result`, including zero on a failed result.
The outcome contains stage before/after but no list of newly unlocked objects;
no unlock names or counts are fabricated. Quick races have no reward page.
The existing career reward policy is provisional; presentation does not replace
it with a new purse or grant anything itself.

[data] Race time, drift points and banked/cancelled drift counts come from the
host. Per-race speed measurements reset when an event is armed and when racing
begins. Each unpaused physics tick samples planar velocity magnitude in km/h:
top speed is the maximum and average speed is the speed integral divided by
sampled racing time. Countdown, paused and finished ticks are excluded. Lap races
record elapsed time between real lap-line crossings, including the final lap
whose progression counter is clamped at the lap limit. Partial laps, knockouts
and developer-forced finishes do not invent a completed lap; Best Lap remains
empty until a real lap completes. Drag shift counts remain unavailable. Reusable stats row templates are authored outside the visible
frame; their placement and English measurement captions are inferred. URL round
results use that round's places, elapsed times and awarded points. The standings
page sorts cumulative places/points independently. Missing entrants and values
are hidden, including authored sample values and countdowns.

[data] The career post-race map reuses `fe_worldmap.c` loading, TrackInfo projection,
retail `TRACKMAP4000` texture and player marker. Its outer frame is the post-race
package. It shows the finish pose supplied by the host and does not select or
teleport to a destination. [inferred] Showing this recap before the menu is a
career-only policy; exact retail map eligibility requires retail captures.

[data] `MU_QuickRaceCarSelect.fng` was inspected in FrontB. It is a car-select shell
with Select Car/customize/back tags, not a post-race page. The car-selection module
is outside this task's owned files; its gap remains recorded separately.

## Verification

`make postrace-test` exercises every supported race kind in quick/career flows,
copy isolation, backward navigation and keyboard/controller/finger input without
retail data. `make postrace-check` additionally opens all family packages on retail
data, checks bound values and captures each page in three presentation fixtures.
`POSTRACE_SHOTS` chooses the output directory. Fixtures deliberately distinguish
round order from cumulative standings. Host race scripts supply separate live
career and quick-race evidence. Evidence and exact final check counts are recorded
in the external task log; no data files or workstation paths belong in this tree.
