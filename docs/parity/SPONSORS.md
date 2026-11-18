# Career sponsor contracts

Clean-room evidence: retail data and source only. No executable code was inspected.

## Contract table

[data] The first career container `0x80034A10` in `GLOBAL/GLOBALB.BUN`
contains `0x34A19`: 320 bytes, exactly twenty 16-byte records. Stages 2–5
reference five contract hashes each; stages 0–1 have none.

| Offset | Type | Interpretation |
|---|---|---|
| 0 | u16 | [data] Name offset into `0x34A1D` |
| 2 | u16 | [inferred] URL victory bonus; matches the authored "Each URL Victory" field |
| 4–6 | three u8 | [data] Required race kinds: 1 circuit, 2 drift, 3 drag, 4 sprint, 5 Street X; independently matches all twenty `SPONSOR_INFO_NAME` texts |
| 7 | u8 | [data] Zero; purpose unknown |
| 8 | u32 | [data] Name hash, matches stage sponsor references |
| 12 | u16 | [inferred] Signing bonus; increasing amounts match the authored bonus fields |
| 14 | u16 | [inferred] Minimum average reputation; 0–275, ascending within each stage |

[data] Names use `SPONSOR_NAME`, `SPONSOR_INFO_NAME` and
`SPONSOR_DETAILS_NAME` language keys. Details describe stage contract totals,
three sponsor victories, URL victories and DVD covers. No separate style
requirement occurs in this table. The threshold's exact retail calculation
cannot be established from static data. The implementation compares lifetime
reputation divided by entered attempts [inferred]. This uses existing career
reputation; it does not fabricate a visual rating.

| Stage | Sponsor | URL bonus | Signing | Threshold | Race selectors |
|---|---|---:|---:|---:|---|
| 2 | STREETGLOW | 900 | 1275 | 0 | 1,1,1 |
| 2 | EUROLITE | 875 | 1325 | 75 | 2,2,2 |
| 2 | EDELBROCK | 875 | 1375 | 150 | 3,3,3 |
| 2 | TOYO | 850 | 1450 | 200 | 1,4,5 |
| 2 | YOKOHAMA | 825 | 1500 | 250 | 2,3,5 |
| 3 | BRIDE | 1150 | 1775 | 0 | 1,4,3 |
| 3 | PIAA | 1125 | 1875 | 75 | 5,1,4 |
| 3 | ALPINE | 1100 | 1950 | 150 | 5,5,5 |
| 3 | OZ_WHEELS | 1050 | 2200 | 200 | 2,2,2 |
| 3 | NITROUS_EXPRESS | 1000 | 2500 | 250 | 3,3,3 |
| 4 | KENWOOD | 2250 | 5200 | 0 | 1,4,2 |
| 4 | SPARCO | 2175 | 5700 | 75 | 2,3,4 |
| 4 | NOS | 2150 | 6000 | 150 | 5,5,5 |
| 4 | ENKEI | 2075 | 6500 | 200 | 1,1,1 |
| 4 | BREMBO | 2000 | 7000 | 250 | 3,3,3 |
| 5 | KONIG | 3225 | 13000 | 0 | 3,4,5 |
| 5 | BILSTEIN | 3175 | 13400 | 125 | 2,3,1 |
| 5 | STILLEN | 3100 | 14000 | 200 | 2,2,2 |
| 5 | NEUSPEED | 3050 | 14500 | 250 | 1,1,1 |
| 5 | GREDDY | 3000 | 15000 | 275 | 5,5,5 |

## Rules and persistence

[inferred] Offer the stage's contracts on entering free roam in stages 2–5,
including continued old careers with no selection. Selection is immutable for
that stage. Signing and its bank credit are one transaction, rolled back on save
failure. Schema 6 adds TLV 9: eight u32 sponsor hashes indexed by stage.
Schemas 1–5 migrate to unsigned contracts, preserving their bank and progress;
there is no retroactive bonus. Unknown TLVs still round-trip.

[inferred] Select the first unused sponsor event matching each race selector in
retail table order. This chooses exactly three events and avoids making all
sponsor races available. Both category and unlock-mode gating apply, since
many sponsor events use the ordinary stage unlock mode. Their first wins count
toward existing sponsor/stage and performance-unlock counters. Sponsor races
pay their existing race purse. First URL victories in the signed stage add the
contract URL bonus to the existing reward, including the final URL before stage
advance. Loss, failure, abort and replay pay no sponsor bonus.

DVD covers remain the career module's existing inferred satisfied condition;
free-car capacity and promotional covers are outside this sponsor slice.
