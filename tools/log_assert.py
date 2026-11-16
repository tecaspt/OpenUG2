#!/usr/bin/env python3
r"""Check declarative rules over a game run log, so a gate cannot pass on silence.

Every gate step runs the game headlessly and then greps the log by hand, and a hand grep has no
memory of what it was looking for: a step that printed nothing at all still reads as "nothing
wrong" at a glance. This tool keeps the expectations next to the gate and fails loudly, with the
log lines that prove it.

  python3 -I tools/log_assert.py SPEC_FILE LOG_FILE [LOG_FILE ...]
  python3 -I tools/log_assert.py --spec 'rule; rule' LOG_FILE [...] [--json OUT.json | --json]

A spec is one rule per line. A '#' starts a comment unless it sits inside /.../, so a regex may
contain one, and blank lines are ignored. With --spec, rules are separated by ';' (also
slash-aware) and the line a message names is the ';' segment's number. Rules:

  expect /REGEX/            at least one line matches
  expect N /REGEX/          exactly N lines match
  expect >=N /REGEX/        at least N lines match
  expect <=N /REGEX/        at most N lines match
  forbid /REGEX/            no line matches
  order /A/ /B/ [/C/ ...]   the first /A/ comes before the first /B/ before the first /C/, ...
  capture NAME /REGEX/      group 1 of the first matching line is stored as NAME
  equal NAME VALUE          NAME is exactly VALUE (the rest of the line, spaces allowed)
  range NAME LO HI          NAME parses as a number and LO <= NAME <= HI
  within /A/ /B/ N          a /B/ line sits at most N lines after the first /A/ line (0 = same line)

Regexes are Python re with slashes as delimiters: \/ is a literal slash and every other backslash
sequence reaches re untouched, so a spec's \\d is Python's \d. {NAME} inside a later regex expands
to the text NAME captured, escaped so log text with metacharacters in it still matches as literal
text; the reference must come after the capture rule that fills it or the spec is rejected.
Quantifier braces ({3}, {2,5}) and \{ are left alone.

Several LOG_FILEs are checked as one stream, concatenated in the order given. Every number printed
is the naming file's own 1-based line number (PATH:LINE), so evidence stays comparable with
`grep -n` on that file; lines are numbered the way grep numbers them (on \n, dropping a trailing
\r, counting a final line that has no newline).

Output is one line per rule, PASS or FAIL, then the rule text, then - on FAIL - the reason and up
to three evidence lines. A rule that matched nothing also gets up to three near-miss lines: log
lines holding a literal run its own regex asks for ("career: lot"), or - if no line holds one -
two of its individual words, which is where a typo or a reworded log line shows itself. A line is
only named when one of those holds; otherwise the report says the log carries none of the words,
because a hint that is not close is worse than no hint. Near misses are a hint, not a match, and
the output says so.

Exit code: 0 all rules passed, 1 at least one failed, 2 usage, spec or log error - a spec error
prints 'spec:LINE: reason' and checks nothing. A spec with no rules in it is itself a spec error:
a spec that asserts nothing is the bug this tool exists to catch. --json PATH writes the results
to PATH and keeps the human report on stdout; --json alone writes JSON on stdout instead of it.
"""
import argparse
import json
import re
import sys

EXIT_PASS, EXIT_FAIL, EXIT_USAGE = 0, 1, 2

MAX_EVIDENCE = 3      # failing lines shown per rule
MAX_TEXT = 160        # evidence line text is cut to this on the human report
NAME = r"[A-Za-z_][A-Za-z0-9_]*"
CAP_REF_RE = re.compile(r"(?<!\\)\{(" + NAME + r")\}")
COUNT_RE = re.compile(r"^(>=|<=)?(\d+)$")
EQUAL_HEAD_RE = re.compile(r"^equal\s+(" + NAME + r")\s+(\S.*?)\s*$")
KEYWORDS = ("expect", "forbid", "order", "capture", "equal", "range", "within")


class SpecError(Exception):
    """A spec that cannot be honoured; the message carries the line it came from."""

    def __init__(self, line, reason):
        super().__init__("spec:%d: %s" % (line, reason))
        self.line = line


class ToolError(Exception):
    """A usage or I/O problem; exit 2, printed without a traceback."""


class Rule:
    """One parsed rule: what to look for, plus where it came from for reporting."""

    __slots__ = ("kind", "line", "text", "pats", "count", "count_op", "name",
                 "value", "lo", "hi", "span")

    def __init__(self, kind, line, text):
        self.kind = kind
        self.line = line
        self.text = text
        self.pats = []        # regex sources, in the order the rule lists them
        self.count = None     # expect: the N
        self.count_op = None  # expect: None = exact, else '>=' / '<='
        self.name = None      # capture / equal / range
        self.value = None     # equal
        self.lo = None        # range
        self.hi = None        # range
        self.span = None      # within


class Entry:
    """One log line, with the file and line number it came from."""

    __slots__ = ("log", "line", "text")

    def __init__(self, log, line, text):
        self.log = log
        self.line = line
        self.text = text

    def loc(self):
        return "%s:%d" % (self.log, self.line)

    def shown(self):
        t = self.text if len(self.text) <= MAX_TEXT else self.text[:MAX_TEXT] + " ..."
        return "%s: %s" % (self.loc(), t)


# --------------------------------------------------------------------- spec text in

def _scan(text, sep):
    """Split text on `sep` outside /.../; a backslash consumes one character inside a regex."""
    parts = []
    buf = []
    i = 0
    in_re = False
    while i < len(text):
        c = text[i]
        if c == "\\" and in_re and i + 1 < len(text):
            buf.append(text[i:i + 2])
            i += 2
            continue
        if c == "/":
            in_re = not in_re
            buf.append(c)
            i += 1
            continue
        if c == sep and not in_re:
            parts.append("".join(buf))
            buf = []
            i += 1
            continue
        buf.append(c)
        i += 1
    parts.append("".join(buf))
    return parts


def spec_pairs(text, inline):
    """Spec text -> [(line_no, rule_text)]. Files number physical lines, --spec numbers ';'-segments."""
    if inline:
        return [(n, _scan(seg, "#")[0].strip()) for n, seg in enumerate(_scan(text, ";"), 1)]
    out = []
    for n, raw in enumerate(text.split("\n"), 1):
        body = _scan(raw.rstrip("\r"), "#")[0].strip()
        if body:
            out.append((n, body))
    return out


# --------------------------------------------------------------------- rule parsing

def tokenize(text, line):
    """Split a rule into ('w', word) and ('re', pattern) tokens; /.../ is one token."""
    toks = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c.isspace():
            i += 1
        elif c == "/":
            i += 1
            buf = []
            while i < n and text[i] != "/":
                if text[i] == "\\" and i + 1 < n:
                    nxt = text[i + 1]
                    if nxt == "/":
                        buf.append("/")           # the only escape this layer owns
                    elif nxt == "\\":
                        buf.append("\\\\")         # an escaped backslash stays one backslash
                    else:
                        buf.append("\\" + nxt)     # everything else is re's business
                    i += 2
                    continue
                buf.append(text[i])
                i += 1
            if i >= n:
                raise SpecError(line, "unterminated /regex/ in '%s'" % text)
            i += 1
            toks.append(("re", "".join(buf)))
        else:
            j = i
            while j < n and not text[j].isspace() and text[j] != "/":
                j += 1
            toks.append(("w", text[i:j]))
            i = j
    return toks


def _need_regex(toks, line, usage):
    if not toks or toks[0][0] != "re":
        raise SpecError(line, "usage: %s" % usage)
    return toks[0][1]


def _need_word(toks, line, usage):
    if not toks or toks[0][0] != "w":
        raise SpecError(line, "usage: %s" % usage)
    return toks[0][1]


def _number(text, line, usage):
    try:
        return float(text)
    except ValueError:
        raise SpecError(line, "%s - '%s' is not a number" % (usage, text))


def check_regex(src, line, what):
    """Compile now unless the pattern waits on a capture, which nothing knows yet."""
    if CAP_REF_RE.search(src):
        return
    try:
        re.compile(src)
    except re.error as exc:
        raise SpecError(line, "%s: bad regex /%s/: %s" % (what, src, exc))


def parse_rule(line, text):
    toks = tokenize(text, line)
    if not toks:
        return None
    if toks[0][0] != "w":
        raise SpecError(line, "rule must start with a keyword (%s)" % ", ".join(KEYWORDS))
    kw = toks[0][1]
    if kw not in KEYWORDS:
        raise SpecError(line, "unknown rule '%s' (known: %s)" % (kw, ", ".join(KEYWORDS)))
    rest = toks[1:]
    r = Rule(kw, line, text)

    if kw == "expect":
        if rest and rest[0][0] == "w" and COUNT_RE.match(rest[0][1]):
            m = COUNT_RE.match(rest[0][1])
            r.count_op, r.count = m.group(1), int(m.group(2))
            rest = rest[1:]
        r.pats = [_need_regex(rest, line, "expect [N|>=N|<=N] /regex/")]
        rest = rest[1:]
    elif kw == "forbid":
        r.pats = [_need_regex(rest, line, "forbid /regex/")]
        rest = rest[1:]
    elif kw == "order":
        if any(t[0] != "re" for t in rest):
            raise SpecError(line, "order takes only /regex/ arguments")
        r.pats = [t[1] for t in rest]
        if len(r.pats) < 2:
            raise SpecError(line, "order needs at least two /regex/es")
        rest = []
    elif kw == "capture":
        r.name = _need_word(rest, line, "capture NAME /regex/")
        r.pats = [_need_regex(rest[1:], line, "capture NAME /regex/ with a (group)")]
        rest = []
        if not CAP_REF_RE.search(r.pats[0]):  # a pattern waiting on a capture cannot be compiled yet
            if re.compile(r.pats[0]).groups < 1:
                raise SpecError(line, "capture %s: /%s/ has no (group) to capture" % (r.name, r.pats[0]))
    elif kw == "equal":
        m = EQUAL_HEAD_RE.match(text)
        if not m:
            raise SpecError(line, "usage: equal NAME VALUE")
        r.name, r.value = m.group(1), m.group(2)
        rest = []
    elif kw == "range":
        r.name = _need_word(rest, line, "range NAME LO HI")
        rest = rest[1:]
        if len(rest) != 2 or any(t[0] != "w" for t in rest):
            raise SpecError(line, "usage: range %s LO HI" % r.name)
        r.lo = _number(rest[0][1], line, "range %s LO HI" % r.name)
        r.hi = _number(rest[1][1], line, "range %s LO HI" % r.name)
        if r.lo > r.hi:
            raise SpecError(line, "range %s: LO %g is above HI %g" % (r.name, r.lo, r.hi))
        rest = []
    elif kw == "within":
        a = _need_regex(rest, line, "within /a/ /b/ N")
        b = _need_regex(rest[1:], line, "within /a/ /b/ N")
        rest = rest[2:]
        if len(rest) != 1 or rest[0][0] != "w" or not rest[0][1].isdigit():
            raise SpecError(line, "usage: within /a/ /b/ N (N a whole number of lines)")
        r.pats = [a, b]
        r.span = int(rest[0][1])
        rest = []

    if rest:
        raise SpecError(line, "%s: unexpected '%s'" % (kw, rest[0][1]))
    for src in r.pats:
        check_regex(src, line, kw)
    return r


def parse_spec(pairs):
    """Parse [(line, text)] into rules, rejecting bad grammar and forward capture references."""
    rules = []
    for line, text in pairs:
        r = parse_rule(line, text)
        if r:
            rules.append(r)
    if not rules:
        raise SpecError(0, "no rules in this spec - a spec that asserts nothing checks nothing")
    declared = {r.name for r in rules if r.kind == "capture"}
    defined = set()
    for r in rules:
        refs = [n for src in r.pats for n in CAP_REF_RE.findall(src)]
        if r.kind in ("equal", "range"):
            refs.append(r.name)
        for name in refs:
            if name not in defined:
                hint = "" if name in declared else ", and no rule captures it"
                shown_as = name if r.kind in ("equal", "range") else "{%s}" % name
                raise SpecError(r.line, "%s uses %s before it is captured%s"
                                % (r.kind, shown_as, hint))
        if r.kind == "capture":
            defined.add(r.name)
    return rules


# --------------------------------------------------------------------- logs in

def read_log(path):
    """One log file -> [Entry], numbered the way grep -n numbers it."""
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        raise ToolError("cannot read %s: %s" % (path, exc.strerror or exc))
    lines = data.decode("utf-8", "replace").split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    return [Entry(path, n, text[:-1] if text.endswith("\r") else text)
            for n, text in enumerate(lines, 1)]


# --------------------------------------------------------------------- evidence

_FRAGMENT_SPLIT = re.compile(r"(?:\\[A-Za-z0-9]|\\.|[\[\](){}|*+?^$.])|\d+")


def literal_fragments(src):
    """The plain-text runs a pattern must contain, longest first (only ever used for hints)."""
    return sorted({p.strip() for p in _FRAGMENT_SPLIT.split(src) if len(p.strip()) >= 4},
                  key=lambda s: (-len(s), s))


def near_misses(src, entries):
    """Log lines that carry the pattern's own literal words, for a rule that matched nothing.

    Tier 1 is a line that holds one of the pattern's literal runs ("career: lot"), which is what a
    near miss usually looks like - a reworded or renumbered line. If none does, tier 2 settles for a
    line carrying two of the individual words ("career" plus "lot"), which catches a paraphrased
    line without naming every line that says "career". A wrong hint is worse than none, so when
    neither tier holds, the answer is empty and the report says the log carries none of the words.
    """
    def rank(words):
        scored = []
        for i, e in enumerate(entries):
            hits = sum(1 for w in words if w in e.text)
            if hits:
                scored.append((-hits, len(e.text), i, e))
        scored.sort(key=lambda t: (t[0], t[1], t[2]))
        return [(-t[0], t[3]) for t in scored[:MAX_EVIDENCE]]

    frags = literal_fragments(src)
    if not frags:
        return []
    strong = rank(frags)
    if strong:
        return [e for _, e in strong]
    words = sorted({w for f in frags for w in re.split(r"\W+", f) if len(w) >= 3})
    if len(words) < 2:
        return []
    return [e for hits, e in rank(words) if hits >= 2]


def missed(src, entries, reason):
    """Detail text plus evidence for a pattern that found nothing, worded to what was found."""
    ev = near_misses(src, entries)
    return reason + ("; closest lines:" if ev else " - no line of the log carries its words"), ev


# --------------------------------------------------------------------- evaluation

class Checker:
    """The log under test, plus whatever the capture rules have filled in so far."""

    def __init__(self, entries):
        self.entries = entries
        self.captures = {}

    def find(self, src, limit=0):
        """Indices of matching entries, first `limit` only when limit is given."""
        rx = re.compile(src)
        hits = []
        for i, e in enumerate(self.entries):
            if rx.search(e.text):
                hits.append(i)
                if limit and len(hits) >= limit:
                    break
        return hits

    def expand(self, src):
        """Fill {NAME} with captured text, escaped so it matches literally.

        Returns (pattern, None), or (None, reason) when a reference was never captured or the
        expanded pattern does not compile - a failing rule, not a bad spec.
        """
        names = CAP_REF_RE.findall(src)
        missing = sorted({n for n in names if n not in self.captures})
        if missing:
            return None, "%s was never captured - its capture rule matched nothing" % ", ".join(missing)
        out = CAP_REF_RE.sub(lambda m: re.escape(self.captures[m.group(1)]), src)
        try:
            re.compile(out)
        except re.error as exc:
            return None, "/%s/ built from %s does not compile: %s" % (
                out, ", ".join(sorted(set(names))), exc)
        return out, None


def want_text(r):
    if r.count is None:
        return "at least 1"
    if r.count_op == ">=":
        return "at least %d" % r.count
    if r.count_op == "<=":
        return "at most %d" % r.count
    return "exactly %d" % r.count


def count_ok(r, n):
    if r.count is None:
        return n >= 1
    if r.count_op == ">=":
        return n >= r.count
    if r.count_op == "<=":
        return n <= r.count
    return n == r.count


def evaluate(r, ck):
    """Run one rule -> (passed, detail, evidence, capture_filled)."""
    if r.kind in ("expect", "forbid"):
        src, err = ck.expand(r.pats[0])
        if err:
            return False, err, [], None
        hits = ck.find(src)
        if r.kind == "forbid":
            if hits:
                return False, "found %d match(es), wanted none" % len(hits), \
                    [ck.entries[i] for i in hits[:MAX_EVIDENCE]], None
            return True, "", [], None
        if count_ok(r, len(hits)):
            return True, "", [], None
        detail = "found %d match(es), wanted %s" % (len(hits), want_text(r))
        if hits:
            return False, detail, [ck.entries[i] for i in hits[:MAX_EVIDENCE]], None
        detail, ev = missed(src, ck.entries, detail)
        return False, detail, ev, None

    if r.kind == "order":
        srcs = []
        for p in r.pats:
            src, err = ck.expand(p)
            if err:
                return False, err, [], None
            srcs.append(src)
        firsts = [(ck.find(s, 1) or [None])[0] for s in srcs]
        bits = ["/%s/ %s" % (s, ck.entries[i].loc() if i is not None else "not found")
                for s, i in zip(srcs, firsts)]
        evidence = [ck.entries[i] for i in firsts if i is not None]
        if any(i is None for i in firsts):
            missing = [srcs[i] for i, v in enumerate(firsts) if v is None]
            return False, "steps missing from the log: " + ", ".join(
                "/%s/" % m for m in missing) + " - " + ", ".join(bits), evidence, None
        if all(a < b for a, b in zip(firsts, firsts[1:])):
            return True, "", [], None
        return False, "not in order: " + ", ".join(bits), evidence, None

    if r.kind == "capture":
        src, err = ck.expand(r.pats[0])
        if err:
            return False, err, [], None
        hits = ck.find(src, 1)
        if not hits:
            detail, ev = missed(src, ck.entries,
                                "/%s/ matched nothing, so %s has no value" % (src, r.name))
            return False, detail, ev, None
        entry = ck.entries[hits[0]]
        m = re.search(src, entry.text)
        if m is None or len(m.groups()) < 1:  # only reachable when a {NAME} expansion dropped the group
            return False, "/%s/ has no group 1 to capture from %s" % (src, entry.loc()), [entry], None
        return True, "%s=%s" % (r.name, m.group(1)), [], {r.name: m.group(1)}

    if r.kind == "equal":
        if r.name not in ck.captures:
            return False, "%s was never captured" % r.name, [], None
        got = ck.captures[r.name]
        if got == r.value:
            return True, "", [], None
        return False, "%s is '%s', wanted '%s'" % (r.name, got, r.value), [], None

    if r.kind == "range":
        if r.name not in ck.captures:
            return False, "%s was never captured" % r.name, [], None
        got = ck.captures[r.name]
        try:
            num = float(got)
        except ValueError:
            return False, "%s is '%s', which is not a number" % (r.name, got), [], None
        if r.lo <= num <= r.hi:
            return True, "", [], None
        return False, "%s is %s, outside %g..%g" % (r.name, got, r.lo, r.hi), [], None

    if r.kind == "within":
        a, err = ck.expand(r.pats[0])
        if err:
            return False, err, [], None
        b, err = ck.expand(r.pats[1])
        if err:
            return False, err, [], None
        a_hits = ck.find(a, 1)
        if not a_hits:
            detail, ev = missed(a, ck.entries, "/%s/ matched nothing" % a)
            return False, detail, ev, None
        start = a_hits[0]
        after = [i for i in ck.find(b) if i >= start]
        if not after:
            return False, "/%s/ never appears after /%s/ at %s" % (
                b, a, ck.entries[start].loc()), [ck.entries[start]], None
        gap = after[0] - start
        if gap <= r.span:
            return True, "", [], None
        return False, "/%s/ first appears %d line(s) after /%s/ at %s, only %d allowed" % (
            b, gap, a, ck.entries[start].loc(), r.span), \
            [ck.entries[start], ck.entries[after[0]]], None

    return False, "rule kind '%s' is not implemented" % r.kind, [], None


# --------------------------------------------------------------------- driver

def run(rules, entries, ck, out):
    """Evaluate the rules in order, reporting each one as it goes; returns (pass, fail, records)."""
    results = []
    passed = failed = 0
    for i, r in enumerate(rules, 1):
        ok, detail, ev, filled = evaluate(r, ck)
        if filled:
            ck.captures.update(filled)
        passed += 1 if ok else 0
        failed += 0 if ok else 1
        rec = {"index": i, "spec_line": r.line, "kind": r.kind, "rule": r.text,
               "status": "pass" if ok else "fail", "detail": detail,
               "evidence": [{"log": e.log, "line": e.line, "text": e.text} for e in ev]}
        if r.kind == "capture":
            rec["capture"] = {"name": r.name, "value": filled[r.name]} if filled else None
        results.append(rec)
        line = "%-4s spec:%d  %s" % ("PASS" if ok else "FAIL", r.line, r.text)
        if detail:
            line += "  - " + detail
        out.append(line)
        out.extend("      " + e.shown() for e in ev)
    return passed, failed, results


def main(argv):
    ap = argparse.ArgumentParser(
        prog="log_assert.py",
        usage="log_assert.py SPEC_FILE LOG_FILE [LOG_FILE ...]\n"
              "  or: log_assert.py --spec 'rule; rule' LOG_FILE [...] [--json [OUT]]",
        description="check declarative rules over game run logs",
        epilog="the rule grammar is in tools/log_assert.py's module docstring")
    ap.add_argument("args", nargs="*", metavar="ARG",
                    help="SPEC_FILE then LOG_FILE(s), or just the LOG_FILE(s) with --spec")
    ap.add_argument("--spec", metavar="RULES", help="rules inline, separated by ';'")
    ap.add_argument("--json", metavar="OUT", nargs="?", const="-",
                    help="write the results as JSON to OUT (stdout when OUT is left out)")
    try:
        o = ap.parse_args(argv[1:])
    except SystemExit as exc:
        return EXIT_PASS if not exc.code else EXIT_USAGE

    def usage(reason):
        sys.stderr.write("log_assert: %s\n" % reason)
        ap.print_usage(sys.stderr)
        return EXIT_USAGE

    if o.spec is not None:
        spec_src = "--spec"
        pairs = spec_pairs(o.spec, True)
        logs = o.args
        if not logs:
            return usage("no LOG_FILE given")
    else:
        if len(o.args) < 2:
            return usage("expected a SPEC_FILE and at least one LOG_FILE")
        spec_src = o.args[0]
        logs = o.args[1:]
        try:
            with open(spec_src, "rb") as fh:
                raw = fh.read()
        except OSError as exc:
            raise ToolError("cannot read %s: %s" % (spec_src, exc.strerror or exc))
        pairs = spec_pairs(raw.decode("utf-8", "replace"), False)
    rules = parse_spec(pairs)

    entries = []
    for path in logs:
        entries.extend(read_log(path))
    ck = Checker(entries)

    out = []
    passed, failed, results = run(rules, entries, ck, out)
    summary = {"rules": len(rules), "pass": passed, "fail": failed}
    report = {"spec": spec_src,
              "logs": [{"log": p, "lines": sum(1 for e in entries if e.log == p)} for p in logs],
              "summary": summary,
              "results": results}

    if o.json == "-":
        json.dump(report, sys.stdout, indent=1)
        sys.stdout.write("\n")
    else:
        tally = "%d rules, %d pass, %d fail" % (summary["rules"], summary["pass"], summary["fail"])
        if o.json:
            try:
                with open(o.json, "w") as fh:
                    json.dump(report, fh, indent=1)
                    fh.write("\n")
            except OSError as exc:
                raise ToolError("cannot write %s: %s" % (o.json, exc.strerror or exc))
        print("\n".join(out))
        print(tally)
    return EXIT_PASS if failed == 0 else EXIT_FAIL


if __name__ == "__main__":
    try:
        sys.exit(main(sys.argv))
    except ToolError as exc:
        sys.stderr.write("log_assert: %s\n" % exc)
        sys.exit(EXIT_USAGE)
    except SpecError as exc:
        sys.stderr.write("%s\n" % exc)
        sys.exit(EXIT_USAGE)
