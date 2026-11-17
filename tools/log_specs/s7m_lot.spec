# s7m_lot.spec - buying a car by hand, with no --career-dev help: the lot opens, one screenshot
# proves the confirm prompt is up, and the purchase lands in the save.
#   python3 -I tools/log_assert.py tools/log_specs/s7m_lot.spec <gate>/s7m.log
# gate.sh grepped '^career: (lot |bought)'. The purchase alone would also be printed by career-dev,
# so the shot taken between the lot and the bought line is what pins this run to the manual path.
capture LOT /career: lot (\d+) cars:/
equal LOT 6
expect 1 /career: bought \S+ for/
expect 1 /script: wrote s7m_confirm\.png/
order /career: new stage/ /career: lot {LOT} cars:/ /script: wrote s7m_confirm\.png/ /career: bought/
forbid /[Ss]egmentation fault/
