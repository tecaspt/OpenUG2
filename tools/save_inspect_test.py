#!/usr/bin/env python3
"""Unit tests for tools/save_inspect.py. Every byte sequence is built here: the fixtures are
synthetic files written the way the C writers write them (st_container_build, career.c:tlv,
garage.c:gs_encode, st_profile_pack_payload), never copies of anything the game wrote.

  python3 -I tools/save_inspect_test.py -v
"""

import json
import os
import struct
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import save_inspect as si  # noqa: E402

TOOL = os.path.join(os.path.dirname(os.path.abspath(__file__)), "save_inspect.py")


# ------------------------------------------------------------------ synthetic file builders
def ref_crc(data: bytes) -> int:
    """src/settings.c:st_checksum, statement for statement, without the lookup table: the table
    entry for the byte walking out of the top is recomputed by hand each time."""
    if len(data) < 4:
        return 0
    poly = 0x04C11DB7

    def entry(byte):
        r = byte << 24
        for _ in range(8):
            r = ((r << 1) ^ poly) & 0xFFFFFFFF if r & 0x80000000 else (r << 1) & 0xFFFFFFFF
        return r

    r = (~((data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3])) & 0xFFFFFFFF
    for byte in data[4:]:
        r = (((r << 8) & 0xFFFFFFFF) | byte) ^ entry(r >> 24)
    return (~r) & 0xFFFFFFFF


def core(started=1, stage=2, intro=1, bank=2550, rep=700, entered=7, won=7):
    d = bytearray(24)
    d[0], d[1], d[2] = started, stage, intro
    d[4:12] = struct.pack("<q", bank)
    d[12:16] = struct.pack("<I", rep)
    d[16:20] = struct.pack("<I", entered)
    d[20:24] = struct.pack("<I", won)
    return si.cr_tlv(1, bytes(d))


def races(records=((0xDD60E402, 1, 3, 1), (0xDD60E403, 1, 3, 1))):
    body = b"".join(struct.pack("<IBBH", h, bp, fl, at) for h, bp, fl, at in records)
    return si.cr_tlv(2, body)


def car(name=b"240SX"):
    return si.cr_tlv(3, name)


def pose(x=275.0, y=-311.0, z=0.0, heading=1.37232):
    return si.cr_tlv(4, struct.pack("<4f", x, y, z, heading))


def owned(cars=("240SX",), active=0, region=1):
    data = bytes([len(cars), active, region, 0])
    for name in cars:
        data += name.encode("latin-1") + b"\0" * (32 - len(name))
    return si.cr_tlv(5, data)


def tlv(tag, body, reserved=0):
    return struct.pack("<HHI", tag, reserved, len(body)) + body


def career_file(tlvs, schema=2, magic=si.CR_MAGIC):
    return si.container_build(struct.pack("<II", magic, schema), b"".join(tlvs))


def raw_container(a, blk_b, magic=si.CONTAINER_MAGIC, total=None, size_a=None, size_b=None,
                  crc_a=None, crc_b=None, crc_h=None):
    """si.container_build with any one header field left wrong, so a test can break exactly that
    thing and leave the rest of the file byte-for-byte as cr_save_file would have written it (the
    header checksum is recomputed over the damaged bytes unless it is the field under test)."""
    out = bytearray(si.CONTAINER_HEADER + len(a) + len(blk_b))
    out[0:4] = magic
    si.wr32(out, 4, si.CONTAINER_HEADER + len(a) + len(blk_b))
    si.wr32(out, 8, len(a))
    si.wr32(out, 12, len(blk_b))
    si.wr32(out, 16, si.st_checksum(a))
    si.wr32(out, 20, si.st_checksum(blk_b))
    out[si.CONTAINER_HEADER:si.CONTAINER_HEADER + len(a)] = a
    out[si.CONTAINER_HEADER + len(a):] = blk_b
    for off, value in ((4, total), (8, size_a), (12, size_b), (16, crc_a), (20, crc_b)):
        if value is not None:
            si.wr32(out, off, value)
    si.wr32(out, 24, si.st_checksum(bytes(out[:24])) if crc_h is None else crc_h)
    return bytes(out)


CFG_OK = {"body_kit": 1, "hood_style": 0, "spoiler_style": 0, "wheel_style_id": 0,
          "parts": [0] * si.N2_PART_COUNT, "rim_brand": "NFSU", "rim_style": 2}


def garage_file(records, version=1, magic=si.GARAGE_MAGIC):
    return si.container_build(struct.pack("<II", magic, version), b"".join(records))


def garage_rec(car_name="GOLF", **over):
    cfg = dict(CFG_OK)
    cfg.update(over)
    return si.garage_record(car_name.encode("latin-1"), cfg)


def payload(**over):
    """A 54 930-byte profile payload with the typed fields set the way st_profile_pack_payload
    writes them, so a decode test reads back what the packer put in."""
    p = bytearray(si.ST_PAYLOAD_SIZE)
    trax = over.get("trax", [(i, 3) for i in range(si.ST_TRAX_COUNT)])
    for i, (track, mode) in enumerate(trax):
        e = si.OFF_TRAX + i * si.TRAX_SIZE
        p[e:e + 4] = struct.pack("<I", track)
        p[e + 4] = mode
    audio = over.get("audio", {"master": 1.0, "speech_movie": 1.0, "menu_music": 0.7,
                               "game_music": 1.0, "sfx": 1.0, "engine": 1.0, "tire": 1.0,
                               "unk28": 1.0, "unk32": 1.0, "unk36": 1.0, "mode": 1, "unk44": 0,
                               "trax_order": 1, "unk52": 0, "unk56": 0})
    for i, key in enumerate(("master", "speech_movie", "menu_music", "game_music", "sfx", "engine",
                             "tire", "unk28", "unk32", "unk36")):
        p[si.OFF_AUDIO + 4 * i:si.OFF_AUDIO + 4 * i + 4] = struct.pack("<f", audio[key])
    p[si.OFF_AUDIO + 40:si.OFF_AUDIO + 60] = struct.pack("<5I", audio["mode"], audio["unk44"],
                                                         audio["trax_order"], audio["unk52"],
                                                         audio["unk56"])
    g = over.get("gameplay", {"rearview_mirror": 1, "unk1": 0, "speedo_units": 0,
                              "race_map_mode": 2, "explore_map_mode": 2, "jump_camera": 127.5,
                              "unk12": 127.5})
    for i, key in enumerate(("rearview_mirror", "unk1", "speedo_units", "race_map_mode",
                             "explore_map_mode")):
        p[si.OFF_GAMEPLAY + i] = g[key]
    p[si.OFF_GAMEPLAY + 8:si.OFF_GAMEPLAY + 16] = struct.pack("<2f", g["jump_camera"], g["unk12"])
    player = over.get("player", {"gauges": 1, "race_position": 1, "lap_info": 1, "score": 1,
                                 "feedback": 1, "leaderboard": 1, "ask_transmission": 1,
                                 "controller_config": 0, "unk12": 0, "camera": 3, "split_time": 0,
                                 "transmission": 0, "stability_control": 1, "unk23": 0,
                                 "unk24": 1, "unk25": 0, "wheel_range": 1.0,
                                 "road_feedback": 12.4, "force_strength": 1.0})
    for slot in range(2):
        r = si.OFF_PLAYER + slot * si.PLAYER_SIZE
        for i, key in enumerate(("gauges", "race_position", "lap_info", "score", "feedback",
                                 "leaderboard", "ask_transmission")):
            p[r + i] = player[key]
        p[r + 8:r + 12] = struct.pack("<I", player["controller_config"])
        p[r + 12] = player["unk12"]
        p[r + 16:r + 20] = struct.pack("<I", player["camera"])
        for i, key in enumerate(("split_time", "transmission", "stability_control", "unk23",
                                 "unk24", "unk25")):
            p[r + 20 + i] = player[key]
        p[r + 28:r + 40] = struct.pack("<3f", player["wheel_range"], player["road_feedback"],
                                       player["force_strength"])
    p[si.OFF_UNKBLK:si.OFF_UNKBLK + 12] = struct.pack("<fII", 0.5, 0, 0)
    p[si.OFF_UNKBLK + 12] = 0
    p[si.OFF_DIFFICULTY:si.OFF_DIFFICULTY + 4] = struct.pack("<I",
                                                             over.get("career_difficulty", 1))
    p[si.OFF_AUTOSAVE] = over.get("autosave", 1)
    p[si.OFF_PROMPT] = over.get("save_prompt", 0)
    name = over.get("name", "OPENUG2").encode("latin-1")
    p[si.OFF_NAME:si.OFF_NAME + 32] = name + b"\0" * (32 - len(name))
    o = over.get("online", {"show_player_names": 1, "bad_connection_warning": 1, "unk10": 0,
                            "prefs_a": [0] * 5, "prefs_b": [10, 11, 12, 13, 14]})
    p[si.OFF_ONLINE + 8] = o["show_player_names"]
    p[si.OFF_ONLINE + 9] = o["bad_connection_warning"]
    p[si.OFF_ONLINE + 10] = o["unk10"]
    for i in range(5):
        p[si.OFF_ONLINE + 11 + 4 * i:si.OFF_ONLINE + 15 + 4 * i] = struct.pack("<I", o["prefs_a"][i])
        p[si.OFF_ONLINE + 31 + 4 * i:si.OFF_ONLINE + 35 + 4 * i] = struct.pack("<I", o["prefs_b"][i])
    dev = over.get("input", None)
    if dev:
        for slot, d in enumerate(dev):
            if d is None:
                continue
            r = si.OFF_INPUT + slot * si.ST_INPUT_RECORD_SIZE
            p[r:r + 16] = bytes.fromhex(d["guid"])
            for k in range(4):
                p[r + 16 + 4 * k:r + 20 + 4 * k] = struct.pack("<i", d["axis"][k])
                p[r + 32 + 4 * k:r + 36 + 4 * k] = struct.pack("<i", d["dir"][k])
                p[r + 48 + k] = d["assigned"][k]
            p[r + 52] = d["combined_mode"]
            for k in range(32):
                p[r + 53 + 4 * k:r + 57 + 4 * k] = struct.pack("<i", d["binding"][k])
    return bytes(p)


def profile_file(pl=None, version=si.ST_PROFILE_VERSION, a=None):
    block_a = a if a is not None else struct.pack("<II", version, si.ST_PAYLOAD_SIZE)
    return si.container_build(block_a, payload() if pl is None else pl)


def read(data, name=None, car=None, named=False):
    rep = si.Report("test")
    si.inspect_bytes(data, rep, car=car, named=named, name_hint=name)
    return rep


class ChecksumTest(unittest.TestCase):
    def test_table_matches_the_statement_by_statement_port(self):
        for data in (b"", b"\0" * 3, b"\0" * 4, b"20CM", b"hello world", bytes(range(256)),
                     payload()[:4096], os.urandom(1000)):
            self.assertEqual(si.st_checksum(data), ref_crc(data), "on %d bytes" % len(data))

    def test_short_block_checksums_to_zero_like_the_c(self):
        self.assertEqual(si.st_checksum(b"\0\0\0"), 0)

    def test_container_round_trip_self_checks(self):
        blob = career_file([core()])
        self.assertEqual(si.container_parse(blob).size_a, 8)


class ContainerTest(unittest.TestCase):
    def test_short_file_is_an_io_error(self):
        rep = read(career_file([core()])[:27])
        self.assertEqual(rep.status, si.ST_E_IO)
        self.assertIn("shorter than the 28-byte container header", rep.reason)

    def test_foreign_magic(self):
        blob = bytearray(career_file([core()]))
        blob[0:4] = b"30CM"
        rep = read(bytes(blob))
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("container magic", rep.reason)

    def test_truncated_tail_is_a_size_mismatch_at_offset_4(self):
        blob = career_file([core()])
        rep = read(blob[:-1])
        self.assertEqual((rep.status, rep.offset), (si.ST_E_CORRUPT, 4))
        self.assertIn("header size", rep.reason)

    def test_broken_block_b_checksum(self):
        blob = raw_container(struct.pack("<II", si.CR_MAGIC, 2), core(), crc_b=0)
        rep = read(blob)
        self.assertEqual((rep.status, rep.offset), (si.ST_E_CORRUPT, 20))
        self.assertIn("block B checksum", rep.reason)

    def test_broken_block_a_checksum(self):
        blob = raw_container(struct.pack("<II", si.CR_MAGIC, 2), core(), crc_a=0)
        rep = read(blob)
        self.assertEqual((rep.status, rep.offset), (si.ST_E_CORRUPT, 16))
        self.assertIn("block A checksum", rep.reason)

    def test_header_checksum(self):
        blob = bytearray(career_file([core()]))
        blob[24:28] = b"\0\0\0\0"
        rep = read(bytes(blob))
        self.assertEqual((rep.status, rep.offset), (si.ST_E_CORRUPT, 24))

    def test_size_fields_that_do_not_fit_the_file(self):
        blob = raw_container(struct.pack("<II", si.CR_MAGIC, 2), core(), size_b=1 << 20)
        rep = read(blob)
        self.assertEqual((rep.status, rep.offset), (si.ST_E_IO, 8))
        self.assertIn("do not fit in the file", rep.reason)


class CareerTest(unittest.TestCase):
    def test_valid_file_decodes_every_field(self):
        rep = read(career_file([core(), races(), car(), pose(), owned()]))
        self.assertEqual((rep.kind, rep.schema, rep.verdict), ("career", 2, "OK"))
        self.assertEqual(rep.fields["core"], {"started": 1, "stage": 2, "intro_done": 1,
                                              "bank": 2550, "rep": 700, "entered": 7, "won": 7})
        self.assertEqual([r["hash"] for r in rep.fields["races"]],
                         ["0xdd60e402", "0xdd60e403"])
        self.assertEqual(rep.fields["races"][0]["flags_name"], "WON|PAID")
        self.assertEqual(rep.fields["car"], "240SX")
        self.assertAlmostEqual(rep.fields["pose"]["x"], 275.0, places=4)
        self.assertAlmostEqual(rep.fields["pose"]["heading"], 1.37232, places=4)
        self.assertEqual(rep.fields["owned"]["owned"], ["240SX"])
        self.assertEqual(rep.fields["owned"]["region_name"], "B")

    def test_negative_bank_is_signed(self):
        rep = read(career_file([core(bank=-4000)]))
        self.assertEqual(rep.fields["core"]["bank"], -4000)

    def test_truncation_at_every_boundary(self):
        blob = career_file([core(), races(), car(), pose(), owned()])
        cases = [(4, si.ST_E_IO), (27, si.ST_E_IO), (28, si.ST_E_CORRUPT),
                 (36, si.ST_E_CORRUPT), (len(blob) - 40, si.ST_E_CORRUPT),
                 (len(blob) - 1, si.ST_E_CORRUPT)]
        for cut, want in cases:
            rep = read(blob[:cut], name=si.CR_FILE_NAME)
            self.assertEqual(rep.status, want, "cut to %d: %s" % (cut, rep.reason))
            self.assertNotEqual(rep.verdict, "OK")

    def test_record_header_cut_short_inside_block_b(self):
        # A whole container whose block B simply stops in the middle of a record header: the
        # framing is sound, so this is cr_parse's own check, not the container's.
        blob = si.container_build(struct.pack("<II", si.CR_MAGIC, 2), core() + b"\x02\x00\x00\x00")
        rep = read(blob)
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("TLV record header is truncated", rep.reason)
        self.assertEqual(rep.offset, si.CONTAINER_HEADER + 8 + 8 + 24)

    def test_record_length_past_the_end_of_block_b(self):
        body = bytearray(core())
        body[4:8] = struct.pack("<I", 200)                 # CORE claims 200 bytes, has 24
        rep = read(career_file([bytes(body), races()]))
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("runs past the end of block B", rep.reason)

    def test_bad_magic_is_refused_when_the_name_names_the_reader(self):
        blob = career_file([core()], magic=0x5243554E)
        rep = read(blob, name=si.CR_FILE_NAME)
        self.assertEqual(rep.kind, "career")
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("not CR_MAGIC", rep.reason)
        self.assertIn("identified by file name", " ".join(rep.notes))

    def test_bad_magic_without_a_name_is_unrecognised(self):
        blob = career_file([core()], magic=0x5243554E)
        rep = read(blob)
        self.assertTrue(rep.skipped)
        self.assertFalse(rep.fatal)
        rep = read(blob, named=True)
        self.assertTrue(rep.fatal)

    def test_schema_zero_and_future_schema(self):
        rep = read(career_file([core()], schema=0))
        self.assertEqual((rep.status, rep.offset), (si.ST_E_CORRUPT, si.CONTAINER_HEADER + 4))
        rep = read(career_file([core()], schema=3))
        self.assertEqual(rep.status, si.ST_E_INCOMPATIBLE)
        self.assertIn("newer than the game's CR_SCHEMA 2", rep.reason)

    def test_short_known_records(self):
        for tag, body, want in ((1, b"\0" * 23, "CORE record"), (4, b"\0" * 15, "POSE record"),
                                (3, b"\0" * 32, "CAR record")):
            rep = read(career_file([tlv(tag, body)]))
            self.assertEqual(rep.status, si.ST_E_CORRUPT)
            self.assertIn(want, rep.reason)

    def test_races_record_must_be_whole_and_within_the_cap(self):
        # These fixtures carry no OWNED record, so the verdict says so; `.ok` is the question here.
        self.assertTrue(read(career_file([races(((1, 1, 3, 1),))])).ok)
        rep = read(career_file([tlv(2, b"\0" * 9)]))
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("RACES record is 9 bytes", rep.reason)
        rep = read(career_file([tlv(2, b"\0" * (8 * (si.CD_MAX_RACES + 1)))]))
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("CD_MAX_RACES", rep.reason)
        self.assertTrue(read(career_file([tlv(2, b"\0" * (8 * si.CD_MAX_RACES))])).ok)

    def test_owned_record_rules(self):
        for body, want in ((bytes([1, 0, 1, 0]) + b"\0" * 31, "needs 4 + 32*nowned"),
                           (bytes([6, 0, 1, 0]) + b"\0" * (32 * 6), "nowned <= 5"),
                           (bytes([2, 2, 1, 0]) + b"\0" * 64, "not one of the 2 cars"),
                           (bytes([0, 3, 1, 0]), "no cars"),
                           (bytes([0, 0, 1]), "needs 4 + 32*nowned")):
            rep = read(career_file([core(), tlv(5, body)]))
            self.assertEqual(rep.status, si.ST_E_CORRUPT, want)
            self.assertIn(want, rep.reason)

    def test_owned_with_no_cars_is_valid(self):
        rep = read(career_file([core(), tlv(5, bytes([0, 0, 0xFF, 0]))]))
        self.assertEqual(rep.verdict, "OK")
        self.assertEqual(rep.fields["owned"]["nowned"], 0)
        self.assertEqual(rep.fields["owned"]["owned"], [])

    def test_schema_1_reports_the_migration(self):
        rep = read(career_file([core(stage=1), car(b"SKYLINE")], schema=1))
        self.assertEqual(rep.verdict, "OK (migrated from schema 1)")
        self.assertEqual(rep.fields["owned"], {"nowned": 1, "active": 0,
                                              "region": si.CR_REGION_UNKNOWN,
                                              "owned": ["SKYLINE"]})

    def test_schema_1_drops_a_5_even_a_malformed_one(self):
        rep = read(career_file([core(), car(b"SKYLINE"), tlv(5, b"\0\0")], schema=1))
        self.assertEqual(rep.verdict, "OK (migrated from schema 1)")
        self.assertEqual(rep.unknown, [])
        self.assertIn("not defined in schema 1", " ".join(rep.notes))

    def test_schema_2_without_owned_also_migrates(self):
        rep = read(career_file([core(), car(b"SKYLINE")]))
        self.assertEqual(rep.verdict, "OK (migrated: no OWNED record, owned rebuilt from car)")
        self.assertEqual(rep.fields["owned"]["owned"], ["SKYLINE"])

    def test_unknown_tlv_is_shown_not_dropped(self):
        rep = read(career_file([core(), tlv(9, b"\xde\xad\xbe\xef"), owned()]))
        self.assertEqual(rep.verdict, "OK")
        self.assertEqual(len(rep.unknown), 1)
        rec = rep.unknown[0]
        self.assertEqual((rec["tag"], rec["length"], rec["offset"]), (9, 4, 36 + 32))
        self.assertIn("de ad be ef", rec["hex"])
        self.assertIn("preserved TLV record at byte offset 68", "\n".join(rep.render()))

    def test_unknown_tlvs_beyond_extra_are_refused(self):
        big = tlv(7, b"\0" * 3000)
        rep = read(career_file([core(), big, big]))
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("extra[]", rep.reason)

    def test_file_larger_than_the_read_cap(self):
        blob = career_file([core(), tlv(7, b"\0" * (si.CR_BUF_MAX))])
        rep = si.Report("test")
        si.inspect_bytes(blob, rep, size=len(blob))
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("CR_BUF_MAX", rep.reason)

    def test_json_shape(self):
        rep = read(career_file([core(), car(), owned()]))
        data = rep.to_json()
        for key in ("path", "kind", "label", "schema", "status", "code", "ok", "skipped",
                    "verdict", "reason", "offset", "fields", "unknown", "notes"):
            self.assertIn(key, data)
        self.assertEqual(data["kind"], "career")
        self.assertTrue(data["ok"])
        self.assertIsNone(data["reason"])


class GarageTest(unittest.TestCase):
    def test_valid_file_decodes_every_field(self):
        blob = garage_file([garage_rec("GOLF"),
                            garage_rec("G35", body_kit=0, rim_brand="", rim_style=0)])
        rep = read(blob)
        self.assertEqual((rep.kind, rep.schema, rep.verdict), ("garage", 1, "OK"))
        recs = rep.fields["records"]
        self.assertEqual([r["car"] for r in recs], ["GOLF", "G35"])
        self.assertEqual(recs[0]["body_kit"], 1)
        self.assertEqual(recs[0]["rim_brand"], "NFSU")
        self.assertEqual(recs[0]["rim_style"], 2)
        self.assertEqual(recs[0]["parts"], [0] * si.N2_PART_COUNT)
        self.assertEqual(recs[1]["rim_brand"], "")                 # stock: brand empty, style 0

    def test_empty_store_is_valid(self):
        rep = read(garage_file([]))
        self.assertEqual(rep.verdict, "OK")
        self.assertEqual(rep.fields["records"], [])

    def test_unknown_records_are_shown_not_dropped(self):
        blob = garage_file([garage_rec("GOLF"), tlv(2, b"whatever"), tlv(9, b"\0" * 4)])
        rep = read(blob)
        self.assertEqual(rep.verdict, "OK")                        # gs_parse walks past them
        self.assertEqual([r["car"] for r in rep.fields["records"]], ["GOLF"])
        self.assertEqual([(r["tag"], r["length"], r["offset"]) for r in rep.unknown],
                         [("02 00", 8, 28 + 8 + 128), ("09 00", 4, 28 + 8 + 144)])
        self.assertIn("02 00 00 00 08 00 00 00 77 68 61 74 65 76 65 72", rep.unknown[0]["hex"])
        text = "\n".join(rep.render())
        self.assertIn("preserved record at byte offset 164: tag 02 00, length 8", text)

    def test_a_dirty_reserved_field_does_not_make_a_record_unknown(self):
        # gs_parse looks at h[0] and h[1] only - the tag - so a non-zero reserved field (h[2],h[3])
        # is still a car record and still has to be valid.
        body = garage_rec("GOLF")[8:]
        blob = garage_file([struct.pack("<HHI", 1, 0x0200, len(body)) + body])
        rep = read(blob)
        self.assertEqual(rep.verdict, "OK")
        self.assertEqual([r["car"] for r in rep.fields["records"]], ["GOLF"])
        self.assertEqual(rep.unknown, [])

    def test_record_shorter_than_gs_decode_needs(self):
        blob = garage_file([si.cr_tlv(1, garage_rec("GOLF")[8:119])])
        rep = read(blob)
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("gs_decode needs 120", rep.reason)

    def test_record_length_past_the_end_of_block_b(self):
        body = garage_rec("GOLF")[8:]
        blob = garage_file([struct.pack("<HHI", 1, 0, len(body) + 80) + body])
        rep = read(blob)
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("runs past the end of block B", rep.reason)

    def test_trailing_bytes_inside_block_b(self):
        blob = garage_file([garage_rec("GOLF"), b"\0\0\0\0"])
        rep = read(blob)
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("record header is truncated", rep.reason)

    def test_each_garage_config_valid_rule(self):
        cases = [({"body_kit": 100}, "body_kit 100 is outside 0..99"),
                 ({"rim_style": 1000}, "rim_style 1000 is outside 0..999"),
                 ({"hood_style": -1}, "hood_style -1 is outside 0..999"),
                 ({"spoiler_style": 1000}, "spoiler_style 1000 is outside 0..999"),
                 ({"wheel_style_id": 1000}, "wheel_style_id 1000 is outside 0..999"),
                 ({"parts": [0] * 10 + [6000]}, "parts[10] (Roof scoop) 6000 is outside 0..5999"),
                 ({"rim_brand": "nfSU"}, "rim brand name byte 0x6e"),
                 ({"rim_brand": "NFSU" * 8}, "rim_brand has no NUL in its 24-byte field"),
                 ({"rim_brand": "", "rim_style": 3}, "rim_style 3 is set with no rim brand")]
        for over, want in cases:
            rep = read(garage_file([garage_rec("GOLF", **over)]))
            self.assertEqual(rep.status, si.ST_E_CORRUPT, "%r -> %s" % (over, rep.reason))
            self.assertIn(want, rep.reason, str(over))

    def test_car_name_rules(self):
        for name, want in ((b"golf", "outside A-Z 0-9 _"), (b"", "name is empty"),
                           (b"N" * 32, "no NUL terminator")):
            rep = read(garage_file([si.garage_record(name, dict(CFG_OK))]))
            self.assertEqual(rep.status, si.ST_E_CORRUPT, name)
            self.assertIn(want, rep.reason)

    def test_duplicate_car_key_is_refused(self):
        rep = read(garage_file([garage_rec("GOLF"), garage_rec("G35"), garage_rec("GOLF")]))
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("GOLF appears twice", rep.reason)

    def test_an_invalid_record_the_query_never_asks_for_still_refuses_the_file(self):
        rep = read(garage_file([garage_rec("GOLF"), garage_rec("G35", body_kit=500)]), car="GOLF")
        self.assertEqual(rep.status, si.ST_E_CORRUPT)              # gs_parse validates all of them
        self.assertIn("car record at byte offset 164 is invalid: body_kit 500 is outside 0..99",
                      rep.reason)

    def test_version_rules(self):
        for version, want in ((0, si.ST_E_CORRUPT), (2, si.ST_E_INCOMPATIBLE)):
            rep = read(garage_file([garage_rec("GOLF")], version=version))
            self.assertEqual(rep.status, want, "version %d: %s" % (version, rep.reason))
        self.assertIn("newer than the game writes",
                      read(garage_file([], version=3)).reason)

    def test_a_record_beyond_the_length_is_shown_not_dropped(self):
        blob = garage_file([si.cr_tlv(1, garage_rec("GOLF")[8:] + b"\xf0\x0d" * 6)])
        rep = read(blob)
        self.assertEqual(rep.verdict, "OK")                        # the writer keeps the tail as is
        self.assertIn("f0 0d", rep.fields["records"][0]["tail"])

    def test_file_over_the_garage_limit_is_an_io_error(self):
        blob = garage_file([garage_rec("GOLF"), tlv(9, b"\0" * si.GARAGE_SAVE_LIMIT)])
        self.assertGreater(len(blob), si.GARAGE_SAVE_LIMIT)
        rep = read(blob)
        self.assertEqual(rep.status, si.ST_E_IO)                   # gs_read: a file, not a buffer
        self.assertIn("GARAGE_SAVE_LIMIT", rep.reason)

    def test_garage_store_get_answers_for_the_named_car(self):
        blob = garage_file([garage_rec("GOLF")])
        hit = read(blob, car="GOLF")
        self.assertEqual(hit.verdict, "OK")
        self.assertIn("garage_store_get('GOLF') = ST_OK", " ".join(hit.notes))
        miss = read(blob, car="G35")
        self.assertEqual(miss.verdict, "OK")                       # NOTFOUND is not a refusal
        self.assertIn("ST_E_NOTFOUND", " ".join(miss.notes))

    def test_json_lists_records_and_unknown(self):
        data = read(garage_file([garage_rec("GOLF"), tlv(3, b"\1")])).to_json()
        self.assertEqual(data["fields"]["records"][0]["car"], "GOLF")
        self.assertEqual(data["unknown"][0]["kind"], "preserved")


class ProfileTest(unittest.TestCase):
    def test_valid_profile_decodes_every_typed_field(self):
        rep = read(profile_file())
        self.assertEqual((rep.kind, rep.schema, rep.verdict), ("profile", "0x10d", "OK"))
        f = rep.fields
        self.assertEqual(len(f["trax"]), si.ST_TRAX_COUNT)
        self.assertEqual(f["trax"][26], {"track": 26, "mode": 3})
        self.assertAlmostEqual(f["audio"]["menu_music"], 0.7, places=6)
        self.assertEqual(f["audio"]["trax_order"], 1)
        self.assertEqual(f["gameplay"]["explore_map_mode"], 2)
        self.assertAlmostEqual(f["gameplay"]["jump_camera"], 127.5, places=4)
        self.assertEqual(f["player"][0]["camera"], 3)
        self.assertEqual(f["player"][1]["stability_control"], 1)
        self.assertAlmostEqual(f["player"][0]["road_feedback"], 12.4, places=4)
        self.assertEqual(f["name"], "OPENUG2")
        self.assertEqual(f["career_difficulty"], 1)
        self.assertEqual(f["autosave"], 1)
        self.assertEqual(f["online"]["prefs_b"], [10, 11, 12, 13, 14])
        self.assertEqual(len(f["input"]), si.ST_INPUT_SLOTS)
        self.assertNotIn("raw_nonzero", f)                         # nothing outside the typed fields

    def test_input_record_decodes_the_joystick_binding(self):
        dev = [None, {"guid": "11" * 16, "axis": [0, 1, -1, 32767], "dir": [1, 2, 3, 4],
                      "assigned": [1, 1, 0, 0], "combined_mode": 2,
                      "binding": [-1] * 10 + [7] * 22}]
        rep = read(profile_file(payload(input=dev)))
        self.assertEqual(rep.verdict, "OK")
        self.assertEqual(rep.fields["input"][1]["guid"], "11" * 16)
        self.assertEqual(rep.fields["input"][1]["axis"], [0, 1, -1, 32767])
        self.assertEqual(rep.fields["input"][1]["binding"][0], -1)
        self.assertEqual(rep.fields["input"][1]["binding"][15], 7)
        self.assertEqual(rep.fields["input"][0]["binding"], [0] * 32)

    def test_bytes_outside_the_typed_fields_are_reported_as_raw(self):
        pl = bytearray(payload())
        pl[52000] = 0x7F                                           # inside the 2.7 KB raw gap
        pl[52001] = 0x11
        rep = read(profile_file(pl=bytes(pl)))
        self.assertEqual(rep.verdict, "OK")                        # the game keeps them verbatim
        self.assertEqual(rep.fields["raw_nonzero"], ["52000..52002 = 7f 11"])
        self.assertIn("StProfile.raw", " ".join(rep.notes))

    def test_a_foreign_payload_size_is_incompatible(self):
        blob = si.container_build(struct.pack("<II", si.ST_PROFILE_VERSION, 4096), b"\0" * 4096)
        rep = read(blob)
        self.assertEqual(rep.status, si.ST_E_INCOMPATIBLE)
        self.assertIn("54930-byte payloads", rep.reason)

    def test_a_version_the_game_does_not_write_is_incompatible(self):
        rep = read(profile_file(version=0x10E))
        self.assertEqual((rep.status, rep.offset), (si.ST_E_INCOMPATIBLE, si.CONTAINER_HEADER))
        self.assertIn("0x10e", rep.reason)

    def test_block_a_shorter_than_eight_bytes(self):
        blob = si.container_build(struct.pack("<I", si.ST_PROFILE_VERSION), b"\0" * 64)
        rep = read(blob)
        self.assertEqual(rep.status, si.ST_E_INCOMPATIBLE)         # 3.5 step 6, before any field
        self.assertIn("too short for version and payload size", rep.reason)

    def test_a_longer_block_a_is_kept_and_noted(self):
        blob = si.container_build(struct.pack("<II", si.ST_PROFILE_VERSION, si.ST_PAYLOAD_SIZE)
                                  + b"\0" * 8, payload())
        rep = read(blob)
        self.assertEqual(rep.verdict, "OK")
        self.assertIn("8 bytes past its 8-byte header", " ".join(rep.notes))

    def test_broken_payload_checksum(self):
        blob = bytearray(profile_file())
        blob[si.CONTAINER_HEADER + 8 + 50503] ^= 0xFF              # a player flag, crc_b untouched
        rep = read(bytes(blob))
        self.assertEqual((rep.status, rep.offset), (si.ST_E_CORRUPT, 20))
        self.assertIn("block B checksum", rep.reason)

    def test_truncated_profile_file(self):
        blob = profile_file()
        self.assertEqual(read(blob[:27]).status, si.ST_E_IO)
        cut = read(blob[:-16])
        self.assertEqual((cut.status, cut.offset), (si.ST_E_CORRUPT, 4))

    def test_name_field_rules(self):
        long_name = read(profile_file(pl=payload(name="ABCDEFGHIJKLMNOPQRSTUVWXYZ012345")))
        self.assertIn("no NUL", " ".join(long_name.notes))
        odd = read(profile_file(pl=payload(name="ROD.")))
        self.assertIn("st_profile_name_valid", " ".join(odd.notes))
        self.assertEqual(odd.fields["name"], "ROD.")

    def test_layout_note_for_a_misplaced_profile(self):
        with tempfile.TemporaryDirectory() as tmp:
            os.mkdir(os.path.join(tmp, "S7"))
            with open(os.path.join(tmp, "S7", "other"), "wb") as handle:
                handle.write(profile_file())
            rep = si.inspect_path(os.path.join(tmp, "S7", "other"))
            self.assertEqual(rep.verdict, "OK")                    # readable, just not where...
            self.assertIn("st_profile_path", " ".join(rep.notes))  # ...the game would look


INI_HEAD = b"[Software\\EA Games\\Need for Speed Underground 2]\n"

# The shape the game itself writes (settings.ini as st_reg_flush writes it): the key name is bare,
# the value is quoted or 'dword:'. Quoting the key name is a registry-export habit that the game's
# own reader cannot undo - see test_a_quoted_key_name_is_invisible_to_the_store.
INI_OK = (b"[Software\\EA Games\\Need for Speed Underground 2]\r\n"
          b"Install Dir=\"C:\\\\NFSU2\"\r\n"
          b"CacheSize=dword:00000002\r\n"
          b"Language=\"french\"\r\n"
          b"ActiveProfile=\"S7\"\r\n"
          b"NotFirstTime=dword:00000001\r\n"
          b"g_CarShadowEnable=dword:00000001\r\n")


class IniTest(unittest.TestCase):
    def test_quoted_and_dword_values_are_kept(self):
        rep = read(INI_OK)
        self.assertEqual((rep.kind, rep.verdict), ("settings-ini", "OK"))
        self.assertEqual(rep.fields["section"], [si.INI_SECTION])
        values = {v["name"]: v for v in rep.fields["values"]}
        self.assertEqual(values["Install Dir"]["value"], "C:\\NFSU2")   # \\ unescaped to one \
        self.assertEqual(values["CacheSize"]["number"], 2)
        read_by = rep.fields["read_by_the_game"]
        self.assertEqual(read_by["ActiveProfile"], "S7")
        self.assertEqual(read_by["Language"], "french -> index 1")
        self.assertEqual(read_by["NotFirstTime"], "present")
        self.assertEqual(read_by["graphics"], {"g_CarShadowEnable": 1})

    def test_a_quoted_key_name_is_invisible_to_the_store(self):
        # ini_parse_line trims but never unquotes the name, and mem_find compares whole names, so
        # "Language"="french" is filed under '"language"' and st_reg_get_string("Language") finds
        # nothing. Nothing is refused: the value is simply not there as far as the game is aware.
        rep = read(INI_HEAD + b"\"Language\"=\"french\"\n")
        self.assertEqual(rep.verdict, "OK")
        self.assertEqual(rep.fields["values"][0]["name"], '"Language"')
        self.assertNotIn("Language", rep.fields["read_by_the_game"])

    def test_quoted_values_are_written_the_way_the_game_writes_them(self):
        # st_reg_flush escapes a backslash as \\, and ini_parse_line undoes exactly that (an
        # unknown escape loses its backslash, so "C:\NFSU2" would come back as "C:NFSU2").
        rep = read(INI_HEAD + b"ActiveProfile=\"OPENUG2\"\nInstall Dir=\"C:\\\\Games\\\\NFSU2\"\n")
        self.assertEqual(rep.fields["read_by_the_game"]["ActiveProfile"], "OPENUG2")
        self.assertEqual(rep.fields["read_by_the_game"]["Install Dir"], "C:\\Games\\NFSU2")
        odd = read(INI_HEAD + b"Install Dir=\"C:\\NFSU2\"\n")
        self.assertEqual(odd.fields["values"][0]["value"], "C:NFSU2")

    def test_a_bare_value_is_never_seen_by_the_store(self):
        rep = read(INI_HEAD + b"Language=french\n")
        self.assertEqual(rep.verdict, "OK")                        # not an error, just invisible
        self.assertEqual(rep.fields["values"], [])
        self.assertIn("never holds it", " ".join(rep.notes))

    def test_values_outside_the_section_are_skipped(self):
        rep = read(INI_HEAD + b"Language=\"german\"\n[Somewhere Else]\nActiveProfile=\"BOB\"\n")
        self.assertEqual(rep.verdict, "OK")
        self.assertEqual(len(rep.fields["values"]), 1)
        self.assertIn("outside", " ".join(rep.notes))

    def test_a_foreign_section_only_is_noted(self):
        rep = read(b"[Software\\Other Game]\nLanguage=\"german\"\n")
        self.assertEqual(rep.verdict, "OK")                        # read, then ignored
        self.assertEqual(rep.fields["values"], [])
        self.assertIn("not the game's section", " ".join(rep.notes))

    def test_section_header_without_a_bracket_is_not_a_section(self):
        # Called on the reader directly: with no '[...]' line anywhere the tool cannot identify the
        # file by bytes at all (it reports it as foreign), so this is the reader's own rule.
        rep = si.Report("test")
        si.ini_report(b"[Software\\EA Games\\Need for Speed Underground 2\nLanguage=\"german\"\n",
                      rep)
        self.assertEqual(rep.fields["values"], [])
        self.assertIn("section header has no ']'", " ".join(rep.notes))

    def test_last_value_wins_and_the_earlier_one_is_noted(self):
        rep = read(INI_HEAD + b"language=\"german\"\nLANGUAGE=\"spanish\"\n")
        self.assertEqual(len(rep.fields["values"]), 1)             # mem_find matches ignoring case
        self.assertEqual(rep.fields["values"][0]["value"], "spanish")
        self.assertIn("keeps the last value", " ".join(rep.notes))

    def test_one_bad_line_empties_the_whole_store(self):
        for tail, want in ((b"CacheSize=dword:zz\n", "no hexadecimal digits"),
                           (b"CacheSize=dword:1x\n", "trailing"),
                           (b"CacheSize=dword:-1\n", "no hexadecimal digits"),
                           (b"CacheSize=dword:+FFFFFFFFF\n", "wider than a DWORD"),
                           (b"CacheSize=dword:+1\n", None),         # '+' is fine, strtoul takes it
                           (b"Install Dir=\"unterminated\n", "no closing quote"),
                           (b"Install Dir=\"a\\\n", "trailing backslash"),
                           (b"Install Dir=\"a\"junk\n", "after the closing quote"),
                           (b"ba\x01dkey=dword:1\n", "key name holds"),
                           (b"=dword:1\n", "key name is empty")):
            rep = read(INI_HEAD + b"Language=\"german\"\n" + tail)
            if want is None:
                self.assertEqual(rep.verdict, "OK", rep.reason)
                self.assertEqual(rep.fields["values"][-1]["number"], 1)
                continue
            self.assertEqual(rep.status, si.ST_E_CORRUPT, tail)
            self.assertIn(want, rep.reason)

    def test_a_nul_byte_corrupts_the_store(self):                                    # ini_read
        rep = read(INI_OK + b"\0more\n")
        self.assertEqual(rep.status, si.ST_E_CORRUPT)
        self.assertIn("NUL byte", rep.reason)
        self.assertEqual(rep.offset, len(INI_OK))

    def test_missing_equals_is_ignored_not_fatal(self):
        rep = read(INI_HEAD + b"Language\nLanguage=\"dutch\"\n")
        self.assertEqual(rep.verdict, "OK")
        self.assertIn("has no '='", " ".join(rep.notes))

    def test_the_keyboard_table_is_reported_as_cs_kb_load_sees_it(self):
        good = ",".join(str(v) for v in range(1, si.CB_KB_ENTRIES + 1)).encode()
        rep = read(INI_HEAD + b"ControlsKeyboard=\"1:" + good + b"\"\n")
        self.assertEqual(rep.verdict, "OK")
        self.assertIn("53 keys, 0 dropped", rep.fields["read_by_the_game"]["ControlsKeyboard"])
        for tail, want in ((b"ControlsKeyboard=\"1:1,2,3\"\n", "the table holds 53"),
                           (b"ControlsKeyboard=\"1:" + b",".join([b"1"] * 53) + b"\"\n",
                            "52 dropped as duplicates"),
                           (b"ControlsKeyboard=\"2:" + good + b"\"\n",
                            "version prefix is not '1:'")):
            rep = read(INI_HEAD + tail)
            self.assertIn(want, rep.fields["read_by_the_game"]["ControlsKeyboard"])
        oob = b",".join([b"0"] + [b"99999"] + [str(v).encode() for v in range(2, 53)])
        rep = read(INI_HEAD + b"ControlsKeyboard=\"1:" + oob + b"\"\n")
        self.assertIn("2 dropped as out of range",
                      rep.fields["read_by_the_game"]["ControlsKeyboard"])

    def test_an_unrecognised_language_maps_to_minus_one(self):
        rep = read(INI_HEAD + b"Language=\"klingon\"\n")
        self.assertIn("index -1 (unrecognised)", rep.fields["read_by_the_game"]["Language"])

    def test_an_absent_active_profile_defaults_to_the_built_in_name(self):
        rep = read(INI_HEAD + b"Language=\"german\"\n")
        self.assertIn("absent: OPENUG2 is used",
                      rep.fields["read_by_the_game"]["ActiveProfile"])


class CameraTest(unittest.TestCase):
    def test_valid_lines_are_taken(self):
        rep = read(b"camera_option_p1=4\ncamera_option_p2=0\nright_mouse_look=1\n")
        self.assertEqual((rep.kind, rep.verdict), ("camera", "OK"))
        self.assertIsNone(rep.schema)
        self.assertEqual([o["label"] for o in rep.fields["opt"]], ["Super Far", "Bumper"])
        self.assertEqual(rep.fields["mouse_look"], 1)
        self.assertEqual(len(rep.fields["lines_taken"]), 3)

    def test_a_file_the_parser_takes_nothing_from_leaves_the_defaults(self):
        rep = read(b"# my camera file\ncamera_option_p1=9\ncamera_option_p3=1\nrubbish\n")
        self.assertEqual(rep.verdict, "OK")                        # cam_settings_load never refuses
        self.assertEqual([o["opt"] for o in rep.fields["opt"]], [si.CAM_OPT_DEFAULT] * 2)
        self.assertEqual(rep.fields["mouse_look"], 0)
        self.assertIn("the game runs on the defaults", " ".join(rep.notes))
        self.assertIn("matched neither sscanf", " ".join(rep.notes))

    def test_a_tail_after_the_last_digit_is_not_read(self):
        rep = read(b"camera_option_p2=2junk\nright_mouse_look=5\n")
        self.assertEqual(rep.fields["opt"][1]["opt"], 2)           # %d stops at 'j', sscanf is happy
        self.assertEqual(rep.fields["mouse_look"], 0)              # 5 is not 0 or 1
        self.assertEqual(rep.fields["lines_taken"], ["camera_option_p2=2 (Close)"])
        self.assertIn("matched neither sscanf", " ".join(rep.notes))  # the =5 line took nothing

    def test_a_long_line_is_read_in_127_byte_pieces(self):
        # fgets(line, 128) hands back 127 bytes at a time, so the game sees two lines here. The
        # filler is cut so the piece boundary falls exactly before the second option; one byte
        # shorter and the boundary would land inside the key name, which the game also misses.
        blob = b"camera_option_p1=2" + b"x" * 109 + b"right_mouse_look=1\n"
        rep = read(blob)
        self.assertEqual(len(rep.fields["lines_taken"]), 2)
        self.assertEqual(rep.fields["opt"][0]["opt"], 2)
        self.assertEqual(rep.fields["mouse_look"], 1)
        rep = read(b"camera_option_p1=2" + b"x" * 110 + b"right_mouse_look=1\n")
        self.assertEqual(len(rep.fields["lines_taken"]), 1)        # boundary lands mid-key
        self.assertEqual(rep.fields["mouse_look"], 0)


class CliTest(unittest.TestCase):
    def run_tool(self, *argv):
        proc = subprocess.run([sys.executable, "-I", TOOL] + list(argv), capture_output=True,
                              text=True, check=False)
        return proc.returncode, proc.stdout + proc.stderr

    def write(self, folder, name, blob):
        path = os.path.join(folder, name)
        with open(path, "wb") as handle:
            handle.write(blob)
        return path

    def test_exit_zero_on_a_good_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(tmp, "openug2_career.bin", career_file([core(), car()]))
            rc, out = self.run_tool(path)
            self.assertEqual(rc, 0, out)
            self.assertIn("-> OK", out)
            self.assertIn("stage = 2", out)
            self.assertIn("1 file(s): 1 ok, 0 refused, 0 skipped", out)

    def test_exit_one_on_a_refused_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(tmp, "f.bin", career_file([core()], schema=3))
            rc, out = self.run_tool(path)
            self.assertEqual(rc, 1, out)
            self.assertIn("REFUSED (ST_E_INCOMPATIBLE)", out)
            self.assertIn("byte offset 32", out)                   # schema sits 4 bytes into block A

    def test_exit_one_on_a_missing_file(self):
        with tempfile.TemporaryDirectory() as tmp:
            rc, out = self.run_tool(os.path.join(tmp, "nope.bin"))
            self.assertEqual(rc, 1, out)
            self.assertIn("ST_E_NOTFOUND", out)

    def test_usage_errors_exit_two(self):
        with tempfile.TemporaryDirectory() as tmp:
            good = self.write(tmp, "c.bin", career_file([core()]))
            for argv in ((), (good, "--dir", tmp), ("--dir", os.path.join(tmp, "none")),
                         (good, "--car", "golf"), (good, "--car", "N" * 32)):
                rc, out = self.run_tool(*argv)
                self.assertEqual(rc, 2, "%r -> %d\n%s" % (argv, rc, out))

    def test_dir_walk_skips_foreign_files_but_naming_one_is_a_failure(self):
        with tempfile.TemporaryDirectory() as tmp:
            inner = os.path.join(tmp, "S7")
            os.mkdir(inner)
            self.write(inner, "openug2_career.bin", career_file([core(), car()]))
            self.write(inner, "notes.txt", b"just a note, not a save\n")
            rc, out = self.run_tool("--dir", tmp)
            self.assertEqual(rc, 0, out)
            self.assertIn("2 file(s): 1 ok, 0 refused, 1 skipped", out)
            self.assertIn("no OpenUG2 reader opens this", out)
            rc2, out2 = self.run_tool(os.path.join(inner, "notes.txt"))
            self.assertEqual(rc2, 1, out2)

    def test_dir_walk_finds_a_career_file_the_magic_only_half_advertises(self):
        # Block A carries a magic no reader knows, but the game opens this path with cr_load_file,
        # so the report names the reader and gives the real refusal instead of a shrug.
        with tempfile.TemporaryDirectory() as tmp:
            inner = os.path.join(tmp, "S7")
            os.mkdir(inner)
            path = self.write(inner, si.CR_FILE_NAME,
                              si.container_build(struct.pack("<II", 0x12345678, 2), core()))
            rc, out = self.run_tool("--dir", tmp)
            self.assertEqual(rc, 1, out)                           # a real refusal, not a shrug
            self.assertIn("identified by file name, not magic", out)
            self.assertIn("block A magic is 0x12345678", out)

    def test_json_output(self):
        with tempfile.TemporaryDirectory() as tmp:
            inner = os.path.join(tmp, "S7")
            os.mkdir(inner)
            self.write(inner, "openug2_garage.bin", garage_file([garage_rec("GOLF"),
                                                                tlv(4, b"\xaa")]))
            out = os.path.join(tmp, "report.json")
            rc, text = self.run_tool("--dir", inner, "--json", out, "--car", "GOLF")
            self.assertEqual(rc, 0, text)
            with open(out, encoding="utf-8") as handle:
                data = json.load(handle)
            self.assertEqual(data["tool"], "save_inspect")
            self.assertEqual(data["summary"]["exit_code"], 0)
            self.assertEqual(data["summary"]["files"], 1)
            one = data["files"][0]
            self.assertEqual(one["kind"], "garage")
            self.assertEqual(one["fields"]["records"][0]["car"], "GOLF")
            self.assertEqual(one["unknown"][0]["tag"], "04 00")
            self.assertTrue(one["ok"])

    def test_json_written_even_when_a_file_is_refused(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = self.write(tmp, "f.bin", garage_file([], version=7))
            out = os.path.join(tmp, "r.json")
            rc, text = self.run_tool(path, "--json", out)
            self.assertEqual(rc, 1, text)
            with open(out, encoding="utf-8") as handle:
                data = json.load(handle)
            self.assertEqual(data["summary"]["refused"], 1)
            self.assertEqual(data["files"][0]["status"], "ST_E_INCOMPATIBLE")


if __name__ == "__main__":
    unittest.main(verbosity=2)
