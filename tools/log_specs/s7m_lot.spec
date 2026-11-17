# s7m_lot.spec - buying a car by hand, with no --career-dev help: the lot opens, one screenshot
# proves the confirm prompt is up, and the purchase is made.
#   python3 -I tools/log_assert.py tools/log_specs/s7m_lot.spec <gate>/s7m.log
# gate.sh grepped '^career: (lot |bought)'. The purchase alone would also be printed by career-dev,
# so the shot taken between the lot and the bought line is what pins this run to the manual path.
capture LOT /career: lot (\d+) cars:/
equal LOT 6
expect 1 /career: bought [A-Z][A-Z0-9]+/
expect 1 /script: wrote s7m_confirm\.png/
order /career: new stage/ /career: lot {LOT} cars:/ /script: wrote s7m_confirm\.png/ /career: bought/
forbid /[Ss]egmentation fault/

# Why 'bought <CAR>' and not 'bought <CAR> for $0 at <SHOP>': the purchase line is written to stdout
# in pieces and the frontend's 'fem: screen ...' logging can land between them, splitting the line in
# two (seen on a real run: 'career: bought PEUGOT106 fem: screen LOADING' with 'for $0 at
# CC_CAR_LOT_1...' on the following line). What the game has logged as one unbroken line is 'career:
# bought <CAR>', so that is what is asserted. The split itself is a game bug, not a gate one - see
# TASK-U-LOG - and is left visible in the log rather than papered over.
