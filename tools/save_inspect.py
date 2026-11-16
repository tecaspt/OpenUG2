#!/usr/bin/env python3
"""Decode an OpenUG2 save file the way the game reads it, and say why it would refuse it.

The app writes its own small binary formats into its user-data folder. This tool reads any of
them back with the game's own rules, so a file sent by an owner can be diagnosed without running
the game. Formats are identified by the magic the reader checks, never by file name.

Reader ports, in the order the bytes are touched:

  src/settings.c   st_checksum / st_container_parse  -> the shared "20CM" container (3.2/3.4/3.5)
  src/career.c     cr_serialize / cr_parse / cr_save_file / cr_load_file (CR_MAGIC, CR_SCHEMA,
                   TLV 1 CORE, 2 RACES, 3 CAR, 4 POSE, 5 OWNED; schema 1 migrates, schema > 2 is
                   incompatible; unknown TLVs are kept in extra[] and rewritten)
  src/garage.c     gs_parse / gs_decode / garage_store_get / garage_store_put (GARAGE_FILE_NAME,
                   one 120-byte record per car, unknown records preserved by the writer)
  src/settings.c   st_profile_parse / st_profile_unpack_payload (the 54 930-byte profile payload;
                   everything the typed fields do not cover stays in StProfile.raw)
  src/fe_profile.c fep_create / fep_refresh / fep_load -> where profiles live and which names the
                   game will open at all (st_profile_name_valid)
  src/settings.c   st_reg_open_ini / ini_read / ini_parse_line / ini_name_valid -> the settings.ini
                   value store, and st_install_read / k_gfx_names -> the names the game reads
  src/controls_store.c cs_kb_load / cs_kb_save -> the keyboard table lives in that same INI as
                   "ControlsKeyboard" (CB_KB_ENTRIES scancodes); controls_store has no file of its
                   own, and the joystick records live in the profile's six input slots above
  src/camera.c     cam_settings_load -> the camera.cfg option file the gate folders also carry

Output per file: kind, schema/version, the verdict the game would reach (OK, OK with the schema-1
migration, or the refusal with a byte offset), then every field the C reader fills in, named after
the C struct members. Records the game preserves without reading them are shown as hex with their
tag and length.

  python3 -I tools/save_inspect.py FILE [FILE ...] [--json OUT.json]
  python3 -I tools/save_inspect.py --dir USERDATA_DIR [--json OUT.json]
  python3 -I tools/save_inspect.py FILE --car 240SX      # also answer garage_store_get for a car

--dir walks a folder and reports every save file in it. The layout the C code builds paths with is
<data home>/OpenUG2/NFS Underground 2/<profile>/<profile>, <profile>/openug2_career.bin and
<profile>/openug2_garage.bin, with settings.ini at <config home>/OpenUG2/settings.ini and
camera.cfg at <config home>/openug2/camera.cfg (src/settings.c:656-674, src/camera.c:92-99);
walking from any parent of that, including the XDG home itself, works - paths are reported
relative to the folder given. Files no OpenUG2 reader claims are reported and skipped, which does
not fail a --dir run; naming such a file on the command line does (you asked us to read it).

Exit code: 0 = every file was readable by the game, 1 = at least one file it would refuse (or an
unrecognisable file named on the command line), 2 = usage error.

Deliberate differences from the game, all toward a more useful report: the game's readers stop at
the first refusal, so this tool reports the first one too but names the field and the byte offset
(it has to - the C codes carry neither); a profile whose block A is longer than its own 8-byte
header is accepted by the game (settings.c:252 only checks the first 8 bytes) and is reported with
its extra header bytes; files larger than 8 MiB are refused by this tool before parsing (a tool
limit, marked as such) although the game streams profiles of any size; and when a 20CM container
carries a block A magic none of the three readers recognises, the *file name* is used for nothing
but naming the reader the game would have used (openug2_career.bin / openug2_garage.bin), so the
report still says which C check fails - it never identifies a format by name when its magic says
otherwise.

Three more identification rules, because "by magic" alone cannot see real damage:
  * a file whose own size fields add up to its length is treated as a container even when its
    "20CM" magic is broken - rot at the head of a save is exactly that shape, and container_parse
    then names the damage instead of the tool calling the file foreign;
  * the profile format has no magic at all, only a version and a payload size in block A, so a
    build that bumped ST_PROFILE_VERSION is invisible to a magic test. The other byte-level tell
    the game itself checks (st_profile_parse wants 54 930 bytes of block B behind a plain 8-byte
    block A) identifies it, and the report can then say ST_E_INCOMPATIBLE for the version;
  * a NUL byte does not make a file "not settings.ini": ini_read reads byte by byte and answers the
    first NUL with ST_E_CORRUPT for the whole store, so the INI shape test looks through NULs and
    lets ini_report name the damage.
"""

from __future__ import annotations

import argparse
import json
import os
import struct
import sys

# ---------------------------------------------------------------- status codes (settings.h)
ST_OK = 0
ST_E_NOTFOUND = -1
ST_E_IO = -2
ST_E_CORRUPT = -3
ST_E_INCOMPATIBLE = -4
ST_E_ARG = -5
STATUS_NAME = {
    ST_OK: "ST_OK",
    ST_E_NOTFOUND: "ST_E_NOTFOUND",
    ST_E_IO: "ST_E_IO",
    ST_E_CORRUPT: "ST_E_CORRUPT",
    ST_E_INCOMPATIBLE: "ST_E_INCOMPATIBLE",
    ST_E_ARG: "ST_E_ARG",
}

# ------------------------------------------------------------- 3.2 container / 3.4 checksum
CONTAINER_HEADER = 28
CONTAINER_MAGIC = b"20CM"
CRC_POLY = 0x04C11DB7          # src/settings.c:crc_init - big-endian CRC, no final reflection


def _crc_table():
    tab = []
    for i in range(256):
        r = i << 24
        for _ in range(8):
            r = ((r << 1) ^ CRC_POLY) if r & 0x80000000 else (r << 1)
            r &= 0xFFFFFFFF
        tab.append(r)
    return tab


_CRC_TAB = _crc_table()


def st_checksum(data: bytes) -> int:
    """st_checksum: bytes 0..3 are XORed into the register inverted, the rest is table-fed."""
    n = len(data)
    if n < 4:
        return 0
    r = (~((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3])) & 0xFFFFFFFF
    for byte in data[4:]:
        r = (((r << 8) & 0xFFFFFFFF) | byte) ^ _CRC_TAB[r >> 24]
    return (~r) & 0xFFFFFFFF


def rd16(b: bytes, i: int) -> int:
    return b[i] | (b[i + 1] << 8)


def rd32(b: bytes, i: int) -> int:
    return rd16(b, i) | (rd16(b, i + 2) << 16)


def rd64(b: bytes, i: int) -> int:
    return rd32(b, i) | (rd32(b, i + 4) << 32)


def rd_i64(b: bytes, i: int) -> int:
    v = rd64(b, i)
    return v - (1 << 64) if v >= (1 << 63) else v


def rd_f32(b: bytes, i: int) -> float:
    return struct.unpack_from("<f", b, i)[0]


def wr32(b: bytearray, i: int, v: int) -> None:
    b[i:i + 4] = (v & 0xFFFFFFFF).to_bytes(4, "little")


def container_build(a: bytes, blk_b: bytes) -> bytes:
    """st_container_build - used by the tests to write files the way cr_save_file does."""
    total = CONTAINER_HEADER + len(a) + len(blk_b)
    out = bytearray(total)
    out[0:4] = CONTAINER_MAGIC
    wr32(out, 4, total)
    wr32(out, 8, len(a))
    wr32(out, 12, len(blk_b))
    wr32(out, 16, st_checksum(a))
    wr32(out, 20, st_checksum(blk_b))
    wr32(out, 24, st_checksum(bytes(out[:24])))
    out[CONTAINER_HEADER:CONTAINER_HEADER + len(a)] = a
    out[CONTAINER_HEADER + len(a):] = blk_b
    assert len(out) == total
    return bytes(out)


class Refused(Exception):
    """A refusal the game would reach, with the offset it happened at (the C codes return only a
    status; the offset is this tool's addition, and it is the point of the exercise)."""

    def __init__(self, code: int, reason: str, offset: int | None = None):
        super().__init__(reason)
        self.code = code
        self.reason = reason
        self.offset = offset


class Container:
    """StContainerView over a whole buffer."""

    __slots__ = ("a", "b", "size_a", "size_b")

    def __init__(self, a: bytes, b: bytes):
        self.a, self.b = a, b
        self.size_a, self.size_b = len(a), len(b)


def container_parse(buf: bytes) -> Container:
    """st_container_parse, steps 2..5 of SPEC 3.5, in that order."""
    length = len(buf)
    if length < CONTAINER_HEADER:                       # 3.5 step 2: short file
        raise Refused(ST_E_IO, "file shorter than the %d-byte container header" % CONTAINER_HEADER,
                      length)
    total = rd32(buf, 4)
    if total != length or buf[0:4] != CONTAINER_MAGIC or rd32(buf, 24) != st_checksum(buf[:24]):
        want = st_checksum(buf[:24])
        if buf[0:4] != CONTAINER_MAGIC:
            raise Refused(ST_E_CORRUPT, "container magic is %r, not %r" % (buf[0:4], CONTAINER_MAGIC), 0)
        if total != length:
            raise Refused(ST_E_CORRUPT,
                          "header size %d does not match the %d-byte file" % (total, length), 4)
        raise Refused(ST_E_CORRUPT,
                      "header checksum is 0x%08x, expected 0x%08x" % (rd32(buf, 24), want), 24)
    size_a, size_b = rd32(buf, 8), rd32(buf, 12)
    if size_a + size_b > length - CONTAINER_HEADER:
        raise Refused(ST_E_IO, "blocks A(%d)+B(%d) do not fit in the file" % (size_a, size_b), 8)
    a = buf[CONTAINER_HEADER:CONTAINER_HEADER + size_a]
    b = buf[CONTAINER_HEADER + size_a:CONTAINER_HEADER + size_a + size_b]
    if st_checksum(a) != rd32(buf, 16):
        raise Refused(ST_E_CORRUPT, "block A checksum is 0x%08x, expected 0x%08x"
                      % (rd32(buf, 16), st_checksum(a)), 16)
    if st_checksum(b) != rd32(buf, 20):
        raise Refused(ST_E_CORRUPT, "block B checksum is 0x%08x, expected 0x%08x"
                      % (rd32(buf, 20), st_checksum(b)), 20)
    return Container(a, b)


# ------------------------------------------------------------------------ report container
HEX_RUN = 32          # bytes of an opaque run shown before the count
HEX_RECORD = 256      # bytes of a preserved record shown inline before the count


def hexstr(data: bytes, limit: int = HEX_RUN) -> str:
    """Hex with the byte count in front; anything past `limit` is counted, never hidden."""
    body = " ".join("%02x" % x for x in data[:limit])
    if len(data) > limit:
        body += " ... (%d more bytes)" % (len(data) - limit)
    return "%d bytes: %s" % (len(data), body)


class Report:
    """One file: what it is, whether the game would take it, and the fields the reader fills in."""

    def __init__(self, path: str, kind: str = "unknown", label: str = ""):
        self.path = path
        self.kind = kind
        self.label = label or kind
        self.schema: int | str | None = None
        self.status = ST_OK
        self.verdict = "OK"
        self.reason: str | None = None
        self.offset: int | None = None
        self.fields: dict = {}
        self.unknown: list[dict] = []
        self.notes: list[str] = []
        self.skipped = False
        self.fatal = False

    def refuse(self, exc: Refused) -> "Report":
        self.status = exc.code
        self.reason = exc.reason
        self.offset = exc.offset
        at = "" if exc.offset is None else " at byte offset %d" % exc.offset
        self.verdict = "REFUSED (%s): %s%s" % (STATUS_NAME[exc.code], exc.reason, at)
        return self

    def skip(self, note: str, fatal: bool = False) -> "Report":
        """No OpenUG2 reader claims this file. Naming one is a failure; finding one in a --dir
        walk is not (a user-data folder legitimately holds other files)."""
        self.skipped = True
        self.fatal = fatal
        self.verdict = "not an OpenUG2 save file (%s)" % note
        self.notes.append(note)
        return self

    @property
    def ok(self) -> bool:
        return self.status == ST_OK

    def to_json(self) -> dict:
        return {
            "path": self.path,
            "kind": self.kind,
            "label": self.label,
            "schema": self.schema,
            "status": STATUS_NAME[self.status],
            "code": self.status,
            "ok": self.ok and not self.skipped,
            "skipped": self.skipped,
            "verdict": self.verdict,
            "reason": self.reason,
            "offset": self.offset,
            "fields": self.fields,
            "unknown": self.unknown,
            "notes": self.notes,
        }

    # ---- text rendering of `fields` (one renderer for text and JSON, so they cannot drift)
    def render(self) -> list[str]:
        out = ["%s: %s%s -> %s" % (self.path, self.label,
                                   "" if self.schema is None else ", schema/version %s" % _num(self.schema),
                                   self.verdict)]
        for line in render_fields(self.fields, "  "):
            out.append(line)
        for rec in self.unknown:
            out.append("  %s record at byte offset %d: %s"
                       % (rec["kind"], rec["offset"], describe_rec(rec)))
        for note in self.notes:
            out.append("  note: %s" % note)
        return out


def _num(v):
    if isinstance(v, int) and not isinstance(v, bool) and v > 0xFFFF:
        return "0x%x" % v
    return v


def describe_rec(rec: dict) -> str:
    tag = "tag 0x%04x" % rec["tag"] if isinstance(rec["tag"], int) else "tag %s" % rec["tag"]
    return "%s, length %d, %s" % (tag, rec["length"], rec["hex"])


def fmt_scalar(v) -> str:
    if isinstance(v, bool):
        return "1" if v else "0"
    if isinstance(v, float):
        return "%.6g" % v                      # matches the game's own log style ("291.7 -323.5")
    if isinstance(v, int) and not isinstance(v, bool):
        return str(v)
    return json.dumps(v, ensure_ascii=False)


def render_fields(fields: dict, pad: str) -> list[str]:
    out = []
    for name, value in fields.items():
        if isinstance(value, dict):
            out.append("%s%s:" % (pad, name))
            out.extend(render_fields(value, pad + "  "))
        elif isinstance(value, list):
            if not value:
                out.append("%s%s = []" % (pad, name))
                continue
            if all(isinstance(v, dict) for v in value):
                out.append("%s%s (%d):" % (pad, name, len(value)))
                for i, item in enumerate(value):
                    body = " ".join("%s=%s" % (k, fmt_scalar(v)) for k, v in item.items())
                    out.append("%s  [%d] %s" % (pad, i, body))
            else:
                out.append("%s%s = [%s]" % (pad, name, ", ".join(fmt_scalar(v) for v in value)))
        else:
            out.append("%s%s = %s" % (pad, name, fmt_scalar(value)))
    return out


# ------------------------------------------------------------------- career save (career.c)
CR_MAGIC = 0x5243554F                # 'OUCR', career.h:CR_MAGIC
CR_SCHEMA = 2
CR_FILE_NAME = "openug2_career.bin"
CR_BUF_MAX = 16384                   # career.c:CR_BUF_MAX - the read cap of cr_load_file
CR_MAX_OWNED = 5
CR_REGION_UNKNOWN = 0xFF
CD_MAX_RACES = 256                   # career_data.h
CR_REC_FLAGS = {1: "WON", 2: "PAID"}
CR_EXTRA_MAX = 4096                  # CareerState.extra


def cr_tlv(tag: int, data: bytes) -> bytes:
    """career.c:tlv - u16 tag, u16 zero, u32 length, data."""
    return struct.pack("<HHI", tag, 0, len(data)) + data


def cr_parse(buf: bytes, rep: Report, base: int = 0) -> None:
    """cr_parse, in its own order: container, magic, schema, then the TLV walk."""
    view = container_parse(buf)
    a = view.a
    if len(a) < 8:
        raise Refused(ST_E_CORRUPT, "block A is %d bytes, too short for magic and schema" % len(a),
                      base + CONTAINER_HEADER)
    if rd32(a, 0) != CR_MAGIC:
        raise Refused(ST_E_CORRUPT, "block A magic is 0x%08x, not CR_MAGIC 0x%08x"
                      % (rd32(a, 0), CR_MAGIC), base + CONTAINER_HEADER)
    schema = rd32(a, 4)
    rep.schema = schema
    if schema == 0:
        raise Refused(ST_E_CORRUPT, "schema is 0", base + CONTAINER_HEADER + 4)
    if schema > CR_SCHEMA:
        raise Refused(ST_E_INCOMPATIBLE, "schema %d is newer than the game's CR_SCHEMA %d"
                      % (schema, CR_SCHEMA), base + CONTAINER_HEADER + 4)

    pos, b, size_b = 0, view.b, view.size_b
    b_at = base + CONTAINER_HEADER + len(a)
    core, races, car, pose, owned = None, None, None, None, None
    nextra = 0
    have_owned = False
    while pos < size_b:
        at = b_at + pos
        if size_b - pos < 8:
            raise Refused(ST_E_CORRUPT, "TLV record header is truncated (%d bytes left)"
                          % (size_b - pos), at)
        tag = rd16(b, pos)
        length = rd32(b, pos + 4)
        if length > size_b - pos - 8:
            raise Refused(ST_E_CORRUPT, "TLV %d length %d runs past the end of block B (%d left)"
                          % (tag, length, size_b - pos - 8), at + 4)
        d = b[pos + 8:pos + 8 + length]
        d_at = at + 8
        if tag == 1:                                    # TLV 1 CORE
            if length < 24:
                raise Refused(ST_E_CORRUPT, "CORE record is %d bytes, needs 24" % length, at + 4)
            core = {
                "started": d[0], "stage": d[1], "intro_done": d[2],
                "bank": rd_i64(d, 4), "rep": rd32(d, 12),
                "entered": rd32(d, 16), "won": rd32(d, 20),
            }
        elif tag == 2:                                  # TLV 2 RACES
            if length % 8 or length // 8 > CD_MAX_RACES:
                raise Refused(ST_E_CORRUPT,
                              "RACES record is %d bytes: not a whole number of 8-byte records"
                              " or more than CD_MAX_RACES %d of them" % (length, CD_MAX_RACES),
                              at + 4)
            races = []
            for i in range(length // 8):
                q = d[8 * i:8 * i + 8]
                flags = q[5]
                names = "|".join(n for bit, n in sorted(CR_REC_FLAGS.items()) if flags & bit)
                races.append({"hash": "0x%08x" % rd32(q, 0), "best_place": q[4],
                              "flags": flags, "flags_name": names, "attempts": rd16(q, 6)})
        elif tag == 3:                                  # TLV 3 CAR
            if length > 31:
                raise Refused(ST_E_CORRUPT, "CAR record is %d bytes, the car[32] field holds 31"
                              % length, at + 4)
            car = d
        elif tag == 4:                                  # TLV 4 POSE
            if length < 16:
                raise Refused(ST_E_CORRUPT, "POSE record is %d bytes, needs 16" % length, at + 4)
            pose = [rd_f32(d, 4 * i) for i in range(4)]
        elif tag == 5:                                  # TLV 5 OWNED (schema 2)
            if schema < 2:
                rep.notes.append("TLV 5 OWNED at byte offset %d is not defined in schema %d: the"
                                 " game drops it and rebuilds owned from car" % (at, schema))
            else:
                if length < 4 or d[0] > CR_MAX_OWNED or length != 4 + 32 * d[0]:
                    raise Refused(ST_E_CORRUPT,
                                  "OWNED record is %d bytes: needs 4 + 32*nowned with nowned <= %d"
                                  % (length, CR_MAX_OWNED), at + 4)
                n, active, region = d[0], d[1], d[2]
                if n and active >= n:
                    raise Refused(ST_E_CORRUPT, "OWNED active %d is not one of the %d cars"
                                  % (active, n), d_at + 1)
                if n == 0 and active != 0:
                    raise Refused(ST_E_CORRUPT, "OWNED active is %d with no cars" % active, d_at + 1)
                owned = {"nowned": n, "active": active, "region": region,
                         "owned": [name_of(d[4 + 32 * i:4 + 32 * i + 32]) for i in range(n)]}
                have_owned = True
        else:                                           # unknown tag: extra[], rewritten as is
            if 8 + length > CR_EXTRA_MAX - nextra:
                raise Refused(ST_E_CORRUPT, "unknown TLV %d would not fit in the %d-byte extra[]"
                              % (tag, CR_EXTRA_MAX), at + 4)
            nextra += 8 + length
            rep.unknown.append({"kind": "preserved TLV", "tag": tag, "length": length,
                                "offset": at, "hex": hexstr(b[pos:pos + 8 + length], HEX_RECORD)})
        pos += 8 + length

    rep.fields["core"] = core if core else {}
    if races is not None:
        rep.fields["races"] = races
    if pose is not None:
        rep.fields["pose"] = dict(zip(("x", "y", "z", "heading"), pose))
    if car is not None:
        rep.fields["car"] = car.decode("latin-1") if printable(car) else hexstr(car)
    if owned is None:
        # cr_parse's migration block: no OWNED record -> the one car driven so far is the one owned.
        region = CR_REGION_UNKNOWN
        active = 0
        cars = []
        if car:
            cars = [car.decode("latin-1")]
        rep.fields["owned"] = {"nowned": len(cars), "active": active,
                               "region": region, "owned": cars}
        rep.verdict = ("OK (migrated from schema 1)" if schema == 1 else
                       "OK (migrated: no OWNED record, owned rebuilt from car)")
        rep.notes.append("schema %d file carried no TLV 5, so cr_parse rebuilt owned = {car}"
                         % schema + (", region = CR_REGION_UNKNOWN (0x%02x)" % region
                                     if region == CR_REGION_UNKNOWN else ""))
    else:
        owned["region_name"] = {0: "A", 1: "B"}.get(owned["region"], "unknown")
        rep.fields["owned"] = owned


def printable(data: bytes) -> bool:
    return all(0x20 <= x < 0x7F for x in data)


def name_of(field: bytes) -> str:
    """A char[32] the C code reads with a NUL terminator (memcpy of 31 bytes at most)."""
    end = field.find(b"\0")
    raw = field if end < 0 else field[:end]
    return raw.decode("latin-1") if printable(raw) else hexstr(raw)


def as_i32(v: int) -> int:
    return v - (1 << 32) if v >= (1 << 31) else v


def as_u32_name(field: bytes) -> str:
    """gs_name reads a NUL-terminated name; bytes after it are padding and are not compared."""
    end = field.find(b"\0")
    return field if end < 0 else field[:end]


def gs_name_ok(field: bytes, cap: int, empty_ok: bool) -> tuple[bool, str]:
    """garage.c:gs_name - NUL within cap, non-empty unless empty_ok, [A-Z0-9_] only."""
    end = field[:cap].find(b"\0")
    if end < 0:
        return False, "no NUL terminator in its %d-byte field" % cap
    raw = field[:end]
    if not raw and not empty_ok:
        return False, "name is empty"
    for ch in raw:
        c = chr(ch)
        if not (("A" <= c <= "Z") or ("0" <= c <= "9") or c == "_"):
            return False, "name byte 0x%02x (%r) is outside A-Z 0-9 _" % (ch, c)
    return True, ""


N2_PART_COUNT = 11
N2_PART_LABELS = ["Front bumper", "Rear bumper", "Side skirts", "Hood", "Headlights",
                  "Taillights", "Engine cover", "Trunk audio", "Spoiler", "Exhaust", "Roof scoop"]
GARAGE_MAGIC = 0x5241474F            # 'OGAR', garage.c:gs_parse
GARAGE_FILE_NAME = "openug2_garage.bin"
GARAGE_SAVE_LIMIT = 1048576          # garage.h:GARAGE_SAVE_LIMIT
GARAGE_RECORD_BYTES = 120
GARAGE_VERSION = 1


def garage_record(car: bytes, cfg: dict) -> bytes:
    """gs_encode into a 120-byte tag-1 record (tests build the files the writer writes)."""
    rec = bytearray(GARAGE_RECORD_BYTES)
    rec[0:len(car)] = car
    for off, key in ((32, "body_kit"), (36, "hood_style"), (40, "spoiler_style"),
                     (44, "wheel_style_id"), (116, "rim_style")):
        wr32(rec, off, cfg[key] & 0xFFFFFFFF)
    for i, part in enumerate(cfg["parts"]):
        wr32(rec, 48 + 4 * i, part & 0xFFFFFFFF)
    brand = cfg["rim_brand"].encode()
    rec[92:92 + len(brand) + 1] = brand + b"\0"
    return cr_tlv(1, bytes(rec))


def gs_decode(rec: bytes) -> tuple[dict | None, str]:
    """gs_decode + garage_config_valid: returns the config, or the first rule it fails."""
    brand_raw = rec[92:92 + 24]                    # kept raw: garage_config_valid reads it as a
    cfg = {                                        # C string, so the NUL is part of the check
        "body_kit": as_i32(rd32(rec, 32)), "hood_style": as_i32(rd32(rec, 36)),
        "spoiler_style": as_i32(rd32(rec, 40)), "wheel_style_id": as_i32(rd32(rec, 44)),
        "parts": [as_i32(rd32(rec, 48 + 4 * i)) for i in range(N2_PART_COUNT)],
        "rim_brand": brand_raw, "rim_style": as_i32(rd32(rec, 116)),
    }
    ok, why = gs_name_ok(rec, 32, False)                       # the car key, gs_name(p, 32, 0)
    if not ok:
        return None, "car name %s" % why
    if b"\0" not in brand_raw:                             # memchr(rim_brand, 0, sizeof rim_brand)
        return None, "rim_brand has no NUL in its 24-byte field"
    for key, hi in (("rim_style", 999), ("body_kit", 99), ("hood_style", 999),
                    ("spoiler_style", 999), ("wheel_style_id", 999)):
        if not 0 <= cfg[key] <= hi:
            return None, "%s %d is outside 0..%d" % (key, cfg[key], hi)
    ok, why = gs_name_ok(brand_raw, 24, True)
    if not ok:
        return None, "rim brand %s" % why
    brand = as_u32_name(brand_raw)
    if not brand and cfg["rim_style"]:
        return None, "rim_style %d is set with no rim brand" % cfg["rim_style"]
    for i, part in enumerate(cfg["parts"]):
        if not 0 <= part <= 5999:
            return None, "parts[%d] (%s) %d is outside 0..5999" % (i, N2_PART_LABELS[i], part)
    shown = dict(cfg)
    shown["rim_brand"] = brand.decode("latin-1")
    return shown, ""


def gs_parse(buf: bytes, rep: Report, base: int = 0) -> tuple[Container, list, dict]:
    """gs_parse: validate every known record, reject duplicate car keys, and keep the rest."""
    if len(buf) > GARAGE_SAVE_LIMIT:
        raise Refused(ST_E_CORRUPT, "file is %d bytes, over the %d-byte GARAGE_SAVE_LIMIT"
                      % (len(buf), GARAGE_SAVE_LIMIT), 0)
    view = container_parse(buf)
    if CONTAINER_HEADER + view.size_a + view.size_b != len(buf):
        raise Refused(ST_E_CORRUPT, "blocks A(%d)+B(%d) plus the %d-byte header do not add up to"
                      " the %d-byte file" % (view.size_a, view.size_b, CONTAINER_HEADER, len(buf)),
                      8)
    if len(view.a) < 8:
        raise Refused(ST_E_CORRUPT, "block A is %d bytes, too short for magic and version"
                      % len(view.a), base + CONTAINER_HEADER)
    if rd32(view.a, 0) != GARAGE_MAGIC:
        raise Refused(ST_E_CORRUPT, "block A magic is 0x%08x, not 0x%08x"
                      % (rd32(view.a, 0), GARAGE_MAGIC), base + CONTAINER_HEADER)
    version = rd32(view.a, 4)
    rep.schema = version
    if version == 0:
        raise Refused(ST_E_CORRUPT, "version is 0", base + CONTAINER_HEADER + 4)
    if version > GARAGE_VERSION:
        raise Refused(ST_E_INCOMPATIBLE, "version %d is newer than the game writes (max %d)"
                      % (version, GARAGE_VERSION), base + CONTAINER_HEADER + 4)

    b, size_b = view.b, view.size_b
    b_at = base + CONTAINER_HEADER + len(view.a)
    records: list = []
    by_name: dict[bytes, int] = {}
    at = 0
    while at < size_b:
        where = b_at + at
        if size_b - at < 8:
            raise Refused(ST_E_CORRUPT, "record header is truncated (%d bytes left)" % (size_b - at),
                          where)
        head = b[at:at + 8]
        length = rd32(head, 4)
        if length > size_b - at - 8:
            raise Refused(ST_E_CORRUPT, "record length %d runs past the end of block B (%d left)"
                          % (length, size_b - at - 8), where + 4)
        data = b[at + 8:at + 8 + length]
        if head[0] == 1 and head[1] == 0:                 # gs_parse looks at both tag bytes: a
            if length < GARAGE_RECORD_BYTES:              # record with a dirty reserved field is
                raise Refused(ST_E_CORRUPT,             # not a known record at all (see the note)
                              "car record is %d bytes, gs_decode needs %d"
                              % (length, GARAGE_RECORD_BYTES), where + 4)
            cfg, why = gs_decode(data)
            if cfg is None:
                raise Refused(ST_E_CORRUPT, "car record at byte offset %d is invalid: %s"
                              % (where, why), where + 8)
            key = as_u32_name(data[:32])
            if key in by_name:                            # the writer refuses to make a second one
                raise Refused(ST_E_CORRUPT, "car %s appears twice (also at byte offset %d)"
                              % (key.decode("latin-1"), by_name[key]), where + 8)
            by_name[key] = where
            item = {"car": key.decode("latin-1") if printable(key) else hexstr(key),
                    "offset": where}
            item.update(cfg)
            if length > GARAGE_RECORD_BYTES:              # opaque tail the writer keeps in place
                item["tail"] = hexstr(data[GARAGE_RECORD_BYTES:], HEX_RECORD)
            records.append(item)
        else:
            rep.unknown.append({"kind": "preserved", "tag": "%02x %02x" % (head[0], head[1]),
                                "length": length, "offset": where,
                                "hex": hexstr(b[at:at + 8 + length], HEX_RECORD)})
        at += 8 + length
    return view, records, by_name


def garage_report(buf: bytes, rep: Report, car: str | None = None, base: int = 0) -> None:
    """gs_parse, plus the answer garage_store_get would give for `car` (main validates the name
    the way gs_car does, so a bad name is a usage error here, not a refusal of the file)."""
    view, records, by_name = gs_parse(buf, rep, base)
    rep.fields["records"] = records
    if car is None:
        return
    found = by_name.get(car.encode("latin-1"))
    if found is None:
        rep.notes.append("garage_store_get(%r) = ST_E_NOTFOUND: the car has no record, so the game"
                         " shows it stock (no parts applied)" % car)
    else:
        rep.notes.append("garage_store_get(%r) = ST_OK, record at byte offset %d" % (car, found))


# ----------------------------------------------------------- profile payload (settings.c 5.x)
ST_PROFILE_VERSION = 0x10D
ST_PAYLOAD_SIZE = 54930
ST_PROFILE_FILE_SIZE = CONTAINER_HEADER + 8 + ST_PAYLOAD_SIZE      # 54 966
ST_TRAX_COUNT = 27
ST_INPUT_SLOTS = 6
ST_INPUT_RECORD_SIZE = 181
ST_NAME_MAX = 15
OFF_TRAX, OFF_AUDIO, OFF_GAMEPLAY, OFF_PLAYER = 50211, 50427, 50487, 50503
OFF_UNKBLK, OFF_DIFFICULTY, OFF_AUTOSAVE, OFF_PROMPT = 50583, 50979, 50987, 50988
OFF_NAME, OFF_ONLINE, OFF_INPUT = 53761, 53793, 53844
PLAYER_SIZE, TRAX_SIZE = 40, 8


def profile_name_valid(name: str) -> bool:
    """st_profile_name_valid: 1..15 visible bytes, no path or Windows-hostile characters."""
    if not 1 <= len(name) <= ST_NAME_MAX:
        return False
    if any(ord(c) < 0x20 or ord(c) == 0x7F or c in '/\\:*?\"<>|' for c in name):
        return False
    return name[-1] not in " ."


def st_profile_unpack(p: bytes, rep: Report, base: int = 0) -> dict:
    """st_profile_unpack_payload: every typed field, plus what is left in StProfile.raw."""
    typed = bytearray(len(p))

    def mark(off: int, n: int) -> None:
        if n:
            typed[off:off + n] = b"\1" * n

    f: dict = {}
    trax = []
    for i in range(ST_TRAX_COUNT):
        e = OFF_TRAX + i * TRAX_SIZE
        mark(e, 4)
        mark(e + 4, 1)                            # bytes 5..8 of the slot are padding: raw
        trax.append({"track": rd32(p, e), "mode": p[e + 4]})
    f["trax"] = trax

    r = OFF_AUDIO
    mark(r, 60)
    f["audio"] = {
        "master": rd_f32(p, r), "speech_movie": rd_f32(p, r + 4), "menu_music": rd_f32(p, r + 8),
        "game_music": rd_f32(p, r + 12), "sfx": rd_f32(p, r + 16), "engine": rd_f32(p, r + 20),
        "tire": rd_f32(p, r + 24), "unk28": rd_f32(p, r + 28), "unk32": rd_f32(p, r + 32),
        "unk36": rd_f32(p, r + 36), "mode": rd32(p, r + 40), "unk44": rd32(p, r + 44),
        "trax_order": rd32(p, r + 48), "unk52": rd32(p, r + 52), "unk56": rd32(p, r + 56),
    }

    mark(OFF_GAMEPLAY, 5)
    mark(OFF_GAMEPLAY + 8, 4)
    mark(OFF_GAMEPLAY + 12, 4)
    f["gameplay"] = {
        "rearview_mirror": p[OFF_GAMEPLAY], "unk1": p[OFF_GAMEPLAY + 1],
        "speedo_units": p[OFF_GAMEPLAY + 2], "race_map_mode": p[OFF_GAMEPLAY + 3],
        "explore_map_mode": p[OFF_GAMEPLAY + 4], "jump_camera": rd_f32(p, OFF_GAMEPLAY + 8),
        "unk12": rd_f32(p, OFF_GAMEPLAY + 12),
    }

    players = []
    for i in range(2):
        r = OFF_PLAYER + i * PLAYER_SIZE
        for off, n in ((0, 7), (8, 1), (12, 1), (16, 4), (20, 6), (28, 4), (32, 4), (36, 4)):
            mark(r + off, n)
        players.append({
            "gauges": p[r], "race_position": p[r + 1], "lap_info": p[r + 2], "score": p[r + 3],
            "feedback": p[r + 4], "leaderboard": p[r + 5], "ask_transmission": p[r + 6],
            "controller_config": rd32(p, r + 8), "unk12": p[r + 12], "camera": rd32(p, r + 16),
            "split_time": p[r + 20], "transmission": p[r + 21],
            "stability_control": p[r + 22], "unk23": p[r + 23], "unk24": p[r + 24],
            "unk25": p[r + 25], "wheel_range": rd_f32(p, r + 28),
            "road_feedback": rd_f32(p, r + 32), "force_strength": rd_f32(p, r + 36),
        })
    f["player"] = players

    mark(OFF_UNKBLK, 13)
    f["unkblk_f"] = rd_f32(p, OFF_UNKBLK)
    f["unkblk_a"] = rd32(p, OFF_UNKBLK + 4)
    f["unkblk_b"] = rd32(p, OFF_UNKBLK + 8)
    f["unkblk_c"] = p[OFF_UNKBLK + 12]
    mark(OFF_DIFFICULTY, 4)
    f["career_difficulty"] = rd32(p, OFF_DIFFICULTY)
    mark(OFF_AUTOSAVE, 1)
    mark(OFF_PROMPT, 1)
    f["autosave"] = p[OFF_AUTOSAVE]
    f["save_prompt"] = p[OFF_PROMPT]
    mark(OFF_NAME, 32)
    raw_name = p[OFF_NAME:OFF_NAME + 32]
    nm = as_u32_name(raw_name)
    f["name"] = nm.decode("latin-1") if printable(nm) else hexstr(nm)
    if b"\0" not in raw_name:
        rep.notes.append("the 32-byte name field has no NUL: st_profile_load truncates it to 32"
                         " bytes and the game treats the profile as unnamed")
    elif not profile_name_valid(nm.decode("latin-1")):
        rep.notes.append("name %r is not one st_profile_name_valid accepts, so fep_refresh never"
                         " lists the profile" % nm.decode("latin-1"))

    mark(OFF_ONLINE, 8)
    mark(OFF_ONLINE + 8, 3)
    mark(OFF_ONLINE + 11, 20)
    mark(OFF_ONLINE + 31, 20)
    f["online"] = {
        "always_zero_a": rd32(p, OFF_ONLINE), "always_zero_b": rd32(p, OFF_ONLINE + 4),
        "show_player_names": p[OFF_ONLINE + 8],
        "bad_connection_warning": p[OFF_ONLINE + 9], "unk10": p[OFF_ONLINE + 10],
        "prefs_a": [rd32(p, OFF_ONLINE + 11 + 4 * i) for i in range(5)],
        "prefs_b": [rd32(p, OFF_ONLINE + 31 + 4 * i) for i in range(5)],
    }

    devices = []
    for i in range(ST_INPUT_SLOTS):
        r = OFF_INPUT + i * ST_INPUT_RECORD_SIZE
        mark(r, 181)
        devices.append({
            "guid": p[r:r + 16].hex(),
            "axis": [as_i32(rd32(p, r + 16 + 4 * k)) for k in range(4)],
            "dir": [as_i32(rd32(p, r + 32 + 4 * k)) for k in range(4)],
            "assigned": [p[r + 48 + k] for k in range(4)],
            "combined_mode": p[r + 52],
            "binding": [as_i32(rd32(p, r + 53 + 4 * k)) for k in range(32)],
        })
    f["input"] = devices

    # StProfile.raw: the bytes no typed field reads are kept and written back unchanged, so the
    # only interesting question about them is whether any of them is set.
    opaque_zero = opaque_runs(p, typed)
    if opaque_bytes := opaque_zero[0]:
        f["raw_nonzero"] = opaque_zero[1]
        rep.notes.append("%d byte(s) outside the typed fields are non-zero (StProfile.raw keeps"
                         " them byte for byte); offsets shown above" % opaque_bytes)
    return f


def opaque_runs(p: bytes, typed: bytearray) -> tuple[int, list[str]]:
    """Non-zero byte runs the C payload reader never looks at."""
    runs, start = [], None
    total = 0
    for i, byte in enumerate(p):
        opaque = not typed[i]
        if opaque and byte:
            if start is None:
                start = i
            total += 1
            continue
        if start is not None:
            runs.append((start, i))
            start = None
    if start is not None:
        runs.append((start, len(p)))
    shown = ["%d..%d = %s" % (a, b, " ".join("%02x" % x for x in p[a:b][:HEX_RUN]))
             for a, b in runs[:20]]
    if len(runs) > 20:
        shown.append("... %d more non-zero opaque runs" % (len(runs) - 20))
    return total, shown


def profile_report(buf: bytes, rep: Report, base: int = 0) -> None:
    """st_profile_load / st_profile_parse, in that order."""
    if len(buf) < CONTAINER_HEADER or len(buf) > 0x7FFFFFFF:
        raise Refused(ST_E_IO, "file length %d is outside the range the profile loader reads"
                      % len(buf), 0)
    view = container_parse(buf)
    if len(view.a) < 8:
        raise Refused(ST_E_INCOMPATIBLE, "block A is %d bytes, too short for version and payload"
                      " size (3.5 step 6)" % len(view.a), base + CONTAINER_HEADER)
    version, payload_size = rd32(view.a, 0), rd32(view.a, 4)
    rep.schema = "0x%x" % version
    if version != ST_PROFILE_VERSION or payload_size != ST_PAYLOAD_SIZE or view.size_b != ST_PAYLOAD_SIZE:
        raise Refused(ST_E_INCOMPATIBLE,
                      "profile version 0x%x, block A payload size %d, block B size %d: the game"
                      " wants version 0x%x and %d-byte payloads in both"
                      % (version, payload_size, view.size_b, ST_PROFILE_VERSION, ST_PAYLOAD_SIZE),
                      base + CONTAINER_HEADER)
    if len(view.a) > 8:
        rep.notes.append("block A carries %d bytes past its 8-byte header; st_profile_parse only"
                         " reads the first 8 and the game keeps the rest" % (len(view.a) - 8))
    rep.fields.update(st_profile_unpack(view.b, rep, base))


# ------------------------------------------------------- settings.ini value store (settings.c 2)
INI_SECTION = "Software\\EA Games\\Need for Speed Underground 2"
CB_KB_ENTRIES = 53                                  # controls_bind.h
SDL_NUM_SCANCODES = 512
CS_KB_VALUE = "ControlsKeyboard"                    # controls_store.h
CS_PROFILE_NAME = "OPENUG2"
GFX_NAMES = ["VERSION", "SIZE", "g_CarEnvironmentMapEnable", "g_CarEnvironmentMapUpdateData",
             "g_CarShadowEnable", "g_CarHeadlightEnable", "g_CarLightingEnable",
             "g_CarDamageEnable", "g_CrowdEnable", "g_RoadReflectionEnable", "g_FogEnable",
             "g_MotionBlurEnable", "g_LightStreaksEnable", "g_LightGlowEnable",
             "g_AnimatedTextureEnable", "g_ParticleSystemEnable", "g_DepthOfFieldEnable",
             "g_WorldLodLevel", "g_CarLodLevel", "g_OverBrightEnable", "g_BleachByPassEnable",
             "g_TintingEnable", "g_FSAALevel", "g_HorizonFogEnable", "g_RainEnable",
             "g_TextureFiltering", "g_RacingResolution", "g_PerformanceLevel", "g_VSyncOn"]
LANGUAGES = {"english us": 0, "english uk": 0, "french": 1, "german": 2, "italian": 3,
             "spanish": 4, "dutch": 5, "swedish": 6, "danish": 7, "korean": 8,
             "chinese (traditional)": 9, "english china": 9, "japanese": 10, "thai": 11}


def ini_name_valid(name: str) -> tuple[bool, str]:
    """ini_name_valid: the store refuses a whole file over one bad key name."""
    if not name:
        return False, "key name is empty"
    if name[0] in " #;[\t":
        return False, "key name starts with %r" % name[0]
    if name[-1] in " \t":
        return False, "key name ends in whitespace"
    for c in name:
        if ord(c) < 32 or ord(c) == 127 or c == "=":
            return False, "key name holds %r" % c
    return True, ""


def strtoul_hex(text: str) -> tuple[int | None, str]:
    """strtoul(val, &end, 16) with the checks of ini_parse_line: at least one digit, nothing
    after it (the caller's *end test), no sign beyond '+', and no value over UINT32_MAX."""
    i, n = 0, len(text)
    while i < n and text[i] in " \t\n\v\f\r":
        i += 1
    if i < n and text[i] == "+":
        i += 1
    if text[i:i + 2].lower() == "0x":
        i += 2
    start = i
    while i < n and text[i] in "0123456789abcdefABCDEF":
        i += 1
    if i == start:
        return None, "no hexadecimal digits after 'dword:'"
    value = int(text[start:i], 16)
    if i != n:
        return None, "trailing %r after the hexadecimal value" % text[i:]
    if value > 0xFFFFFFFF:
        return None, "value 0x%x is wider than a DWORD" % value
    return value, ""


def ini_unescape(val: str) -> tuple[str | None, str]:
    """The quoted-string branch of ini_parse_line, escapes and all."""
    out, i, n = [], 1, len(val)
    while i < n and val[i] != '"':
        if val[i] == "\\":
            if i + 1 >= n:
                return None, "string ends with a trailing backslash"
            i += 1
            out.append({0: "\0", "n": "\n", "r": "\r", "t": "\t"}.get(val[i], val[i]))
        else:
            out.append(val[i])
        i += 1
    if i >= n:
        return None, "quoted value has no closing quote"
    rest = val[i + 1:].strip(" \t")
    if rest:
        return None, "trailing %r after the closing quote" % rest
    return "".join(out), ""


def ini_report(data: bytes, rep: Report) -> None:
    """ini_read / ini_parse_line: the game reads the file line by line and stops on the first
    line it cannot parse, so one bad line means the whole store is empty to the game."""
    text = data.decode("latin-1")                        # the C code is byte-transparent
    values: dict[str, dict] = {}                          # lower-cased name -> entry
    order: list[str] = []
    in_section = False
    seen_section: list[str] = []
    outside = 0
    ignored: list[str] = []
    pos = 0
    for raw_line in text.split("\n"):
        line = raw_line.strip(" \t\r\n")
        if "\0" in raw_line:                              # ini_read: a NUL byte stops the read
            raise Refused(ST_E_CORRUPT, "a NUL byte is inside the file",
                          pos + raw_line.index("\0"))
        at = pos
        pos += len(raw_line) + 1
        if not line or line[0] in ";#":
            continue
        if line[0] == "[":
            close = line.find("]")
            if close < 0:
                ignored.append("byte offset %d: section header has no ']'" % at)
            else:
                name = line[1:close]
                seen_section.append(name)
                in_section = name.lower() == INI_SECTION.lower()
                if not in_section:
                    rep.notes.append("section %r at byte offset %d is not the game's section, so"
                                     " its values are read and then ignored" % (name, at))
            continue
        if not in_section:
            outside += 1
            continue
        eq = line.find("=")
        if eq < 0:
            ignored.append("byte offset %d: %r has no '='" % (at, line[:32]))
            continue
        name, val = line[:eq].strip(" \t"), line[eq + 1:].strip(" \t")
        ok, why = ini_name_valid(name)
        if not ok:
            raise Refused(ST_E_CORRUPT, "key name %r: %s" % (name, why), at)
        key = name.lower()                                # mem_find matches names case-insensitively
        if val.startswith("dword:"):
            number, why = strtoul_hex(val[len("dword:"):])
            if number is None:
                raise Refused(ST_E_CORRUPT, "key %r: %s" % (name, why), at)
            entry = {"name": name, "type": "dword", "value": "0x%08x" % number,
                     "number": number, "line": at}
        elif val.startswith('"'):
            string, why = ini_unescape(val)
            if string is None:
                raise Refused(ST_E_CORRUPT, "key %r: %s" % (name, why), at)
            entry = {"name": name, "type": "string", "value": string, "line": at}
        else:
            # ini_parse_line stores only 'dword:' and quoted values: a bare value is not an error,
            # it is simply never seen by the game (st_reg_get_string finds nothing).
            ignored.append("byte offset %d: %s=%s is not quoted and not a dword, so the store"
                           " never holds it" % (at, name, val[:32]))
            continue
        if key in values:
            ignored.append("byte offset %d: %s is set twice; the store keeps the last value"
                           % (at, name))
        elif key not in values:
            order.append(key)
        values[key] = entry                               # mem_slot reuses the first entry
    rep.fields["section"] = seen_section
    rep.fields["values"] = [values[k] for k in order]
    if outside:
        rep.notes.append("%d value line(s) sit outside %s and are skipped" % (outside, INI_SECTION))
    for note in ignored[:20]:
        rep.notes.append(note)
    if len(ignored) > 20:
        rep.notes.append("... %d further ignored lines" % (len(ignored) - 20))
    rep.fields["read_by_the_game"] = store_lookup(values)


def store_lookup(values: dict) -> dict:
    """What the callers of the store make of it: st_install_read, st_gfx_load, fep_create,
    cs_kb_load. Names the file does not carry are left at the caller's default."""
    out: dict = {}

    def get(name):
        return values.get(name.lower())

    def text(name):
        e = get(name)
        return e["value"] if e and e["type"] == "string" else None

    def num(name):
        e = get(name)
        return e["number"] if e and e["type"] == "dword" else None

    active = text("ActiveProfile")
    out["ActiveProfile"] = active if active is not None else "absent: %s is used" % CS_PROFILE_NAME
    if active is not None and not profile_name_valid(active):
        out["ActiveProfile"] = "%s (not a name st_profile_name_valid accepts)" % active
    out["NotFirstTime"] = "present" if get("NotFirstTime") else "absent: first run"
    for name in ("Install Dir", "CD Drive", "CacheSize", "SwapSize"):
        v = text(name)
        if v is not None:
            out[name] = v
    lang = text("Language")
    if lang is not None:
        out["Language"] = "%s -> index %s" % (lang, LANGUAGES.get(lang.lower(), "-1 (unrecognised)"))
    stream = text("StreamingInstall")
    if stream is not None:
        out["StreamingInstall"] = "off" if stream.lower() == "off" else stream
    kb = text(CS_KB_VALUE)
    if kb is not None:
        out[CS_KB_VALUE] = cs_kb_summary(kb)
    gfx = {n: num(n) for n in GFX_NAMES if num(n) is not None}
    if gfx:
        out["graphics"] = gfx
    return out


def cs_kb_summary(value: str) -> str:
    """cs_kb_load: '1:' then 53 comma-separated scancodes; anything else leaves the defaults."""
    if not value.startswith("1:"):
        return "cs_kb_load rejects it (version prefix is not '1:'): the default keys stay"
    body = value[2:]
    parts = body.split(",")
    if len(parts) != CB_KB_ENTRIES:
        return ("cs_kb_load rejects it (%d numbers, the table holds %d): the default keys stay"
                % (len(parts), CB_KB_ENTRIES))
    keys, bad = [], 0
    for piece in parts:
        if "_" in piece:                                # int() would read Python-style underscores
            return "cs_kb_load rejects it (%r is not a number): the default keys stay" % piece[:12]
        try:
            v = int(piece)
        except ValueError:
            return "cs_kb_load rejects it (%r is not a number): the default keys stay" % piece[:12]
        keys.append(v if 0 < v < SDL_NUM_SCANCODES else 0)
        if not 0 < v < SDL_NUM_SCANCODES:
            bad += 1
    seen, dropped = set(), 0
    for i, v in enumerate(keys):
        if v and v in seen:
            dropped += 1
            keys[i] = 0
        elif v:
            seen.add(v)
    return "%d keys, %d dropped as out of range, %d dropped as duplicates" % (CB_KB_ENTRIES, bad, dropped)


# --------------------------------------------------------------------- camera.cfg (camera.c)
CAM_NOPT = 5
CAM_PLAYERS = 2
CAM_OPT_DEFAULT = 3                                     # CAM_OPT_FAR
CAM_LABELS = ["Bumper", "Hood", "Close", "Far", "Super Far"]


def cam_report(data: bytes, rep: Report) -> None:
    """cam_settings_load: sscanf over 128-byte fgets lines. It never refuses a file - a line it
    does not like leaves that option at its default - so the report says which lines it took."""
    text = data.decode("latin-1")
    opt = [CAM_OPT_DEFAULT] * CAM_PLAYERS
    mouse = 0
    took, skipped = [], 0
    for line in split_fgets(text, 128):
        line = line.rstrip("\n")
        hit = match_int_pair(line, "camera_option_p")
        if hit:
            slot, value = hit
            if 1 <= slot <= CAM_PLAYERS and 0 <= value < CAM_NOPT:
                opt[slot - 1] = value
                took.append("camera_option_p%d=%d (%s)" % (slot, value, CAM_LABELS[value]))
                continue
        hit = match_int_pair(line, "right_mouse_look", prefix=False)
        if hit and hit[1] in (0, 1):
            mouse = hit[1]
            took.append("right_mouse_look=%d" % mouse)
            continue
        if line.strip():
            skipped += 1
    rep.schema = None
    rep.fields["lines_taken"] = took
    rep.fields["opt"] = [{"player": i + 1, "opt": v, "label": CAM_LABELS[v]}
                        for i, v in enumerate(opt)]
    rep.fields["mouse_look"] = mouse
    if skipped:
        rep.notes.append("%d line(s) matched neither sscanf and left their option at its default"
                         % skipped)
    if not took:
        rep.notes.append("nothing was taken from the file: the game runs on the defaults"
                         " (opt %d = %s, mouse_look 0)" % (CAM_OPT_DEFAULT, CAM_LABELS[CAM_OPT_DEFAULT]))


def split_fgets(text: str, width: int) -> list[str]:
    """fgets(line, 128, f) hands back at most 127 bytes per call, so a line the owner edited onto
    one line is read as two lines by the game."""
    out: list[str] = []
    for line in text.split("\n"):
        if len(line) <= width - 1:
            out.append(line)
            continue
        for i in range(0, len(line), width - 1):
            out.append(line[i:i + width - 1])
    return out


def match_int_pair(line: str, key: str, prefix: bool = True) -> tuple[int, int] | None:
    """sscanf(line, "camera_option_p%d=%d", ...) / ("right_mouse_look=%d", &v): the literal has to
    match from byte 0, every %d reads a signed integer, and the character it stops at has to be the
    format's next literal. A tail after the last %d is never read, so "p1=3x" still counts as 3."""
    rest = line
    if not rest.startswith(key):
        return None
    rest = rest[len(key):]
    if not prefix:                                        # "right_mouse_look" then '=' then %d
        if not rest.startswith("="):
            return None
        value, _ = read_int(rest[1:])
        return None if value is None else (0, value)
    slot, rest = read_int(rest)                           # "camera_option_p" then %d then '='
    if slot is None or not rest.startswith("="):
        return None
    value, _ = read_int(rest[1:])
    return None if value is None else (slot, value)


def read_int(text: str) -> tuple[int | None, str]:
    i = 0
    while i < len(text) and text[i] in " \t":
        i += 1
    start = i
    if i < len(text) and text[i] in "+-":
        i += 1
    while i < len(text) and text[i].isdigit():
        i += 1
    if i == start:
        return None, text
    try:
        return int(text[start:i]), text[i:]
    except ValueError:
        return None, text


# ------------------------------------------------------------------- identify and dispatch
TOOL_READ_CAP = 8 * 1024 * 1024          # inspector limit, not a game rule (see the docstring)
LABELS = {
    "career": "career save (CR_MAGIC 'OUCR')",
    "garage": "Garage store (0x5241474F 'OGAR')",
    "profile": "profile (ST_PROFILE_VERSION 0x10d)",
    "settings-ini": "settings store (settings.ini)",
    "camera": "camera options (camera.cfg)",
    "container": "'20CM' container",
}


def sniff(data: bytes) -> str | None:
    """Which reader claims the file, from the bytes it looks at - never from the file name."""
    shape = data[:4] == CONTAINER_MAGIC
    if not shape and len(data) >= CONTAINER_HEADER + 8:
        # A file whose own size fields add up to its length is a container even when its magic bytes
        # are damaged, which is what rot at the head of a save looks like. Naming the damage is more
        # useful than shrugging, and container_parse is the one that calls it ST_E_CORRUPT.
        shape = (rd32(data, 4) == len(data)
                 and rd32(data, 8) + rd32(data, 12) + CONTAINER_HEADER == len(data))
    if shape:
        if len(data) >= CONTAINER_HEADER + 8:
            magic = rd32(data, CONTAINER_HEADER)
            if magic == CR_MAGIC:
                return "career"
            if magic == GARAGE_MAGIC:
                return "garage"
            # A profile's block A holds a version, not a magic, so a build that bumped the version is
            # invisible to that test. The payload size is the other byte-level tell the game itself
            # checks (st_profile_parse wants 54 930 bytes in a plain 8-byte block A), so use it: the
            # report can then say ST_E_INCOMPATIBLE for the version instead of "unknown file".
            if magic == ST_PROFILE_VERSION or (rd32(data, 8) == 8
                                               and rd32(data, 12) == ST_PAYLOAD_SIZE):
                return "profile"
        return "container"
    head = data[:4096]
    lines = [line.strip(" \t\r\n") for line in head.decode("latin-1").split("\n")]
    # A NUL byte is not evidence that a file is not the INI store: ini_read reads byte by byte and
    # answers the first NUL with ST_E_CORRUPT for the whole store, so the shape test looks through
    # NULs and ini_report is the one that names the damage. Binary files stay out because they carry
    # no '[section]' line, which is the only thing that makes the game open a file as an INI.
    shape = [line.replace("\0", "") for line in lines]
    if any(line.startswith("[") and "]" in line for line in shape if line):
        return "settings-ini"
    if any(line.startswith("camera_option_p") or line.startswith("right_mouse_look=")
           for line in shape if line):
        return "camera"
    return None


def inspect_bytes(data: bytes, rep: Report, car: str | None = None,
                  named: bool = False, size: int | None = None,
                  name_hint: str | None = None) -> Report:
    size = len(data) if size is None else size
    kind = sniff(data)
    if kind is None:
        return rep.skip("no 20CM container, no INI section header and no camera_option_p line"
                        " - no OpenUG2 reader opens this", named)
    rep.kind, rep.label = kind, LABELS[kind]
    try:
        if kind == "career" and size > CR_BUF_MAX:
            raise Refused(ST_E_CORRUPT, "file is %d bytes: cr_load_file reads CR_BUF_MAX = %d and"
                          " refuses anything bigger" % (size, CR_BUF_MAX), 0)
        if kind == "garage" and size > GARAGE_SAVE_LIMIT:
            raise Refused(ST_E_IO, "file is %d bytes: gs_read refuses a file over the %d-byte"
                          " GARAGE_SAVE_LIMIT (the in-memory store calls the same limit"
                          " ST_E_CORRUPT)" % (size, GARAGE_SAVE_LIMIT), 0)
        if kind in ("profile", "container") and size > TOOL_READ_CAP:
            raise Refused(ST_E_IO, "file is %d bytes, past this tool's %d-byte read cap (a tool"
                          " limit, not a game rule)" % (size, TOOL_READ_CAP), 0)
        if kind == "career":
            cr_parse(data, rep)
        elif kind == "garage":
            garage_report(data, rep, car)
        elif kind == "profile":
            profile_report(data, rep)
        elif kind == "settings-ini":
            ini_report(data, rep)
        elif kind == "camera":
            cam_report(data, rep)
        else:                                    # a 20CM container no OpenUG2 reader claims
            container_parse(data)
            magic = rd32(data, CONTAINER_HEADER)
            if name_hint == CR_FILE_NAME:                # see the docstring: the name only names
                rep.kind, rep.label = "career", LABELS["career"]     # the reader the game would use
                rep.notes.append("identified by file name, not magic: block A holds 0x%08x"
                                 % magic)
                cr_parse(data, rep)
            elif name_hint == GARAGE_FILE_NAME:
                rep.kind, rep.label = "garage", LABELS["garage"]
                rep.notes.append("identified by file name, not magic: block A holds 0x%08x"
                                 % magic)
                garage_report(data, rep, car)
            else:
                return rep.skip("block A magic is 0x%08x: no OpenUG2 reader knows it" % magic,
                                named)
    except Refused as exc:
        return rep.refuse(exc)
    return rep


def layout_notes(rep: Report, path: str) -> None:
    """fe_profile.c / career.c / garage.c build their paths from a profile directory name; a file
    that is readable but misplaced is a save the game will never load."""
    parent = os.path.basename(os.path.dirname(os.path.abspath(path)))
    base = os.path.basename(path)
    if rep.kind == "profile":
        if base != parent:
            rep.notes.append("st_profile_path opens the profile at <root>/%s/%s, and this file is"
                             " named %s - the game would not open it here" % (parent, parent, base))
        elif not profile_name_valid(parent):
            rep.notes.append("profile directory %r is not a name st_profile_name_valid accepts"
                             ", so no code path reaches this file" % parent)
    elif rep.kind in ("career", "garage"):
        want = CR_FILE_NAME if rep.kind == "career" else GARAGE_FILE_NAME
        if base != want:
            rep.notes.append("%s is read as <root>/<profile>/%s; this copy is named %s"
                             % (rep.kind, want, base))
        if not profile_name_valid(parent):
            rep.notes.append("cr_path / garage_load take <root>/<profile>/... and profile %r is"
                             " not a name st_profile_name_valid accepts, so the game reports"
                             " ST_E_ARG before reading this file" % parent)


def inspect_path(path: str, shown: str | None = None, car: str | None = None,
                 named: bool = True) -> Report:
    rep = Report(shown if shown is not None else path)
    try:
        size = os.path.getsize(path)
        with open(path, "rb") as handle:
            data = handle.read(TOOL_READ_CAP + 1)
    except OSError as exc:
        code = ST_E_NOTFOUND if exc.errno == 2 else ST_E_IO
        return rep.refuse(Refused(code, "%s: %s" % (path, exc.strerror), None))
    inspect_bytes(data, rep, car, named, size, os.path.basename(path))
    if rep.kind in ("profile", "career", "garage"):
        layout_notes(rep, path)
    return rep


def walk_dir(root: str, car: str | None = None) -> list[Report]:
    """Every regular file under a user-data folder, in path order. Links and special files are
    left alone - the game's own openers refuse them (openat O_NOFOLLOW)."""
    reports: list[Report] = []
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames.sort()
        for name in sorted(filenames):
            full = os.path.join(dirpath, name)
            rel = os.path.relpath(full, root)
            if os.path.islink(full):
                reports.append(Report(rel).skip("symbolic link; the game opens save files with"
                                                " O_NOFOLLOW and would not follow it"))
                continue
            if not os.path.isfile(full):
                reports.append(Report(rel).skip("not a regular file"))
                continue
            reports.append(inspect_path(full, rel, car, named=False))
    return reports


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(
        prog="save_inspect.py", description=__doc__.split("\n")[0],
        epilog="  python3 -I tools/save_inspect.py FILE [FILE ...] [--json OUT.json]\n"
               "  python3 -I tools/save_inspect.py --dir USERDATA_DIR [--json OUT.json]\n"
               "  python3 -I tools/save_inspect.py FILE --car 240SX")
    parser.add_argument("files", nargs="*", metavar="FILE", help="save file to decode")
    parser.add_argument("--dir", metavar="USERDATA_DIR",
                        help="walk a user-data folder and report every save file in it")
    parser.add_argument("--json", metavar="OUT.json", help="also write the report as JSON")
    parser.add_argument("--car", metavar="NAME",
                        help="for Garage files, also answer garage_store_get for this car")
    args = parser.parse_args(argv)

    if bool(args.files) == bool(args.dir):
        parser.error("give FILE(s) or --dir, one of the two")
    if args.dir and not os.path.isdir(args.dir):
        parser.error("--dir %s is not a directory" % args.dir)
    if args.car is not None:
        name = args.car.encode("latin-1")
        ok, why = gs_name_ok(name + b"\0", len(name) + 1, False)
        if not ok or len(name) >= 32:            # garage.c:gs_car
            parser.error("--car %s is not a car name the store accepts (%s)"
                         % (args.car, why or "32 bytes or longer"))

    if args.dir:
        reports = walk_dir(args.dir, args.car)
        if not reports:
            print("%s: no files under this folder" % args.dir)
    else:
        reports = [inspect_path(path, car=args.car) for path in args.files]

    for rep in reports:
        for line in rep.render():
            print(line)
        print()
    counted = [rep for rep in reports if not rep.skipped or rep.fatal]
    bad = [rep for rep in counted if not rep.ok or rep.fatal]
    print("%d file(s): %d ok, %d refused, %d skipped"
          % (len(reports), sum(1 for r in reports if r.ok and not r.skipped),
             len([r for r in bad if not r.ok]), len(reports) - len(counted)))

    if args.json:
        payload = {
            "tool": "save_inspect",
            "source": args.dir if args.dir else args.files,
            "files": [rep.to_json() for rep in reports],
            "summary": {
                "files": len(reports),
                "ok": sum(1 for r in reports if r.ok and not r.skipped),
                "refused": len([r for r in bad if not r.ok]),
                "skipped": len(reports) - len(counted),
                "exit_code": 1 if bad else 0,
            },
        }
        try:
            with open(args.json, "w", encoding="utf-8") as handle:
                json.dump(payload, handle, indent=1)
                handle.write("\n")
        except OSError as exc:
            print("cannot write --json %s: %s" % (args.json, exc.strerror), file=sys.stderr)
            return 2
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
