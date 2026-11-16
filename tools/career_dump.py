#!/usr/bin/env python3
"""Dump the retail career database as tables, the way OpenUG2 reads it.

Career mode reads its races, stages, map zones, shops and car unlocks out of one container, so this
tool is a second reader of the same bytes: every offset, size limit and validity rule below is a
line-for-line port of OpenUG2's own reader, in the order that reader touches them:

  src/career_data.h  the record layouts, CD_* limits, CD_UNLOCK_* / CD_CAT_* / CD_SHOP_* names,
                     CD_RACE_ZONE_MAX (20 m) and CD_SHOP_ZONE_MAX (32 m)
  src/career_data.c  find_chunk -> find_chunk, blob_str -> blob_str, fixed_str -> fixed_str,
                     kind_from_type -> kind_of, parse_shops / parse_unlocks / cd_parse_globalb ->
                     parse_globalb, cd_parse_zones -> parse_zones, cd_parse_markers ->
                     parse_markers, cd_load -> load, cd_find* -> the *_by_hash helpers,
                     cd_hash -> lang_key_audit.ug_hash_upper
  src/race.h         n2_race_name -> KIND_NAME (the mode name a race `type` maps to)

  python3 -I tools/career_dump.py --data "$OPENUG2_DATA" [--json OUT.json]
      [--section races|stages|zones|shops|unlocks|all]
  python3 -I tools/career_dump.py --data "$OPENUG2_DATA" --race STAGE_1_DRAG_1

Data files and the required/optional split are the C loader's: GLOBAL/GLOBALB.BUN (chunk 0x80034A10,
the FIRST one; a second smaller copy is a demo career) holds the races and stages tables, the
string blob, and - optionally - the shops and car-unlock tables; TRACKS/ROUTESL4RA/PathsFreeRoam.bin
(chunk 0x3414A) holds the locator zones; TRACKS/ROUTESL4RA/TrackPosMarkersFreeRoam.bin (chunk
0x34146) holds named position markers and is optional. Missing or malformed shops/unlocks read as
zero records, exactly as parse_shops / parse_unlocks do; a missing races, stages or zone table is a
data error, as it is for cd_load.

Hashes are resolved to names wherever the dump or the data tree has them: race hashes to race names,
shop hashes to shop names, unlock car hashes to CARS/<folder> names (the same mapping
tools/career_data_check.c:installed_cars uses). Anything else prints as hex - stage sponsor hashes,
for instance, are read by the C code but resolve to nothing in this dump, so they stay hex. One race
can carry up to four heats, so the race table lists every event id and lap count it has (19 of the
224 retail races carry 2 or 3, all of them category-URL races); --race and the JSON add the
directions.

Display strings: the brief expects a language key somewhere in these records, and the LANGUAGES /
English.bin reader is imported for it (tools/lang_key_audit.py, task M, reused rather than copied),
but on the data at hand no field career_data.c reads is one. --section prints a
"language strings" line with the number of hash lookups made against the table and the text of any
hit; on the retail dump that line reads "0 hits in 978 hash lookups", covering every hash the dump
prints (race names and locators, unlock args, presets, shops, sponsors, cars, zones, markers), and
the C reader itself has no string-table read path (no ug_strtab_* / gv_text call anywhere in
src/career_data.c or src/career.c).

Exit code: 0 = the request was served, 2 = usage or data error (an unknown --race name prints close
matches, and --race with an explicit --section is a usage error rather than a silently ignored
flag). Float maths is float32-faithful (see f32) so the numbers match the C reader bit for bit.
"""
import argparse
import difflib
import json
import math
import os
import struct
import sys

# `python3 -I` (how the brief runs this) also implies -P since Python 3.11, which keeps the
# script's own directory off sys.path; the sibling reader has to be found by path.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import lang_key_audit  # noqa: E402  task M's string-table reader: reused, not copied

# Chunk ids, src/career_data.c.
ID_CAREER = 0x80034A10
ID_RACES = 0x00034A11
ID_STAGES = 0x00034A18
ID_STRINGS = 0x00034A1D
ID_SHOPS = 0x00034A12
ID_UNLOCKS = 0x00034A1F
ID_ZONES = 0x0003414A
ID_MARKERS = 0x00034146
ID_ZONES_IN = 0x80034147          # the container 0x3414A may be nested in (cd_parse_zones)

# CD_MAX_* / CD_*_BYTES / CD_*_ZONE_MAX, src/career_data.h.
MAX_RACES = 256
MAX_STAGES = 8
MAX_ZONES = 192
MAX_MARKERS = 64
MAX_SHOPS = 32
MAX_UNLOCKS = 48
RACE_BYTES = 136
STAGE_BYTES = 80
SHOP_BYTES = 160
UNLOCK_BYTES = 12
RACE_ZONE_MAX = 20.0
SHOP_ZONE_MAX = 32.0
MAX_FILE = 256 * 1024 * 1024      # CD_MAX_FILE

# CD_UNLOCK_* (+0x0C), CD_CAT_* (+0x34), CD_SHOP_* (+0x50), CD_TYPE_SCRIPTED, src/career_data.h.
UNLOCK_MODE = {0: "scripted", 1: "stage", 2: "sponsor", 3: "url", 4: "url_final"}
CATEGORY = {0: "intro", 1: "sponsor", 2: "url", 3: "world"}
SHOP_TYPE = {0: "crib", 1: "paint", 2: "body", 3: "perf", 4: "car lot", 5: "specialty",
             6: "uniques"}
TYPE_SCRIPTED = 3
# kind_from_type (src/career_data.c) into n2_race_name (src/race.h).
TYPE_TO_KIND = {0: 1, 1: 2, 2: 5, 4: 3, 5: 4}
KIND_NAME = {0: "Unknown", 1: "Circuit", 2: "Sprint", 3: "Drag", 4: "Drift", 5: "Street X",
             6: "URL"}

SECTIONS = ("races", "stages", "zones", "shops", "unlocks", "all")
GLOBALB = os.path.join("GLOBAL", "GLOBALB.BUN")
PATHS = os.path.join("TRACKS", "ROUTESL4RA", "PathsFreeRoam.bin")
MARKERS = os.path.join("TRACKS", "ROUTESL4RA", "TrackPosMarkersFreeRoam.bin")


def f32(value):
    """Round to float32, because every float and float literal in the C reader is one (rdf, the
    0.01f bbox tolerance, the zone size limits, cx/cy, marker heading). Doubles would disagree with
    C within ~1e-7 of a limit, which is enough to keep or drop a borderline zone."""
    try:
        return struct.unpack("<f", struct.pack("<f", value))[0]
    except (OverflowError, ValueError):
        return math.inf if value > 0 else -math.inf


def f32u32(word):
    """Port of rdf: little-endian u32 reinterpreted as float32."""
    return struct.unpack("<f", struct.pack("<I", word))[0]


def rd16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def rd32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def finite_f(value):
    """Port of finite_f: not NaN and |value| < 1e9 (1e9f is exact in float32)."""
    return value == value and abs(value) < 1e9


def find_chunk(buf, off, end, cid):
    """Port of find_chunk: first chunk with `cid` in [off, end), as (data offset, payload size).

    Like the C original it stops rather than skipping a chunk whose payload claims to run past
    `end`, so a corrupt header ends the walk instead of sending it into the middle of nowhere."""
    while off <= end and end - off >= 8:
        cid_got, size = struct.unpack_from("<II", buf, off)
        if size > end - off - 8:
            return None
        if cid_got == cid:
            return (off + 8, size)
        off += 8 + size
    return None


def blob_str(buf, blen, offset, cap):
    """Port of blob_str: string at blob offset `offset`, truncated to cap-1. (terminated, text).

    The C version keeps scanning after the output buffer is full and returns 0 only when the blob
    ends without a NUL, so a long terminated name is truncated, not rejected."""
    if offset >= blen:
        return False, ""
    end = buf.find(b"\x00", offset, offset + blen)
    raw = buf[offset:] if end < 0 else buf[offset:end]
    return end >= 0, raw[:cap - 1].decode("latin-1")


def fixed_str(buf, off, size, cap):
    """Port of fixed_str: NUL-padded field, up to the first NUL or `size` bytes, capped at cap-1."""
    raw = buf[off:off + size]
    nul = raw.find(b"\x00")
    if nul >= 0:
        raw = raw[:nul]
    return raw[:cap - 1].decode("latin-1")


class Career:
    """CareerData (src/career_data.h): the parsed tables, in file order."""

    def __init__(self):
        self.dataroot = None
        self.races = []
        self.stages = []
        self.zones = []
        self.markers = []
        self.shops = []
        self.unlocks = []
        self.error = ""

    # cd_find / cd_find_zone / cd_find_marker / cd_find_shop: first match wins.
    def race_by_hash(self, name_hash):
        for race in self.races:
            if race["name_hash"] == name_hash:
                return race
        return None

    def zone_by_hash(self, locator_hash):
        for zone in self.zones:
            if zone["hash"] == locator_hash:
                return zone
        return None

    def marker_by_hash(self, marker_hash):
        for marker in self.markers:
            if marker["hash"] == marker_hash:
                return marker
        return None

    def shop_by_hash(self, shop_hash):
        for shop in self.shops:
            if shop["hash"] == shop_hash:
                return shop
        return None

    def counts(self):
        return {"races": len(self.races), "stages": len(self.stages), "zones": len(self.zones),
                "markers": len(self.markers), "shops": len(self.shops),
                "unlocks": len(self.unlocks)}


def kind_of(race_type):
    """Port of kind_from_type: race type (+0x0F) -> N2_RACE_* name."""
    return KIND_NAME[TYPE_TO_KIND.get(race_type, 0)]


def parse_shops(cd, buf, co, cs):
    """Port of parse_shops: 0x34A12, 160-byte records. Missing or malformed -> no shops."""
    cd.shops = []
    got = find_chunk(buf, co, co + cs, ID_SHOPS)
    if not got or got[1] % SHOP_BYTES:
        return
    off, size = got
    for i in range(min(size // SHOP_BYTES, MAX_SHOPS)):
        rec = buf[off + i * SHOP_BYTES: off + (i + 1) * SHOP_BYTES]
        cd.shops.append({
            "name": fixed_str(rec, 0x00, 0x38, 32),
            "hash": rd32(rec, 0x38),
            "zone_hash": rd32(rec, 0x3C),
            "platform": fixed_str(rec, 0x40, 0x10, 16),
            "type": rec[0x50],
            "known": rec[0x9C],
            "open_stage": rec[0x9D],
        })


def parse_unlocks(cd, buf, co, cs):
    """Port of parse_unlocks: 0x34A1F, {car, race A, race B} u32 triples. Missing -> no unlocks."""
    cd.unlocks = []
    got = find_chunk(buf, co, co + cs, ID_UNLOCKS)
    if not got or got[1] % UNLOCK_BYTES:
        return
    off, size = got
    for i in range(min(size // UNLOCK_BYTES, MAX_UNLOCKS)):
        rec = buf[off + i * UNLOCK_BYTES: off + (i + 1) * UNLOCK_BYTES]
        cd.unlocks.append({"car_hash": rd32(rec, 0), "race": [rd32(rec, 4), rd32(rec, 8)]})


def parse_globalb(cd, buf):
    """Port of cd_parse_globalb: races, stages, shops and unlocks out of GLOBALB.BUN.

    Returns (nrace, error), like the C function's return value plus cd->error."""
    cd.races, cd.stages, cd.shops, cd.unlocks, cd.error = [], [], [], [], ""
    if buf is None:
        return 0, "null buffer"
    career = find_chunk(buf, 0, len(buf), ID_CAREER)
    if not career:
        return 0, "no 0x80034A10 chunk"
    co, cs = career
    blob = find_chunk(buf, co, co + cs, ID_STRINGS)
    if not blob:
        return 0, "no 0x34A1D string blob"
    races = find_chunk(buf, co, co + cs, ID_RACES)
    if not races:
        return 0, "no 0x34A11 races"
    stages = find_chunk(buf, co, co + cs, ID_STAGES)
    if not stages:
        return 0, "no 0x34A18 stages"
    bo, bs = blob
    ro, rs = races
    so, ss = stages
    nr, ns = rs // RACE_BYTES, ss // STAGE_BYTES
    if not nr or rs % RACE_BYTES:
        return 0, "bad race table size"
    if not ns or ss % STAGE_BYTES:
        return 0, "bad stage table size"
    if nr > MAX_RACES:
        return 0, "too many races"
    if ns > MAX_STAGES:
        return 0, "too many stages"
    for i in range(nr):
        r = buf[ro + i * RACE_BYTES: ro + (i + 1) * RACE_BYTES]
        ok, name = blob_str(buf[bo:], bs, rd16(r, 0), 32)
        if not ok:
            return 0, "bad race name offset"
        ok, locator = blob_str(buf[bo:], bs, rd16(r, 6), 24)
        if not ok:
            return 0, "bad locator offset"
        heat = []
        nheat = 0
        for k in range(4):
            track = rd16(r, 0x18 + 4 * k)
            heat.append({"track": track, "dir": r[0x1A + 4 * k], "laps": r[0x1B + 4 * k]})
            if track:
                nheat += 1
        cd.races.append({
            "index": i,
            "name": name,
            "name_hash": rd32(r, 0x08),
            "locator": locator,
            "locator_hash": rd32(r, 0x28),
            "unlock_mode": r[0x0C],
            "suv": r[0x0D],
            "type": r[0x0F],
            "kind": kind_of(r[0x0F]),
            "unlock_arg": rd32(r, 0x10),
            "rep_base": rd32(r, 0x14),
            "heat": heat,
            "nheat": nheat,
            "preset_hash": rd32(r, 0x2C),
            "purse": rd32(r, 0x30),
            "category": r[0x34],
            "stage": r[0x37],
            "shop_hash": rd32(r, 0x3C),
            "nai": r[0x7C],
            "hidden": r[0x7F],
        })
    for i in range(ns):
        r = buf[so + i * STAGE_BYTES: so + (i + 1) * STAGE_BYTES]
        nsponsors = min(r[1], 5)
        cd.stages.append({
            "index": r[0],
            "nsponsors": nsponsors,
            "sponsor": [rd32(r, 0x08 + 4 * k) for k in range(nsponsors)],
            "final_race": rd32(r, 0x28),
            "req_sponsor": r[0x2D],
            "req_url": r[0x2E],
            "req_dvd": r[0x2F],
            "req_total": r[0x42],
        })
    parse_shops(cd, buf, co, cs)
    parse_unlocks(cd, buf, co, cs)
    return nr, ""


def parse_zones(cd, buf):
    """Port of cd_parse_zones: 0x3414A is a stream of mixed variable-size records, so zones are
    found structurally at 4-byte offsets - bbox f32[4], u32 hash, 3 words, u16 n=4, u16 100, then 4
    (x, y) pairs whose extent equals the bbox within 0.01 and whose width/height pass
    CD_SHOP_ZONE_MAX, or CD_RACE_ZONE_MAX unless the hash is one of the loaded shops' zone hashes.
    Races or shops must be loaded first for the owner filter to apply, exactly as in C.

    Returns (nzone, error)."""
    cd.zones = []
    if buf is None:
        return 0, "null buffer"
    found = find_chunk(buf, 0, len(buf), ID_ZONES)
    if not found:
        outer = find_chunk(buf, 0, len(buf), ID_ZONES_IN)
        found = find_chunk(buf, outer[0], outer[0] + outer[1], ID_ZONES) if outer else None
    if not found:
        return 0, "no 0x3414A chunk"
    co, cs = found
    shop_hashes = set(s["zone_hash"] for s in cd.shops)
    race_hashes = set(r["locator_hash"] for r in cd.races)
    keep = bool(cd.races or cd.shops)          # cd_parse_zones' `filter`
    seen = set()
    for p in range(co, co + cs - 68 + 1, 4) if cs >= 68 else ():
        h = rd32(buf, p + 16)
        if not h or rd16(buf, p + 32) != 4 or rd16(buf, p + 34) != 100:
            continue
        bb = [f32u32(rd32(buf, p + 4 * k)) for k in range(4)]
        pt = [f32u32(rd32(buf, p + 36 + 4 * k)) for k in range(8)]
        if not all(finite_f(v) for v in bb) or not all(finite_f(v) for v in pt):
            continue
        xs, ys = pt[0::2], pt[1::2]
        x0, x1, y0, y1 = f32(min(xs)), f32(max(xs)), f32(min(ys)), f32(max(ys))
        width, height = f32(x1 - x0), f32(y1 - y0)     # C does the subtractions in float too
        if (abs(f32(x0 - bb[0])) > f32(0.01) or abs(f32(y0 - bb[1])) > f32(0.01)
                or abs(f32(x1 - bb[2])) > f32(0.01) or abs(f32(y1 - bb[3])) > f32(0.01)
                or width > SHOP_ZONE_MAX or height > SHOP_ZONE_MAX):
            continue
        is_shop, is_race = h in shop_hashes, h in race_hashes
        if not is_shop and (width > RACE_ZONE_MAX or height > RACE_ZONE_MAX):
            continue
        if keep and not (is_race or is_shop):
            continue
        if h in seen:
            continue
        if len(cd.zones) >= MAX_ZONES:
            break
        seen.add(h)
        cd.zones.append({"hash": h, "cx": f32((bb[0] + bb[2]) * 0.5),
                         "cy": f32((bb[1] + bb[3]) * 0.5),
                         "poly": [[pt[2 * k], pt[2 * k + 1]] for k in range(4)],
                         "width": width, "height": height})
    if not cd.zones:
        return 0, "no zones found"
    return len(cd.zones), ""


def parse_markers(cd, buf):
    """Port of cd_parse_markers: 0x34146, 8-byte filler then 48-byte records [+8 hash, +12 slot,
    +16 xyz, +32 u16 heading in 1/65536 of a turn]. Only slot 0 of a name, first occurrence only.

    Returns (nmarker, error)."""
    cd.markers = []
    if buf is None:
        return 0, "null buffer"
    found = find_chunk(buf, 0, len(buf), ID_MARKERS)
    if not found:
        return 0, "no 0x34146 chunk"
    co, cs = found
    seen = set()
    for p in range(co + 8, co + cs - 48 + 1, 48) if cs >= 8 else ():
        h = rd32(buf, p + 8)
        if rd32(buf, p + 12) != 0:
            continue
        x, y, z = (f32u32(rd32(buf, p + 16 + 4 * k)) for k in range(3))
        if not (finite_f(x) and finite_f(y) and finite_f(z)):
            continue
        if h in seen:
            continue
        if len(cd.markers) >= MAX_MARKERS:
            break
        seen.add(h)
        cd.markers.append({"hash": h, "x": x, "y": y, "z": z,
                           "heading": f32(rd16(buf, p + 32) * f32(6.2831853 / 65536.0))})
    if not cd.markers:
        return 0, "no markers found"
    return len(cd.markers), ""


def read_file(dataroot, rel):
    """Port of read_file: bytes, or None when the file is missing, empty or bigger than CD_MAX_FILE
    (the C reader refuses those and cd_load reports the file as unreadable)."""
    path = os.path.join(dataroot, rel)
    try:
        with open(path, "rb") as handle:
            data = handle.read(MAX_FILE + 1)
    except OSError:
        return None
    return data if 0 < len(data) <= MAX_FILE else None


def load(dataroot):
    """Port of cd_load. Returns (Career, error); markers stay optional, as they are in C."""
    cd = Career()
    if not dataroot:
        return cd, "null dataroot"
    cd.dataroot = dataroot
    buf = read_file(dataroot, GLOBALB)
    if buf is None:
        return cd, "cannot read %s" % GLOBALB
    _, error = parse_globalb(cd, buf)
    if error:
        cd.error = error
        return cd, error
    buf = read_file(dataroot, PATHS)
    if buf is None:
        return cd, "cannot read %s" % PATHS
    _, error = parse_zones(cd, buf)
    if error:
        cd.error = error
        return cd, error
    buf = read_file(dataroot, MARKERS)
    if buf is not None:
        parse_markers(cd, buf)        # optional for the load result, and its error is cleared too
        cd.error = ""
    return cd, ""


# --------------------------------------------------------------------------- name resolution


def car_names(dataroot):
    """ug_hash -> folder name for CARS/*, the mapping installed_cars in the C checker uses."""
    out = {}
    try:
        entries = os.listdir(os.path.join(dataroot, "CARS"))
    except OSError:
        return out
    for name in entries:
        if name and name[0] != ".":
            out.setdefault(lang_key_audit.ug_hash_upper(name), name)
    return out


class Names:
    """Resolves the hashes the dump prints, and nothing else: races by name hash, shops by hash,
    unlock cars by CARS folder name. Unknown hashes stay hex."""

    def __init__(self, cd, dataroot):
        self.races = dict((r["name_hash"], r["name"]) for r in cd.races)
        self.shops = dict((s["hash"], s["name"]) for s in cd.shops)
        self.cars = car_names(dataroot)
        self.locators = {}
        for race in cd.races:
            self.locators.setdefault(race["locator_hash"], set()).add(race["name"])
        self.shop_zones = {}
        for shop in cd.shops:
            self.shop_zones.setdefault(shop["zone_hash"], set()).add(shop["name"])

    def race(self, name_hash):
        if not name_hash:
            return "-"
        return self.races.get(name_hash, "%08x" % name_hash)

    def shop(self, shop_hash):
        if not shop_hash:
            return "-"
        return self.shops.get(shop_hash, "%08x" % shop_hash)

    def car(self, car_hash):
        return self.cars.get(car_hash, "%08x" % car_hash)

    def hexonly(self, value):
        return "%08x" % value if value else "-"


def language_report(cd):
    """(lookups, hits) against LANGUAGES/English.bin for every hash this dump prints.

    The check is a lookup of the raw hash: the table is keyed by ug_hash_upper of a label, and every
    hash career_data.c reads is such a hash (cd_hash), so a hit means that field doubles as a
    language key and its retail text is shown. 0 hits is the honest answer for this data."""
    path = os.path.join(cd.dataroot, "LANGUAGES", "English.bin")
    try:
        with open(path, "rb") as handle:
            buf = handle.read()
    except OSError as exc:
        return 0, [], "cannot read %s: %s" % (path, exc)
    offsets = lang_key_audit.strtab_offsets(buf)
    if not offsets:
        return 0, [], "no string table chunk (0x%08x) in %s" % (
            lang_key_audit.STRTAB_CHUNK_ID, path)
    try:
        _, _, table = lang_key_audit.load_string_table(buf, offsets[0], os.path.basename(path))
    except lang_key_audit.StrTabError as exc:
        return 0, [], str(exc)
    fields = []
    for race in cd.races:
        fields += [("race %s name" % race["index"], race["name_hash"]),
                   ("race %s locator" % race["index"], race["locator_hash"]),
                   ("race %s unlock_arg" % race["index"], race["unlock_arg"]),
                   ("race %s preset" % race["index"], race["preset_hash"]),
                   ("race %s shop" % race["index"], race["shop_hash"])]
    for i, stage in enumerate(cd.stages):
        fields += [("stage %d sponsor %d" % (i, k), h) for k, h in enumerate(stage["sponsor"])]
        fields.append(("stage %d final" % i, stage["final_race"]))
    for shop in cd.shops:
        fields += [("shop %s" % shop["name"], shop["hash"]),
                   ("shop %s zone" % shop["name"], shop["zone_hash"])]
    for unlock in cd.unlocks:
        fields += [("unlock car", unlock["car_hash"])]
        fields += [("unlock race %d" % k, h) for k, h in enumerate(unlock["race"])]
    for zone in cd.zones:
        fields.append(("zone", zone["hash"]))
    for marker in cd.markers:
        fields.append(("marker", marker["hash"]))
    lookups = [(label, h) for label, h in fields if h]
    hits = ["%s -> %s" % (label, table[h]) for label, h in lookups if h in table]
    return len(lookups), hits, ""


# --------------------------------------------------------------------------- tables


def row_text(cells, widths):
    return "  ".join(str(c).ljust(w) for c, w in zip(cells, widths)).rstrip()


def render(header, rows):
    """Aligned columns: every cell padded to the width of the widest entry in its column."""
    widths = [max([len(h)] + [len(r[i]) for r in rows]) for i, h in enumerate(header)]
    print(row_text(header, widths))
    for row in rows:
        print(row_text(row, widths))
    if not rows:
        print("(none)")


def unlock_cell(race, names):
    """unlock_mode (+0x0C) and its argument. The argument is a hash-shaped field the C code reads
    without naming what it points at, so it is printed as a race name only where some race carries
    that name hash, and as hex everywhere else."""
    mode = UNLOCK_MODE.get(race["unlock_mode"], "?%d" % race["unlock_mode"])
    if not race["unlock_arg"]:
        return mode
    return "%s %s" % (mode, names.race(race["unlock_arg"]))


def zone_owner(zone, names):
    """Which race locator or shop this zone belongs to; both names are shown when both claim it."""
    parts = ["race=%s" % who for who in sorted(zone_races(zone, names))]
    parts += ["shop=%s" % who for who in sorted(zone_shops(zone, names))]
    return " ".join(parts) or "-"


def zone_races(zone, names):
    return names.locators.get(zone["hash"], ())


def zone_shops(zone, names):
    return names.shop_zones.get(zone["hash"], ())


def section_races(cd, names):
    header = ["idx", "name", "hash", "stage", "event", "mode/type", "purse", "laps", "hid",
              "unlock", "preset", "shop"]
    rows = []
    for race in cd.races:
        # One race can carry up to 4 heats (heat[4], nheat): 19 of the 224 retail races do, all of
        # them category-URL (2) races. Listing them in heat order keeps every event id and lap
        # count the C code reads visible in the table; --race and the JSON give the directions too.
        heats = race["heat"][:race["nheat"]]
        rows.append([str(race["index"]), race["name"], "%08x" % race["name_hash"],
                     str(race["stage"]), "+".join(str(h["track"]) for h in heats) or "-",
                     "%s/%d" % (race["kind"], race["type"]), str(race["purse"]),
                     "+".join(str(h["laps"]) for h in heats) or "-", str(race["hidden"]),
                     unlock_cell(race, names), names.hexonly(race["preset_hash"]),
                     names.shop(race["shop_hash"])])
    return header, rows


def section_stages(cd, names):
    header = ["idx", "index", "final race", "req_total", "req_sponsor", "req_url", "req_dvd",
              "nsponsors", "sponsors"]
    rows = []
    for i, stage in enumerate(cd.stages):
        rows.append([str(i), str(stage["index"]), names.race(stage["final_race"]),
                     str(stage["req_total"]), str(stage["req_sponsor"]), str(stage["req_url"]),
                     str(stage["req_dvd"]), str(stage["nsponsors"]),
                     ",".join("%08x" % h for h in stage["sponsor"]) or "-"])
    return header, rows


def section_zones(cd, names):
    header = ["hash", "owner", "centre x", "centre y", "size"]
    rows = []
    for zone in cd.zones:
        rows.append(["%08x" % zone["hash"], zone_owner(zone, names), "%.1f" % zone["cx"],
                     "%.1f" % zone["cy"], "%.1fx%.1f" % (zone["width"], zone["height"])])
    return header, rows


def section_shops(cd, names):
    header = ["name", "hash", "type", "open stage", "known", "platform", "marker", "zone"]
    rows = []
    for shop in cd.shops:
        kind = SHOP_TYPE.get(shop["type"], "?%d" % shop["type"])
        rows.append([shop["name"], "%08x" % shop["hash"], "%d %s" % (shop["type"], kind),
                     str(shop["open_stage"]), "yes" if shop["known"] else "no",
                     shop["platform"] or "-",
                     "yes" if cd.marker_by_hash(shop["hash"]) else "no",
                     "yes" if cd.zone_by_hash(shop["zone_hash"]) else "no"])
    return header, rows


def section_unlocks(cd, names):
    header = ["car", "race A", "race B"]
    return header, [[names.car(u["car_hash"]), names.race(u["race"][0]), names.race(u["race"][1])]
                    for u in cd.unlocks]


SECTIONS_F = {"races": section_races, "stages": section_stages, "zones": section_zones,
              "shops": section_shops, "unlocks": section_unlocks}


def races_per_stage(cd):
    """Races per stage slot, as the C checker counts them (a stage byte below CD_MAX_STAGES)."""
    counts = [0] * MAX_STAGES
    loose = 0
    for race in cd.races:
        if race["stage"] < MAX_STAGES:
            counts[race["stage"]] += 1
        else:
            loose += 1
    return counts, loose


def print_summary(cd, names):
    counts = cd.counts()
    print("career data in %s" % cd.dataroot)
    print("races %d stages %d zones %d markers %d shops %d unlocks %d"
          % (counts["races"], counts["stages"], counts["zones"], counts["markers"],
             counts["shops"], counts["unlocks"]))
    per_stage, loose = races_per_stage(cd)
    print("races per stage: %s%s" % (" ".join("%d=%d" % (i, n) for i, n in enumerate(per_stage)),
                                     "" if not loose else
                                     "  (stage byte >= %d: %d)" % (MAX_STAGES, loose)))
    lookups, hits, note = language_report(cd)
    print("language strings: %d hit%s in %d hash lookups against LANGUAGES/English.bin%s"
          % (len(hits), "" if len(hits) == 1 else "s", lookups, "" if not note else " (%s)" % note))
    for hit in hits:
        print("  %s" % hit)


# --------------------------------------------------------------------------- one race


def find_race_by_name(cd, wanted):
    """Race whose name matches `wanted` ignoring case (names are hashed upper-case, cd_hash), first
    one wins, as cd_find does for repeated hashes."""
    upper = wanted.upper()
    for race in cd.races:
        if race["name"].upper() == upper:
            return race
    return None


def unlock_arg_text(race, names):
    """unlock_arg (+0x10) as hex, with the race name in front when a race carries that name hash."""
    arg = race["unlock_arg"]
    if not arg:
        return "-"
    name = names.races.get(arg)
    return "%s (%08x)" % (name, arg) if name else "%08x" % arg


def print_race(cd, names, race):
    print("race %d  %s  hash %08x" % (race["index"], race["name"], race["name_hash"]))
    print("  stage      %d   category %s (%d)   type %d%s   kind %s"
          % (race["stage"], CATEGORY.get(race["category"], "?"), race["category"], race["type"],
             " (scripted event)" if race["type"] == TYPE_SCRIPTED else "", race["kind"]))
    print("  purse      %d   rep_base %d   hidden %d   suv %d   nai %d"
          % (race["purse"], race["rep_base"], race["hidden"], race["suv"], race["nai"]))
    print("  unlock     %s (%d)   arg %s"
          % (UNLOCK_MODE.get(race["unlock_mode"], "?"), race["unlock_mode"],
             unlock_arg_text(race, names)))
    print("  locator    %s (%08x)" % (race["locator"] or "-", race["locator_hash"]))
    zone = cd.zone_by_hash(race["locator_hash"])
    print("  zone       %s" % ("cx %.1f cy %.1f, %.1f x %.1f m"
                               % (zone["cx"], zone["cy"], zone["width"], zone["height"])
                               if zone else "none"))
    print("  preset     %s" % names.hexonly(race["preset_hash"]))
    shop = cd.shop_by_hash(race["shop_hash"]) if race["shop_hash"] else None
    if shop:
        marker = cd.marker_by_hash(shop["hash"])
        print("  shop       %s (%08x) type %d (%s) open stage %d known %d platform %s"
              % (shop["name"], shop["hash"], shop["type"], SHOP_TYPE.get(shop["type"], "?"),
                 shop["open_stage"], shop["known"], shop["platform"] or "-"))
        print("             zone %s, marker %s"
              % ("yes" if cd.zone_by_hash(shop["zone_hash"]) else "no", "yes" if marker else "no"))
    else:
        print("  shop       %s" % names.hexonly(race["shop_hash"]))
    print("  heats      %d of 4" % race["nheat"])
    for k, heat in enumerate(race["heat"]):
        print("    heat %d  event %5s  dir %s  laps %s"
              % (k, heat["track"] if heat["track"] else "-", heat["dir"], heat["laps"]))
    finals = [i for i, stage in enumerate(cd.stages) if stage["final_race"] == race["name_hash"]]
    print("  final of   %s" % (", ".join("stage %d" % i for i in finals) or "-"))
    cars = [names.car(u["car_hash"]) for u in cd.unlocks if race["name_hash"] in u["race"]]
    print("  unlocks    %s" % (", ".join(cars) or "nothing"))


# --------------------------------------------------------------------------- JSON


def json_rows(cd, names):
    """The same records the tables print, keyed by the C field names, plus the resolved names and
    the cd_find_* indices the tables show as yes/no columns."""
    races = []
    for race in cd.races:
        zone = cd.zone_by_hash(race["locator_hash"])
        shop = cd.shop_by_hash(race["shop_hash"]) if race["shop_hash"] else None
        row = dict(race)
        row.update({
            "unlock_mode_name": UNLOCK_MODE.get(race["unlock_mode"], None),
            "category_name": CATEGORY.get(race["category"], None),
            "unlock_ref": names.race(race["unlock_arg"]) if race["unlock_arg"] else None,
            "preset_ref": names.hexonly(race["preset_hash"]) if race["preset_hash"] else None,
            "shop_ref": shop["name"] if shop else None,
            "zone_index": cd.zones.index(zone) if zone else None,
        })
        races.append(row)
    stages = []
    for stage in cd.stages:
        row = dict(stage)
        row["final_race_ref"] = names.race(stage["final_race"]) if stage["final_race"] else None
        stages.append(row)
    zones = []
    for zone in cd.zones:
        row = dict(zone)
        row["owner_races"] = sorted(zone_races(zone, names))
        row["owner_shops"] = sorted(zone_shops(zone, names))
        zones.append(row)
    shops = []
    for shop in cd.shops:
        zone = cd.zone_by_hash(shop["zone_hash"])
        marker = cd.marker_by_hash(shop["hash"])
        row = dict(shop)
        row["type_name"] = SHOP_TYPE.get(shop["type"], None)
        row["zone_index"] = cd.zones.index(zone) if zone else None
        row["marker_index"] = cd.markers.index(marker) if marker else None
        shops.append(row)
    unlocks = []
    for unlock in cd.unlocks:
        row = dict(unlock)
        row["car"] = names.car(unlock["car_hash"])
        row["race_ref"] = [names.race(h) if h else None for h in unlock["race"]]
        row["race_index"] = [cd.races.index(r) if r else None
                             for r in (cd.race_by_hash(h) for h in unlock["race"])]
        unlocks.append(row)
    markers = []
    for marker in cd.markers:
        row = dict(marker)
        row["name"] = names.shops.get(marker["hash"], None)
        markers.append(row)
    per_stage, loose = races_per_stage(cd)
    lookups, hits, note = language_report(cd)
    return {"counts": cd.counts(), "races": races, "stages": stages, "zones": zones, "shops": shops,
            "unlocks": unlocks, "markers": markers,
            "races_per_stage": {"counts": per_stage, "stage_byte_out_of_range": loose},
            "language": {"file": "LANGUAGES/English.bin", "lookups": lookups, "hits": hits,
                         "note": note or None}}


def write_json(path, payload):
    with open(path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, indent=1)
        handle.write("\n")


# --------------------------------------------------------------------------- CLI


def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    ap = argparse.ArgumentParser(prog="career_dump.py", description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True, help='retail data root ($OPENUG2_DATA)')
    ap.add_argument("--section", choices=SECTIONS, default="all",
                    help="tables to print (default all)")
    ap.add_argument("--race", help="dump one race by name instead of the tables")
    ap.add_argument("--json", dest="json_out", help="also write the same data as JSON here")
    args = ap.parse_args(argv)
    if args.race and any(a == "--section" or a.startswith("--section=") for a in argv):
        sys.stderr.write("usage error: --race dumps one race; drop --section\n")
        return 2
    if not os.path.isdir(args.data):
        sys.stderr.write("data error: %s is not a directory\n" % args.data)
        return 2
    cd, error = load(args.data)
    if error:
        sys.stderr.write("data error: %s\n" % error)
        return 2
    names = Names(cd, args.data)

    if args.race:
        race = find_race_by_name(cd, args.race)
        if not race:
            upper = [r["name"].upper() for r in cd.races]
            close = difflib.get_close_matches(args.race.upper(), upper, n=5, cutoff=0.6)
            sys.stderr.write("no race named %s%s\n" % (args.race, "" if not close else
                                                       "; closest: " + ", ".join(close)))
            return 2
        print_race(cd, names, race)
        payload = {"counts": cd.counts(), "race": json_rows(cd, names)["races"][race["index"]]}
    else:
        print_summary(cd, names)
        wanted = SECTIONS_F if args.section == "all" else {args.section: SECTIONS_F[args.section]}
        for name in SECTIONS:
            if name not in wanted:
                continue
            header, rows = SECTIONS_F[name](cd, names)
            print("\n== %s ==" % name)
            render(header, rows)
        payload = json_rows(cd, names) if args.json_out else None

    if args.json_out:
        try:
            write_json(args.json_out, payload)
        except OSError as exc:
            sys.stderr.write("data error: cannot write %s: %s\n" % (args.json_out, exc))
            return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
