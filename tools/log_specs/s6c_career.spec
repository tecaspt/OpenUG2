# s6c_career.spec - the first career run from the menu: a new save is created and the player is
# placed in the world, in that order, and the lot opens.
#   python3 -I tools/log_assert.py tools/log_specs/s6c_career.spec <gate>/s6c.log
# gate.sh grepped '^career: (new|placed)' and printed the first three lines. A save that is placed
# but never created (or the reverse) is the bug the order rule catches.
capture STAGE /career: new stage (\d+) bank/
equal STAGE 0
expect /career: placed at \(-?[\d.]+ -?[\d.]+ -?[\d.]+\) heading -?[\d.]+/
expect /career: lot [1-9][0-9]* cars:/
order /career: new stage/ /career: placed at/ /career: lot [1-9][0-9]* cars:/
forbid /[Ss]egmentation fault/
