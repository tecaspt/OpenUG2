#!/usr/bin/env python3
"""Dump the named-handler tail table of every retail `.abk` bank and correlate it with the
`SOUND/EVT_SYS/*.csi` event-name tables (OpenUG2 task AQ, front-end menu sounds).

Both structures are read as pure data; nothing here comes from an executable.

A `.csi` file is the container `MOIR`: +0x0c a record count, +0x2c the offset of a pool of
NUL-terminated event names that runs to the end of the file behind up to three padding bytes, and
12-byte records (u32 absolute offset of its name, u32 event id, u32 zero). The records sit just
below the name pool; nothing points at them, so the window that wins is the longest run of records
that each land exactly on a pool name -- at +0x2c in six of the eight retail files and at +0x28 in
the two small MW_* ones. The position read is printed, so the derivation stays checkable. The header's count is
not always the whole story (SOUND/EVT_SYS/UG2_ENGINES_AEMS2 says four and its pool holds five
strings), so records beyond it are read while they still check out and any string left over is
reported as unbound. What +0x10 and the other header words mean is not claimed and only printed.

A bank whose handlers are named has them at the end of the file, pointed at by the header's +0x38:
(u32 handler count, u32 96), `count` 8-byte (u32 pointer, u32 marker) records, and then the
handlers themselves, each a four-byte id word immediately followed by its NUL-terminated name, the
record's pointer addressing that word. The id is the word's HIGH two bytes -- SOUND/FE/FE_MB.abk's
`07 2D BC 5A` is id 0x5abc in front of `PlayFrontEndSample_loop`, and the low two bytes (`07 2D`
in the FE banks) are a per-bank tag. The record's first word addresses its id, except where the
record is stored (junk, pointer) and only its second word does (FE_MB record 1: 0xa8, 0x12948c).
The 96 beside the count is not a record stride and is printed rather than interpreted, and +0x38
is zero or ordinary data in the banks with no names.

The two tables meet on the event id: a bank handler's id and a `.csi` record's id are the same
16-bit number, and the names agree exactly. That is checked here for every bank in the install.

Usage:
  python3 -I tools/sfx_fe_tags.py --data "$OPENUG2_DATA"             # full sweep (text)
  python3 -I tools/sfx_fe_tags.py --data "$OPENUG2_DATA" --json OUT  # full sweep (json)
  python3 -I tools/sfx_fe_tags.py --data "$OPENUG2_DATA" --bank SOUND/FE/FE_MB.abk

Exit code 0 = every `.csi` and every `.abk` in the tree parsed; 2 = usage or an unreadable
file; 1 = a file whose layout broke, printed as ERROR.
"""
import argparse
import json
import os
import struct
import sys



def read(path):
    with open(path, "rb") as fh:
        return fh.read()


CSI_HDR = 0x2c                       # the `MOIR` header ends here, and its +0x2c is field 12
CSI_SZ = 12                          # sizeof a record: (u32 name offset, u32 id, u32 zero)


def csi_names(data, path):
    """Walk the pool of NUL-terminated event names at the offset in the `MOIR` header's +0x2c."""
    pool = struct.unpack_from("<I", data, 0x2c)[0]
    if not 0x30 <= pool < len(data):
        raise ValueError("%s: name pool offset 0x%x out of range" % (path, pool))
    names, at = [], pool
    while at < len(data):
        if not data[at]:
            at += 1                                    # the pool's trailing alignment padding
            continue
        stop = data.find(b"\0", at)
        name = data[at:stop].decode("ascii", "replace")
        if stop < 0 or not name or not all(32 <= ord(c) < 127 for c in name):
            raise ValueError("%s: name %r at 0x%x is not printable" % (path, name, at))
        names.append((at, name))
        at = stop + 1
    return pool, names


def csi_records(data, path):
    """Parse one `MOIR` file: returns (first record offset, [(event id, event name)], [unbound names]).

    The header is +0x00 `MOIR`, +0x04 the constant 0x200, +0x0c a record count, +0x10 an id-sized
    word nobody needs here, and +0x2c the offset of the pool of NUL-terminated event names, which
    runs to the end of the file. The records sit between the header and that pool as 12-byte triples,
    (u32 absolute offset of the name, u32 event id, u32 filler), so SOUND/EVT_SYS/MW_MAIN_AEMS reads
    (0x58, 0x13e) at 0x2c -- 0x58 is its first name, `FX_SKID` -- and (0x60, 0x723e) at 0x38.

    The last triple shares its filler word with the first four bytes of the pool, because the pool
    begins as soon as the records end: SOUND/EVT_SYS/UG2_TURBO.csi is (0x34, 0x5ef2) and the filler
    word is 0x545f5846, `FX_T_`, the start of `FX_TURBO_01` at 0x34. Requiring the filler to be zero
    would throw that record away, so it is only required where the triple ends at or before the pool.

    Where the pool does not begin flush on the record run, the filler word is genuinely zero and the
    id is the plain 24-bit word it leaves: SOUND/EVT_SYS/MW_MAIN_AEMS's first record is (0x58, 0x13e,
    0) and the three bytes at 0x58 are not `FX_SKID`'s start, so the id is 0x13e with no overlap.

    The header's count is not always the record run either: SOUND/EVT_SYS/UG2_ENGINES_AEMS2 declares
    four records and holds five names, so the run is read while it checks out -- every name offset
    landing exactly on a pool string -- and a leftover string is reported as unbound rather than given
    an invented id. What +0x10 means is not claimed and only printed.

    Each record is returned with both its plain id and the four-byte word at its own address, which
    is what a bank's handler tail is compared against.
    """
    if data[:4] != b"MOIR":
        raise ValueError("%s: not MOIR" % path)
    declared = struct.unpack_from("<I", data, 0x0c)[0]
    if declared < 1:
        raise ValueError("%s: header count %d" % (path, declared))
    pool, names = csi_names(data, path)
    pool_offs = {addr: nm for addr, nm in names}
    recs, k = [], 0
    while 0x2c + 12 * k + 8 <= pool:
        at = 0x2c + 12 * k
        off, eid = struct.unpack_from("<II", data, at)
        last = at + 12 > pool
        if (off not in pool_offs or not 0 < eid < (1 << 32)
                or (not last and struct.unpack_from("<I", data, at + 8)[0] != 0)):
            break
        name = pool_offs[off]
        recs.append((eid & 0xFFFFFF, struct.unpack_from("<I", data, at)[0], name))
        k += 1
    if not recs:
        raise ValueError("%s: no record at 0x2c names one of the %d pool strings"
                         % (path, len(names)))
    if len(recs) != declared:
        print("note: %s declares %d records, %d name a pool string"
              % (path, declared, len(recs)), file=sys.stderr)
    bound = {n for _, _, n in recs}
    return 0x2c, recs, [n for _, n in names if n not in bound]


def bank_tails(data, path):
    """Read the named-handler tail of one `.abk`: ([{name, event_id, tag, tag_at, name_at,
    record_at, marker}], meta).

    A bank that names its handlers ends with the table its header's +0x38 points at: (u32 handler
    count, u32 96), `count` 8-byte records of (u32 pointer, u32 marker), and then the handlers
    themselves -- each a four-byte event id word immediately followed by its NUL-terminated name.
    An id is the low three bytes of its word, exactly what its `.csi` record carries.

    A record's pointer addresses its id, the name beginning four bytes later. SOUND/FE/FE_MB.abk has
    two handlers, `07 2D BC 5A 50 6c...` (`PlayFrontEndSample_loop`, id 0x5abc) and `07 2D 3D 1E 50
    6c...` (`PlayFrontEndSample`, id 0x1e3d), and its records say 0x129470 and 0x12948c, those two
    ids' addresses. SOUND/GLOBAL/FE_COMMON_MB.abk's single record says 0x28964, likewise, for
    `07 2D 29 26 50 6c...` (`PlayCommonSample`, id 0x2629). The `.csi` table carries the same ids:
    SOUND/EVT_SYS/UG2_FE_AEMS.csi lists PlayFrontEndSample as 0x1e3d and PlayCommonSample as 0x2629.

    The id is the high two bytes of the word -- 0x5ef2 in SOUND/TURBO/TURBO_BIG_1_MB.abk's
    `F2 5E F2 5E`, whose name `FX_TURBO_01` begins at the very next byte -- and every handler in
    the install thus matches a `.csi` record's id exactly. A bank qualifies only if every handler
    it claims checks out: small count, the 96 beside it, every handler an in-file id word in front
    of a printable name, the names unique, and the last of them ending the file up to its alignment
    padding. Everything else is a bank with no named handlers, which is most of them: +0x38 is zero
    in many and ordinary data in the rest. The 96 and the records' marker word (0xf0733a01 in
    FE_MB, 0x4052a501 in FE_COMMON) are printed, not interpreted.
    """
    if data[:4] != b"ABKC":
        raise ValueError("%s: not ABKC" % path)
    size = struct.unpack_from("<I", data, 0x14)[0]
    if size != len(data):
        raise ValueError("%s: header size 0x%x != file 0x%x" % (path, size, len(data)))
    tbl = struct.unpack_from("<I", data, 0x38)[0]
    if not tbl:
        return [], {"tail_ptr": "none"}
    if tbl + 8 > len(data):
        raise ValueError("%s: header +0x38 = 0x%x is outside the file" % (path, tbl))
    count, width = struct.unpack_from("<II", data, tbl)
    plain = {"tail_ptr": "0x%x" % tbl, "count": count, "width_field": width}
    if not 1 <= count <= 64 or width != 96 or tbl + 8 + 8 * count > len(data):
        return [], plain                                   # not a named-handler table

    def name_at(at):
        """The printable NUL-terminated run starting at AT, or None."""
        stop = data.find(b"\0", at)
        nm = data[at:stop].decode("ascii", "replace") if stop >= 0 else ""
        return nm if stop >= 0 and nm and all(32 <= ord(c) < 127 for c in nm) else None

    def handler(at):
        """One handler: the four-byte id word at AT, then the NUL-terminated name at AT+4. The id
        is the word's HIGH two bytes, which is exactly the .csi id: SOUND/FE/FE_MB.abk's
        `07 2D BC 5A` gives 0x5abc, the id UG2_FE_AEMS.csi attaches to PlayFrontEndSample_loop,
        and SOUND/TURBO/TURBO_BIG_1_MB.abk's `F2 5E F2 5E` gives 0x5ef2 = FX_TURBO_01. The low
        two bytes are a per-bank tag (`07 2D` in the FE banks). The name always begins at AT+4:
        in FE_MB the `P` of `PlayFrontEndSample_loop` follows the id word directly."""
        if at + 4 > len(data):
            return None
        word = struct.unpack_from("<I", data, at)[0]
        nm = name_at(at + 4)
        if nm is None or not 0 < (word >> 16) < (1 << 16):
            return None
        return {"name": nm, "event_id": word >> 16, "tag": word,
                "tag_at": "0x%x" % at, "name_at": "0x%x" % (at + 4)}

    def resolve(k, taken):
        """Where record K's handler is: the record's first word addresses its four-byte id word,
        else -- if the second word does -- the record is (junk, pointer): SOUND/FE/FE_MB.abk
        record 1 says (0xa8, 0x12948c), where the first word alone is junk. Else the handler
        follows the record run."""
        w0, w1 = struct.unpack_from("<II", data, tbl + 8 + 8 * k)
        if tbl < w0 < len(data) and w0 not in taken and handler(w0):
            return w0, None
        if tbl < w1 < len(data) and w1 not in taken and handler(w1):
            return w1, "0x%x" % w1
        scan = tbl + 8 + 8 * count
        while scan + 8 <= len(data):
            if scan not in taken and handler(scan):
                return scan, None
            scan += 1
        return None, None

    try:
        out = []
        for k in range(count):
            taken = {int(r["tag_at"], 16) for r in out}
            at, followed = resolve(k, taken)
            if at is None:
                return [], dict(plain, tail_end="record %d addresses no handler" % k)
            found = handler(at)
            found["stale_ptr"] = followed
            found["record_at"] = "0x%x" % (tbl + 8 + 8 * k)
            found["marker"] = "0x%08x" % struct.unpack_from("<I", data, tbl + 12 + 8 * k)[0]
            out.append(found)
    except Exception as why:                               # noqa: BLE001 - any layout surprise
        return [], dict(plain, tail_end=str(why))
    if len({r["name"] for r in out}) != len(out):
        return [], dict(plain, tail_end="duplicate names %r" % [r["name"] for r in out])
    end = max(int(r["name_at"], 16) + len(r["name"]) + 1 for r in out)  # +1: the name's NUL
    while end < len(data) and not data[end]:
        end += 1                                           # the tail's alignment padding
    if end != len(data):
        return [], dict(plain, tail_end="names end at 0x%x, file at 0x%x" % (end, len(data)))
    return out, dict(plain, tail_end="0x%x" % end)


_CSI_TAGS = {}                           # csi record's word at +0x00 -> retail event name


def sweep(root):
    """Read every `.csi` and `.abk` under ROOT/SOUND; returns the parsed tables."""
    csi_files, banks, unnamed = {}, [], 0
    for dirpath, dirs, files in os.walk(os.path.join(root, "SOUND")):
        dirs.sort()
        for fn in sorted(files):
            if not fn.endswith(".csi"):
                continue
            rel = os.path.relpath(os.path.join(dirpath, fn), root)
            data = read(os.path.join(dirpath, fn))
            tbl, recs, unbound = csi_records(data, rel)
            csi_files[rel] = {
                "records_at": ("0x%x" % tbl) if tbl is not None else None,
                "declared": struct.unpack_from("<I", data, 0x0c)[0],
                "head": "0x%x" % struct.unpack_from("<I", data, 0x10)[0],
                "pool": "0x%x" % struct.unpack_from("<I", data, 0x2c)[0],
                "file_size": len(data),
                "events": [{"id": eid, "name": nm, "tag": tag} for eid, tag, nm in recs],
                "unbound_names": unbound,
            }
    events = {}
    for rel, csi in sorted(csi_files.items()):
        for ev in csi["events"]:
            events.setdefault(ev["name"], []).append(dict(ev, csi=rel))
    _CSI_TAGS.update({ev["id"]: nm for nm, evs in events.items() for ev in evs})
    for dirpath, dirs, files in os.walk(os.path.join(root, "SOUND")):
        dirs.sort()
        for fn in sorted(files):
            if not fn.endswith(".abk"):
                continue
            rel = os.path.relpath(os.path.join(dirpath, fn), root)
            recs, meta = bank_tails(read(os.path.join(dirpath, fn)), rel)
            if not recs:
                unnamed += 1
                continue
            for r in recs:                                      # both tables carry the same id:
                r["links"] = sorted("%s %04x" % (e["csi"], e["id"])  # the .csi word and the bank
                    for e in events.get(r["name"], [])            # word's high two bytes
                    if e["id"] == r["event_id"])                  # exactly
                r["event_id"] = "0x%04x" % r["event_id"]
            banks.append(dict(meta, bank=rel, tails=recs))
    return {"csi": csi_files, "named_banks": banks, "banks_without_names": unnamed,
            "event_names": events}


def report(out, root, only):
    """Print the sweep and return it (also what --json writes)."""
    d = sweep(root)
    print("== .csi: %d files, %d distinct event names"
          % (len(d["csi"]), len(d["event_names"])))
    for rel, csi in sorted(d["csi"].items()):
        print("%s  %d records at %s (header says %d), pool %s, %d bytes, +0x10 = %s"
              % (rel, len(csi["events"]), csi["records_at"] or "nowhere", csi["declared"],
                 csi["pool"], csi["file_size"], csi["head"]))
        for ev in csi["events"]:
            print("   %08x  %s" % (ev["id"], ev["name"]))
        for nm in csi["unbound_names"]:
            print("   no id     %s   (a pool name with no record to carry it)" % nm)
    named = sum(len(b["tails"]) for b in d["named_banks"])
    print("\n== %d banks end in named handlers (%d names), %d banks end in none"
          % (len(d["named_banks"]), named, d["banks_without_names"]))
    for b in d["named_banks"]:
        if only and not b["bank"].endswith(only):
            continue
        print("%s  (+0x38 -> %s, header reads (%d, %d), tail runs to %s)"
              % (b["bank"], b["tail_ptr"], b["count"], b["width_field"], b["tail_end"]))
        for r in b["tails"]:
            print("   %-26s id %s  (id at %s, name at %s, record %s marker %s%s)  csi: %s"
                  % (r["name"], r["event_id"], r["tag_at"], r["name_at"],
                     r["record_at"], r["marker"],
                     "" if r.get("stale_ptr") is None else ", pointer %s is stale" % r["stale_ptr"],
                     ", ".join(r["links"]) or "- no id match -"))
    matched = sum(1 for b in d["named_banks"] for r in b["tails"] if r["links"])
    print("\n%d of %d named handlers match a .csi event id"
          % (matched, named))
    return d


def main(argv):
    ap = argparse.ArgumentParser(prog="sfx_fe_tags.py", description=__doc__.splitlines()[0])
    ap.add_argument("--data", required=True, help="retail data root (holds SOUND/)")
    ap.add_argument("--bank", help="restrict the bank listing to this suffix")
    ap.add_argument("--json", help="write the parsed tables here as JSON")
    args = ap.parse_args(argv)
    if not os.path.isdir(os.path.join(args.data, "SOUND")):
        ap.error("%s has no SOUND/" % args.data)
    try:
        d = report(sys.stdout, args.data, args.bank)
    except (ValueError, struct.error) as exc:
        print("ERROR: %s" % exc, file=sys.stderr)
        return 1
    if args.json:
        with open(args.json, "w") as fh:
            json.dump(d, fh, indent=1, sort_keys=True)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
