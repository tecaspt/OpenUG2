# Full empty-profile story loop, contract selection and signing credit.
capture START /career: new stage (\d+) bank/
equal START 0
expect 1 /career: bought 240SX for \$0/
expect 1 /career: stage 0 -> 1/
expect 5 /career: STAGE_1_\S+ place 1 \+250 cash \+100 rep/
expect 1 /career: sponsor offer stage 2 bank 1250/
expect 1 /career: sponsor STREETGLOW signing 1275 bank 2525 stage 2/
order /career: new stage/ /career: bought/ /career: stage 0 -> 1/ /stage complete/ /career: sponsor offer/ /career: sponsor STREETGLOW signing/
forbid /[Ss]egmentation fault|AddressSanitizer|LeakSanitizer|Assertion/
