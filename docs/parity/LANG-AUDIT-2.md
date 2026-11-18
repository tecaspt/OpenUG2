# Language-key audit 2: career, dialogs, SMS, world map, post-race (Task AG)

Every retail language key the career / dialog / SMS / world-map / post-race code writes, checked against
`LANGUAGES/English.bin` with `tools/lang_key_audit.py` (its reader is the C loader's,
`src/ugui/ugui_strtab.c:ug_strtab_load`, and `ug_hash_upper` is self-tested before any verdict is
printed). 3 925 entries, one string-table chunk at offset 0, count field 3925, 3 925 unique hashes.

Scope is the files the brief names, at the HEAD of `tools/lang-audit-2`: `src/fe_lot.c`, `src/fe_sms.c`,
`src/gui_dialog.c`, `src/career_host.c`, `src/main.c` (career shop / engage / world-map lines plus every
text call in it), `src/fe_worldmap.c`, `src/rear_mirror.c`, `src/postrace.c`.

Verdicts: **OK** = the key is in the table and the text follows; **MISSING** = not in the table;
**UNKNOWN-NAME** = the key resolves to retail text but no recoverable label hashes to it (the string
table stores hashes and text, never labels — section 1 says exactly what names are evidenced).
A fourth class, **not-a-language-key**, is listed separately in section 4: the career code stores three
other 32-bit namespaces in the same-looking hex constants, and judging those against a string table
would be a false finding.

Tags as elsewhere: `[data]` = read out of the game DATA or the OpenUG2 sources; `[inferred]` = our
reading. All paths are repo-relative; no absolute host paths, no game data in this file.

```
mkdir -p /tmp/scope/src && cp src/fe_lot.c src/fe_sms.c src/gui_dialog.c src/career_host.c \
  src/fe_worldmap.c src/rear_mirror.c src/postrace.c src/main.c /tmp/scope/src/
flock "$HEAVY_LOCK" nice -n 10 \
  python3 -I tools/lang_key_audit.py --data "$OPENUG2_DATA" --src /tmp/scope/src --json /tmp/rows.json
python3 -I tools/lang_key_audit_test.py            # 42 tests, no data access, no wrapper needed
```

Data-touching runs go under the owner's shared heavy lock plus `nice -n 10`, as in the other parity docs
(`$HEAVY_LOCK` is that lock file here, not an exported name — use the one your environment defines). The
commands quoted in section 6 are shown bare for readability, and the `fng_dump.py` / `career_dump.py`
results this file cites were reproduced under the wrapper (`cmp` clean against the un-wrapped run).

`--src` takes one directory, so the in-scope count comes from a copy of the named files into a scratch
tree (`main.c` in full, not just the career lines): 48 rows — 26 `key-field`, 18 accessor `call`, 4
`labelled-literal` — in `gui_dialog.c` (29), `postrace.c` (6), `fe_lot.c` (5), `fe_sms.c` (3),
`main.c` (3) and `fe_worldmap.c` (2). Collapsing the repeats (the four dialog button keys are each
written in two branches, and `fe_worldmap.c:137` / `postrace.c:117` check two keys on one line) leaves
the 46 rows of section 1; `postrace.c:238`, whose three keys sit in a plain array the tool cannot see,
is added by hand. Section 3 then covers the keys that reach the tool only as a runtime value, resolved
from the source that produces them.

## 1. Verdict table [data]

`name` records where the key's identity comes from, and only from evidence:

- **comment** — the source says it (`/* ASK_ENTER_CAR_LOT */`, `/* LABEL_PLAYER */`).
- **vectors** — `docs/cleanroom-settings/test-vectors-gui/tvg10_dialogs.json` names the key and its
  English text for the dialog specs (`message_key` / `label_key` / `message_english`).
- **sample** — the key is the `ug_hash_upper` of its own text, the retail "the text is the label"
  spelling `tools/fng_dump.py:label_of` reports; `0x00000639` "OK", `0x0000061c` "No", `0x0000f770`
  "Yes" all hash from their own caption. The two world-map rows are named the same way from the other
  side: `fng_dump` reports a node's `label_hash` and the text it resolves to, so "World Map" /
  "Select Event" are the retail labels of nodes 105 / 11, not guesses. `QR_URL` is named because
  `ug_hash_upper("QR_URL")` reproduces its hash.
- **—** — retail stores no recoverable label for the key. The string table holds hashes and text only,
  never the label, so identity is unrecoverable unless one of the three above supplies it. This is the
  **UNKNOWN-NAME** case, and it is the majority here: 12 of the 46 keys have no retail label at all —
  the M6 engage case, the three shop-engage prompts, the knocked-out pair, both post-race "Won:" labels
  and the three post-race menu captions. The candidate names worth trying were tried and are absent
  (`WORLD_MAP_TITLE`, `SELECT_EVENT`, `RESTART_RACE`, `JUMP_GARAGE`, `ENGAGE_RACE`, `KNOCKED_OUT`,
  `MENU_RESTART`, `MENU_CONTINUE`, `MENU_QUIT_TO_MAIN`, `PRR_BANK_WON`, `PRR_REP_WON` — none hashes into
  the table).

| path:line | key | name | verdict | text |
| --- | --- | --- | --- | --- |
| `src/fe_lot.c:98` | `0xda8f3e96` | CAR_SELECT_CONFIRM_CHOOSE_CAR (comment) | OK | "Do you want this stock %s?" |
| `src/fe_lot.c:99` | `0x7ee84e4e` | CAR_SELECT_SAME_CAR (comment) | OK | "You're already driving your %s!" |
| `src/fe_lot.c:100` | `0x272ebeba` | CAR_LOT_MUST_BUY (comment) | OK | "Rachel needs her car back. You have to select a new car before you leave." |
| `src/fe_lot.c:101` | `0x7edc9715` | CAREER_CAR_LOT_TITLE (comment) | OK | "Car Lot" |
| `src/fe_lot.c:104` | `0x3298ccdb` | HUD_BANK (comment) | OK | "Bank:" |
| `src/fe_sms.c:264` | `0xcc438b1e` | SMS_MESSAGE_HEADER (comment in `fe_sms.c:260`) | OK | "Message %d of %d" |
| `src/fe_sms.c:272` | `0xaaabbd7a` | SMS_HEADER (comment in `fe_sms.c:259`) | OK | "Incoming Mobile Message" |
| `src/fe_sms.c:279` | `0x2ccba545` | SMS_MSG_NUM_UNREAD (comment in `fe_sms.c:275`) | OK | "%d unread messages" |
| `src/fe_worldmap.c:137` | `0xa0c6ae56` | World Map — the retail "SH" label key of node 105 of `UI_CareerWorldMap.fng` | OK | "World Map" |
| `src/fe_worldmap.c:137` | `0xdaac95f9` | Select Event — the "SH" label key of node 11 of the same package | OK | "Select Event" |
| `src/gui_dialog.c:120` | `0x6f4796a6` | vectors (career quit case) | OK | "Quit to Main Menu?" |
| `src/gui_dialog.c:121` | `0x30ed2368` | vectors | OK | "Quit to Main Menu?" |
| `src/gui_dialog.c:122` | `0xe15b558a` | vectors | OK | "Quit to Main Menu?^All unsaved race progress will be lost." |
| `src/gui_dialog.c:123` | `0xce50da14` | vectors | OK | "Are you sure you want to quit to the Online Main Menu?" |
| `src/gui_dialog.c:124` | `0xbc25c6b1` | vectors | OK | "Are you sure you want to quit to the Online Main Menu?  You will receive a loss and a disconnect for this game." |
| `src/gui_dialog.c:125` | `0x555bd5b6` | vectors | OK | "Leaving the race now will end this session.  Are you sure you want to leave?" |
| `src/gui_dialog.c:126` | `0x79a85513` | vectors | OK | "Leaving the race now will end this session and you will be given a loss and a disconnect.  Are you sure you want to leave?" |
| `src/gui_dialog.c:127` | `0xd63b6982` | vectors | OK | "Are you sure you want to quit to the LAN main menu?" |
| `src/gui_dialog.c:128` | `0x0972a44b` | vectors | OK | "Restart Race?" |
| `src/gui_dialog.c:129` | `0xaa50aa02` | vectors | OK | "Restart race?^All unsaved race progress will be lost." |
| `src/gui_dialog.c:130` | `0x60b42afb` | vectors | OK | "Return to Explore Mode?" |
| `src/gui_dialog.c:131` | `0x07cd17b0` | vectors | OK | "Return to Explore Mode?^All unsaved race progress will be lost." |
| `src/gui_dialog.c:132` | `0x33c42e03` | vectors | OK | "Back to the World Map?" |
| `src/gui_dialog.c:133` | `0xe761c8b8` | vectors | OK | "Back to the World Map?^All unsaved race progress will be lost." |
| `src/gui_dialog.c:134` | `0xc9ba9e3e` | vectors | OK | "Jump to Garage?" |
| `src/gui_dialog.c:135` | `0x2bbcca6b` | vectors (tvg06 defaults) | OK | "Reset to default settings?" |
| `src/gui_dialog.c:136` | `0xec9488bb` | — (UNKNOWN-NAME; the M6 engage case is asserted in `tools/gui_art_test.c:350`, not in a vector file) | OK | "Would you like to race this event?" |
| `src/gui_dialog.c:137` | `0x0aab553e` | ASK_ENTER_CAR_LOT (comment) | OK | "Do you want to enter the car lot?" |
| `src/gui_dialog.c:139` | `0x78dad861` | — (UNKNOWN-NAME; note `ASK_ENTER_BODY_SHOP` `0x979e6441` does exist and reads "Body Shop", so this is not its key) | OK | "Would you like to enter the Body Shop?" |
| `src/gui_dialog.c:140` | `0x85d2ed13` | — (UNKNOWN-NAME) | OK | "Would you like to enter the Graphics Shop?" |
| `src/gui_dialog.c:141` | `0x15d6b438` | — (UNKNOWN-NAME) | OK | "Would you like to enter the Shop?" |
| `src/gui_dialog.c:167`,`:183` | `0x0000f770` | YES (sample) | OK | "Yes" |
| `src/gui_dialog.c:168`,`:184` | `0x0000061c` | NO (sample) | OK | "No" |
| `src/gui_dialog.c:192`,`:211` | `0x00000639` | OK (sample, comment, vectors) | OK | "OK" |
| `src/gui_dialog.c:193`,`:212` | `0x541389e5` | CANCEL (comment; `ug_hash_upper("CANCEL")`) | OK | "Cancel" |
| `src/main.c:10090` | `0x5d82dba2` | — (UNKNOWN-NAME; M9 banner caption) | OK | "Knocked Out" |
| `src/main.c:10094` | `0x1b59940c` | — (UNKNOWN-NAME) | OK | "You were knocked out!" |
| `src/postrace.c:60`,`:117` | `0x70802830` | PRR_POINTS_TITLE (comment) | OK | "Points" |
| `src/postrace.c:65` | `0xfa37740b` | LABEL_PLAYER (comment) | OK | "Player" |
| `src/postrace.c:117` | `0x75f903f4` | QR_URL (`ug_hash_upper("QR_URL")`, the same key `wm_event_label` uses for URL races) | OK | "URL Race" |
| `src/postrace.c:153` | `0x75d23e29` | — (UNKNOWN-NAME) | OK | "Bank Won:" |
| `src/postrace.c:154` | `0x40152751` | — (UNKNOWN-NAME) | OK | "Reputation Points Won:" |
| `src/postrace.c:238` | `0x80833fc2` | — (UNKNOWN-NAME) | OK | "Restart" |
| `src/postrace.c:238` | `0xde02cf84` | — (UNKNOWN-NAME) | OK | "Continue" |
| `src/postrace.c:238` | `0xc7e1420c` | — (UNKNOWN-NAME) | OK | "Quit to Main Menu" |
| `src/main.c:6453` | `0x5671b2b5` | TRN_GRASSC (comment) | **MISSING** | — (texture name, see 5.1) |

Counts, from the `key` column, one key per row (a name-column counterexample such as
`ASK_ENTER_BODY_SHOP` is prose about another key and is not counted). 46 rows, 46 distinct keys, none
repeated: `0x7edc9715` is both `fe_lot.c:101`'s caption and `main.c:1678`'s map-icon key, but only the
`fe_lot.c` line calls an accessor, so it is one row here and a section 3 line for the other site. Each
row carries exactly one evidence class, precedence MISSING > UNKNOWN-NAME > vectors > comment > sample:
1 MISSING (`0x5671b2b5`, a texture name, not a text key), 11 UNKNOWN-NAME, 34 named — 16 from the dialog
vectors, 13 from a source comment, 5 from the key's own text or the dump's `label_hash`. Every key with a
verdict of OK resolves to non-empty text in `English.bin`, and every quoted text above was re-checked
against the table. Two notes that are not key findings but were checked: `fe_lot.c:100`'s fallback text
is not retail's (5.4), and `postrace.c:65`'s comment is stale (5.4).

## 2. Keys the tool reads, keys it cannot [data]

`lang_key_audit.py` reads three sources: the text accessors (`gv_text`, `pause_menu_text`,
`ug_strtab_get`, `ug_strtab_or`, second argument), a hex literal whose comment is exactly one label
word, and — added for this audit — the language-key fields: `->message_key = 0x…`,
`->label_key[n] = 0x…`, `{ .message_key = 0x… }`, and `{0x…, "English text"}` records inside a
`Text` / `LangKey` struct. `src/gui_dialog.c` went from 3 rows to 29 with that (the pre-change tool
reports the three labelled literals only; 26 `key-field` rows are new), and every one of the new keys
resolves OK, which is the evidence the patterns are aimed at the right namespace.

The patterns are named, not inferred. `grep -rn "label_key *=\|message_key *=" src/` gives nine hits in
five files, and `src/gui_view.c:815-817` is where those two fields reach the accessor
(`gv_text(s->assets, d->spec.message_key, …)` and both `label_key[i]`); inside `gui_dialog.c` the
assignments are at `:165`, `:167-168`, `:181`, `:183-184`, `:191-193`, `:209`, `:211-212`.
`.model_key`, `.package_key`, `.icon_key`, `.source_key`, `.shift_quality_key` are other namespaces and
stay out of the rows. A bare `#define KEY_PKG_SMALL 0x6962C0CDu` has no `.` / `->`, so package identity
is never a row, and `result_msg` / `back_msg` are FE message ids. Section 4 lists each constant that
looks like a key and is not, with the reason.

Thirteen in-scope lines reach the tool with a runtime value, so section 3 resolves each to the constant
set that feeds it (plus the `postrace.c:240` loop, which just re-reads the section 1 array) rather than
pretending the tool found them.

## 3. Keys that travel through a variable [data]

The tool lists these as `unresolved key argument`; each is resolved here to the constant set that feeds
it. All resolve OK except where marked.

| path:line | expression | resolves to | verdict |
| --- | --- | --- | --- |
| `src/main.c:1680-1682` | `wm_event_label()` → `ug_hash_upper(keys[kind])` | QR_CIRCUIT `0x93fdee74` "Circuit", QR_SPRINT `0xdcb9f861` "Sprint", QR_DRAG `0x35102edf` "Drag", QR_DRIFT `0xd7162cfa` "Drift", QR_STREET_CROSS `0x30da61e1` "Street X", QR_URL `0x75f903f4` "URL Race" | OK (6/6) — five of the six have a text-identical `MINIMAP_*` twin and `QR_URL` does not ("U.R.L."); see 5.2b before switching families |
| `src/main.c:1678` | car-lot map icon `label_key` | `0x7edc9715` | OK "Car Lot" — but see 5.2b: retail's own label for this caption is `MINIMAP_CARLOT` `0x93ff4a6e`, the same text under a different key |
| `src/main.c:1679` | crib map icon `label_key` | `0x7ce21662` | OK "Garage" — same shape: `MINIMAP_GARAGE` `0x9d53bd10` also reads "Garage"; see 5.2b |
| `src/main.c:1682` | body / graphics / performance shop icon | MINIMAP_BODY `0x40c7f697` "Body Shop", MINIMAP_GRAPHICS `0x373dad1a` "Graphics Shop", MINIMAP_PERFORMANCE `0xd6f5819b` "Performance Shop" | OK (3/3) — one key each, no second spelling of the family resolves (`MINIMAP_SHOP`, `MINIMAP_CRIB`, `MINIMAP_CAR`, `MINIMAP_SPECIALTY` all absent) |
| `src/main.c:1682` | remaining shop kinds (uniques) | `0xa4e5ee6c` | OK "Car Specialties Shop"; no `MINIMAP_*` spelling of it exists |
| `src/fe_worldmap.c:181` | `m->icon[…].label_key` | the values above, passed in by `wm_snapshot` | OK |
| `src/gui_dialog.c:165`, `:181`, `:191`, `:209` | `out->message_key = t->key` / `= T_DEFAULTS.key` | the `Text` table of section 1: the engage switch picks `T_SHOP_BODY` / `T_SHOP_PAINT` / `T_SHOP_GENERIC` / `T_ENGAGE_SHOP` / `T_ENGAGE` (`:176-178`), the defaults branch uses `T_DEFAULTS` twice (`:165`, `:191`), and the in-game switch uses `quit_text()` plus `T_RESTART{,_LOST}` / `T_EXPLORE{,_LOST}` / `T_MAP{,_LOST}` / `T_GARAGE` (`:200-204`) | OK |
| `src/gui_dialog.c:191` | `out->message_key = T_DEFAULTS.key` | `0x2bbcca6b` | OK |
| `src/main.c:12506` | `pause_menu_text(…, hs.shift_quality_key, NULL)` | `rr_drag_grade_label()`: HUD_SHIFT_SHORT `0x2303d76c`, HUD_SHIFT_GOOD `0x27d2dd45`, HUD_SHIFT_PERFECT `0x0598b065`, HUD_SHIFT_OVER `0x27d75cd8` | OK (4/4) |
| `src/fe_lot.c:96` | `gv_text(a, ug_hash_upper(key), name)`, `key = "CAR_NAME_" + model` | one key per installed car | `name` comes from the lot list, so the reachable set is whatever the career installs; every model name the tree names resolves — `CAR_NAME_PEUGOT` `0x02300668` "206", `CAR_NAME_COROLLA` `0x769d5fe0` "Corolla", `CAR_NAME_240SX` `0xb054c635` "240SX", `CAR_NAME_FOCUS` `0xb1cdcf94` "Focus" (the four of `tools/fe_lot_test.c:27`) and `CAR_NAME_MIATA` `0xb2492820` "MX-5" (`src/fe_lot.c:95`). The catalogue-wide set is unverified — that is 5.3 |
| `src/fe_sms.c:21` (`sms_key`), called at `:62`, `:64`, `:70-71`, `:90`, `:97` | `ug_strtab_get(lang, ug_hash_upper(key))` | the key is built from the record name, so the reachable set is the 167 `tools/career_dump.py --section sms` names: 123 resolve under their own `<name>_SUBJECT` / `<name>_BODY`, 16 `SMS_CAR_UNLOCK_*` reuse the `_1` pair, 28 magazine blurbs use the generic pair | every branch resolves, checked per record: `SMS_CAR_UNLOCK_1_SUBJECT` `0xf81e6031` "New Inventory in the Car Lot", `SMS_CAR_UNLOCK_1_BODY` `0x98f11a2f`, `SMS_MAG_GENERIC_SUBJ_1` `0xd8d26325` / `_2` `0xd8d26326` "%s cover opportunity", `SMS_MAG_GENERIC_BODY_1` `0xb0bf75ff` / `_2` `0xb0bf7600` — all OK. One caveat on the pairing: `src/fe_sms.c:81` keys the `_1`/`_2` choice off the record `flag` byte, and among those 28 the flag splits 14/14, not the 18/10 the comment at `src/fe_sms.c:75-79` states (and its "18/10" and "28" do not add up either — `SMS-TABLE.md` also has the flag count for the whole table, 31 of 167, so 18/10 is not that number restricted to the blurbs). The prose is wrong, the code is not — both keys resolve, so no text is at risk either way. Same comment: `SMS_SHOWCASE_GENERIC_SUBJ_1` / `_2` do **not** exist in the table (they hash to `0xb951362d` / `0xb951362e`, absent), so that clause is wrong too |
| `src/postrace.c:240` | `gv_text(&p->assets, labels[i], fallback[i])` | the `labels[]` array of `src/postrace.c:238` | see `src/postrace.c:238` rows in section 1 |

`src/career_host.c` and `src/rear_mirror.c` hold no language key: every 32-bit value in
`career_host.c` is a career-record hash (`shop_hash`, `zone_hash`, `name_hash`, `locator_hash`) resolved
through `cd_find_*`, and `rear_mirror.c` is shader and RHI code. Both read as zero rows, which is a
result, not a gap. `src/fe_worldmap.h` likewise holds none; its `FeMapIcon.label_key` field is filled by
`wm_snapshot`, covered above.

## 4. Constants that look like keys and are not [data]

These are the hex literals in scope that a string-table lookup would wrongly condemn. Each is named by
the source and cross-checked below; `English.bin` was also searched for the raw little-endian bytes of
every one of them, so "absent" is not just "absent as a hash".

| path:line | constant | namespace | evidence |
| --- | --- | --- | --- |
| `src/fe_sms.c:181,185,186,187` | `FES_N_HEADER` `0x42adb44c`, `FES_COUNT` `0x980822b3`, `FES_ROW_TOP` `0x9a3674c8`, `FES_ROW` `0x0c6e4198` | FNG node name hash | matched against `obj->name_hash` (`src/fe_sms.c:209`, `:218`, `:230`, `:299`); `tools/fng_dump.py --package UI_SMS_Mailbox.fng` lists the node ids — 98 / 113 / 13, and `FES_ROW` is the bottom row (the dump's `--text-only` row says "nodes 14–21 and 58", but only node 58's id equals the constant; nodes 14-21 step up one per row, `0x0c6e4199`-`0x0c6e41a0`, which is why the loader matches `FES_ROW_TOP` plus `FES_ROW` and then requires exactly ten hits) |
| `src/fe_sms.c:188-190` | `FES_MAIL_TOP` `0xe7afbca7`, `FES_MAIL_LO`…`HI` `0x6bde8997`-`0x6bde899f` | FNG node name hash | the ten `Engage_SMS_mail.tga` envelope nodes (2 and 3-11) of the same dump; matched as a top node plus a closed id range (`src/fe_sms.c:219`) |
| `src/fe_sms.c:182-184` | `FES_RETURN` `0x001c0cbb`, `FES_DELETE` `0x56b00632`, `FES_SELECT_CAP` `0x001335f0` | FNG node name hash | nodes 109 / 111 / 140 (`tools/fng_dump.py --package UI_SMS_Mailbox.fng`). All three collide with real language keys (`0x001c0cbb` = SMS_RETURN "Return to Gameplay", `0x001335f0` = BACK "Back", `0x56b00632` = "Delete" — the last was first recorded as hashing to nothing, which is wrong), which is why the comments matter — see 5.2 |
| `src/gui_dialog.c:20-21`, `gd_frames[15]` | `KEY_PKG_IN_GAME` `0xaa469286`, `KEY_PKG_SMALL` `0x6962c0cd`, the 15 `package_key` values | FNG package name hash | `tools/gui_art_test.c:641` asserts `hash_name(f->package) == f->package_key` for every frame row |
| `src/gui_dialog.c:23-24`, `:169`, `:200-208` | `MSG_CANCEL` `0xb4623f67`, `MSG_DEFAULTS_NO` `0x34dc1bcf`, `result_msg[]`, `back_msg`, the `ok` ids `0xe1a57d51` / `0x451e768e` / `0x43da9fd0` | FE message id | the file's own line 22 says "not language keys; absent from English.bin by design"; `tools/gui_art_test.c:327-329` compares them against the JSON `message` field, never a caption |
| `src/main.c:6453`, `src/world_resident.c:256` | `TRN_GRASSC` `0x5671b2b5` | texture name hash | see 5.1 |
| `src/postrace.c:238` | `icons[]` `0xb295a6b6`, `0x62aa0df6`, `0x4c9e34e6` | texture name hash | passed to `gv_texture`, not `gv_text`, on the same line |

## 5. Fixes for the orchestrator

> **Status after Task AN (`fix/lang-audit-fixes`) — every row below is closed.** 5.1 / 5.6, 5.2, 5.2b,
> 5.3, 5.4 and 5.5 are all applied; `python3 -I tools/lang_key_audit.py --data "$OPENUG2_DATA" --src src`
> now exits **0** (67 rows, 67 OK), so a non-zero exit from that command is a regression from here on.
> Two details of this section were themselves wrong and are corrected in place below: the 5.1 / 5.6 fix
> recipe (`5.1`) and the claim that `0x56b00632` hashes to nothing (`5.2`). The `postrace.c:65` comment
> of 5.4 was left to that file's owner. Sections 1-4 keep their pre-fix line numbers, which drifted
> again after the merges that followed this audit; the code is findable by content.
>
> Nothing in section 1 is MISSING, so no in-scope caption can come up blank. These are the real defects
> the audit found, in the order I would take them. I did not edit any `src/` file.

One-line summary, details in the numbered subsections:

- **5.1 / 5.6** comment shape: 13 tree-wide non-OK rows — 1 in scope plus the 12 of 5.6 — where a bare
  `/* NAME */` makes a texture, node or material id read as a language key; the fix is one word in the
  comment, and then the tool can exit 0.
- **5.2** comment shape: three `FES_*` constants are node ids whose comments imply they are caption keys.
- **5.2b** parity choice: two map captions use screen-title keys where a `MINIMAP_*` twin exists.
- **5.3** the one code-side gap: no gate checks `CAR_NAME_<model>` across the whole car catalogue.
- **5.4** three fallback literals differ from retail's text (harmless while the keys resolve).
- **5.5** two `[data]` comment claims that the data contradicts.

### 5.1 `0x5671b2b5` is a texture name, not a language key — fix the comment, not the key

`src/main.c:6534` (was `:6453`) and `src/world_resident.c:256` carry `/* TRN_GRASSC */`, which is why
`lang_key_audit.py` reports it as MISSING: its labelled-literal rule trusts a comment that is exactly
one label word, and `TRN_GRASSC` hashes to `0x5671b2b5` but is not in the string table. The value is a
`TRN_` model/texture name.

**Applied fix (Task AN), and a correction to the recipe first proposed here.** A leading lowercase word
does *not* work: in `scan_lines` the `agreed` test (some comment word hashes to the literal) runs before
the one-word test, so `/* texture TRN_GRASSC */` still produces the MISSING row — the wording only helps
the three truncated LABEL-MISMATCH rows of 5.6. `tools/lang_key_audit_test.py` now pins both halves of
that (`test_unquoted_single_name_does_claim_the_literal`,
`test_quoted_name_never_claims_a_literal`). What does work is the tool's own documented escape: quoted
spans are dropped before any word is a candidate, so `/* texture "TRN_GRASSC" */` keeps the name verbatim
and produces neither row nor note. All 13 sites are quoted that way. `ug_hash_upper("TRN_GRASSC")` was
not used: at `main.c` that line is outside the caption lines the fixer was allowed to touch, and
`world_resident.c` does not include `ugui_core.h` (adding it would be a code change). No behaviour
change; the tree-wide false finding is gone and the tool can exit 0.

### 5.2 The three SMS button hints are already correct by retail's own mechanism — fix the comments

`SMS-MAILBOX.md` 3 calls the hint keys the label keys of nodes 109 / 111 / 140, and it is right, but the
keys are not the `FES_*` constants: each text node carries its own "SH" label hash, which is what
retail substitutes at draw time and what our reader already applies
(`src/ugui/ugui_fng.c:186` stores it, `src/ugui/ugui_scene.c:575-577` looks it up when no
`text_override` is set — the same rule `tools/fng_dump.py:label_of` reports):

| node | name hash (`FES_*`) | retail SH label key | text |
| --- | --- | --- | --- |
| 109 | `0x001c0cbb` | `0xc20a08f1` = `ug_hash_upper("SMS_RETURN")` | "Return to Gameplay" |
| 111 | `0x56b00632` | `0x0836f968` = `ug_hash_upper("SMS_DELETE_MESSAGE")` | "Delete Message" |
| 140 | `0x001335f0` | `0x3b550c71` = `ug_hash_upper("SMS_READ_MESSAGE")` | "Read Message" |

So no caption fix is needed — the scene draws retail's text without any code change. What is wrong is
the comment shape: `#define FES_SELECT_CAP 0x001335f0u /* node 140: hint "Read Message" (key
SMS_READ_MESSAGE) */` puts a node id and a label in one comment, and `0x001335f0` also happens to be
the language key BACK ("Back") while `0x56b00632` reads "Delete" — **all three node ids collide with a
real language key**, not two of them as this section first said (checked again against `English.bin`:
`0x001c0cbb` = SMS_RETURN's node name, `0x001335f0` = BACK "Back", `0x56b00632` = "Delete"). All three
therefore look like the caption key and are not, and `lang_key_audit.py` correctly refuses to guess (all
three land in its "ambiguous comment label" list).
Fix (applied by Task AN): `/* node 140 name; caption SMS_READ_MESSAGE 0x3b550c71 "Read Message" */` for
each of the three — the node id keeps its name, the caption gets its real key, and the audit stops
having to explain the collision. The block comment above them now says outright that every `FES_*` value
is a node name hash and never a caption key.

The same two unkeyed retail labels are worth naming in one line, because the screen does depend on them
falling through to the authored sample: node 13 / 58's row label is `0xd530af89` and node 113's count
label is `0x9588bd38` (both read from the dump's `label_hash` column), and `English.bin` has no entry for
either — `SMS-MAILBOX.md` 4 records the same, plus the `$JOY_EVENT_FENG_*` tokens on the button-hint
nodes. That is why `fe_sms_sync` writes `text_override` for the row and count nodes instead of relying on
the label.

### 5.2b The map has two parallel caption families; `wm_snapshot` mixes them [data]

`wm_snapshot` feeds icon captions from two keys that are screen titles rather than map captions:
`src/main.c:1678` `0x7edc9715` ("Car Lot", also the car-lot screen title at `fe_lot.c:101`) and
`src/main.c:1679` `0x7ce21662` ("Garage"). Both resolve, so nothing is broken. What the audit found is
that retail keeps a complete second set of caption keys for exactly these icons, and the file uses the
`MINIMAP_` spelling for three shop kinds while using the title keys for the other two:

| `main.c` today | text today | retail `MINIMAP_` twin | text there |
| --- | --- | --- | --- |
| `:1678` `0x7edc9715` | "Car Lot" | `MINIMAP_CARLOT` `0x93ff4a6e` | "Car Lot" |
| `:1679` `0x7ce21662` | "Garage" | `MINIMAP_GARAGE` `0x9d53bd10` | "Garage" |
| `:1682` `ug_hash_upper("MINIMAP_BODY")` | "Body Shop" | — already the twin | |
| `:1682` `ug_hash_upper("MINIMAP_GRAPHICS")` | "Graphics Shop" | — already the twin | |
| `:1682` `ug_hash_upper("MINIMAP_PERFORMANCE")` | "Performance Shop" | — already the twin | |
| `:1682` `0xa4e5ee6c` | "Car Specialties Shop" | no twin exists (`MINIMAP_SHOP` `0x40d12ca3`, `MINIMAP_CRIB` `0x40c89049`, `MINIMAP_CAR` `0xb46328ff`, `MINIMAP_SPECIALTY` `0xd1e8b1b7` all absent) | |

The family is bigger than the shop icons, and that is where the caution below matters — retail also ships
`MINIMAP_CIRCUIT` `0x268d063c` "Circuit", `MINIMAP_SPRINT` `0xba613729` "Sprint", `MINIMAP_DRAG`
`0x40c91ba7` "Drag", `MINIMAP_DRIFT` `0x59ecb2c2` "Drift", `MINIMAP_STREETX` `0x0fda6758` "Street X" and
`MINIMAP_URL` `0xb46377bc` "**U.R.L.**", while `wm_event_label` (`src/main.c:1652-1656`) uses the
`QR_*` family: the first five carry the same text as their `MINIMAP_` twin, but `QR_URL` `0x75f903f4`
reads "**URL Race**".

Fix (orchestrator's call — a parity choice, not a bug): switch the two ternaries to
`ug_hash_upper("MINIMAP_CARLOT")` / `ug_hash_upper("MINIMAP_GARAGE")`, which makes all five shop kinds
that *have* a map-caption twin use it, and leaves `0xa4e5ee6c` alone because no twin exists. Do **not**
extend that to the event captions on the theory that one family is tidier: the six `QR_*` keys in
`wm_event_label` are text-identical to their twins except URL, and retargeting that one changes the drawn
caption from "URL Race" to "U.R.L." — retail's own choice between the two is unknown to this audit, so
that line stays as it is until something shows what the retail map draws.

**Applied by Task AN, exactly as scoped**: the two ternary arms in `wm_snapshot` now read
`ug_hash_upper("MINIMAP_CARLOT")` / `ug_hash_upper("MINIMAP_GARAGE")`, `0xa4e5ee6c` and `wm_event_label`
are untouched. Worth stating plainly for anyone re-checking this with a screenshot: because both twins
carry the *same* text as the keys they replace ("Car Lot", "Garage"), the drawn caption cannot change —
only the key identity does. A world-map PNG therefore confirms the caption still renders, but it is not
evidence of the switch; the evidence is the key values and their table lookups. `tools/fe_screens_check.c`
builds its own diagnostic icons and still names `CAREER_CAR_LOT_TITLE` / `0x7ce21662u` for the two shop
markers; that is a control path that constructs its own labels, not the map's, and stays valid.

### 5.3 The car-lot display name has no catalogue-wide check in any gate

`src/fe_lot.c:96` builds `CAR_NAME_<model>` at run time, so no static scan can judge it; the key is also
a **second reader** of the catalogue name, and its fallback path is worth knowing exactly:
`gv_text(assets, key, name)` returns the raw catalogue name when the table lacks the key, and when
`assets->have_strings` is 0 it does the same even for a key the table does hold
(`src/gui_view.c:35-38`) — so the screen silently degrades to "PEUGOT" instead of "206" in two different
situations. Every key this path is seen to use resolves (section 3).
Fix: a host check that walks the installed-car catalogue and asserts every `CAR_NAME_<model>` is
present — the natural home is `tools/fe_lot_test.c` next to the other data-root checks, since it already
names those four cars. One line per car, and it turns a silent caption downgrade into a gate failure.

**Applied by Task AN.** The reachable set is narrower than `CARS/`, and getting that wrong is the trap:
44 directories hold a `GEOMETRY.BIN`, but 15 of them are traffic bodies with no `CAR_NAME_*` key at all
(`4DR_SEDAN`, `AMBULANCE`, `BUS`, `TAXI02`, `SUV`, `PICKUP`, …), so "every car in the catalogue" read
literally fails on a correct tree. The lot only ever sees what `career_host.c:inst_list` installs — the
catalogue's non-traffic cars — and `game_catalog.c` defines traffic as an empty or absent
`CARS/<model>/TEXTURES.BIN`. The check mirrors that rule rather than guessing from names, which matters
because the eye gets both directions wrong (`FIRETRUCK` is a player car, `COUPE` is traffic). On this
data: 29 installed cars, 0 missing. An empty candidate list, an unloadable table and an unset
`OPENUG2_DATA` are a fail or a 77 skip, never a pass; verified negatively by deleting one entry's hash
from a copy of `English.bin`, which reports `CAR_NAME_MIATA` and exits 1. `src/resource.c` was added to
the `fe-lot-test` rule for `res_list_cars`.

### 5.4 Fallback texts that differ from retail

**Applied by Task AN, and the count here is wrong: one code change was in scope, not three.** The
`fe_sms.c` pair was only listed as checked, and the third bullet is a comment, not a literal.

- `src/fe_lot.c:100` (key `0x272ebeba`): fallback "You have to select a new car before you leave."
  vs retail "Rachel needs her car back. You have to select a new car before you leave." — **applied**;
  the caption buffer is `sizeof`-sized so the longer text is safe, and no test pinned the short one.
- `src/fe_sms.c:264` / `:272` / `:279`: fallbacks "Message %d of %d", "Incoming Mobile Message",
  "%d unread messages" match retail verbatim (re-checked against the table); listed because the audit
  checked them, no change needed.
- `src/postrace.c:65` comment still reads "(no PLAYER key in the table)" while the same line passes the
  label `LABEL_PLAYER` `0xfa37740b`, which does resolve to "Player". The comment is stale — the key was
  found; drop the parenthetical. **Not touched here**: `src/postrace.*` belongs to another agent on this
  branch, so this row is reported, not fixed.

### 5.5 Comment claims the data does not support

- `src/fe_sms.c:75-79` is tagged `[data]` but two of its claims are wrong: the `flag` byte "splits the
  blurbs 18/10" (among the 28 blurbs that reach it, the flag splits 14/14), and "SMS_SHOWCASE_GENERIC_\*
  carry the same "%s cover opportunity" text" (no `SMS_SHOWCASE_GENERIC_*` key is in the table; the two
  obvious spellings hash to `0xb951362d` / `0xb951362e` and neither is present). The code path is
  unaffected — the four `SMS_MAG_GENERIC_*` keys it actually reads all resolve — so this is a comment
  fix: put the real 14/14 count in, keep the pairing `[inferred]` (retail's rule is genuinely unknown),
  and drop the SHOWCASE clause. **Applied by Task AN**, both counts re-derived from
  `career_dump.py --section sms` against `English.bin`: 38 records carry the magazine kind, 10 of them
  have their own text and never reach the branch, and the remaining 28 split 14/14.

### 5.6 The other 12 non-OK rows in the tree, all outside the named files [data]

A whole-tree scan (`--src src`) exits 1 on 13 non-OK rows; only the `main.c:6453` one is in scope (5.1).
All 13 are the labelled-literal rule trusting a comment that names something other than a language key —
every one of those comments was checked against the source by hand, and none is a caption a screen can
show blank. The list, so the orchestrator can close them in one pass:

| Row | Comment label | What the value really is |
| --- | --- | --- |
| `src/world_resident.c:256` | `TRN_GRASSC` | the same texture name as 5.1, second copy |
| `src/pause_menu.c:332` | `OPTIONS_ICON_CONTROLS` | first entry of the `textures[]` array two lines below the `texts[]` array, and the array is checked with `gv_texture`, not `ug_strtab_get` (`src/pause_menu.c:337-339`) |
| `src/car_mod.h:140` | `TRAFFIC` | a `car_material` id compared against `m->car_material` |
| `src/car_mod.h:161` | `REAR_LICENSE_PLATE` | a socket id passed to `n2_car_socket_frame` |
| `src/gui_view.c:16` | `U2_MENU_ARROW_WHITE` | `H_ARROW_TEX`, a texture name |
| `src/gui_view.c:17` | `Header_Text` | `H_HEADER`, an FNG node name (the comment lists its three spellings) |
| `src/nfsu2.h:992`-`:994` | `TRN_COASTROADLOD_A_DM`, `TRN_TREELINEA_DM`, `TRN_FREEWAYLOD_A_DM` | impostor atlas texture names; the block comment above them says the hashes are "verified on all eight" bundles |
| `src/nfsu2.h:995`-`:997` (LABEL-MISMATCH) | `TRN_TREES_FENCES_LOD_A_`, `ARC_PANARAMABUILDINGSB_`, `ARC_PANARAMABUILDINGSC_` | the same array, with the 21-character truncation visible in the comment itself, which is why the label hash disagrees |

Fix for all 13 is the 5.1 one-liner — put a word the rule cannot read as a label in front of the name
(`/* texture TRN_GRASSC */`) or write `ug_hash_upper("…")` — and the tree then exits 0, which is what
lets a later gate treat a non-zero exit as a real regression.

**Applied by Task AN, with the recipe corrected by 5.1**: a leading word is not enough, so every one of
these 12 names is quoted (`/* texture "TRN_GRASSC" */`, `/* material "TRAFFIC" */`,
`/* socket "REAR_LICENSE_PLATE" */`, `/* node name "Header_Text" … */`, and the six `nfsu2.h` atlas names
under a `texture names` header comment). All 13 rows are gone and `--src src` exits 0.

## 6. Checks

- `python3 -I tools/lang_key_audit_test.py` — 42 tests, OK. Seven are new: the field-assignment and
  designated-initialiser forms, a `Text`-struct table, and four negative cases (a package-key table
  row, node-id and FE-message `#define`s, other `_key` namespaces, a key named by a constant is
  reported unresolved rather than guessed). Task AN adds three more (45 total): a quoted name never
  claims a literal, the same name unquoted does, and a second upper-case word demotes the claim to the
  informational note.
- `python3 -I tools/lang_key_audit.py --data "$OPENUG2_DATA" --src src` — exits 1 by design while the
  out-of-scope labelled-literal findings of 5.1 / 5.6 stand: 75 rows in the whole tree
  (27 accessor calls, 27 key-field rows, 21 labelled literals), 62 OK / 10 MISSING / 3 LABEL-MISMATCH,
  50 unresolved key arguments and 18 ambiguous comment labels.
  **After Task AN: exit 0, 67 rows, 67 OK / 0 MISSING / 0 LABEL-MISMATCH** (the 13 labelled literals
  stopped being rows; nothing else moved). A non-zero exit from this command is now a regression.
- The same tool on the scoped copy of the named files: 48 rows, 47 OK and the 1 MISSING of 5.1.
  **After Task AN: exit 0, 47 rows, 47 OK / 0 MISSING / 0 skipped** (`main.c` shrank a row because
  5.2b's QR-caption row was itself the fix target and the car-lot keys are not accessor calls).
- `tools/fe_lot_test.c` retail block — 29 installed cars, 0 missing `CAR_NAME_<model>`; exits 77 when
  `OPENUG2_DATA` is unset and 1 when the table lacks a key (verified with an entry hash deleted).
- `git grep -n` for the home-directory prefix over this branch's files — empty, including the check
  line itself once committed.
- `tools/fng_dump.py --data "$OPENUG2_DATA" --package UI_SMS_Mailbox.fng [--json OUT.json]` — the node
  ids, `label_hash` columns and samples behind section 4 and 5.2 (`--package` needs `--data`; the JSON
  rows carry `name_hash`, `label_hash` and `label_text` per node).
- `tools/career_dump.py --data … --section sms` — the record-name side of the `fe_sms.c` runtime keys.
