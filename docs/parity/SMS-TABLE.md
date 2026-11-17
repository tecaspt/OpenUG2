# SMS / message table (GLOBALB sub-chunk 0x34A17)

Task V. Everything below comes from reading the retail DATA files only
(`GLOBAL/GLOBALB.BUN`, `LANGUAGES/English.bin`, `LANGUAGES/Labels.bin`,
`TRACKS/ROUTESL4RA/*`); `SPEED2.EXE` and any decompilation were never opened.
Each claim is tagged `[data]` (read directly from the bytes) or `[inferred]`
(our interpretation, with the reason stated). Where we did not resolve a field,
that is said plainly.

## 1. Where the table lives [data]

`GLOBAL/GLOBALB.BUN` holds a career container at file offset `0x751480`
(magic `0x80034A10`, payload `0x116b0`). Inside it is a list of
`{u32 magic, u32 size, payload}` sub-chunks; the ones that matter here are:

| sub-chunk | size | content |
|---|---|---|
| `0x34A11` | 30464 | races, 136-byte records (224) - already parsed by `career_data.c` |
| `0x34A12` | 3840 | shops, 160-byte records (24) |
| **`0x34A17`** | **3340** | **the SMS/message table - 167 records x 20 bytes** |
| `0x34A18` | 480 | stages, 80-byte records (6) |
| `0x34A1D` | 10492 | the name string blob (827 NUL-terminated names) |
| `0x34A1F` | 288 | car unlocks, 12-byte records (24) |

`3340 = 167 * 20`, so the chunk is exactly 167 fixed-size records with no
header and no padding [data]. A second, smaller career container exists at
`0x762b80` (the demo career) and carries its own `0x34A17` of 280 bytes
(14 records); `cd_parse_globalb` reads the first container only, exactly as it
already ignores the demo copy of every other table, so `nsms` is 167 and not
181 [data].

Record order is stable and meaningful: retail table order groups the records by
kind (tutorials 0-7, part unlocks 8-60, location notices 61-69, magazine/unique
blurbs 70-97, DVD covers 98-107, car unlocks 108-124, driving tips 125-166)
[data].

## 2. The 20-byte record

```
+0x00 u16  name_off   offset of the record's name in the 0x34A1D blob
+0x02 u8   trigger    14 distinct values 01..0f, meaning not resolved
+0x03 u8   class      02/03/04/05, in 1:1 correspondence with +0x05
+0x04 u8   flag       0 or 1
+0x05 u8   kind       01/03/05/09
+0x06 u8   0x88       constant
+0x07 u8   0x00       constant
+0x08 u32  arg        entity hash, self hash, or two u16 args (see 2.2)
+0x0C u16  weight     0 for 125 records, 25 for 38, 50 for 4
+0x0E u16  0          constant
+0x10 u32  sender     language key of the sender display name
```

### 2.1 `+0x00` name offset [data]

A u16 offset into the `0x34A1D` blob, the same convention as the races table
(`+0x00` name, `+0x06` locator). All 167 offsets resolve to a NUL-terminated
name; range 872..3851 within the 10492-byte blob. Names are `SMS_*` (125
records) and `ENGAGE_TIPS_*` / `ENGAGE_TIP_CAREER_PROG` (42). The longest retail
name is `SMS_UNIQUE_STAGE_4_VISUAL_3` (27 chars), so `char name[40]` never
truncates on retail.

The name doubles as the base of two language keys: `ug_hash(NAME + "_BODY")` and
`ug_hash(NAME + "_SUBJECT")` [data]. 123 of the 167 records have both; 44 have
neither (see section 4). We found no other suffix in use: the only suffixes that
ever hit are `_BODY` and `_SUBJECT` (`_TEXT`, `_TITLE`, `_MSG`, `_DESC`, `_TIP`,
`_SMS`, `_MESSAGE` and the bare name were all tested against the full English
table and hit nothing beyond those two) [data].

### 2.2 `+0x02..+0x07` the classification bytes

Value counts over all 167 records [data]:

```
+0x02: 01:1  02:3  03:4  04:1  05:1  06:1  09:5  0a:30  0c:41  0d:28  0e:10  0f:42
+0x03: 02:42  03:38  04:34  05:53
+0x04: 00:136 01:31
+0x05: 01:34  03:38  05:53  09:42
+0x06: 88:167      +0x07: 00:167
```

* `+0x06 == 0x88` and `+0x07 == 0` in every record, and in no other career
  sub-chunk at the same offsets [data]. We have no referent for the constant; it
  is stored and reported, not interpreted.
* `+0x03` and `+0x05` are in exact 1:1 correspondence over all 167 records:
  `02<->09` (tips), `03<->03` (magazine/unique blurbs), `04<->01` (SMS messages),
  `05<->05` (part-unlock notices) [data]. One of them is therefore redundant;
  which one the game actually branches on is not determinable from data alone,
  so both are stored [inferred].
* `+0x05` is the more descriptive of the two: each value coincides with a
  distinct name prefix and a distinct text-key situation - 01 = `SMS_*` message,
  03 = `SMS_MAG_*`/`SMS_UNIQUE_*`/`SMS_DVD_COVER_*` blurb, 05 = `SMS_UNLOCK_*`
  part notice, 09 = `ENGAGE_TIPS_*` driving tip [data for the coincidence,
  inferred for the causality]. We name it `kind`.
* `+0x04` is a binary flag, 1 in exactly 31 records and 0 in 136 [data]. The 31
  are: `SMS_GPS_INTRO`, the four `SMS_LOC_TEXT_*`, `SMS_END_OF_GAME_MESSAGE`,
  all eight `SMS_UNIQUE_*`, the six `SMS_MAG_S2/STAGE_3_SHOW_*`, all ten
  `SMS_DVD_COVER_*`, and `ENGAGE_TIP_CAREER_PROG`. It is 0 for the fourteen
  `SMS_MAG_STAGE_4/5_SHOW_*` and for every other record. No referent found;
  stored as `flag`, meaning unknown.
* `+0x02` takes 14 distinct values and splits each kind into named sub-blocks
  (0x03 = the four control tutorials, 0x04/0x05/0x06 = the three Outrun notices,
  0x0d = magazine/unique blurbs, 0x0e = the ten DVD covers, 0x0f = all 42 tips,
  0x09/0x0a/0x0c = the part-unlock notices and car/location notices) [data]. It
  behaves like a trigger/grouping id, but no cross-reference gives it a meaning:
  it matches no shop, race, stage, car, marker or language key. Stored as
  `trigger`, meaning unknown.

### 2.3 `+0x08 arg` - one u32, three documented readings

Over all 167 records the field takes 103 distinct values, 0..`0xfc25fd37`
[data]. Three disjoint readings cover every record:

1. **Self hash - 42 records, all tips (kind 09).** `arg == ug_hash(name)` for
   all 42 `ENGAGE_TIPS_*` records and for those 42 only [data]. Same convention
   as the races table (`+0x08 name_hash`) and the shops table (`+0x38 hash`), so
   the field is plausibly "this record's own key" for the tips - the tips are the
   records that must be looked up by hash at runtime [inferred].
2. **Entity hash - 4 records, all kind 01.** Found by hashing every printable
   string of at least four characters in all 653 `.bin/.bun/.dat/.fng/.txt`
   files of the data tree [data]:
   * `SMS_GPS_INTRO` and `SMS_LOC_GO_TO_GARAGE` both hold `0x008ba2d3` =
     `ug_hash("CAR_LOT_1")`, a shop name in the `0x34A12` table (the two records
     share the value, consistent with "go to the car lot");
   * `SMS_LOC_EVENTS_OUT_THERE` holds `0x55446b8f` = `ug_hash("CRIB_1")`, a shop
     and a position marker;
   * `SMS_END_OF_GAME_MESSAGE` holds `0x04cce3e1` = `ug_hash("S5_CIRCUIT_36")`, a
     race name in `0x34A11`.
     All four are "go to X / after X" messages, so the reading "a `ug_hash`
     reference to another career table's name" is [inferred] from [data].
3. **Two u16 arguments - the remaining 121 records.** Read as u16 pairs the bytes
   split into `arg_lo` (+0x08) 0..30 and `arg_hi` (+0x0A) 0..5, and the pair is
   always small: `arg < 0x200000` for all 121, so a hash reading is excluded
   [data]. The upper u16 groups the records into 1..5 ascending runs that match
   career progression, and the lower u16 counts within the group:
   * `SMS_CAR_UNLOCK_n` (17 records): `arg_hi` = 2,3,4,5 and `arg_lo` restarts at
     1 in each group (1-3 / 1-5 / 1-7 / 1-2) while the names number 1..17
     continuously [data]. So `arg_hi` behaves like the 1-based stage index and
     `arg_lo` like the ordinal of this notice within that stage [inferred]. Note
     this is *not* the number of cars: the car-unlock table `0x34A1F` has
     6/7/8/2 cars per stage 3/4/5/6, so about two cars per notice - `arg_lo`
     counts notices, not cars [data].
   * `SMS_UNLOCK_*` (53 part notices): same pair shape; `arg_hi` = 1..5 and
     `arg_lo` a per-stage value up to 30, always at most the number of races that
     stage holds (races per stage byte: 5/5/35/48/62/69) [data]. We could not
     decide between "win N races in stage S" and "the Nth unlockable of stage S";
     the values repeat within a stage (stage 3 holds 1,1,2,3,3,3,3,4,...), which
     rules out a plain per-stage ordinal, so this is reported as an unresolved
     (count, stage) pair [inferred].
   * `SMS_DVD_COVER_n` (10): `arg_lo` = 1..10 equals the `_n` in the name and
     `arg_hi` = 0 - a plain index [data].

Because the same four bytes carry all three readings, the parser exposes one
`uint32_t arg` and keeps the raw bytes; the u16 halves are derived where a
consumer needs them (`SMS_CdSms_ARG_LO` / `_ARG_HI` in `career_data.h`).

### 2.4 `+0x0C weight` [data values, unknown meaning]

0 for 125 records, 25 (`0x19`) for 38, 50 (`0x32`) for 4. The 42 non-zero records
are exactly the 42 tips - 25 for 38 of them and 50 for the last four
(`..._FILTER`, `..._HIDDEN`, `..._TRLBRK`, `..._ORGANIC`) [data]. It is zero for
every non-tip record, including the DVD covers. Two round values only; "chance"
or "delay" are guesses we cannot support, so the field is named `weight` and its
meaning is left unknown.

### 2.5 `+0x0E` and `+0x10` [data]

`+0x0E` is `0x0000` in every record. `+0x10` is a u32 language key: all 167
records hit `LANGUAGES/English.bin`, giving 11 distinct display names - `Rachel`
(96), `Bayview Body Shop` (26), `Bayview Car Specialties Shop` (15),
`Bayview Performance Shop` (10), `Rachel` again under a second key (10),
`Kristian` (3), `Bayview Graphics Shop` (2), `Chuck` (2), and one each of
`Scott`, `Serena`, `Stefan`. The label behind each key is in
`LANGUAGES/Labels.bin` [data]:

| key | Labels.bin label | English.bin text |
|---|---|---|
| `0x770af86e` | (also `ug_hash("RACHEL")`) | Rachel |
| `0x02902eed` | `CHUCK` | Chuck |
| `0x1b4a3984` | `KRISTIAN` | Kristian |
| `0x03aee1cc` | `SCOTT` | Scott |
| `0x79b0b11d` | `SERENA` | Serena |
| `0x7ab90260` | `STEFAN` | Stefan |
| `0xc9e0946b` | `SMS_FROM_BODY_SHOP` | Bayview Body Shop |
| `0x3194b26f` | `SMS_FROM_CAR_SPECIALTIES` | Bayview Car Specialties Shop |
| `0xf779c04a` | `SMS_FROM_PERF_SHOP` | Bayview Performance Shop |
| `0x7aca0a4e` | `SMS_FROM_GRAPHICS_SHOP` | Bayview Graphics Shop |
| `0x34a89700` | `SMS_FROM_DVD_COVER` | Rachel |

So the field is the sender of the message: a person's name for the personal
messages and tips, and a shop type for the part-unlock notices [data for the
lookup and the label; inferred for "sender" as the role, which is how the
`SMS_FROM_*` label names read]. Note the last row: the DVD-cover records use the
`SMS_FROM_DVD_COVER` key, whose text happens to be `Rachel` as well - two keys,
same display text, so text equality must not be used to compare senders [data].

Which sender each kind gets: tips and `SMS_*` messages are all `Rachel`; part
notices carry one of the four shop keys; magazine/unique blurbs carry a person
(`Chuck`, `Kristian`, `Scott`, `Serena`, `Stefan`) or `Rachel`; DVD covers carry
`SMS_FROM_DVD_COVER` [data].

## 3. Grouping the 167 records by kind

| kind `+0x05` | class `+0x03` | n | names | has `_BODY`/`_SUBJECT` | arg reading |
|---|---|---|---|---|---|
| 01 | 04 | 34 | tutorials, Outrun notices, `SMS_LOC_*`, `SMS_END_OF_GAME_MESSAGE`, `SMS_CAR_UNLOCK_n` | 18 | self? no: index / entity hash |
| 03 | 03 | 38 | `SMS_MAG_*_SHOW_*`, `SMS_UNIQUE_*`, `SMS_DVD_COVER_n` | 10 (exactly the DVD covers) | 0, or 1..10 for the covers |
| 05 | 05 | 53 | `SMS_UNLOCK_nn` part notices | 53 | (count, stage) pair |
| 09 | 02 | 42 | `ENGAGE_TIPS_*` | 42 | self hash |

Breakdown of the 34 kind-01 records: 8 tutorial/Outrun/GPS notices (0-7), 9
location notices (61-69), 17 `SMS_CAR_UNLOCK_n` (108-124) [data].

## 4. Language text [data]

`LANGUAGES/English.bin` is read with the project's own loader
(`tools/lang_key_audit.py`, a port of `ug_strtab_load`); it yields 3925 keys.

* 123 of 167 records have both `NAME_BODY` and `NAME_SUBJECT` (the sets are
  identical - no record has one without the other).
* 44 have neither: all 28 `SMS_MAG_*_SHOW_*` / `SMS_UNIQUE_*` blurbs, and
  `SMS_CAR_UNLOCK_2..17` (16 records). `SMS_CAR_UNLOCK_1` does have text ("New
  Inventory in the Car Lot"), so retail reuses one message for the rest of the
  car-lot notices rather than having 17 [data]. The magazine blurbs have no text
  under any tested suffix, so their presentation must come from elsewhere (we did
  not resolve it; chunk `0x34A16` holds names like `STAGE_2_SHOW_1_PICTURE`,
  which suggests images, but that is outside this task and unverified).

Three short examples [data]:

* `SMS_UNLOCK_01_SUBJECT` = "Window tint";
  `SMS_UNLOCK_01_BODY` = "Give your ride that extra touch of style with all new
  window tints. Now available at all Bayview Car..."
* `SMS_CAR_UNLOCK_1_SUBJECT` = "New Inventory in the Car Lot"; body opens "Nice
  driving out there! Head over to the Car Lot when you have a moment..."
* `SMS_LOC_TEXT_COAL_HARBOR_SUBJECT` = "Coal Harbor"; body opens "Yo dude, the
  construction on the road to Coal Harbor has been cleared up..."

## 5. What stays unknown

* `+0x02 trigger`: 14 values, consistent grouping behaviour, no referent found.
* `+0x04 flag`: exact correlation with the arg reading and with the
  magazine-blurbs, no referent found.
* `+0x06 == 0x88` and the `+0x0E` zero: constants with no referent.
* `+0x0C weight`: 25/50 on tips only, meaning unknown.
* The lower u16 of the part-notice pair (`SMS_UNLOCK_*`): a per-stage count of
  something unresolved (races won, notices, or unlockables - see 2.3).
* Which of `+0x03` / `+0x05` the game actually branches on (they are 1:1 here, so
  retail data cannot separate them).
* Where the 28 magazine-blurb records get their presentation text.

## 6. How OpenUG2 reads it

`src/career_data.c:parse_sms` fills `CareerData.sms[]` / `nsms` from
`0x34A17`, called from `cd_parse_globalb` next to `parse_unlocks`. Missing chunk,
size not a multiple of 20, or a malformed table leaves `nsms == 0` and is never
an error, exactly like the shop and unlock tables. A record whose name offset is
out of range keeps its raw bytes and its other fields with an empty `name`, so
the array index always equals the record index in the table. `name_hash` is
`cd_hash(name)` (the record itself carries no name hash), and
`cd_find_sms(cd, name_hash)` returns the first index or -1. The sentinel for "no
such record" is -1 and the guard inside is against `cd_hash("")`, not 0: a record
whose name offset failed to decode ends up with an empty name, which hashes to
`0xFFFFFFFF`, so asking for that hash must not find it (no non-empty name used in
these tables hashes there - the only other `0xFFFFFFFF` producers would be
one- to three-character names, and the blob has none).

### 6.1 Printing it

`tools/career_dump.py --section sms` prints one row per record (name, kind, the
classification bytes, the applicable reading of `+0x08`, weight, the sender's
display text, and whether `NAME_BODY` / `NAME_SUBJECT` resolve); `--json` writes
every field, including both `arg` halves and the resolved body/subject text.
`tools/career_data_check.c DATAROOT --sms FILE` writes the C parser's records as
JSON and `career_dump.py --cd-sms FILE` writes the same layout from the Python
port, so `diff` between the two is the agreement check: on retail it is empty
(167 records x 10 fields). `career_data_check DATAROOT` alone asserts the counts
this document states - `nsms == 167`, the 34/38/53/42 kind split, 42 tip
self-hash args, 31 flagged records, 42 weighted ones, and the 4 entity-hash
records by name.
