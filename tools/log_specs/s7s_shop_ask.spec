# s7s_shop_ask.spec - the shop-ask gate: a brand-new career buys a car by hand, drives onto the
# lot, is asked the retail question, answers Yes, and the lot opens with an owned car on the list.
#   python3 -I tools/log_assert.py tools/log_specs/s7s_shop_ask.spec <gate>/s7s_shop_ask.log
# No --career-dev runs this step, so 'asking to enter' can only come from the player's Enter Race
# (s7 automates the lot and s7m never drives to a shop, so neither prints it). The order is the
# whole point: the question comes after the zone, the Yes after the question, and a lot carrying an
# owned car (the '*' suffix on a car name) only exists after the purchase.
capture ZONE /career: shop zone (CC_CAR_LOT_1)/
expect 1 /career: shop zone {ZONE}/
expect 1 /career: asking to enter {ZONE}/
expect 1 /career: shop question answered Yes/
forbid /career: shop question answered No/
order /career: new stage/ /career: shop zone {ZONE}/ /career: asking to enter {ZONE}/ /career: shop question answered Yes/
expect 1 /career: bought /
expect 1 /career: lot [A-Z0-9]+ \S+.*\*/
order /career: shop question answered Yes/ /career: lot [A-Z0-9]+ \S+.*\*/
within /career: asking to enter/ /career: shop question answered/ 60
forbid /Segmentation|ASan|AddressSanitizer|LeakSanitizer|abort/
