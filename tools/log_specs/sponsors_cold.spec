# The same selected contract and bank survive a fresh process with no second credit.
expect 1 /career: sponsor restored STREETGLOW stage 2 bank 2525/
expect 1 /career: continue stage 2 bank 2525/
order /career: sponsor restored/ /career: continue stage/
forbid /career: new stage|career: sponsor offer|career: sponsor STREETGLOW signing/
forbid /[Ss]egmentation fault|AddressSanitizer|LeakSanitizer|Assertion/
