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
