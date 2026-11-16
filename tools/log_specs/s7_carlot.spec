# s7_carlot.spec - the M6 car-lot gate: stage 0, a full lot, a purchase, the crib, stage 0 -> 1.
# What tonight's s7 run had to prove, said as rules instead of a hand grep:
#   python3 -I tools/log_assert.py tools/log_specs/s7_carlot.spec <gate>/s7.log
capture STAGE /career: new stage (\d+) bank/
equal STAGE 0
capture LOT /career: lot (\d+) cars:/
equal LOT 6
expect 1 /career: bought \S+ for/
expect /career: stage {STAGE} ->/
order /career: new stage/ /career: lot (\d+) cars:/ /career: bought/ /career-dev: crib/
order /career-dev: crib/ /career: stage (\d+) -> (\d+)/
expect 1 /stage complete/
within /career: stage (\d+) -> (\d+)/ /stage complete/ 500
# The brief's crash set was Segmentation|ASan|abort; a sanitizer's own banner is
# "ERROR: AddressSanitizer:" / "SUMMARY: AddressSanitizer:" / "ERROR: LeakSanitizer:", which a bare
# "ASan" never matches, so all three spellings are forbidden here. None of them is in any gate log.
forbid /Segmentation|ASan|AddressSanitizer|LeakSanitizer|abort/
