# p3_cold.spec - the cold-restart half of the persistence proof: this run opens the save the
# p3-desktop run wrote, so the kit and rim must already be on the car before anything is touched.
#   python3 -I tools/log_assert.py tools/log_specs/p3_cold.spec <gate>/p3-cold-desktop.log
# Nothing here installs a kit: body=1 (KIT01) and rim NFSU STYLE02 have to arrive from the save
# alone, and the game saves on entry to the garage page - twice, because the script opens the
# garage, leaves, and returns. The other car is still stock, which is what shows the save is per-car
# rather than one global mod state.
expect 2 /garage saved: GOLF body=1 rim=NFSU STYLE02/
expect 1 /garage saved: G35 body=0 rim=NFSU STYLE01/
order /garage saved: G35 body=0 rim=NFSU STYLE01/ /garage saved: GOLF body=1 rim=NFSU STYLE02/
forbid /body kit: .* preset installed/
forbid /[Ss]egmentation fault/
