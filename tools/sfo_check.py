#!/usr/bin/env python3
"""Parse PARAM.SFO out of an EBOOT.PBP and print every key/value.

Exit 1 if a MEMSIZE key exists with a non-zero value: that requests the
extended (32 MB+) user memory partition, which a PSP-1000 does not have.
(cfwmacros/systemctrl_se.h: "only force highmem on homebrew with MEMSIZE>0".)

Layout was confirmed against real PARAM.SFO files produced by pspsdk's mksfoex
and against SFOHeader/SFODir/SFOtable in psp/sdk/include/pspextratypes.h:
  header  : magic "\\0PSF", version u32, fields_table_offs u32,
            values_table_offs u32, nitems u32          (0x14 bytes)
  entries : SFOtable[] starts IMMEDIATELY after the header (0x14), not at
            fields_table_offs; 16 bytes each:
            field_offs u16, type u16, size u32, size_padded u32, data_offset u32
  key     : fields_table_offs + field_offs
  value   : values_table_offs + data_offset
  type    : in SFODir the u16 is {u8 unk; u8 type}, little-endian, so the TYPE
            IS THE HIGH BYTE: 0x0204 = string, 0x0404 = number. Masking with
            0xff reads the flag byte and mislabels every string as a number.
"""
import struct
import sys

PBP_MAGIC = b"\x00PBP"
SFO_MAGIC = b"\x00PSF"
SFO_HDR = 0x14
TYPE_MASK = 0xFF00
TYPE_SHIFT = 8
TYPE_STRING = 0x02
TYPE_NUMBER = 0x04


def pbp_sfo_range(data):
    """Return (pbp_version, sfo_start, sfo_end) for PARAM.SFO inside a PBP."""
    if len(data) < 0x28 or data[0:4] != PBP_MAGIC:
        raise ValueError("not a PBP (magic %r)" % (data[0:4],))
    version = struct.unpack_from("<I", data, 4)[0]
    offsets = struct.unpack_from("<8I", data, 0x08)
    start = offsets[0]
    # Offset[1] (ICON0) marks where PARAM.SFO ends.
    end = offsets[1] if offsets[1] > start else len(data)
    if start < 0x28 or end > len(data) or end <= start:
        raise ValueError("bad PARAM.SFO range %d..%d in %d bytes" % (start, end, len(data)))
    return version, start, end


def parse_sfo(buf):
    if len(buf) < SFO_HDR or buf[0:4] != SFO_MAGIC:
        raise ValueError("not an SFO (magic %r)" % (buf[0:4],))
    version, fields_off, values_off, count = struct.unpack_from("<IIII", buf, 4)
    if count < 0 or SFO_HDR + 16 * count > len(buf):
        raise ValueError("SFO entry count %d overruns %d bytes" % (count, len(buf)))
    out = []
    for i in range(count):
        field_off, fmt, size, _pad, data_off = struct.unpack_from("<HHIII", buf, SFO_HDR + i * 16)
        k = fields_off + field_off
        if k < 0 or k >= len(buf):
            raise ValueError("SFO key %d points outside (%#x)" % (i, k))
        key = buf[k:buf.index(b"\x00", k)].decode("utf-8", "replace")
        p = values_off + data_off
        if p < 0 or p + size > len(buf):
            raise ValueError("SFO value %s points outside (%#x+%d)" % (key, p, size))
        raw = buf[p:p + size]
        kind = (fmt & TYPE_MASK) >> TYPE_SHIFT
        if kind == TYPE_NUMBER:
            n = (size + 3) // 4
            ints = struct.unpack_from("<%dI" % n, buf, p)
            value = ints[0] if n == 1 else ints
        elif kind == TYPE_STRING:
            value = raw.split(b"\x00", 1)[0].decode("utf-8", "replace")
        else:
            value = raw.hex()
        out.append((key, kind, value))
    return version, out


def main(argv):
    if len(argv) != 2:
        print("usage: sfo_check.py <EBOOT.PBP|PARAM.SFO>", file=sys.stderr)
        return 2
    path = argv[1]
    try:
        with open(path, "rb") as fh:
            data = fh.read()
    except OSError as exc:
        print("sfo_check: %s" % exc, file=sys.stderr)
        return 1

    try:
        if data[0:4] == SFO_MAGIC:          # accept a bare PARAM.SFO too
            sfo_version, entries = parse_sfo(data)
            print("%s: PARAM.SFO v%#x, %d entries" % (path, sfo_version, len(entries)))
        else:
            pbp_version, start, end = pbp_sfo_range(data)
            sfo_version, entries = parse_sfo(data[start:end])
            print("%s: PBP v%#x, PARAM.SFO v%#x, %d bytes at %#x, %d entries"
                  % (path, pbp_version, sfo_version, end - start, start, len(entries)))
    except (ValueError, IndexError, StopIteration) as exc:
        print("sfo_check: %s: %s" % (path, exc), file=sys.stderr)
        return 1

    for key, kind, value in entries:
        print("  %-16s %-6s %s" % (key, "int" if kind == TYPE_NUMBER else "str", value))

    mem = [v for k, f, v in entries if k == "MEMSIZE"]
    if not mem:
        print("sfo_check: OK (no MEMSIZE key: 24 MB user partition on PSP-1000)")
        return 0
    if mem[0] == 0:
        print("sfo_check: OK (MEMSIZE = 0)")
        return 0
    print("sfo_check: FAIL (MEMSIZE = %#x requests extended memory; a PSP-1000 "
          "does not have it)" % mem[0], file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
