#!/usr/bin/env python3
"""Check an OpenUG2 --input-script spec before spending a run on it.

    python3 -I tools/input_script_lint.py SCRIPT_OR_@FILE [...] [--timeline] [--fps 60]
    python3 -I tools/input_script_lint.py --scan DIR        # every *.txt that looks like a script

Each input is either an "@FILE" spec or the inline spec text itself ("10:key=Return,20:quit"),
exactly as --input-script takes it: a bare path is inline text to the game, not a file, and is
refused the same way here (see read_spec).
Output is one finding per line, "name:line: error|warning: message"; --timeline adds one line per
frame that carries events. Exit status: 0 clean, 1 warnings, 2 a spec the game would reject, or a
usage error.

GRAMMAR
    A port of this repository's own parser -- src/input_script.c is_parse()/parse_text()/
    parse_token() and src/input_script.h. It accepts what the parser accepts and refuses the rest
    with the C wording, so a linter message and a start-up log line read the same. Commands are
    separated by ',' or whitespace; '#' starts a comment tail that ends on a newline character and
    on nothing else; a command is "FRAME:what", FRAME a run of ASCII digits; the verbs are key=,
    pad=, click=, fdown=, fup=, tdown=, tup=, shot= and the bare quit (words are strcmp'ed, so
    they are lower case). handoffs/TASK-P-LOG.md tabulates each rejection rule against its line in
    src/input_script.c.

SDL VOCABULARY -- where the accepted names came from
    key= names are matched the way SDL_GetKeyFromName() matches them, pad= names the way
    SDL_GameControllerGetButtonFromString() matches them. Neither list was copied from SDL's
    source: both were measured against libSDL2-2.0.so.0.3200.4 -- SDL 2.32.4, the build this tree
    links (sysroot/usr/include/SDL2/SDL_version.h) -- by calling those two functions from Python
    through ctypes (table lookups, no SDL_Init, no game) and enumerating every key code and scan
    code name they report. Measured behaviour, all reproduced here:
      * key names match case-insensitively: "reTurn", "RETURN" and "return" all mean SDLK_RETURN;
      * "_" is NOT a space to SDL, so input_script.c:71-73 re-tries the name with every "_" as a
        space -- "Keypad_1" works, "Keypad__1" and "Print Screen" do not (and a token can never
        hold a space anyway, the tokenizer would have split it);
      * the name is cut to 63 bytes before lookup (char name[64], input_script.c:45);
      * SDL_GetKeyFromName() then falls back to "this name is exactly one UTF-8 sequence", using
        the decoded value as the key, which is why "a" and a lone 0x80 byte are keys while "ab" is
        not. SDL's decoder is the permissive pre-strict one: overlong forms, surrogates and values
        past U+10FFFF all pass, and only a decode of zero fails -- see _one_codepoint(), which
        reproduces it. One-sequence names are therefore not listed below; _one_codepoint() covers
        them;
      * pad= names are exactly the SDL_CONTROLLER_BUTTON_* strings SDL accepts, case-insensitive;
        "lefttrigger"/"righttrigger" are not buttons and are refused, as is anything else;
      * tdown=/tup= names come from TC_NAMES (input_script.c:13-14), are strcmp'ed so they are
        case-SENSITIVE unlike key names, and a plain number below 64 is accepted as an index.
    The generated block below is re-derived and diffed against that library by
    input_script_lint_test.py, which skips when no libSDL2 is present, so the list stays checkable.

WHAT THE GAME DOES WITH A SCRIPT (why the warnings read the way they do)
    main.c:6824 counts frames up from 0 and main.c:6825 stops the run once the frame counter
    reaches the quit frame; events are pulled by exact frame match at main.c:6875. So events on
    the quit frame itself still fire, events on later frames never do, frames may repeat or be
    listed out of order without being refused, and there is no frame cap: without a quit the run
    only ends when the window closes. A touch index the overlay has no layout for (0..10) parses
    but is dropped by tc_layout (touch_controls.c:39), which is the silent no-op this warns about.

Python 3 standard library only.
"""

import argparse
import os
import re
import struct
import sys

# --------------------------------------------------------------------------- vocabulary

SDL_KEY_NAMES = frozenset((
    'AC Back', 'AC Bookmarks', 'AC Forward', 'AC Home', 'AC Refresh', 'AC Search',
    'AC Stop', 'Again', 'AltErase', 'App1', 'App2', 'Application',
    'AudioFastForward', 'AudioMute', 'AudioNext', 'AudioPlay', 'AudioPrev', 'AudioRewind',
    'AudioStop', 'Backspace', 'BrightnessDown', 'BrightnessUp', 'Calculator', 'Call',
    'Cancel', 'CapsLock', 'Clear', 'Clear / Again', 'Computer', 'Copy',
    'CrSel', 'CurrencySubUnit', 'CurrencyUnit', 'Cut', 'DecimalSeparator', 'Delete',
    'DisplaySwitch', 'Down', 'Eject', 'End', 'EndCall', 'Escape',
    'ExSel', 'Execute', 'F1', 'F10', 'F11', 'F12',
    'F13', 'F14', 'F15', 'F16', 'F17', 'F18',
    'F19', 'F2', 'F20', 'F21', 'F22', 'F23',
    'F24', 'F3', 'F4', 'F5', 'F6', 'F7',
    'F8', 'F9', 'Find', 'Help', 'Home', 'Insert',
    'KBDIllumDown', 'KBDIllumToggle', 'KBDIllumUp', 'Keypad !', 'Keypad #', 'Keypad %',
    'Keypad &', 'Keypad &&', 'Keypad (', 'Keypad )', 'Keypad *', 'Keypad +',
    'Keypad +/-', 'Keypad ,', 'Keypad -', 'Keypad .', 'Keypad /', 'Keypad 0',
    'Keypad 00', 'Keypad 000', 'Keypad 1', 'Keypad 2', 'Keypad 3', 'Keypad 4',
    'Keypad 5', 'Keypad 6', 'Keypad 7', 'Keypad 8', 'Keypad 9', 'Keypad :',
    'Keypad <', 'Keypad =', 'Keypad = (AS400)', 'Keypad >', 'Keypad @', 'Keypad A',
    'Keypad B', 'Keypad Backspace', 'Keypad Binary', 'Keypad C', 'Keypad Clear', 'Keypad ClearEntry',
    'Keypad D', 'Keypad Decimal', 'Keypad E', 'Keypad Enter', 'Keypad F', 'Keypad Hexadecimal',
    'Keypad MemAdd', 'Keypad MemClear', 'Keypad MemDivide', 'Keypad MemMultiply', 'Keypad MemRecall', 'Keypad MemStore',
    'Keypad MemSubtract', 'Keypad Octal', 'Keypad Space', 'Keypad Tab', 'Keypad XOR', 'Keypad ^',
    'Keypad {', 'Keypad |', 'Keypad ||', 'Keypad }', 'Left', 'Left Alt',
    'Left Ctrl', 'Left GUI', 'Left Shift', 'Mail', 'MediaSelect', 'Menu',
    'ModeSwitch', 'Mute', 'NumLock', 'Numlock', 'Oper', 'Out',
    'PageDown', 'PageUp', 'Paste', 'Pause', 'Power', 'PrintScreen',
    'Prior', 'Return', 'Right', 'Right Alt', 'Right Ctrl', 'Right GUI',
    'Right Shift', 'ScrollLock', 'Select', 'Separator', 'Sleep', 'SoftLeft',
    'SoftRight', 'Space', 'Stop', 'SysReq', 'Tab', 'ThousandsSeparator',
    'Undo', 'Up', 'VolumeDown', 'VolumeUp', 'WWW',
))

SDL_PAD_BUTTON_NAMES = frozenset((
    "a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder",
    "rightshoulder", "dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2",
    "paddle3", "paddle4", "touchpad",
))

# TC_NAMES, src/input_script.c:13-14, in TcButton order (src/touch_controls.h:22-23).
TOUCH_CONTROLS = ("steer_l", "steer_r", "gas", "brake", "nos", "handbrake", "camera", "pause",
                  "shift_up", "shift_down", "enter_race")

# --------------------------------------------------------------------------- constants

MAX_ITEMS = 512          # IS_MAX_ITEMS, src/input_script.h
PATH_MAX = 192           # IS_PATH_MAX, src/input_script.h
FILE_MAX = 1 << 20       # is_parse(): "script file '%s' too large or unreadable"
KEY_NAME_BYTES = 63      # snprintf into char name[64], src/input_script.c:45
TC_MAX_ID = 64           # tc_button(): a number below 64 parses, src/input_script.c:38
LONG_MAX = 2 ** 63 - 1   # parse_long() ignores strtol's ERANGE, so long digit runs saturate
DELIMS = ", \t\r\n"      # parse_text() splits tokens on these and only these


class ParseError(Exception):
    """A spec the game refuses at start-up; the message text is is_parse()'s."""

    def __init__(self, message):
        Exception.__init__(self, message)
        self.line = None


class Warning(object):
    """A script the parser accepts but that probably does not do what it looks like it does."""

    __slots__ = ("line", "message")

    def __init__(self, line, message):
        self.line = line
        self.message = message

    def __str__(self):
        return "%d: warning: %s" % (self.line, self.message)


class Event(object):
    """One parsed command, carrying what the linter needs."""

    __slots__ = ("frame", "kind", "value", "what", "line", "a")

    def __init__(self, frame, kind, value, what, line=1, a=0):
        self.frame = frame    # long, as parsed
        self.kind = kind      # key pad click fdown fup tdown tup shot quit
        self.value = value    # text after the '=', "" for quit
        self.what = what      # "what" of FRAME:what as written, which is what --timeline prints
        self.line = line      # 1-based line the token started on
        self.a = a            # tdown/tup: the resolved TcButton index

    def __repr__(self):
        return "Event(%d, %s, %r)" % (self.frame, self.kind, self.value)


# --------------------------------------------------------------------------- numbers

_FLOAT_RE = re.compile(r"""[-+]?(?:
      0[xX](?:[0-9a-fA-F]+(?:\.[0-9a-fA-F]*)?|\.[0-9a-fA-F]+)(?:[pP][-+]?[0-9]+)?    # glibc strtod takes hex
    | (?:[0-9]+(?:\.[0-9]*)?|\.[0-9]+)(?:[eE][-+]?[0-9]+)?                           # decimal
    | [iI][nN][fF](?:[iI][nN][iI][tT][yY])?                                          # inf, infinity
    | [nN][aA][nN](?:\([0-9A-Za-z]*\))?                                              # nan, nan(chars)
)""", re.VERBOSE)


def _f32(value):
    """input_script.c:26 stores the strtod double into a float, and that rounding decides 0..1."""
    if value != value:
        return value
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except OverflowError:
        return float("inf") if value > 0 else float("-inf")


def _parse_long(text, at=0):
    """parse_long(), src/input_script.c:16: digits from here, as strtol base 10 would read them."""
    if at >= len(text) or not ("0" <= text[at] <= "9"):
        return None
    end = at
    while end < len(text) and "0" <= text[end] <= "9":
        end += 1
    value = int(text[at:end])
    return (LONG_MAX if value > LONG_MAX else value), end


def _parse_float(text, at=0):
    """parse_float(), src/input_script.c:26: a maximal strtod prefix, not float(), which differs."""
    match = _FLOAT_RE.match(text, at)
    if not match:
        return None
    token = match.group(0)
    magnitude = token.lower().lstrip("+-")
    try:
        if magnitude.startswith("0x"):
            value = float.fromhex(token)
        elif magnitude.startswith("nan"):
            value = float("nan")
        elif magnitude.startswith("inf"):
            value = float("inf")
        else:
            value = float(token)
    except ValueError:
        return None
    if token[0] == "-" and value != value:
        return None  # float("nan") keeps no sign C would have to model
    if token[0] == "-" and value > 0:
        value = -value
    return _f32(value), match.end()


def _out_of_unit(value):
    """C's `x < 0 || x > 1`, which is false for NaN: that is how a NaN gets past the range check."""
    return value < 0 or value > 1


# --------------------------------------------------------------------------- names

def _fold(text):
    """SDL_strcasecmp() folds ASCII bytes only: 'A'..'Z' go to 'a'..'z', everything else is
    compared as it is. str.lower() is not the same thing -- it also folds non-ASCII letters."""
    return "".join(chr(ord(ch) + 32) if "A" <= ch <= "Z" else ch for ch in text)


_KEY_LOOKUP = frozenset(_fold(name) for name in SDL_KEY_NAMES)
_PAD_LOOKUP = frozenset(SDL_PAD_BUTTON_NAMES)


def _one_codepoint(name):
    """SDL_GetKeyFromName()'s last resort: the name is exactly one UTF-8 sequence.

    SDL tries its name table first; on a miss it accepts a name that is a single UTF-8 sequence
    and uses the decoded code point as the keycode. input_script.c only compares the result
    against SDLK_UNKNOWN (0), so what the game calls "a known key name" here is "this sequence
    decodes to something non-zero". Rules measured on libSDL2 2.32.4 -- every lead byte 0x01..0xFF
    against 0..4 continuation bytes, plus the keycodes returned -- because that decoder is not the
    modern UTF-8 rule and guessing it from what UTF-8 "should" do gets both of these wrong:
      * overlong forms are accepted ("\\xc0\\xaf" decodes to '/'), as are surrogates and code
        points past U+10FFFF. Only a decode of zero is refused -- and "\\xc0\\x80", "\\xe0\\x80\\x80"
        and "\\xf8\\x80\\x80\\x80" are refused for exactly that reason, not because they are ill
        formed. (A decoded NUL is also where the C string would have ended.)
      * there is no five-byte path: leads 0xF8..0xFF are taken as four-byte leads, masking only
        three bits, which is why "\\xf9\\x80\\x80\\x80" is a key name and "\\xf8\\x80\\x80\\x80" is not.
    """
    if not name:
        return False
    lead = name[0]
    if lead < 0xC0:
        # A byte below 0xC0 is a sequence of one, and it decodes to itself -- non-zero, since a
        # NUL cannot survive the read in the first place. A lone continuation byte counts too.
        return len(name) == 1 and lead != 0
    need = 1 if lead < 0xE0 else (2 if lead < 0xF0 else 3)
    if len(name) != need + 1 or any(not (0x80 <= byte <= 0xBF) for byte in name[1:]):
        return False
    value = lead & (0x1F, 0x0F, 0x07)[need - 1]
    for byte in name[1:]:
        value = (value << 6) | (byte & 0x3F)
    return value != 0


def _key_name_known(value):
    """input_script.c:66-75: cut to name[64], look it up, then re-try with '_' read as ' '."""
    name = value.encode("utf-8", "surrogateescape")[:KEY_NAME_BYTES]
    text = name.decode("utf-8", "surrogateescape")
    if _fold(text) in _KEY_LOOKUP or _one_codepoint(name):
        return True
    text = text.replace("_", " ")
    return _fold(text) in _KEY_LOOKUP or _one_codepoint(text.encode("utf-8", "surrogateescape"))


def _touch_button(value):
    """tc_button(), src/input_script.c:36-43: numbers below 64 first, then strcmp over TC_NAMES."""
    number = _parse_long(value)
    if number and number[1] == len(value):
        return number[0] if number[0] < TC_MAX_ID else None
    return TOUCH_CONTROLS.index(value) if value in TOUCH_CONTROLS else None


# --------------------------------------------------------------------------- parser

def _tokens(text):
    """parse_text(), src/input_script.c:122-136: yields (token, line).

    The comment tail ends on '\n' and on nothing else, so a file with bare-CR line endings has the
    rest of the file commented out. '#' inside a token is ordinary text, which is why
    10:shot=a#b.png is a legal path.
    """
    pos, size, line, comment = 0, len(text), 1, False
    while pos < size:
        char = text[pos]
        if comment:
            pos += 1
            if char == "\n":
                comment, line = False, line + 1
            continue
        if char == "#":
            comment, pos = True, pos + 1
            continue
        if char in DELIMS:
            pos += 1
            if char == "\n":
                line += 1
            continue
        start = pos
        while pos < size and text[pos] not in DELIMS:
            pos += 1
        yield text[start:pos], line


def _rest(text, at):
    """The character at `at`, or the NUL that C finds at the end of the string."""
    return text[at] if at < len(text) else ""


def _parse_token(token, count):
    """parse_token(), src/input_script.c:46-119. Raises ParseError carrying C's message."""
    if count >= MAX_ITEMS:
        raise ParseError("too many commands (max %d)" % MAX_ITEMS)
    head = _parse_long(token)
    if head is None or _rest(token, head[1]) != ":":
        raise ParseError("bad command '%s' (expected FRAME:what)" % token)
    frame, body = head[0], token[head[1] + 1:]

    if body == "quit":
        return Event(frame, "quit", "", body)
    equals = body.find("=")
    if equals < 0:
        raise ParseError("bad command '%s' (missing '=')" % token)
    command, value = body[:equals], body[equals + 1:]
    if not value:
        # C splits the token in place before reporting, so the '=' is not in the message.
        raise ParseError("empty value in '%s'" % token[:token.index("=")])

    if command == "key":
        if not _key_name_known(value):
            raise ParseError("unknown key name '%s'" % value)
        return Event(frame, "key", value, body)

    if command == "pad":
        if _fold(value) not in _PAD_LOOKUP:
            raise ParseError("unknown pad button '%s'" % value)
        return Event(frame, "pad", value, body)

    if command == "click":
        first = _parse_float(value, 0)
        second = None
        if first is not None and _rest(value, first[1]) == ":":
            second = _parse_float(value, first[1] + 1)
        if first is None or second is None or _rest(value, second[1]) != "":
            raise ParseError("bad click position '%s' (expected X:Y)" % value)
        if _out_of_unit(first[0]) or _out_of_unit(second[0]):
            raise ParseError("click position '%s' out of 0..1" % value)
        return Event(frame, "click", value, body)

    if command == "fdown":
        finger = _parse_long(value)
        first = second = None
        if finger is not None and _rest(value, finger[1]) == ":":
            first = _parse_float(value, finger[1] + 1)
            if first is not None and _rest(value, first[1]) == ":":
                second = _parse_float(value, first[1] + 1)
        if finger is None or first is None or second is None or _rest(value, second[1]) != "":
            raise ParseError("bad fdown '%s' (expected ID:X:Y)" % value)
        if _out_of_unit(first[0]) or _out_of_unit(second[0]):
            raise ParseError("fdown position '%s' out of 0..1" % value)
        return Event(frame, "fdown", value, body)

    if command == "fup":
        finger = _parse_long(value)
        if finger is None or _rest(value, finger[1]) != "":
            raise ParseError("bad fup id '%s'" % value)
        return Event(frame, "fup", value, body)

    if command in ("tdown", "tup"):
        button = _touch_button(value)
        if button is None:
            raise ParseError("unknown touch button '%s'" % value)
        return Event(frame, command, value, body, a=button)

    if command == "shot":
        if len(value.encode("utf-8", "surrogateescape")) >= PATH_MAX:
            raise ParseError("shot path too long: '%s'" % value)
        return Event(frame, "shot", value, body)

    raise ParseError("unknown command '%s'" % command)


def parse_script(text):
    """is_parse() over script text already read in. Returns the events, or raises ParseError."""
    events = []
    for token, line in _tokens(text):
        try:
            event = _parse_token(token, len(events))
        except ParseError as error:
            error.line = line
            raise
        event.line = line
        events.append(event)
    if not events:
        raise ParseError("input script has no commands")
    return events


# --------------------------------------------------------------------------- warnings

def lint(events):
    """Warnings for a script is_parse() accepted: the run starts and then quietly misbehaves."""
    found = []

    previous = None
    for event in events:
        if previous is not None and event.frame < previous:
            found.append(Warning(event.line, "frames not ascending: frame %d is listed after frame %d"
                                            % (event.frame, previous)))
        previous = event.frame

    pressed = {}
    for event in events:
        if event.kind == "tdown":
            pressed.setdefault(event.a, []).append(event)
        elif event.kind == "tup":
            if pressed.get(event.a):
                pressed[event.a].pop(0)
            else:
                found.append(Warning(event.line, "tup=%s at frame %d releases nothing: no tdown=%s "
                                                 "comes before it" % (event.value, event.frame, event.value)))
    for waiting in pressed.values():
        for event in waiting:
            found.append(Warning(event.line, "tdown=%s at frame %d is never released: no tup=%s "
                                             "comes after it" % (event.value, event.frame, event.value)))

    for event in events:
        if event.kind in ("tdown", "tup") and event.a >= len(TOUCH_CONTROLS) and event.value.isdigit():
            found.append(Warning(event.line, "%s=%s parses, but only touch controls 0..%d have a "
                                             "layout (touch_controls.c:39), so the game ignores it"
                                 % (event.kind, event.value, len(TOUCH_CONTROLS) - 1)))

    shots = {}
    for event in events:
        if event.kind != "shot":
            continue
        if event.value in shots:
            found.append(Warning(event.line, "shot=%s writes the same file as the shot at frame %d "
                                             "(line %d): the earlier one is overwritten"
                                 % (event.value, shots[event.value].frame, shots[event.value].line)))
        else:
            shots[event.value] = event

    quits = [event.frame for event in events if event.kind == "quit"]
    if not quits:
        found.append(Warning(events[0].line, "no quit event: a scripted run has no frame cap, so it "
                                             "only ends when the window closes (main.c:6825)"))
    else:
        stop = min(quits)
        for event in events:
            if event.frame > stop:
                found.append(Warning(event.line, "frame %d is never reached: the run quits at frame %d"
                                            % (event.frame, stop)))

    found.sort(key=lambda warn: warn.line)
    return found


def timeline(events, fps):
    """One line per frame that carries events, with the time that frame lands at."""
    order = sorted(range(len(events)), key=lambda index: (events[index].frame, index))
    lines, at = [], 0
    while at < len(order):
        frame = events[order[at]].frame
        spelled = []
        while at < len(order) and events[order[at]].frame == frame:
            spelled.append(events[order[at]].what)
            at += 1
        lines.append("%5d  %.2f s  %s" % (frame, frame / fps, ", ".join(spelled)))
    return lines


# --------------------------------------------------------------------------- reading input

def read_spec(argument):
    """Returns (text, display_name, spec_was_empty).

    Only a leading "@" makes it a file, exactly as it does to the game (input_script.c:144 tests
    spec[0] and nothing else): a bare path is not a file to is_parse(), it is an inline script
    whose first token is a path, and it gets the same "bad command ... (expected FRAME:what)" the
    game gives it. A linter that quietly read the file instead would hand back a clean verdict for
    the very argument the game refuses, which is the failure this tool exists to catch. main()
    notices that case and says "did you mean @path" on stderr; the verdict itself stays C's.

    An empty inline spec is refused before anything is opened, like is_parse(). Files are read as
    bytes and cut at the first NUL, because C reads them in binary and ends the string there;
    nothing is decoded, so shot paths survive intact.
    """
    if not argument.startswith("@"):
        return argument, "inline", not argument
    path = argument[1:]
    try:
        with open(path, "rb") as handle:
            data = handle.read(FILE_MAX + 1)
    except (IOError, OSError):
        # A directory gets here too: fopen() would have succeeded and the short read would
        # have left C parsing an uninitialised byte (input_script.c:155-157).
        raise ParseError("cannot open script file '%s'" % path)
    if len(data) > FILE_MAX:
        raise ParseError("script file '%s' too large or unreadable" % path)
    return data.split(b"\x00", 1)[0].decode("utf-8", "surrogateescape"), path, False


# --------------------------------------------------------------------------- --scan

_SCRIPT_LINE = re.compile(r"^[0-9]+:")


def looks_like_script(text):
    """(--scan only) Whether a .txt is an input script; returns (bool, reason for saying no).

    is_parse() has no such notion, it just rejects whatever it is handed, so this is a heuristic
    with a rule of its own: at least 80% of the file's token-bearing lines have to be made only
    of "FRAME:..." tokens. reports/ is full of gate logs and prose with "4201: ..." lines in
    them, and reporting one of those as a broken script would be pure noise.

    80% is where that corpus separates: measured over the 419 .txt files under reports/ on
    2026-11-16, every real script was 100% script-shaped and every log or notes file far below
    it. An earlier version also required a token naming a known command; over the same files
    that selected exactly the same 63 scripts, so it bought nothing while making the tool skip a
    file whose one and only command has a typo in the verb -- the sort of file a linter most
    needs to report. The verb test was therefore dropped.
    """
    lines = {}
    for token, line in _tokens(text):
        lines.setdefault(line, []).append(token)
    if not lines:
        return False, "no commands"
    shaped = sum(1 for tokens in lines.values() if all(_SCRIPT_LINE.match(t) for t in tokens))
    if shaped * 5 < len(lines) * 4:
        return False, "only %d of %d token-bearing lines look like commands" % (shaped, len(lines))
    return True, ""


def scan_dir(directory):
    paths = []
    for root, _dirs, files in os.walk(directory):
        for name in sorted(files):
            if name.endswith(".txt"):
                paths.append(os.path.join(root, name))
    return sorted(paths)


# --------------------------------------------------------------------------- driver

def lint_text(display, text, spec_was_empty, fps, out, want_timeline):
    """Report one script. Returns (parse_error, warning_count, events)."""
    if spec_was_empty:
        out.write("%s:1: error: empty input script\n" % display)
        return True, 0, []
    try:
        events = parse_script(text)
    except ParseError as error:
        out.write("%s:%s: error: %s\n" % (display, error.line or 1, error))
        return True, 0, []
    found = lint(events)
    for warn in found:
        out.write("%s:%d: warning: %s\n" % (display, warn.line, warn.message))
    if want_timeline:
        out.write("# timeline for %s at %g fps\n" % (display, fps))
        for line in timeline(events, fps):
            out.write(line + "\n")
    out.flush()
    return False, len(found), events


def check(argument, fps, out, want_timeline):
    try:
        text, display, empty = read_spec(argument)
    except ParseError as error:
        out.write("%s:1: error: %s\n" % (argument, error))
        out.flush()
        return True, 0
    failed, count, _events = lint_text(display, text, empty, fps, out, want_timeline)
    return failed, count


def scan_tree(directory, fps, out, want_timeline):
    """Lint every .txt under directory that looks like a script. Returns the exit status."""
    found = errors = warned = with_warnings = 0
    for path in scan_dir(directory):
        try:
            text, _display, _empty = read_spec("@" + path)
        except ParseError as error:
            out.write("%s:1: error: %s\n" % (path, error))
            errors += 1
            continue
        if not looks_like_script(text)[0]:
            continue
        found += 1
        failed, count, events = lint_text(path, text, False, fps, out, want_timeline)
        errors += failed
        warned += count
        with_warnings += 1 if count else 0
    out.write("scan: %d of %d .txt file(s) under %s looked like input scripts: %d parse error(s), "
              "%d with warnings, %d warning(s) in total\n"
              % (found, len(scan_dir(directory)), directory, errors, with_warnings, warned))
    out.flush()
    return 2 if errors else (1 if warned else 0)


def main(argv=None):
    parser = argparse.ArgumentParser(
        prog="input_script_lint.py",
        description="Check an OpenUG2 --input-script spec before spending a run on it.",
        epilog="exit 0 clean, 1 warnings, 2 parse error (what the game would reject) or usage error")
    parser.add_argument("scripts", nargs="*", metavar="SCRIPT_OR_@FILE")
    parser.add_argument("--timeline", action="store_true", help="print one line per frame with events")
    parser.add_argument("--fps", type=float, default=60.0, metavar="N",
                        help="frame rate for --timeline (default 60)")
    parser.add_argument("--scan", metavar="DIR", help="lint every *.txt under DIR that looks like a script")
    args = parser.parse_args(argv)

    if args.scan and args.scripts:
        sys.stderr.write("input_script_lint.py: --scan takes a directory, not script arguments\n")
        return 2
    if args.scan and not os.path.isdir(args.scan):
        sys.stderr.write("input_script_lint.py: %s is not a directory\n" % args.scan)
        return 2
    if not args.scan and not args.scripts:
        sys.stderr.write("input_script_lint.py: no script given (try --help)\n")
        return 2
    if not (args.fps > 0):
        sys.stderr.write("input_script_lint.py: --fps must be above zero\n")
        return 2

    if args.scan:
        return scan_tree(args.scan, args.fps, sys.stdout, args.timeline)

    errors = warned = 0
    for argument in args.scripts:
        failed, count = check(argument, args.fps, sys.stdout, args.timeline)
        if not argument.startswith("@") and os.path.isfile(argument):
            # The verdict above is the game's: a bare path is inline text to is_parse(), so the
            # file was not read. Say so, or the error looks like a puzzle.
            sys.stderr.write("input_script_lint.py: %s is a file on disk, but the game reads a "
                             "spec only when it starts with '@'; pass @%s to check its contents\n"
                             % (argument, argument))
        errors += failed
        warned += count
    return 2 if errors else (1 if warned else 0)


if __name__ == "__main__":
    sys.exit(main())
