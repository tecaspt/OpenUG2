#!/usr/bin/env python3
"""Unit tests for tools/career_dump.py.

Every byte here is built in this file: a small GLOBALB.BUN-style container, a zone file and a
marker file, framed the way the retail files are, so the tests exercise career_dump's port of
src/career_data.c (offsets, size limits, the structural zone scan, the optional tables) without
touching retail data. Run: python3 -I tools/career_dump_test.py -v
"""
import io
import json
import os
import struct
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import career_dump as K  # noqa: E402  the module under test


# --------------------------------------------------------------------------- record builders


def table(cid, payload, size=None):
    """A chunk whose size field can be overridden, which is how a malformed table is simulated."""
    return struct.pack("<II", cid, len(payload) if size is None else size) + payload


def chunk(cid, payload):
    """A chunk the way career_data.c's find_chunk frames them."""
    return table(cid, payload)


def blob_of(names):
    """The 0x34A1D string blob: NUL-terminated names, and {name: offset}."""
    out, offsets, offset = bytearray(), {}, 0
    for name in names:
        offsets[name] = offset
        out += name.encode("ascii") + b"\x00"
        offset += len(name) + 1
    return bytes(out), offsets


def race(name_off=0, locator_off=0, name_hash=0, locator_hash=0, unlock_mode=0, suv=0, type_=0,
         unlock_arg=0, rep_base=0, heats=(), preset_hash=0, purse=0, category=0, stage=0,
         shop_hash=0, nai=0, hidden=0):
    """A 136-byte 0x34A11 record, written at the offsets career_data.c reads."""
    rec = bytearray(K.RACE_BYTES)
    struct.pack_into("<H", rec, 0x00, name_off)
    struct.pack_into("<H", rec, 0x06, locator_off)
    struct.pack_into("<I", rec, 0x08, name_hash)
    rec[0x0C], rec[0x0D], rec[0x0F] = unlock_mode, suv, type_
    struct.pack_into("<I", rec, 0x10, unlock_arg)
    struct.pack_into("<I", rec, 0x14, rep_base)
    for k, (track, direction, laps) in enumerate(heats[:4]):
        struct.pack_into("<H", rec, 0x18 + 4 * k, track)
        rec[0x1A + 4 * k], rec[0x1B + 4 * k] = direction, laps
    struct.pack_into("<I", rec, 0x28, locator_hash)
    struct.pack_into("<I", rec, 0x2C, preset_hash)
    struct.pack_into("<I", rec, 0x30, purse)
    rec[0x34], rec[0x37] = category, stage
    struct.pack_into("<I", rec, 0x3C, shop_hash)
    rec[0x7C], rec[0x7F] = nai, hidden
    return bytes(rec)


def stage(index=0, nsponsors=0, sponsors=(), final_race=0, req_sponsor=0, req_url=0, req_dvd=0,
          req_total=0):
    """An 80-byte 0x34A18 record."""
    rec = bytearray(K.STAGE_BYTES)
    rec[0x00], rec[0x01] = index, nsponsors
    for k, sponsor in enumerate(sponsors[:5]):
        struct.pack_into("<I", rec, 0x08 + 4 * k, sponsor)
    struct.pack_into("<I", rec, 0x28, final_race)
    rec[0x2D], rec[0x2E], rec[0x2F], rec[0x42] = req_sponsor, req_url, req_dvd, req_total
    return bytes(rec)


def shop(name, shop_hash, zone_hash, platform="", type_=0, known=0, open_stage=0):
    """A 160-byte 0x34A12 record."""
    rec = bytearray(K.SHOP_BYTES)
    raw = name.encode("ascii")
    rec[0x00:len(raw)] = raw                                    # char[0x38], NUL-padded
    struct.pack_into("<II", rec, 0x38, shop_hash, zone_hash)
    plat = platform.encode("ascii")
    rec[0x40:0x40 + len(plat)] = plat                           # char[0x10]
    rec[0x50], rec[0x9C], rec[0x9D] = type_, known, open_stage
    return bytes(rec)


def unlock(car_hash, race_a=0, race_b=0):
    """A 12-byte 0x34A1F record."""
    return struct.pack("<III", car_hash, race_a, race_b)


def zone(hash_, cx, cy, width, height, n=4, kind=100):
    """A 68-byte locator-zone record: bbox f32[4], u32 hash, 3 ignored words, u16 n, u16 100, then
    the four corner points - the shape cd_parse_zones recognises structurally."""
    x0, x1 = cx - width / 2.0, cx + width / 2.0
    y0, y1 = cy - height / 2.0, cy + height / 2.0
    rec = bytearray(68)
    struct.pack_into("<4f", rec, 0, x0, y0, x1, y1)
    struct.pack_into("<I", rec, 16, hash_)
    struct.pack_into("<HH", rec, 32, n, kind)
    struct.pack_into("<8f", rec, 36, x0, y0, x1, y0, x1, y1, x0, y1)
    return bytes(rec)


def zones_buf(*records, nested=False):
    """A PathsFreeRoam.bin body: the 0x3414A chunk holding `records`, optionally inside the
    0x80034147 container cd_parse_zones falls back to."""
    payload = b"\x00" * 8 + b"".join(records)        # filler: the real file mixes other records in
    if not nested:
        return chunk(K.ID_ZONES, payload)
    return chunk(K.ID_ZONES_IN, chunk(K.ID_ZONES, payload))


def markers_buf(*records):
    """A TrackPosMarkersFreeRoam.bin body: the 0x34146 chunk, whose payload starts with the 8-byte
    filler cd_parse_markers steps over before its 48-byte records."""
    return chunk(K.ID_MARKERS, b"\x00" * 8 + b"".join(records))


def marker(hash_, slot=0, x=1.0, y=2.0, z=3.0, heading_units=0):
    rec = bytearray(48)
    struct.pack_into("<II", rec, 8, hash_, slot)
    struct.pack_into("<3f", rec, 16, x, y, z)
    struct.pack_into("<H", rec, 32, heading_units)
    return bytes(rec)


def globalb(races=(), stages=(), shops=(), unlocks=(), blob_bytes=b"", shops_size=None,
            races_size=None, unlocks_size=None, drop_races=False, drop_stages=False,
            drop_blob=False, races_last=False):
    """A GLOBALB.BUN body: the 0x80034A10 container with the tables career_data.c looks for.

    shops_size / races_size override those chunks' size fields and the drop_* flags leave a required
    table out, which is how the malformed and missing-table cases are simulated. races_size needs
    races_last: a wrong size misaligns the chunk walk from that chunk onwards, so the tables looked
    up before it (blob, races, stages) must already have been passed - that is the C reader's own
    behaviour, not something the test should fight."""
    inner = b""
    if not drop_blob:
        inner += chunk(K.ID_STRINGS, blob_bytes)
    if races_last:
        if not drop_stages:
            inner += chunk(K.ID_STAGES, b"".join(stages))
        if not drop_races:
            inner += table(K.ID_RACES, b"".join(races), races_size)
    else:
        if not drop_races:
            inner += chunk(K.ID_RACES, b"".join(races))
        if not drop_stages:
            inner += chunk(K.ID_STAGES, b"".join(stages))
    if shops:
        inner += table(K.ID_SHOPS, b"".join(shops), shops_size)
    if unlocks:
        inner += table(K.ID_UNLOCKS, b"".join(unlocks), unlocks_size)
    return chunk(K.ID_CAREER, inner)


def write_file(root, rel, data):
    full = os.path.join(root, rel)
    os.makedirs(os.path.dirname(full), exist_ok=True)
    with open(full, "wb") as handle:
        handle.write(data)


class Fixture(unittest.TestCase):
    """One small container: two races, one stage, one shop, two unlocks, two zones, one marker."""

    NAMES = ("RACE_A", "RACE_B", "LOC_A", "LOC_B", "SHOP_A")

    def setUp(self):
        self.blob, self.off = blob_of(self.NAMES)
        self.hash = dict((n, K.lang_key_audit.ug_hash_upper(n)) for n in self.NAMES)
        self.hash["350Z"] = K.lang_key_audit.ug_hash_upper("350Z")     # a CARS/ folder name
        self.loc_hash = 0x11111111
        self.shop_zone = 0x22222222
        self.races = [
            race(self.off["RACE_A"], self.off["LOC_A"], self.hash["RACE_A"], self.loc_hash,
                 unlock_mode=3, type_=4, unlock_arg=self.hash["RACE_B"], rep_base=100,
                 heats=[(4201, 2, 1)], purse=250, category=3, stage=1, nai=3),
            race(self.off["RACE_B"], self.off["LOC_B"], self.hash["RACE_B"], 0, unlock_mode=0,
                 type_=3, preset_hash=0xf7e19102, category=0, stage=0,
                 shop_hash=self.hash["SHOP_A"], heats=[(4000, 0, 0), (4001, 1, 2)]),
        ]
        self.stages = [stage(index=0, nsponsors=2, sponsors=(0x0a0a0a0a, 0x0b0b0b0b),
                             final_race=self.hash["RACE_A"], req_sponsor=3, req_url=1, req_dvd=0,
                             req_total=7)]
        self.shops = [shop("SHOP_A", self.hash["SHOP_A"], self.shop_zone, platform="CARLOT01.BIN",
                           type_=4, known=1, open_stage=2)]
        self.unlocks = [unlock(self.hash["350Z"], self.hash["RACE_A"], 0),
                        unlock(self.hash["LOC_B"], self.hash["RACE_B"], 0)]

    def container(self, **kw):
        return globalb(self.races, self.stages, self.shops, self.unlocks, self.blob, **kw)

    def dataroot(self, root, zones=None, markers=None, cars=("350Z",)):
        """Writes the files cd_load reads under `root`, plus CARS/<folder> for car-name resolution.

        No LANGUAGES/English.bin: the tests then also cover the dump's behaviour when the string
        table is simply absent."""
        write_file(root, K.GLOBALB, self.container())
        if zones is None:
            zones = [zone(self.loc_hash, 10.0, 20.0, 8.0, 8.0),
                     zone(self.shop_zone, 100.0, 200.0, 25.0, 25.0)]
        write_file(root, K.PATHS, zones_buf(*zones))
        if markers is None:
            markers = [marker(self.hash["SHOP_A"], x=101.0, y=201.0, heading_units=16384)]
        write_file(root, K.MARKERS, markers_buf(*markers))
        for car in cars:
            os.makedirs(os.path.join(root, "CARS", car), exist_ok=True)
        return root


class TestParse(Fixture):

    def load(self, root):
        cd, error = K.load(root)
        self.assertEqual(error, "", "load reported %r" % error)
        return cd

    def test_container_rows(self):
        """The small container parses to the rows the C layout says it should."""
        with tempfile.TemporaryDirectory() as tmp:
            cd = self.load(self.dataroot(tmp))
            self.assertEqual(cd.counts(), {"races": 2, "stages": 1, "zones": 2, "markers": 1,
                                           "shops": 1, "unlocks": 2})
            a, b = cd.races
            self.assertEqual(a["name"], "RACE_A")
            self.assertEqual(a["name_hash"], self.hash["RACE_A"])
            self.assertEqual((a["locator"], a["locator_hash"]), ("LOC_A", self.loc_hash))
            self.assertEqual((a["purse"], a["rep_base"], a["nai"]), (250, 100, 3))
            self.assertEqual((a["kind"], a["type"], a["category"], a["stage"]), ("Drag", 4, 3, 1))
            self.assertEqual(a["heat"], [{"track": 4201, "dir": 2, "laps": 1}]
                             + [{"track": 0, "dir": 0, "laps": 0}] * 3)
            self.assertEqual(a["nheat"], 1)                          # entries with track != 0
            self.assertEqual(b["nheat"], 2)
            self.assertEqual(b["kind"], "Unknown")                   # kind_from_type has no type 3
            self.assertEqual(b["preset_hash"], 0xf7e19102)
            self.assertIs(cd.shop_by_hash(b["shop_hash"]), cd.shops[0])
            row = cd.stages[0]
            self.assertEqual((row["index"], row["nsponsors"], row["sponsor"]),
                             (0, 2, [0x0a0a0a0a, 0x0b0b0b0b]))
            self.assertEqual((row["req_sponsor"], row["req_url"], row["req_dvd"], row["req_total"]),
                             (3, 1, 0, 7))
            self.assertEqual(row["final_race"], self.hash["RACE_A"])
            row = cd.shops[0]
            self.assertEqual((row["name"], row["type"], row["open_stage"], row["known"],
                              row["platform"]), ("SHOP_A", 4, 2, 1, "CARLOT01.BIN"))
            self.assertEqual(cd.unlocks[0], {"car_hash": self.hash["350Z"],
                                             "race": [self.hash["RACE_A"], 0]})
            race_zone, shop_zone = cd.zones
            self.assertEqual((race_zone["hash"], race_zone["cx"], race_zone["cy"]),
                             (self.loc_hash, 10.0, 20.0))
            self.assertEqual((shop_zone["hash"], shop_zone["width"]), (self.shop_zone, 25.0))
            self.assertEqual(cd.markers[0]["hash"], self.hash["SHOP_A"])
            # 16384 of 65536 of a turn, in radians
            self.assertAlmostEqual(cd.markers[0]["heading"], 1.5707963, places=6)

    def test_zone_over_size_limit_dropped(self):
        """A 21 m race locator is over CD_RACE_ZONE_MAX (20 m) and is dropped exactly as the C
        parser drops it, while the same width passes for a shop zone (CD_SHOP_ZONE_MAX, 32 m)."""
        with tempfile.TemporaryDirectory() as tmp:
            root = self.dataroot(tmp, zones=[zone(self.loc_hash, 0.0, 0.0, 21.0, 21.0),
                                             zone(self.shop_zone, 0.0, 0.0, 21.0, 21.0)])
            cd, error = K.load(root)
            self.assertEqual(error, "")
            self.assertEqual([z["hash"] for z in cd.zones], [self.shop_zone])
            # 20 m exactly is still inside the race limit; 33 m is past even the shop one.
            root = self.dataroot(tmp, zones=[zone(self.loc_hash, 0.0, 0.0, 20.0, 20.0),
                                             zone(self.shop_zone, 0.0, 0.0, 33.0, 33.0)])
            cd, _ = K.load(root)
            self.assertEqual([z["hash"] for z in cd.zones], [self.loc_hash])

    def test_zone_shape_rules(self):
        """The scan needs n == 4, the 100 word, a non-zero hash, a bbox equal to the point extent,
        keeps the first record of a repeated hash, and finds a chunk nested in 0x80034147."""
        with tempfile.TemporaryDirectory() as tmp:
            self.dataroot(tmp)
            for bad in (zone(self.loc_hash, 0.0, 0.0, 8.0, 8.0, n=3),
                        zone(self.loc_hash, 0.0, 0.0, 8.0, 8.0, kind=99),
                        zone(0, 0.0, 0.0, 8.0, 8.0)):
                write_file(tmp, K.PATHS, zones_buf(bad))
                cd, error = K.load(tmp)
                self.assertEqual((len(cd.zones), error), (0, "no zones found"))
            mismatch = bytearray(zone(self.loc_hash, 0.0, 0.0, 8.0, 8.0))
            struct.pack_into("<f", mismatch, 0, -999.0)   # bbox stops matching the point extent
            write_file(tmp, K.PATHS, zones_buf(bytes(mismatch)))
            self.assertEqual(K.load(tmp)[1], "no zones found")
            # Nested container, and the first of two records that share a hash wins.
            write_file(tmp, K.PATHS, zones_buf(zone(self.loc_hash, 5.0, 5.0, 8.0, 8.0),
                                              zone(self.loc_hash, 9.0, 9.0, 8.0, 8.0), nested=True))
            cd = self.load(tmp)
            self.assertEqual([z["hash"] for z in cd.zones], [self.loc_hash])
            self.assertEqual(cd.zones[0]["cx"], 5.0)

    def test_marker_rules(self):
        """Markers keep slot 0 of each name only, in first-seen order."""
        with tempfile.TemporaryDirectory() as tmp:
            root = self.dataroot(tmp, markers=[marker(0x33333333, slot=1),
                                               marker(self.hash["SHOP_A"], x=1.0, y=2.0),
                                               marker(0x33333333, slot=0)])
            cd = self.load(root)
            self.assertEqual([m["hash"] for m in cd.markers], [self.hash["SHOP_A"], 0x33333333])

    def test_optional_tables_missing_or_malformed(self):
        """A missing or malformed shop / unlock table gives 0 records, not an error or a crash."""
        cd = K.Career()
        _, error = K.parse_globalb(cd, globalb(self.races, self.stages, (), (), self.blob))
        self.assertEqual((error, len(cd.shops), len(cd.unlocks)), ("", 0, 0))
        # A shop payload whose size is not a whole number of records: parse_shops rejects the table.
        partial = b"".join(self.shops) + b"\x11" * 7
        _, error = K.parse_globalb(cd, globalb(self.races, self.stages, [partial], (), self.blob))
        self.assertEqual((error, len(cd.shops)), ("", 0), "accepted a partial shop record")
        self.assertEqual(len(cd.races), 2, "the required tables must still parse")
        _, error = K.parse_globalb(cd, globalb(self.races, self.stages, (), [b"\x22" * 17],
                                               self.blob, unlocks_size=17))
        self.assertEqual((error, len(cd.unlocks)), ("", 0), "accepted a partial unlock record")
        self.assertEqual(len(cd.races), 2)

    def test_required_tables_missing_or_truncated(self):
        """Races, stages and zones are required: the C reader's failure text comes back, never a
        traceback, and its record count comes back 0."""
        cd = K.Career()
        cases = [
            (globalb(self.races, self.stages, (), (), self.blob, drop_stages=True),
             "no 0x34A18 stages"),
            (globalb(self.races, self.stages, (), (), self.blob, drop_races=True),
             "no 0x34A11 races"),
            (globalb(self.races, self.stages, (), (), self.blob, drop_blob=True),
             "no 0x34A1D string blob"),
            (chunk(0x12345678, b"\x00" * 8), "no 0x80034A10 chunk"),
            (b"", "no 0x80034A10 chunk"),
            (None, "null buffer"),
        ]
        for buf, want in cases:
            count, error = K.parse_globalb(cd, buf)
            self.assertEqual((count, error), (0, want))
        # A race table whose declared size runs past the container is rejected by find_chunk's own
        # size check, i.e. reported as a missing table, exactly as in cd_parse_globalb; a size that
        # fits but is not a whole number of records (0 here) is the "bad race table size" branch.
        _, error = K.parse_globalb(cd, globalb(self.races, self.stages, (), (), self.blob,
                                               races_size=K.RACE_BYTES * 2 + 3, races_last=True))
        self.assertEqual(error, "no 0x34A11 races")
        _, error = K.parse_globalb(cd, globalb(self.races[:1], self.stages, (), (), self.blob,
                                               races_size=0, races_last=True))
        self.assertEqual(error, "bad race table size")
        broken = bytearray(self.races[0])
        struct.pack_into("<H", broken, 0x00, len(self.blob) + 4)
        _, error = K.parse_globalb(cd, globalb([bytes(broken)], self.stages, (), (), self.blob))
        self.assertEqual(error, "bad race name offset")
        self.assertEqual(K.parse_zones(cd, b""), (0, "no 0x3414A chunk"))
        self.assertEqual(K.parse_markers(cd, b""), (0, "no 0x34146 chunk"))
        self.assertEqual(K.parse_zones(cd, chunk(K.ID_ZONES, b"\x00" * 40)), (0, "no zones found"))
        empty = chunk(K.ID_MARKERS, b"\x00" * 4)
        self.assertEqual(K.parse_markers(cd, empty), (0, "no markers found"))

    def test_corrupt_chunk_header_ends_the_walk(self):
        """find_chunk stops at a size field that claims more than the buffer holds, as the C
        original does, rather than walking off into the middle of the file."""
        cd = K.Career()
        buf = struct.pack("<II", K.ID_CAREER, 0xFFFFFFFF) + self.container()[8:]
        self.assertEqual(K.parse_globalb(cd, buf), (0, "no 0x80034A10 chunk"))


class TestDump(Fixture):
    """The rendered tables, JSON and the --race path, on the same synthetic container."""

    def run_main(self, root, *extra):
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            code = K.main(("--data", root) + tuple(extra))
        return code, out.getvalue(), err.getvalue()

    def test_section_tables(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.dataroot(tmp)
            code, out, err = self.run_main(root, "--section", "all")
            self.assertEqual((code, err), (0, ""))
            self.assertIn("races 2 stages 1 zones 2 markers 1 shops 1 unlocks 2", out)
            self.assertIn("race=RACE_A", out)                        # zone ownership resolved
            self.assertIn("shop=SHOP_A", out)
            self.assertIn("4 car lot", out)                          # CD_SHOP_CAR_LOT's own name
            self.assertIn("4201", out)                               # event id
            self.assertIn("250", out)                                # purse
            self.assertIn("language strings: 0 hits", out)           # nothing here is a lang key
            for name in ("races", "stages", "zones", "shops", "unlocks"):
                self.assertIn("== %s ==" % name, out)
            code, out, err = self.run_main(root, "--section", "shops")
            self.assertEqual((code, err), (0, ""))
            self.assertIn("SHOP_A", out)
            self.assertNotIn("RACE_A", out)                          # only the asked-for table

    def test_race_lookup_and_close_matches(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.dataroot(tmp)
            code, out, err = self.run_main(root, "--race", "race_a")  # case-insensitive
            self.assertEqual((code, err), (0, ""))
            self.assertIn("race 0  RACE_A", out)
            self.assertIn("heat 0  event  4201", out)
            self.assertIn("cx 10.0 cy 20.0", out)                    # its locator zone
            self.assertIn("RACE_B", out)                             # unlock_arg resolved
            code, out, err = self.run_main(root, "--race", "RACE_QQQ")
            self.assertEqual(code, 2)
            self.assertIn("no race named RACE_QQQ", err)
            self.assertIn("RACE_A", err)                             # close matches offered
            code, out, err = self.run_main(root, "--race", "RACE_A", "--section", "races")
            self.assertEqual(code, 2)
            self.assertIn("usage error", err)

    def test_race_json(self):
        """--race --json writes that one race, keyed like the races table rows."""
        with tempfile.TemporaryDirectory() as tmp:
            root = self.dataroot(tmp)
            target = os.path.join(tmp, "race.json")
            code, out, err = self.run_main(root, "--race", "RACE_B", "--json", target)
            self.assertEqual((code, err), (0, ""))
            with open(target, encoding="utf-8") as handle:
                data = json.load(handle)
            self.assertEqual(sorted(data), ["counts", "race"])
            self.assertEqual(data["race"]["index"], 1)
            self.assertEqual(data["race"]["name"], "RACE_B")
            self.assertEqual(data["race"]["nheat"], 2)               # both heats, not just heat 0
            self.assertEqual(data["race"]["shop_ref"], "SHOP_A")
            self.assertEqual(data["race"]["preset_ref"], "f7e19102")
            self.assertIsNone(data["race"]["zone_index"])            # no locator hash to match

    def test_json_keys(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = self.dataroot(tmp)
            target = os.path.join(tmp, "out.json")
            code, out, err = self.run_main(root, "--section", "all", "--json", target)
            self.assertEqual((code, err), (0, ""))
            with open(target, encoding="utf-8") as handle:
                data = json.load(handle)
            self.assertEqual(sorted(data), ["counts", "language", "markers", "races",
                                            "races_per_stage", "shops", "stages", "unlocks",
                                            "zones"])
            self.assertEqual(data["counts"], {"races": 2, "stages": 1, "zones": 2, "markers": 1,
                                              "shops": 1, "unlocks": 2})
            self.assertEqual(sorted(data["races"][0]), [
                "category", "category_name", "heat", "hidden", "index", "kind", "locator",
                "locator_hash", "nai", "name", "name_hash", "nheat", "preset_hash", "preset_ref",
                "purse", "rep_base", "shop_hash", "shop_ref", "stage", "suv", "type", "unlock_arg",
                "unlock_mode", "unlock_mode_name", "unlock_ref", "zone_index"])
            self.assertEqual(data["races"][0]["unlock_ref"], "RACE_B")
            self.assertEqual(data["races"][0]["unlock_mode_name"], "url")
            self.assertEqual(data["races"][0]["category_name"], "world")
            self.assertEqual(data["races"][0]["zone_index"], 0)
            self.assertEqual(data["stages"][0]["final_race_ref"], "RACE_A")
            self.assertEqual(data["zones"][0]["owner_races"], ["RACE_A"])
            self.assertEqual(data["zones"][1]["owner_shops"], ["SHOP_A"])
            self.assertEqual(data["shops"][0]["type_name"], "car lot")
            self.assertEqual(data["shops"][0]["marker_index"], 0)
            self.assertEqual(data["unlocks"][0]["race_ref"], ["RACE_A", None])
            self.assertEqual([u["car"] for u in data["unlocks"]],
                             ["350Z", "%08x" % self.hash["LOC_B"]])
            self.assertEqual(data["markers"][0]["name"], "SHOP_A")
            self.assertEqual(data["races_per_stage"], {"counts": [1, 1, 0, 0, 0, 0, 0, 0],
                                                       "stage_byte_out_of_range": 0})
            self.assertEqual(sorted(data["language"]), ["file", "hits", "lookups", "note"])
            self.assertEqual(data["language"]["file"], "LANGUAGES/English.bin")

    def test_car_names_and_missing_language_table(self):
        """Unlock car hashes resolve through CARS/ folders; an absent English.bin is a note, not a
        failure."""
        with tempfile.TemporaryDirectory() as tmp:
            root = self.dataroot(tmp)
            code, out, err = self.run_main(root, "--section", "unlocks")
            self.assertEqual((code, err), (0, ""))
            self.assertIn("350Z", out)                               # a CARS/<folder> name
            self.assertIn("%08x" % self.hash["LOC_B"], out)          # hash kept when it is no car
            self.assertIn("English.bin", out)                        # reported, and still exit 0
        with tempfile.TemporaryDirectory() as tmp:                   # no CARS tree at all
            _, out, _ = self.run_main(self.dataroot(tmp, cars=()), "--section", "unlocks")
            self.assertIn("%08x" % self.hash["350Z"], out)           # hashes only, no invented name

    def test_data_errors(self):
        code, out, err = self.run_main("/nonexistent-openug2-root", "--section", "races")
        self.assertEqual(code, 2)
        self.assertIn("data error", err)
        with tempfile.TemporaryDirectory() as tmp:
            root = self.dataroot(tmp)
            os.remove(os.path.join(root, K.PATHS))
            code, out, err = self.run_main(root, "--section", "races")
            self.assertEqual(code, 2)
            self.assertIn("cannot read", err)
            write_file(tmp, K.PATHS, chunk(K.ID_ZONES, b"\x00" * 8))   # a zone table with no zones
            code, out, err = self.run_main(root, "--section", "races")
            self.assertEqual(code, 2)
            self.assertIn("no zones found", err)


if __name__ == "__main__":
    unittest.main(verbosity=2)
