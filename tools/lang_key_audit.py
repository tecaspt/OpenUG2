#!/usr/bin/env python3
"""Audit the retail language keys written in src/ against LANGUAGES/English.bin.

The game shows retail text by looking a 32-bit key up in the English string table, so a key
that is not in that table fails silently (the caller gets its fallback or nothing). This tool
reads the table with the same rules as our own C loader (src/ugui/ugui_strtab.c,
ug_strtab_load), then reports every language key the source writes as a hex literal or as
ug_hash_upper("LABEL") (src/ugui/ugui_util.c:52) and whether the table has it.

  python3 -I tools/lang_key_audit.py --data "$OPENUG2_DATA" --src src [--json OUT.json]

Two sources of keys are read: the accessor call (its second argument is either a hex literal, a
`ug_hash_upper("LABEL")`, or a name that travels there through the source), and — because the newer
career and dialog code stores keys in fields and struct initialisers instead of passing them at the
call site — an assignment or designated initialiser whose target names a language key.

Exit code: 0 = no MISSING and no LABEL-MISMATCH, 1 = at least one of those, 2 = usage/data error.
There is no allowlist: wrong keys are reported, the source is fixed by their owners.

Row status vocabulary (the two finding statuses are the only ones that set the exit code):
  OK              the key is in the table; the retail text follows
  MISSING         the key is not in the table
  LABEL-MISMATCH  a labelled literal whose comment names a label hashing to something else
Keys whose comment mentions label-shaped words but does not consist of exactly one that hashes
to the literal are listed as an informational "ambiguous comment label" note instead of a row:
such a comment (prose, a four-character code, several candidates) does not identify the literal,
so nothing about it can be judged without a human reading it.

The assignment patterns are deliberately narrow. A key field is a literal-name target ending in
`_key` / `_key[n]` (message_key, label_key, package_key) with no dotted access, or one of the named
language-key fields; a designated initialiser is `.message_key` / `.label_key` / a bare `.key`
inside a brace group that says `LangKey` or `Text`. A scene node name hash (`FES_ROW_TOP`), an FE
message id (`MSG_CANCEL`) and a bare `#define KEY_PKG_SMALL` therefore stay out of the rows, and so
does a `result_msg` slot — those namespaces are not looked up in the string table (see
docs/parity/LANG-AUDIT-2.md for the per-constant evidence).
"""
import argparse
import json
import os
import re
import struct
import sys

MASK32 = 0xFFFFFFFF
STRTAB_CHUNK_ID = 0x00039000
# Accessor whose second argument is the string-table key.
TEXT_CALLS = ("gv_text", "pause_menu_text", "ug_strtab_get", "ug_strtab_or")
TEXT_LEN = 60

HEX8 = re.compile(r"(?<![0-9A-Za-z_])0x([0-9a-fA-F]{8})([uU]?)(?![0-9A-Za-z_])")
KEY_HEX = re.compile(r"\A0x([0-9a-fA-F]{8})[uU]?\Z")
KEY_UPPER = re.compile(r'\Aug_hash_upper\(\s*"([^"]*)"\s*\)\Z')
WORD = re.compile(r"[A-Za-z][A-Za-z0-9_]*")
QUOTED = re.compile(r'"(?:\\.|[^"\\])*"' + r"|'(?:\\.|[^'\\])*'")
TABLE_ARG = re.compile(r"\A(?:&)?[A-Za-z_][A-Za-z0-9_]*(?:->[A-Za-z_][A-Za-z0-9_]*)*\Z")
# The language-key fields, named rather than guessed: `grep -c "\.\?label_key *=" src/` and friends
# show that message_key / label_key are the fields fed to gv_text (src/gui_view.c:815-817) and that
# every other `_key` in this tree is another namespace (`.model_key`, `.package_key`, `.icon_key`,
# `.source_key`, ...). A `#define KEY_PKG_SMALL` has no access operator at all, so package identity
# stays out of the rows, and so does `result_msg`, an FE message id.
KEY_TARGET = re.compile(
    r"(?:\.|->)[ \t]*(?:message_key|label_key)(?:\[\d+\])?[ \t]*=(?![=])")
# `{0x…, "Text"}` or `? 0x…, "Text" :`: a hex literal in key position of a record that pairs it
# with its English text, kept only when the line or the struct it belongs to says LangKey / Text.
KEY_INIT_ROW = re.compile(r"(?:\{|\?)\s*0x([0-9a-fA-F]{8})[uU]?\s*,\s*[\"A-Za-z_]")
KEY_EN_TEXT = re.compile(r"(?:\{|\?)\s*0x[0-9a-fA-F]{8}[uU]?\s*,\s*\"")
DESIGNATED = re.compile(r"\.([A-Za-z_][A-Za-z0-9_]*)\s*=\s*([^,;)]+)")
KEY_FIELD = re.compile(r"(?:message_key|label_key|\Akey\Z)")
KEY_MARK = re.compile(r"LangKey|\bText\b")


def ug_hash_upper(text):
    """Port of ug_hash_upper (src/ugui/ugui_util.c:52): ASCII upper-case, h = h*33 + c."""
    h = MASK32
    for byte in text.encode("utf-8", "replace"):
        if 0x61 <= byte <= 0x7A:
            byte -= 0x20
        h = (h * 33 + byte) & MASK32
    return h


def self_test_hash():
    """ug_hash_upper must agree with the C function before any conclusion is drawn from it."""
    want = 0x0598b065
    got = ug_hash_upper("HUD_SHIFT_PERFECT")
    if got != want:
        sys.stderr.write("self-test failed: ug_hash_upper(\"HUD_SHIFT_PERFECT\") = 0x%08x, want 0x%08x\n"
                         % (got, want))
        return 0
    return 1


class StrTabError(Exception):
    pass


def strtab_offsets(buf):
    """Offsets of string-table chunks. Mirrors ug_chunks_walk / tools/chunkwalk.py:
    magic 0 is padding, a top nibble of 8 means nested chunks inside its own payload."""
    found = []

    def walk(start, end):
        pos = start
        while pos + 8 <= end:
            magic, size = struct.unpack_from("<II", buf, pos)
            if magic == STRTAB_CHUNK_ID:
                found.append(pos)
            if magic != 0 and (magic >> 28) == 8:
                walk(pos + 8, min(pos + 8 + size, end))
            pos += 8 + size

    walk(0, len(buf))
    return found


def load_string_table(buf, hdr_off, source):
    """Port of ug_strtab_load. Returns (table_name, count_field, {hash: text}).

    ug_strtab_load's insert() replaces an entry on an equal hash, so a duplicate hash keeps the
    last text and the dictionary size can be below the header count; both numbers are returned.
    """
    if len(buf) - hdr_off < 8:
        raise StrTabError("%s: truncated chunk header" % source)
    chunk_id, psize = struct.unpack_from("<II", buf, hdr_off)
    if chunk_id != STRTAB_CHUNK_ID:
        raise StrTabError("%s: wrong chunk id 0x%08x" % (source, chunk_id))
    if psize > len(buf) - hdr_off - 8:
        raise StrTabError("%s: chunk runs past buffer" % source)
    p = hdr_off + 8
    if psize < 16:
        raise StrTabError("%s: payload too small" % source)
    hdr, count, tab, area = struct.unpack_from("<IIII", buf, p)
    # 0x20: header carries a 16-byte table name; 0x10 (PC retail): no name.
    if hdr not in (0x20, 0x10):
        raise StrTabError("%s: bad header size 0x%x" % (source, hdr))
    if psize < hdr + 4 + 512:
        raise StrTabError("%s: payload too small" % source)
    if struct.unpack_from("<I", buf, p + hdr)[0] != 256:
        raise StrTabError("%s: bad code map length" % source)
    if tab + count * 8 > psize:
        raise StrTabError("%s: entry table out of range" % source)
    if area > psize:
        raise StrTabError("%s: string area out of range" % source)
    cmap = struct.unpack_from("<256H", buf, p + hdr + 4)
    name = ""
    if hdr == 0x20:
        name = buf[p + 16:p + 32].split(b"\x00", 1)[0].decode("latin-1")

    table = {}
    for i in range(count):
        key, off = struct.unpack_from("<II", buf, p + tab + i * 8)
        if area + off >= psize:
            raise StrTabError("%s: string offset out of range" % source)
        start = p + area + off
        end = buf.find(b"\x00", start, p + psize)
        if end < 0:
            raise StrTabError("%s: unterminated string" % source)
        # Bytes >= 0x80 name a code point in the table's own map; otherwise Latin-1.
        table[key] = "".join(chr(cmap[b] if (b >= 0x80 and cmap[b]) else b)
                             for b in buf[start:end])
    return name, count, table


def split_comment(line):
    """Return (code, comment, strings) for one source line. Comments become spaces in code so
    that argument text keeps its original offsets; strings lists the (start, end) spans of
    string and character literals, whose contents are data rather than code."""
    out = []
    comments = []
    strings = []
    i = 0
    n = len(line)
    while i < n:
        ch = line[i]
        if ch in "\"'":
            end = i + 1
            while end < n:
                if line[end] == "\\":
                    end += 2
                    continue
                if line[end] == ch:
                    end += 1
                    break
                end += 1
            out.append(line[i:min(end, n)])
            strings.append((i, min(end, n)))
            i = min(end, n)
            continue
        if line.startswith("/*", i):
            close = line.find("*/", i + 2)
            end = n if close < 0 else close + 2
            comments.append(line[i:end])
            out.append(" " * (end - i))
            i = end
            continue
        if line.startswith("//", i):
            comments.append(line[i:])
            out.append(" " * (n - i))
            i = n
            continue
        out.append(ch)
        i += 1
    return "".join(out), " ".join(comments), strings


def in_string(pos, strings):
    """True when a code index falls inside a string or character literal."""
    return any(start <= pos < end for start, end in strings)


def split_args(code, open_idx):
    """Split a call's arguments starting at its '(' index. Returns (args, closed_on_this_line)."""
    n = len(code)
    i = open_idx + 1
    depth = 1
    args = []
    cur = []
    while i < n:
        ch = code[i]
        if ch in "\"'":
            start = i
            i += 1
            while i < n:
                if code[i] == "\\":
                    i += 2
                    continue
                if code[i] == ch:
                    i += 1
                    break
                i += 1
            cur.append(code[start:i])
            continue
        if ch == "(":
            depth += 1
        elif ch == ")":
            depth -= 1
            if depth == 0:
                args.append("".join(cur).strip())
                return args, True
        elif ch == "," and depth == 1:
            args.append("".join(cur).strip())
            cur = []
            i += 1
            continue
        cur.append(ch)
        i += 1
    args.append("".join(cur).strip())
    return args, False


def key_from_arg(arg):
    """(key, label) when the argument is a hex literal or ug_hash_upper("LABEL"); else None."""
    match = KEY_HEX.match(arg)
    if match:
        return int(match.group(1), 16), None
    match = KEY_UPPER.match(arg)
    if match:
        return ug_hash_upper(match.group(1)), match.group(1)
    return None


def is_declaration(args):
    """A prototype or definition, not a call: the key parameter is declared, not passed."""
    return any("uint32_t" in a for a in args[:2])


def key_assignments(code, marked):
    """(start, key, label, target) for each language-key assignment or initialiser in one line.

    Three shapes, all of them naming the field rather than guessing from the value:
      `out->message_key = 0x…u;` / `out->label_key[0] = …;`  (`_key` field / `label` slot, written
          with `.` or `->` — a bare `KEY_PKG_SMALL =` on its own #define line is not a field)
      `.message_key = 0x…,`                                   (a designated initialiser)
      `{0x…, "English text"}` / `? 0x…, "text" :`             (only where the line, or the struct
          those records belong to, says LangKey / Text)
    `target` is the matched text, so a reviewer can see from the row alone which field the tool
    believed. A target whose value is a constant name instead of a literal comes back with key
    None, which makes the caller record it as unresolved: the value lives in another file or another
    namespace, and this tool does not chase it.
    """
    found = []

    def note(at, key, label, target):
        """Record one hit, dropping the duplicates the two patterns see on the same text
        (`{ .label_key = x }` is both a field assignment and a designated initialiser)."""
        for seen_at, seen_key, seen_label, seen_target in found:
            if seen_at == at or (seen_key == key and seen_label == label):
                return
        found.append((at, key, label, target))

    for match in KEY_TARGET.finditer(code):
        target = match.group(0).strip().rstrip("=").strip()
        rhs = code[match.end():]
        value = re.match(r"\s*0x([0-9a-fA-F]{8})[uU]?\s*\}?", rhs)
        named = re.match(r"\s*(ug_hash_upper\(\s*\"[^\"]*\"\s*\)|[A-Za-z_][A-Za-z0-9_.\[\]]*)", rhs)
        if value:
            note(match.start(), int(value.group(1), 16), None, target)
        elif named and "key" in target:
            # Only a key field can name a key through a constant or a ug_hash_upper call; a
            # `label` field on the same line holds the English text, not a key.
            one = key_from_arg(named.group(1))
            if one is not None:
                note(match.start(), one[0], one[1], target)
            else:
                note(match.start(), None, named.group(1), target)
    for match in DESIGNATED.finditer(code):
        field, rhs = match.group(1), match.group(2).strip()
        if not KEY_FIELD.match(field):
            continue
        if field != "key" or marked:
            one = key_from_arg(rhs)
            target = "." + field
            if one is not None:
                note(match.start(), one[0], one[1], target)
            elif field != "key" and re.match(r"\A[A-Za-z_][A-Za-z0-9_.\[\]]*\Z", rhs):
                note(match.start(), None, rhs, target)
    if marked:
        for match in KEY_INIT_ROW.finditer(code):
            if not KEY_EN_TEXT.match(code[match.start():]):
                continue          # `{0x…, CAR_SOMETHING}` names a car or asset, not a text record
            note(match.start(), int(match.group(1), 16), None, ".key")
    return found


def comment_labels(comment):
    """Return (words, labels): every word a comment contains, and those that could name a label.

    ug_hash_upper folds case and these comments do use mixed case (/* Yes */, /* Header_Text */),
    so a candidate is any word of two or more characters that starts upper-case. Quoted spans are
    dropped first: /* "DXT1" */ names a four-character code held in the literal, not a label.
    """
    words = WORD.findall(QUOTED.sub(" ", comment))
    labels = []
    for word in words:
        if len(word) < 2 or not word[0].isupper() or word in labels:
            continue
        labels.append(word)
    return words, labels


def row(file_name, line_no, key, label, table, source, call=None,
        literal=None, label_hash=None, target=None):
    """One key. A labelled literal whose comment hashes elsewhere is the LABEL-MISMATCH case;
    every other row is judged only by whether the table has the key."""
    text = table.get(key)
    if literal is not None:
        status = "LABEL-MISMATCH"
    else:
        status = "OK" if key in table else "MISSING"
    return {
        "file": file_name,
        "line": line_no,
        "key": "0x%08x" % key,
        "label": label or "",
        "status": status,
        "text": None if text is None else text[:TEXT_LEN],
        "source": source,
        "call": call,
        "literal": "0x%08x" % literal if literal is not None else None,
        "label_hash": "0x%08x" % label_hash if label_hash is not None else None,
        "target": target,
    }


def scan_lines(lines, file_name, table):
    """Find keys in one file. Returns (rows, notes) where notes maps
    'multi-line' / 'unresolved' / 'ambiguous' to 'file:line  detail' strings."""
    rows = []
    notes = {"multi-line": [], "unresolved": [], "ambiguous": []}
    text_struct = text_init = None
    for line_no, raw in enumerate(lines, 1):
        code, comment, strings = split_comment(raw.rstrip("\n"))
        by_key = {}

        # A `{0x…, "English"}` record counts as a language key only where the code says so: on the
        # line of a designated `.key =` initialiser, or within a few lines under the `Text` /
        # `LangKey` struct whose table it is. That window is why the rows of gui_dialog.c's Text
        # table count while a same-shaped `{package_key, "UI_x.fng"}` row of the frame table does
        # not: the frame table is package identity, and nothing above it claims to hold text keys.
        marked_line = bool(KEY_MARK.search(code)) or ".key" in code
        if marked_line:
            text_init = line_no
        elif text_init and line_no - text_init > 6:
            text_init = None

        for call in TEXT_CALLS:
            at = code.find(call + "(")
            while at >= 0:
                if in_string(at, strings):
                    at = code.find(call + "(", at + len(call) + 1)
                    continue
                args, closed = split_args(code, at + len(call))
                if not closed:
                    notes["multi-line"].append("%s:%d  %s(...)" % (file_name, line_no, call))
                elif len(args) < 2 or is_declaration(args):
                    pass  # prototype or a call being completed elsewhere
                else:
                    found = key_from_arg(args[1])
                    if found is None:
                        notes["unresolved"].append("%s:%d  %s(%s, %s" %
                                                   (file_name, line_no, call, args[0], args[1]))
                    else:
                        key, label = found
                        by_key.setdefault(key, row(file_name, line_no, key, label, table,
                                                   "call", call=call))
                at = code.find(call + "(", at + len(call) + 1)

        if comment:
            words, labels = comment_labels(comment)
            for match in HEX8.finditer(code):
                if in_string(match.start(), strings):
                    continue
                literal = int(match.group(1), 16)
                agreed = [c for c in labels if ug_hash_upper(c) == literal]
                if agreed:
                    by_key.setdefault(literal, row(file_name, line_no, literal, agreed[0],
                                                   table, "labelled-literal"))
                elif len(labels) == 1 and len(words) == 1:
                    # The comment is exactly one label word, so it does claim this literal.
                    by_key.setdefault(literal, row(file_name, line_no, literal, labels[0],
                                                   table, "labelled-literal",
                                                   literal=literal,
                                                   label_hash=ug_hash_upper(labels[0])))
                elif labels:
                    # Prose, or several candidates: the comment does not identify this literal.
                    notes["ambiguous"].append("%s:%d  0x%08x  [%s]  %s" %
                                              (file_name, line_no, literal, " ".join(labels),
                                               comment.strip()[:70]))

        for at, key, label, target in key_assignments(code, text_init == line_no):
            if in_string(at, strings):
                continue
            if key is None:
                notes["unresolved"].append("%s:%d  %s = %s" % (file_name, line_no, target, label))
                continue
            by_key.setdefault(key, row(file_name, line_no, key, label, table, "key-field",
                                       target=target))

        rows.extend(by_key.values())
    return rows, notes


def main(argv=None):
    ap = argparse.ArgumentParser(prog="lang_key_audit.py", description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True, help="retail data root ($OPENUG2_DATA)")
    ap.add_argument("--src", required=True, help="source tree to scan, e.g. src")
    ap.add_argument("--json", dest="json_out", help="also write the rows as a JSON list here")
    args = ap.parse_args(argv)

    if not self_test_hash():
        return 2

    bin_path = os.path.join(args.data, "LANGUAGES", "English.bin")
    if not os.path.isfile(bin_path):
        sys.stderr.write("data error: %s not found\n" % bin_path)
        return 2
    if not os.path.isdir(args.src):
        sys.stderr.write("usage error: --src %s is not a directory\n" % args.src)
        return 2
    try:
        with open(bin_path, "rb") as handle:
            buf = handle.read()
    except OSError as exc:
        sys.stderr.write("data error: %s\n" % exc)
        return 2

    offsets = strtab_offsets(buf)
    if not offsets:
        sys.stderr.write("data error: no string table chunk (0x%08x) in %s\n"
                         % (STRTAB_CHUNK_ID, bin_path))
        return 2
    try:
        # Shared bundles may hold unrelated tables; English.bin has exactly one, and the first
        # is the one ug_strtab_load would have filled the table from.
        name, count, table = load_string_table(buf, offsets[0], os.path.basename(bin_path))
    except StrTabError as exc:
        sys.stderr.write("data error: %s\n" % exc)
        return 2

    print("%d strings read from %s (count field %d, unique hashes %d%s)" %
          (len(table), os.path.relpath(bin_path, args.data), count, len(table),
           "" if not name else ", name %s" % name))

    rows = []
    notes = {"multi-line": [], "unresolved": [], "ambiguous": []}
    for root, dirs, files in os.walk(args.src):
        dirs[:] = sorted(d for d in dirs if d != ".git")
        for base in sorted(files):
            if not base.endswith((".c", ".h")):
                continue
            path = os.path.join(root, base)
            try:
                with open(path, "r", encoding="utf-8", errors="replace") as handle:
                    lines = handle.read().splitlines()
            except OSError as exc:
                sys.stderr.write("warning: %s: %s\n" % (path, exc))
                continue
            found, file_notes = scan_lines(lines, path, table)
            rows.extend(found)
            for kind in notes:
                notes[kind].extend(file_notes[kind])

    rows.sort(key=lambda r: (r["file"], r["line"], 0 if r["source"] == "call" else 1, r["key"]))
    for r in rows:
        line = "%s:%d  %s  %s  %s" % (r["file"], r["line"], r["key"], r["label"] or "-", r["status"])
        if r["status"] == "OK":
            line += '  "%s"' % (r["text"] or "")
        elif r["status"] == "LABEL-MISMATCH":
            line += "  literal %s, %s hashes to %s" % (r["literal"], r["label"], r["label_hash"])
            line += ', key reads "%s"' % r["text"] if r["text"] else ", key not in table"
        print(line)

    counts = {}
    for r in rows:
        counts[r["status"]] = counts.get(r["status"], 0) + 1
    print("keys found: %d  OK: %d  MISSING: %d  LABEL-MISMATCH: %d  skipped: %d" %
          (len(rows), counts.get("OK", 0), counts.get("MISSING", 0),
           counts.get("LABEL-MISMATCH", 0), len(notes["multi-line"])))

    for kind, label in (("multi-line", "skipped multi-line"),
                        ("unresolved", "unresolved key argument"),
                        ("ambiguous", "ambiguous comment label")):
        print("%s: %d" % (label, len(notes[kind])))
        for note in notes[kind]:
            print("  %s" % note)

    if args.json_out:
        try:
            out_dir = os.path.dirname(os.path.abspath(args.json_out))
            os.makedirs(out_dir, exist_ok=True)
            with open(args.json_out, "w", encoding="utf-8") as handle:
                json.dump(rows, handle, ensure_ascii=False, indent=1)
                handle.write("\n")
        except OSError as exc:
            sys.stderr.write("usage error: cannot write %s: %s\n" % (args.json_out, exc))
            return 2

    return 1 if (counts.get("MISSING", 0) or counts.get("LABEL-MISMATCH", 0)) else 0


if __name__ == "__main__":
    sys.exit(main())
