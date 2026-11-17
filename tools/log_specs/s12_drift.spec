# s12_drift.spec - the drift event gate: event 4174 is armed as a Drift race and the run drives it.
#   python3 -I tools/log_assert.py tools/log_specs/s12_drift.spec <gate>/s12.log
# gate.sh only printed the rc and the shot count, so the proof of the run itself lives here: the
# event decoded as a Drift race, its race armed with gates and a grid, and the car still racing
# (ract=1) with the handbrake held (held=0x2) while it is being driven sideways.
expect /race event 4174 \(L4RA, Drift/
expect /race armed: event 4174 \(Drift\)/
expect >=4 /script: wrote drift-[0-9]+\.png/
expect >=1 /SCRIPT f=[0-9]+ screen=NONE rs=1 ract=1 ev=4174 .* held=0x2/
forbid /[Ss]egmentation fault/
