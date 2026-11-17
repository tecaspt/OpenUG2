# p3_cold.spec - the cold-restart half of the persistence proof: this run opens the save the
# p3-desktop run wrote, so the kit and rim must already be on the car before anything is touched.
#   python3 -I tools/log_assert.py tools/log_specs/p3_cold.spec <gate>/p3-cold-desktop.log
# Nothing here installs a kit: body=1 (KIT01) and rim NFSU STYLE02 have to arrive from the save
# alone. The game saves on entry to the garage page, so the car is written twice - once on the cold
# load, once again when the script returns to it - and 'expect 2' is what proves both. The other car
# is still stock, which is what shows the save is per-car rather than one global mod state, and it
# has to be seen stock *after* the GOLF save, so the order below is G35 last, not GOLF.
expect 2 /garage saved: GOLF body=1 rim=NFSU STYLE02/
expect 1 /garage saved: G35 body=0 rim=NFSU STYLE01/
order /garage saved: GOLF body=1 rim=NFSU STYLE02/ /garage saved: G35 body=0 rim=NFSU STYLE01/
forbid /body kit: .* preset installed/
forbid /[Ss]egmentation fault/
