# s13_ask.spec - the prompt-to-race gate: the game asks, the answer is Yes, the race engages.
#   python3 -I tools/log_assert.py tools/log_specs/s13_ask.spec <gate>/s13.log
# The order is the whole point: engaging without being asked, or engaging after a No, is a bug.
order /career: asking to race/ /career: race question answered Yes/ /career: engage/
expect 1 /career: asking to race \(-?[\d.]+ -?[\d.]+\)/
expect 1 /career: race question answered Yes/
forbid /race question answered No/
within /career: asking to race/ /career: engage/ 60
