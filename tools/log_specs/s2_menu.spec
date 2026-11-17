# s2_menu.spec - the menu round-trip gate: Quick Race arms, the pause menu opens, Quit returns to
# the main menu, and Free Run then launches from there.
#   python3 -I tools/log_assert.py tools/log_specs/s2_menu.spec <gate>/s2.log
# gate.sh grepped 'race armed|launch quick race'; the two legs are separated here because an arm
# without a return to the menu, or a Free Run that never reached the world, would pass that grep.
# Only the Drag leg prints 'race armed' - a free run has no event to arm - so the proof the second
# leg got somewhere is the live roam frames afterwards: racing off (ract=0), event 0, on L4RA.
expect 1 /fe: launch quick race event 4201/
expect /race armed: event 4201/
expect /pause: opened at/
expect 1 /fe: launch free run event 0/
expect >=3 /SCRIPT f=[0-9]+ screen=NONE rs=1 ract=0 ev=0 trk=STREAML4RA/
order /launch quick race/ /race armed: event 4201/ /pause: opened/ /launch free run/ /screen=NONE rs=1 ract=0 ev=0 trk=STREAML4RA/
forbid /[Ss]egmentation fault/
