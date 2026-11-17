# p3_desktop.spec - the garage keeps what it is given: a stock save, then a body kit previewed and
# kept, then a rim change kept on top of the kit.
#   python3 -I tools/log_assert.py tools/log_specs/p3_desktop.spec <gate>/p3-desktop.log
# The desktop run itself never prints 'garage saved' with the kit on; the kit and rim survive to the
# next boot, which is what p3_cold.spec asserts. So this spec pins the install order - the kit
# installed, then the rims, then the kit still installed after that change (the last two install
# lines come after 'garage rims'). That order is the whole risk here: a rim change that dropped the
# kit would leave the same saves and the same shots, and only the order would show it.
expect 1 /garage saved: GOLF body=0 rim=NFSU STYLE01/
expect 5 /body kit: KIT01 preset installed/
expect 1 /garage rims: NFSU STYLE02 action=1/
within /garage rims: NFSU STYLE02 action=1/ /body kit: KIT01 preset installed/ 20
forbid /[Ss]egmentation fault/
