#!/usr/bin/env python3
"""Inventory every retail .fng layout package against what OpenUG2 actually loads.

Retail ships a few hundred FNG layout packages (front-end screens, dialogs, HUDs). OpenUG2
implements some of them. This tool answers, per package: where it lives, how big it is, what it
shows, whether OpenUG2 references it, which class of screen it is, and which milestone would own
it - the planning input for the screen-parity work.

  python3 -I tools/parity_gap.py --data "$OPENUG2_DATA" --repo . [--json OUT.json]
  python3 -I tools/parity_gap.py --data "$OPENUG2_DATA" --repo . --class frontend-screen --used no

Reads:
  tools/fng_dump.py            the retail package reader (imported, not copied): Catalog, select,
                               rows_for, ha_hash.
  src/                         the OpenUG2 source tree, as text, for the USED scan.

Two rules do the work and both are explicit here rather than per package:

USED - the package is referenced where OpenUG2 names it or its hash:
  name   the package name (with or without the ".fng" suffix) as a whole word, case-insensitive -
         `src/fe_binding.h:917 {"UI_QRTrackSelect.fng", ..}`, `src/pause_entries.c:105
         "open:ui_ReplayControl.fng"`. Case-insensitive and word-anchored on purpose: the retail
         loader hashes the upper-cased name (src/ugui/ugui_assets.c:ug_assets_package), so the
         game itself is case-blind, and `ui_ReplayControl` proves the source is too. Word
         boundaries keep `GenericDialog` from matching `GenericDialog_SMALL`.
  hash   the 32-bit hash of the name as a hex literal (`0xAA469286`), which is how src/gui_dialog.c
         keys the two dialog packages and src/pause_menu.c:30 the pause root. `ha_hash` is
         fng_dump's port of src/hud_asset.c:ha_hash, verified against those constants.
Both kinds of hit are printed as path:line. A hit inside a C comment counts, because a comment
that names a package is what a screen-binding note looks like in this tree, and a human reading
the report can see the line; hits are tagged `code` and `comment` so the distinction is visible
rather than hidden by a guess. `--used code-only` restricts USED to non-comment hits.

CLASS - the first matching rule of CLASS_RULES wins (see that table). Rules match the package
name; where the name is silent the content decides (CONTENT_RULES: a package whose text nodes are
all "DEFAULT STRING" placeholders is dev furniture, one with no text at all is an overlay/art
layer). Out-of-scope classes (2-player, online/LAN, memory-card/console, other) are still counted
and shown per milestone, but the gap table in docs/parity/GAP-SCREENS.md lists in-scope classes
only, per the plan.

Text keys are the resolved English captions (fng_dump's label_of: a node's "SH" label key, else
the entry its sample text hashes to), which is the language-table key the game looks up at draw
time. `$JOY_EVENT_*` and `$SMS_*` samples are controller-prompt and SMS-table placeholders, not
language keys, so they are tallied as `placeholders` and kept out of `language_keys`.

Self-test: `python3 -I tools/parity_gap_test.py -v` (synthetic list and fake src tree, no retail
data). Exit 0 = inventory produced; 2 = usage or data error.
"""
import argparse
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import fng_dump  # noqa: E402  the retail reader: reused, not copied

# The classes the brief asks for. "boot-attract" splits out of "other" because docs/parity/ROADMAP.md
# M3 (EA/THX logos, intro FMV, splash, language select, loading tips) is a real milestone with a
# large package family; shipping it inside "other" would hide a whole milestone's worth of work.
CLASSES = ("frontend-screen", "dialog", "ingame-hud", "ingame-menu", "2-player",
           "online-lan", "memory-card-console", "boot-attract", "other")
IN_SCOPE = ("frontend-screen", "dialog", "ingame-hud", "ingame-menu")

# Class rules, first match wins, matched against the package name with the ".fng" suffix removed.
# Each rule is (class, regex, why). The order carries the meaning: the narrow families (2-player,
# online, memory card, boot) must be tested before the broad "UI_" front-end sweep, or every
# "UI_OL*" screen would land in frontend-screen.
CLASS_RULES = (
    ("2-player", r"^2p", "the 2P_* split-screen family: 2PHudTop/2PHudBot (+_DRAG/_DRIFT/_OUTRUN) "
                         "and 2P_PressStart"),
    ("memory-card-console", r"^mc_|^ui_olage|^ui_olauthdna", "MC_* console save-card screens; the "
                                                            "console/ESRB age gate (UI_OLAgeVerif) "
                                                            "and DNA auth (UI_OLAuthDna) have no PC "
                                                            "path"),
    ("online-lan", r"^ui_ol|^pc_ol|_ol_|^ui_pc_lan", "UI_OL* / PC_OL* online and PC_LAN lobby, "
                                                     "game room, rankings, EA account screens"),
    ("boot-attract", r"^ls_|^mw_|^ug_|^loading$|^loading_|^ig_playmovie|^discerror",
                     "LS_* / *_LS_* logo, FMV, splash and language-select reels, the Loading* "
                     "tips screens, IG_PlayMovie, DiscError*"),
    ("ingame-hud", r"^hud_|^chyron_|^demo_minimap|^demo_shopp", "HUD_* per-mode HUDs, Chyron_* "
                                                                "music tickers, the demo minimap "
                                                                "legend and shop preview"),
    ("dialog", r"genericdialog|helpdialog|^ui_ingamedialog$|^ui_engage.*dialog$|^ui_sponsorpopup$"
               r"|_dialog$|^pc_contexthelp$|^mu_postraceconfirm$", "Generic*/Help* dialog frames, "
                                              "the in-game OK/Cancel dialog, the ENGAGE message "
                                              "dialogs, the sponsor popup, PC context help, the "
                                              "post-race confirm popup (its nodes are literally a "
                                              "\"dialog window\" plus OK/Cancel)"),
    ("ingame-menu", r"^ui_pause|^ui_options$|^ui_ingame_|^ui_replaycontrol$|^ui_chooserimbrand$"
                    r"|^ui_choosespinner$|^ui_cribreward|^ui_perf.*dyno|^ui_performancebrandselect$"
                    r"|^ui_chooseperformanc", "the pause hub and its options, in-world map, replay, "
                                              "garage/performance-part pickers and dyno opened from "
                                              "the garage, sponsor-reward picker"),
    ("frontend-screen", r"^ui_|^mu_|^garagemain$|^worldmapmain$|^credits$|^ea_trax|^screenprintf$"
                        r"|^pc_loading$|^demo_", "UI_* front-end screens, the MU_* menu-utility "
                                                 "frames (Quick Race car select), and the garage/"
                                                 "world-map/credits/jukebox frames"),
)

# Name-agnostic fallbacks, applied only when no name rule fired.
CONTENT_RULES = (
    ("other", "all-default-string", "every text node is the \"DEFAULT STRING\" placeholder: a dev "
                                    "printf/console font sheet, not a screen"),
    ("other", "no-text-nodes", "no text nodes at all: an art/overlay/video layer, not a screen"),
    ("frontend-screen", "default", "no rule matched; treated as a front-end screen until read"),
)

# Milestone map: (regex over the name, milestone id). First match wins; the id is cross-checked
# against the scope text of docs/parity/ROADMAP.md's status board, so each entry cites the phrase
# it came from. A package whose class is out of scope keeps the label but the doc lists counts
# only, and "unmapped" marks a gap the roadmap does not yet cover.
MILESTONE_RULES = (
    # (pattern, milestone label). Same (pattern, text) tuple shape as CLASS_RULES: pattern first.
    # Narrow families before the broad ^ui_ sweep, or every UI_OL screen would fall through.
    (r"^2p", "out-of-scope: 2-player split screen is not a milestone"),
    (r"^ui_ol|^pc_ol|_ol_|^ui_pc_lan",
     "out-of-scope: online/LAN - no milestone (ROADMAP has no online row)"),
    (r"^mc_|^ui_olage|^ui_olauthdna", "out-of-scope: console memory card / console age gate"),
    (r"^loading|^pc_loading$|^hud_world_loading",
     "M3/M9 (loading tips and the world-loading bar; ROADMAP M9 lists loading screens)"),
    (r"^ls_|^mw_|^ug_|^discerror|^ig_playmovie",
     "M3 (boot/attract: EA/THX logos, intro FMV, splash, language select)"),
    (r"^ui_pause$|^ui_pausoptions|^ui_pauseoptions",
     "M1 (pause: restart / quit to menu)"),
    (r"^ui_replaycontrol$", "M10 (replay: ROADMAP lists the pause Replay entry as disabled)"),
    (r"^mu_quickrace|^ui_qr|^ui_main$|^ui_postrace|^mu_",
     "M1 (Quick Race loop: main menu, QR screens including the MU_* frames, post-race results)"),
    (r"^ui_options|^ui_profilemanager|^ui_dateentry|^ui_virtualkeyboard|^ui_wheel_options"
     r"|^ui_pc_customize|^ui_optionscontroller|^ui_pc_help_bar",
     "M2 (front-end options, profiles, controls)"),
    (r"^ui_career|^worldmapmain|^ui_in-game_worldmap|^ui_status_|^ui_engage"
     r"|^ui_sms|^ui_magazine|^ui_sponsor|^ui_rewardssponsor|^ui_cribreward|^ui_buyperformance"
     r"|^ui_startcareer|^demo_shopp|^ui_menu_asset",
     "M6 (career: world map, ENGAGE art, car lot, SMS, sponsors, shops, DVD/magazines)"),
    (r"^ui_perf.*dyno|^ui_performancedyno|^ui_performancebrandselect|^ui_chooseperformance"
     r"|^ui_buyperformanceparts", "M5 (performance tuning and the dyno)"),
    (r"^ui_chooserimbrand$|^ui_choosespinner$|^ui_genericparts|^ui_rims",
     "M5 (customise: parts, rims, spinner pickers)"),
    (r"^hud_|^chyron_|^demo_minimap", "M9 (HUD parity: speedo, position/laps, drag/drift, map, "
                                      "chyron, captions)"),
    (r"^ui_ingame_|^ig_", "M7 (race-mode parity: in-world map and mode chrome)"),
    (r"^genericdialog|^helpdialog|^ui_ingamedialog|^pc_contexthelp",
     "M1/M2 (shared dialog frames the options and pause screens already call)"),
    (r"^ui_", "M4/M5 (front-end 3D scene and screen furniture)"),
    (r"^credits$|^ea_trax|^screenprintf$|^pc_loading$|^demo_",
     "unmapped (front-end package with no milestone row yet)"),
    (r"garage", "M5 (garage hub)"),
)

SIZE_TIERS = ((12, 3), (40, 8))          # S ceilings, then M ceilings; anything past M is L
HIT_LIMIT = 6                             # hits shown per package in JSON/report; counts stay full
PLACEHOLDER_SAMPLES = re.compile(r"^\$[A-Z_0-9]+\$$")     # $JOY_EVENT_* / $SMS_* runtime fills
SOURCE_EXTS = (".c", ".h")
TEXT_NODE = 2                             # HA_KIND_TEXT, src/hud_asset.h:96 (fng_dump KIND_NAMES)
DEFAULT_STRING = "DEFAULT STRING"


class GapError(Exception):
    """A usage or data error: reported as a message, never a traceback."""


# ---- classification ---------------------------------------------------------------------------

def strip_fng(name):
    """Package name without a trailing .fng, whichever case the bundle stored."""
    if name.lower().endswith(".fng"):
        return name[:-4]
    return name


def is_placeholder(sample):
    """A runtime-filled sample ($JOY_EVENT_*, $SMS_*): a prompt/table key, not a language key."""
    return bool(sample) and bool(PLACEHOLDER_SAMPLES.match(sample))


def is_default_string(sample):
    """The authored placeholder retail leaves in unused text nodes."""
    return bool(sample) and sample.strip().upper() == DEFAULT_STRING


def class_of(name):
    """(class, rule-description) by the tables above; name first, content as a fallback."""
    stem = strip_fng(name).lower()
    for cls, pattern, why in CLASS_RULES:
        if re.search(pattern, stem):
            return cls, why
    return None, None


def class_from_content(cls, why, name, text_nodes, default_string_nodes):
    """Decide the class once the content is known; returns (class, rule-why).

    A name rule that fires still wins, with one evidence-based override: a package whose every
    text node is the DEFAULT STRING placeholder shows nothing in the game, whatever it is called,
    so the content outranks the name there (ScreenPrintf.fng is 64 such nodes - a dev text sheet,
    not a screen). Otherwise the content rules only decide packages no name rule claimed.
    """
    all_default = bool(text_nodes) and default_string_nodes == text_nodes
    no_text = not text_nodes
    for candidate, key, note in CONTENT_RULES:
        if key == "all-default-string" and all_default:
            return candidate, note
        if key == "no-text-nodes" and no_text and cls is None:
            return candidate, note
        if key == "default" and cls is None and not (all_default or no_text):
            return candidate, note
    if cls is not None:
        return cls, why
    raise GapError("no content rule matched %s" % strip_fng(name))


def milestone_of(name):
    """(milestone label, matched pattern) from MILESTONE_RULES; first match wins."""
    stem = strip_fng(name).lower()
    for pattern, label in MILESTONE_RULES:
        if re.search(pattern, stem):
            return label, pattern
    return "unmapped (no rule matched)", ""


def size_hint(node_count, text_count):
    """S/M/L from the node and caption counts - the work is placing nodes and wiring captions.

    The tiers are total: S under the first ceiling, M under the second, L past it. A package is
    sized by the larger of its two dimensions, so a 200-node/1-caption HUD is not called S.
    """
    def tier(dimension, ceilings):
        for ceiling, label in ceilings:
            if dimension <= ceiling:
                return label
        return "L"
    ceilings = ((SIZE_TIERS[0][0], "S"), (SIZE_TIERS[1][0], "M"))
    text_ceilings = ((SIZE_TIERS[0][1], "S"), (SIZE_TIERS[1][1], "M"))
    order = {"S": 0, "M": 1, "L": 2}
    a, b = tier(node_count, ceilings), tier(text_count, text_ceilings)
    return a if order[a] >= order[b] else b


# ---- USED detection ---------------------------------------------------------------------------

def hash_variants(name):
    """Hex spellings of the package hash that a C literal could take."""
    value = fng_dump.ha_hash(name)
    return ["0x%08x" % value, "0X%08X" % value, "%08x" % value, "%d" % value]


def code_span(line, depth):
    """(the line with /* */ and // comments stripped, block_depth_after).

    `const char *s = "X.fng"; /* note */` is code that mentions a package in a trailing comment;
    classifying the whole line by its comment would hide the load site. Stripping the comment
    text keeps both facts: the match still happens, and it happens against the code that runs.
    A line with nothing left but whitespace once its comments are gone is a comment line - which
    includes a line inside a block comment that happens to carry the closing "*/".
    """
    out = []
    rest = line
    open_block = depth > 0
    closed_a_block = False
    while rest:
        if open_block:
            end = rest.find("*/")
            if end < 0:
                rest = ""
                continue
            rest = rest[end + 2:]
            open_block = False
            closed_a_block = True
            continue
        slash = rest.find("//")
        start = rest.find("/*")
        if slash >= 0 and (start < 0 or slash < start):
            out.append(rest[:slash])
            rest = ""
            continue
        if start >= 0:
            out.append(rest[:start])
            rest = rest[start + 2:]
            open_block = True
            continue
        out.append(rest)
        rest = ""
    if open_block:                          # still inside a block at the end of the line
        return "".join(out), max(1, depth + 1)
    return "".join(out), max(0, depth - 1) if closed_a_block else depth


def scan_source(repo):
    """[(rel_path, line_number, code_text, kind)] for every source line, kind = code/comment.

    Reads every .c/.h under repo/src once; the per-package match runs over these lines so the tree
    is touched once however many packages there are. Matching uses code_text, so a package named
    only inside a comment is reported as a comment hit and one named in the statement is a code
    hit, whichever way the line is laid out.
    """
    src = os.path.join(repo, "src")
    if not os.path.isdir(src):
        raise GapError("no src/ directory under %s (is --repo the OpenUG2 checkout?)" % repo)
    lines = []
    for dirpath, dirnames, filenames in os.walk(src):
        dirnames[:] = sorted(d for d in dirnames if d not in (".git", "__pycache__"))
        for fn in sorted(filenames):
            if not fn.endswith(SOURCE_EXTS):
                continue
            path = os.path.join(dirpath, fn)
            rel = os.path.relpath(path, repo).replace(os.sep, "/")
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as handle:
                    text = handle.read()
            except OSError as exc:
                raise GapError("cannot read %s: %s" % (rel, exc))
            depth = 0
            for number, line in enumerate(text.splitlines(), 1):
                code, depth = code_span(line, depth)
                kind = "comment" if not code.strip() else "code"
                lines.append((rel, number, code if kind == "code" else line, kind))
    return lines


def find_hits(name, source_lines):
    """Every source line that references this package, by name and by hash.

    The name rule tries the spellings a source file can actually carry, as plain substrings, each
    anchored so a shorter name cannot claim a longer one:

      `UI_pause.fng`  the whole stored name. The suffix may not be dropped here: src/ writes the
                      quoted literal with it (`return "UI_pause.fng";`), and a rule that strips
                      the suffix and then forbids a following "." - the obvious-looking way to
                      keep `GenericDialog` out of `GenericDialog_SMALL` - matches no quoted
                      package name at all, which silently loses a third of the USED set.
      `UI_pause`      the bare stem, for the places that write it without the suffix - a lowercase
                      `ui_ReplayControl.fng` is caught by the first spelling case-insensitively,
                      while `UI_pause` in prose needs this one.
      the hash        as a hex literal, the src/gui_dialog.c style.

    Each spelling is followed by a negative lookahead over the characters a stem can contain, so
    `GenericDialog` cannot match inside `GenericDialog_SMALL`: the suffixed spelling already ends
    in ".fng", and the bare stem is followed by "_" there. A preceding-character rule is both
    useless and wrong (a match's predecessor is by definition outside the match, and the quote in
    `"UI_pause.fng"` must stay a legal neighbour).
    """
    stem = strip_fng(name)
    spellings = [re.escape(n) + r"(?![A-Za-z0-9_.])" for n in
                 (name if name.lower().endswith(".fng") else name + ".fng", stem)]
    name_re = re.compile("|".join(spellings), re.IGNORECASE)
    hash_re = re.compile(r"(?<![0-9a-fA-Fx])(%s)(?![0-9a-fA-F])"
                         % "|".join(re.escape(v) for v in hash_variants(name)), re.IGNORECASE)
    hits = []
    for rel, number, line, kind in source_lines:
        by = []
        if name_re.search(line):
            by.append("name")
        if hash_re.search(line):
            by.append("hash")
        if by:
            hits.append({"path": rel, "line": number, "by": "+".join(by), "kind": kind,
                         "text": line.strip()})
    return hits


# ---- inventory --------------------------------------------------------------------------------

def extra_bundles(data_root, rel_paths):
    """[(rel, path)] for --extra-bundle, resolved case-insensitively like the loader does."""
    out = []
    for rel in rel_paths:
        path = rel if os.path.isabs(rel) else fng_dump.resolve_ci(data_root, rel)
        if path is None or not os.path.isfile(path):
            raise GapError("--extra-bundle %s not found under %s" % (rel, data_root))
        shown = os.path.relpath(os.path.abspath(path), os.path.abspath(data_root))
        out.append((shown if not shown.startswith("..") else os.path.abspath(path), path))
    return out


def read_extra(rel, path):
    """Package blobs of one bundle the loader does not scan, in fng_dump's Catalog shape."""
    try:
        with open(path, "rb") as handle:
            raw = handle.read()
        if raw[:4] in (b"JDLZ", b"HUFF"):
            raw = fng_dump.decompress(raw)
        entries, notes = fng_dump.package_blobs(raw)
    except (fng_dump.FngError, OSError, ValueError) as exc:
        raise GapError("--extra-bundle %s: %s" % (rel, exc))
    return [(rel, offset, tag, name, wrapped) for offset, tag, name, wrapped in entries], notes


def inventory(data_root, repo, extra=(), language="English", used_mode="all", quiet=False):
    """One record per package occurrence, plus the scan notes and totals."""
    try:
        catalog = fng_dump.Catalog(data_root, language)
    except fng_dump.FngError as exc:
        raise GapError(str(exc))
    blobs = list(catalog.blobs())
    notes = list(catalog.notes)
    for rel, path in extra_bundles(data_root, extra):
        found, extra_notes = read_extra(rel, path)
        blobs.extend(found)
        notes.extend(extra_notes)
    table = catalog.language_table()
    source_lines = scan_source(repo)

    records = []
    cache = {}
    for index, (rel, offset, tag, name, wrapped) in enumerate(blobs):
        if not quiet:
            sys.stderr.write("\r%-40s" % ("[%d/%d] %s" % (index + 1, len(blobs), name[:34])))
        key = name.lower()
        if key in cache:                      # the name is pooled into several bundles
            cached = cache[key]
            rec = dict(cached, file=rel, offset="0x%x" % offset, duplicate_of=cached["file"])
        else:
            pkg = catalog.select(wrapped, name)
            if pkg is None:
                notes.append("%s: %s at 0x%x is not readable" % (rel, name, offset))
                continue
            rows = fng_dump.rows_for(pkg, table)
            texts = [r for r in rows if r["kind"] == TEXT_NODE]
            samples = [(r["sample_full"] or "") for r in texts]
            keys = sorted(set(r["label_text"] for r in texts if r["label_text"]))
            placeholders = sorted(set(s for s in samples if is_placeholder(s)))
            default_count = sum(1 for s in samples if is_default_string(s))
            cls, why = class_of(name)
            cls, why = class_from_content(cls, why, name, len(texts), default_count)
            hits = find_hits(name, source_lines)
            counted = counting_kinds(used_mode)
            counted_hits = [h for h in hits if h["kind"] in counted]
            milestone, pattern = milestone_of(name)
            rec = {
                "package": name,
                "stem": strip_fng(name),
                "file": rel,
                "offset": "0x%x" % offset,
                "node_count": len(rows),
                "text_node_count": len(texts),
                "image_node_count": sum(1 for r in rows if r["kind"] == 1),
                "group_node_count": sum(1 for r in rows if r["kind"] == 5),
                "language_keys": keys,
                "placeholders": placeholders,
                "class": cls,
                "class_rule": why or "",
                "in_scope": cls in IN_SCOPE,
                "milestone": milestone,
                "size": size_hint(len(rows), len(texts)),
                "used": bool(counted_hits),
                "hit_count": len(counted_hits),
                "hits": counted_hits[:HIT_LIMIT],
                "hits_truncated": len(counted_hits) > HIT_LIMIT,
                "duplicate_of": None,
            }
            cache[key] = rec
        records.append(rec)
    if not quiet:
        sys.stderr.write("\r%s\n" % (" " * 60))

    distinct = {}
    for rec in records:
        distinct.setdefault(rec["package"].lower(), rec)
    counts = {cls: {"occurrences": 0, "distinct": 0, "used": 0, "unused": 0} for cls in CLASSES}
    for rec in records:
        counts[rec["class"]]["occurrences"] += 1
    for rec in distinct.values():
        counts[rec["class"]]["distinct"] += 1
        counts[rec["class"]]["used" if rec["used"] else "unused"] += 1
    return {
        "data_root": data_root,
        "repo": repo,
        "language": language,
        "used_mode": used_mode,
        "extra_bundles": list(extra),
        "notes": notes,
        "totals": {
            "occurrences": len(records),
            "distinct_packages": len(distinct),
            "used": sum(1 for r in distinct.values() if r["used"]),
            "unused": sum(1 for r in distinct.values() if not r["used"]),
            "files_scanned": len(set(r["file"] for r in records)),
            "source_files_scanned": len(set(l[0] for l in source_lines)),
        },
        "counts_by_class": counts,
        "packages": records,
    }


def counting_kinds(used_mode):
    """Hit kinds that make a package USED: every kind in `all`, code lines only in `code-only`."""
    return ("code",) if used_mode == "code-only" else ("code", "comment")


def filter_records(payload, args):
    """Apply --class/--used/--in-scope to the package list, in place."""
    kept = payload["packages"]
    if args.cls:
        kept = [r for r in kept if r["class"] == args.cls]
    if args.used:
        want = args.used == "yes"
        kept = [r for r in kept if r["used"] == want]
    if args.in_scope:
        want = args.in_scope == "yes"
        kept = [r for r in kept if r["in_scope"] == want]
    payload["packages"] = kept
    return payload



# ---- output -----------------------------------------------------------------------------------

def render(payload, show_hits=True):
    """Text report: totals, per-class table, then one line per package."""
    out = []
    t = payload["totals"]
    out.append("data %s" % payload["data_root"])
    out.append("repo %s (%d source files)" % (payload["repo"], t["source_files_scanned"]))
    out.append("USED rule: package name (case-insensitive, whole word) or ha_hash hex literal; "
               "mode %s" % payload["used_mode"])
    out.append("")
    out.append("%-22s %11s %9s %6s %8s" % ("class", "occurrences", "distinct", "used", "unused"))
    for cls in CLASSES:
        c = payload["counts_by_class"][cls]
        out.append("%-22s %11d %9d %6d %8d"
                   % (cls, c["occurrences"], c["distinct"], c["used"], c["unused"]))
    out.append("%-22s %11d %9d %6d %8d"
               % ("TOTAL", t["occurrences"], t["distinct_packages"], t["used"], t["unused"]))
    out.append("")
    for rec in payload["packages"]:
        flag = "USED  " if rec["used"] else "UNUSED"
        out.append("%-34s %-20s %-4s n=%-4d t=%-3d keys=%-3d %s  %s"
                   % (rec["package"], rec["class"], rec["size"], rec["node_count"],
                      rec["text_node_count"], len(rec["language_keys"]), flag,
                      "dup of %s" % rec["duplicate_of"] if rec["duplicate_of"] else rec["file"]))
        out.append("    %s" % rec["milestone"])
        if show_hits and rec["used"]:
            for hit in rec["hits"]:
                out.append("    %s:%d [%s/%s] %s"
                           % (hit["path"], hit["line"], hit["by"], hit["kind"], hit["text"]))
            if rec["hits_truncated"]:
                out.append("    (%d more hits)" % (rec["hit_count"] - len(rec["hits"])))
    for note in payload["notes"]:
        out.append("note: %s" % note)
    return "\n".join(out) + "\n"


def main(argv=None):
    ap = argparse.ArgumentParser(prog="parity_gap.py",
                                 description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True, help="retail data root ($OPENUG2_DATA)")
    ap.add_argument("--repo", required=True, help="OpenUG2 checkout (holds src/)")
    ap.add_argument("--json", dest="json_out", metavar="OUT.json", help="write the full payload")
    ap.add_argument("--language", default="English", help="language table for caption keys")
    ap.add_argument("--extra-bundle", action="append", default=[], metavar="PATH",
                    help="bundle the loader does not scan, repeatable "
                         "(e.g. GLOBAL/InGamePerformanceTuning.bun)")
    ap.add_argument("--class", dest="cls", choices=CLASSES, help="keep one class")
    ap.add_argument("--used", choices=("yes", "no"), help="keep only USED or only UNUSED")
    ap.add_argument("--in-scope", dest="in_scope", choices=("yes", "no"),
                    help="keep only in-scope (frontend-screen/dialog/ingame-hud/ingame-menu)")
    ap.add_argument("--used-mode", choices=("all", "code-only"), default="all",
                    help="all: comment mentions count as USED; code-only: only code hits do")
    ap.add_argument("--no-hits", action="store_true", help="omit the path:line evidence lines")
    ap.add_argument("--quiet", action="store_true", help="no per-package progress on stderr")
    args = ap.parse_args(argv)

    try:
        payload = inventory(args.data, args.repo, args.extra_bundle, args.language,
                            args.used_mode, args.quiet)
        filter_records(payload, args)
        sys.stdout.write(render(payload, not args.no_hits))
        if args.json_out:
            fng_dump.write_json(args.json_out, payload)
    except GapError as exc:
        sys.stderr.write("error: %s\n" % exc)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
