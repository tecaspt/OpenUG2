# SMS mailbox screen (Task AA)

`src/fe_sms.{c,h}` + `tools/fe_sms_test.c`. The retail career delivers messages to an SMS mailbox
(`UI_SMS_Mailbox.fng`); this module lists the received ones and shows a single message. No host
wiring lives here — `src/main.c` / `src/fe_menu.c` own that, and this task deliberately does not
touch them.

Everything about the message *records* (the 0x34A17 table, its 167 × 20-byte rows, the language-key
law, the four kinds) is `SMS-TABLE.md` (Task V); this file only covers the screen. Tags: `[data]` =
read out of the game DATA or the OpenUG2 sources; `[inferred]` = our choice.

## 1. Where the package lives [data]

`UI_SMS_Mailbox.fng` is not in the frontend `BUSY_SIGN`-style packages: it sits in
`GLOBAL/InGameCommon.lzc` (chunk `0x13ef00`), 142 nodes. `ug_assets.c` only opens that container
through `ug_assets_open_ingame()`, so a host must call it before `fe_sms_bind` — the frontend-only
asset path will report the package as missing. `fe_sms_bind` reports the failure in `GvScreen.status`
instead of drawing a broken screen.

## 2. Language lookup: the `LangTable` adaptation [data]

The brief asks for `const LangTable *lang`. There is no `LangTable` type in this code base: the
language lookup is `UgStrTab` (`src/ugui/ugui_core.h` — `ug_strtab_get`, `ug_strtab_or`,
`ug_strtab_load`, reached in screen code as `UgAssets.strings` and the `gv_text(a, key, fallback)`
helper). So `fe_sms.h` declares

    #define LangTable UgStrTab

which keeps the brief's signature readable while using the real type; `const LangTable *` is then
exactly `const UgStrTab *`. Keys are looked up with `ug_hash_upper`, the same hash the language files
use, and the message keys are `NAME + "_SUBJECT"` / `NAME + "_BODY"` (`SMS-TABLE.md` 4).

## 3. Nodes used, by node name hash [data]

The FNG reader (`src/ugui/ugui_fng.c`) stores a node's identity as `UgFngObject.name_hash` only — the
authored names are not recoverable, and no readable candidate name (`SMS_ROW_1`, `MAIL_01`, …) hashes
to any node here, so the binding matches name-hash constants inside `fe_sms.c`. `fe_binding.h` is not
owned by this task, which is the second reason the constants stay local. Node indices below are the
`fng_dump` order of the 142-node package.

| Constant | Name hash | Node(s) | Role, geometry, authored sample |
| --- | --- | --- | --- |
| `FES_N_HEADER` | `0x42adb44c` | 98 | Title text, x=275 y=−46, sample `Incoming SMS Message`. Overwritten with the header line. |
| `FES_COUNT` | `0x980822b3` | 113 | Centred count line, x=44 y=8, sample `Unread Messages`. Overwritten with the filled `SMS_MSG_NUM_UNREAD`. |
| `FES_ROW_TOP` | `0x9a3674c8` | 13 | Top subject row, x=−242 y=155. |
| `FES_ROW` | `0x0c6e4198` | 14–21, 58 | The nine other subject rows. Ten rows in total → `FE_SMS_WINDOW 10`. |
| `FES_MAIL_TOP` | `0xe7afbca7` | 2 | `Engage_SMS_mail.tga` envelope at y=165 (top row). |
| `FES_MAIL_LO`…`FES_MAIL_HI` | `0x6bde8997`–`0x6bde899f` | 3–11 | The nine other envelopes, y=136…−88; the id steps by one per row, so the closed range is exactly those nine. |
| `FES_RETURN` | `0x001c0cbb` | 109 | Button hint `Return to Gameplay`; its label key is `SMS_RETURN`. Left as authored. |
| `FES_SELECT_CAP` | `0x001335f0` | 140 | Button hint `Read Message` (`SMS_READ_MESSAGE`). Kept, because Accept really does open the message. |
| `FES_DELETE` | `0x56b00632` | 111 | Button hint `Delete Message` (`SMS_DELETE_MESSAGE`). Hidden — see 6. |

`fes_rows()` accepts the package only when it finds exactly ten subject rows and ten envelopes, and
sorts both groups by `obj->pos[1]` descending (top row first); anything else is a bind failure with a
status string, so a changed package can never half-draw. `fe_sms_bind` then checks the five single
role nodes the same way.

Text goes through the same GUI path as `fe_lot.c`: `gv_screen_load`, `ug_scene_app_write` +
`ug_scene_relayout`, and the scene rule that a node's `text_override` beats its `label_hash` lookup,
which beats the authored sample. Unimplemented nodes use `app_hidden` rather than showing example
values.

## 4. Which labels actually resolve [data]

`English.bin` (chunk `0x00039000`, 3925 keys) resolves the header, count and hint keys:
`SMS_HEADER` `0xaaabbd7a` = "Incoming Mobile Message", `SMS_MESSAGE_HEADER` `0xcc438b1e` =
"Message %d of %d" (variants `0xce04c2fc` / `0xc9c5976e` = "Unread / Stored Messages %d of %d"),
`SMS_MSG_NUM_UNREAD` `0x2ccba545` = "%d unread messages", `SMS_RETURN`, `SMS_READ_MESSAGE`,
`SMS_DELETE_MESSAGE`. The `%d` fields are filled by `fe_sms_sync` (`sel + 1` of `n`, and the unread
count).

Not resolvable, and therefore left at the authored sample or unused: the row label `0xd530af89` and
count label `0x9588bd38` are in neither `English.bin` nor `Labels.bin`, and the button hints carry
`$JOY_EVENT_FENG_*` controller tokens that only the input layer substitutes (out of scope here).
Because both are unkeyed, `fe_sms_sync` never sets a `label_hash` on those nodes — it only writes the
text it can name.

## 5. Item sizes and the text rules [data]/[inferred]

`FeSmsItem` is the contract's `{ name[40], subject[96], body[512], unread }`. Measured against the
retail language file: the longest subject is 38 bytes, and only two bodies exceed 512 —
`SMS_INSTRUCTION_BODY` (931) and `SMS_INSTRUCTION_DEMO_BODY` (569). `[inferred]` those two are
truncated at 512 rather than the contract being broken; `fe_sms_test` checks a 511-byte body survives
intact and a longer one is cut at the buffer end, never run past.

Text resolution order per record, all `[data]` unless marked:

1. `NAME_SUBJECT` / `NAME_BODY` — 123 of the 167 records have both (`SMS-TABLE.md` 4).
2. `SMS_CAR_UNLOCK_2`…`_17` reuse the `SMS_CAR_UNLOCK_1_*` pair (the retail table has one text for the
   whole unlock family).
3. `kind == CD_SMS_MAG` records without their own keys use the generic pair
   `SMS_MAG_GENERIC_SUBJ_1`/`_2` and `SMS_MAG_GENERIC_BODY_1`/`_2`, whose `%s` is filled with the
   magazine word taken out of the record name (`SMS_MAG_<mag>_SHOW_<n>` etc.). `[inferred]` the `_1`
   / `_2` pairing follows the record's `flag` (the 28 blurbs split 18 flagged / 10 unflagged); the
   retail binary was not disassembled for this, and the result is checked only for "text present".
4. Nothing found: the row keeps its name as its subject and an empty body, and `text_missing` counts
   it, so a caller can tell an untranslated record from an empty mailbox. The 44 textless records of
   `SMS-TABLE.md` 4 land here.

`unread` starts at 1 for every built row and `unread_total` counts them. `[inferred]` read state is
not persisted by this module: the career save owns it, and a host that knows which messages are read
should clear the flags on the items it hands to `fe_sms_open_list`.

## 6. What stays [inferred]

- **List order.** Rows follow the SMS table order. `received[]` selects which records are in the
  mailbox but does not reorder them; unknown indices are dropped, so the returned count can be less
  than `nreceived`. `[data]` the table is authored in delivery order (`SMS-TABLE.md`), which is why no
  separate sort exists.
- **Initial selection.** `fe_sms_open_list` lands on the last unread row (the newest message), else
  row 0. Retail behaviour was not observed on console.
- **Scroll window.** With more than ten messages the window starts at `sel − 5`, clamped so ten rows
  stay filled. Retail's exact scroll rule is unknown.
- **Unread marker.** The row envelope icon (`Engage_SMS_mail.tga`) is shown only on unread rows and
  hidden for read rows and in the opened-message view. The package authors one envelope per row and no
  separate unread glyph, so the envelope is the only available marker.
- **Delete hint hidden.** Deleting is not implemented, so node `0x56b00632` is hidden rather than
  offering an action that does nothing; the read/select caption stays because Accept really opens.
- **Header scaling.** A header longer than 562 units (the `U2_Menu_Box.tga` list-window width, node
  `0x22561fee`) is scaled down instead of clipped, mirroring `fel_sync` in `fe_lot.c`.
- **Sender line.** `FeSms.sender` stays empty: the SMS table has no sender field and no sender key
  resolves; keeping the field lets a host fill it if a later dump finds one.
- **Body layout.** The opened message reuses the header/count nodes only; the package has no separate
  body text node, so drawing the body is host-side work (the `fe_lot.c` dialog path is the model).

## 7. Contract deviations

Only the `LangTable` → `UgStrTab` naming of 2. `fe_sms_build` keeps the brief's signature; `FE_SMS_ALL
(-1)` in `nreceived` means "every record of the table", which is how a harness (and a host without a
save file) builds a full list. `fe_sms_select` wraps both ways and ignores `delta == 0` and
`|delta| >= n`; `fe_sms_open` returns −1 on an empty mailbox and is idempotent on a re-open.

## 8. Checks

`make fe-sms-test` runs 36 synthetic checks (empty list, empty language table, missing text, the
magazine and unlock fall-backs, truncation, NULL/zero arguments, out-of-range receipt indices, the
`FE_SMS_MAX` cap, unread→read, re-open, wrap both directions, a full turn, single row, `fe_sms_sync`
with no screen) plus the retail check, which needs `$OPENUG2_DATA` and skips with exit 77 and an
explicit message when unset — a skip is not a pass. The retail check builds five real names
(`SMS_UNLOCK_01`, `SMS_CAR_UNLOCK_1`, `SMS_LOC_TEXT_COAL_HARBOR`, `ENGAGE_TIPS_SMS` and
`SMS_END_OF_GAME_MESSAGE`), prints their subject lines, and requires text for all five, the
newest-unread start, wrap-around and the unread→read transition. The
target links no GL: `fe_sms_test` never draws a package.

## 9. Career host

This section supersedes the standalone module's table-order, 40-row and sender
assumptions above. The host uses receipt order and holds up to `CD_MAX_SMS` (192)
records, including all 167 retail records. Unknown table hashes remain in the
save and are omitted from the visible list until their data is available.

### State and migration

Schema 5 adds TLV 8: `u16 count, u8 initialized, u8 reserved`, followed by
`count` entries `{u32 name_hash, u8 read, u8 reserved[3]}`. Receipt hashes are
nonzero and unique, flags are Boolean, and lengths/counts are checked before
writing the destination state. Existing unknown TLVs still round trip. Name
hashes keep identity independent of table order. The initialized byte preserves
an explicitly empty mailbox and a pending legacy migration across serialization.

The save parser needs no CareerData. `ch_begin` migrates an older save after
loading that data, adds every message implied by the delivery rules below as
received and read, then saves. New careers start initialized and receive live
messages unread. `ch_sms_tick` evaluates delivery only in career roam and saves
new receipts before logging `career: sms received <NAME>`. `ch_sms_read` saves
before the menu marks an item read; failure rolls the career state back.

### Delivery policy

- [data] Part notices carry stage/count pairs. [inferred] Trigger 12 maps to
  ordinary wins, 10 to sponsor wins and 9 to URL wins. A notice requires an exact
  type/value match in `0x34A1C`, then calls `cr_unlock_ok` for that entry/level.
  `SMS_UNLOCK_27` has no such match and remains undelivered.
- [data] Cars unlock at the regional race references in `0x34A1F`; these are
  `S<stage>_URL_<number>` races. [inferred] A notice's ordinal enumerates the
  distinct regional unlock races in ascending numeric URL order within its
  stage. Several cars sharing one event produce one notice. Delivery requires
  that exact event to be won, with no additional sponsor-win threshold. This
  also handles stage 3, where unlock groups begin at URL 4 rather than URL 1.
- [inferred] The four control tutorials (trigger 3) use their ascending arguments
  10/40/70/100 as seconds of active roam in the current session. Pausing stops
  this clock. Legacy migration marks all four read. Receipts prevent redelivery
  across sessions; the clock itself is not persisted.
- [inferred] GPS introduction and the garage direction arrive after buying a car;
  the car-lot direction arrives while no car is owned; the outrun introduction
  arrives at stage 1. The events direction arrives on visiting an open crib
  after stage 0. The raw `CRIB_1` reference is not the career shop's `CC_CRIB`
  identity, so shop type is used for this discovery. Older saves at stage 1 or
  later receive that direction read without requiring another crib visit.
- [inferred] Tips unlock in table order: the first at stage 1, then one per
  lifetime win. Their self hashes and weight fields give no deterministic
  timing rule. The weight is not interpreted as a timer or probability.
- [data] The end message's race reference is checked against the won records.
- Undelivered: the four `SMS_LOC_TEXT_*` district notices (no district discovery
  state or decoded district polygons); `SMS_LOC_X_SPONSOR` (sponsor selection is
  absent); `SMS_OUTRUN_VICTORY` / `SMS_OUTRUN_DEFEAT` (no career outcome hook in
  this scope); all `SMS_MAG_*`, `SMS_UNIQUE_*`, and `SMS_DVD_COVER_*` records
  (their opportunity/cover conditions are unsupported); and the unmatched part
  notice above. No progress threshold is substituted for geographic discovery.

### Retail presentation and controls

[data] `GLOBAL/InGameFreeRoam.bun` / `HUD_SingleRace.fng` authors root image
`0x4b9b8f5a`, `Engage_SMS_mail.tga`, at (-195,-112), 64 by 32. The existing HUD
binding renders this envelope and the right d-pad highlight when the career has
unread messages. The host now supplies its real unread state through
`HudState.message_waiting`; there is no additional notice text or banner.

[data] `GLOBAL/InGameCommon.lzc` / `UI_EngageMessageDialog.fng` authors the
message panel, subject (`0x2c167533`), sender (`0xfeced617`), and the actions
`Go to Mailboxes` (`0xe202f7c4`) and `Return to Gameplay` (`0x00169268`). This
is the in-game mailbox route used here; the pause package does not expose an
SMS entry. The list binds `UI_SMS_Mailbox.fng`. Opening a row uses the authored
message panel and its actions. Subject and sender come from the message's
language keys, including its actual sender key. Placeholder and event-handler
text nodes are hidden. The host draws wrapped body text inside the authored
panel using the retail font, shrinking only when needed to fit. It resolves the
full language body, so the standalone item's 511-byte limit does not truncate
long tutorials in the game.

[inferred] Input adaptations:

- The existing configurable Read SMS action opens the list in career roam;
  its default keyboard bindings are Tab and F3. Pressing the controller right stick also
  opens it. This shortcut avoids taking d-pad right away from its existing
  steering binding; a configured controller Read SMS binding also works.
- Tapping/clicking the unread envelope opens the same list. The hit area follows
  the HUD's selected 4:3/wide fit policy, with a small touch margin.
- Arrows/d-pad and mouse wheel browse; Enter/controller A reads. Touch selects
  and reads the row at its bound scene position. The selected row is amber.
- Escape/controller B or the touch Back target returns from a message to the
  inbox, then to roam. Accept on a message returns directly to gameplay. The
  message's left/right action areas offer the same choices on touch.
- An empty list remains navigable back to gameplay. Reading does not run any
  garage or shop exit side effects. Help and three-button dialogs are unchanged.

The menu owns a separate lazily loaded in-game asset set, so opening messages
preserves the frontend's package and texture identities. All writes remain
inside the current profile's career save.

The live retail check corrected two assumptions in the original binding:
subject nodes 14–21 have hashes `0x0c6e41a0` down to `0x0c6e4199`, and node 58
has `0x0c6e4198`. They form a closed range, not a repeated single hash. Scene Y
increases downward, so subjects and envelopes are sorted by ascending authored
Y. The retail unit check now loads and binds the actual package, exercises the
opened/list visibility transition, and requires one populated row per receipt.
