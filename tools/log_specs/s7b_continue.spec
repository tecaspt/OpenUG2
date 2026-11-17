# s7b_continue.spec - the save survives a restart: the career is continued, not recreated, and the
# car bought in the previous run is the one the player is placed in.
#   python3 -I tools/log_assert.py tools/log_specs/s7b_continue.spec <gate>/s7b.log
# 'continue' with a stage number is the whole point: a run that silently started a new game would
# print 'new stage 0' and still drive fine, so a new game is forbidden here rather than merely absent.
capture STAGE /career: continue stage (\d+) bank/
range STAGE 1 99
expect 1 /car switched in process: 240SX/
order /career: continue stage/ /career: placed at/ /car switched in process: 240SX/
forbid /career: new stage/
forbid /[Ss]egmentation fault/
