#!/usr/bin/env python3
"""Measure the samples of SOUND/FE/FE_MB.abk and SOUND/GLOBAL/FE_COMMON_MB.abk
(OpenUG2 task AQ): length, RMS, onset position, zero-crossing rate and tail level, so the
front-end event->sample mapping in docs/parity/AUDIO.md cites measurable traits instead of
guesses. Parsing and decoding follow src/sfx_bank.c byte for byte: the ABKC header's +0x18
/+0x24 give the BNKl image; samples are "PT\\0" records of big-endian TLVs (0x85 frames,
0x84 rate, 0x88 data offset, 0x82 channels, 0x83 codec, 0x06 level) whose EA-XA stream is a
sequence of 15-byte frames, each (level<<4 | shift) and 14 bytes of signed nibbles. Usage:
  python3 -I tools/sfx_fe_traits.py --data "$OPENUG2_DATA"
Writes nothing; prints one line per sample. Exit 0 when both banks parsed."""
import argparse
import math
import os
import sys

PT_HDR = 4
PT_MAX_SPAN = 0x40
PT_MAX_TLVS = 24
PT_MAX_VLEN = 4
PT_STRIDE = 0x20
EAXA_FRAME = 15
EAXA_SPS = 28
C1 = (0, 240, 460, 392)
C2 = (0, 0, -208, -220)


def rd32(b, p):
    return b[p] | b[p + 1] << 8 | b[p + 2] << 16 | b[p + 3] << 24


def pt_read(bank, p):
    """The PT record at bank[p], or None: {frames, rate, data_off, channels, codec, level}."""
    if bank[p:p + PT_HDR] != b"PT\0\0":
        return None
    q, ntlv, s = p + PT_HDR, 0, {"frames": 0, "rate": 22050, "channels": 1, "codec": 0,
                                 "level": 100, "data_off": None}
    while q + 2 <= len(bank) and q + PT_HDR < p + PT_MAX_SPAN and ntlv < PT_MAX_TLVS:
        tag = bank[q]
        q += 1
        if tag in (0xFF, 0xFE):
            return s if s["frames"] and s["data_off"] is not None else None
        if tag == 0xFD:                                   # TAG_NOVALUE: no length, no value
            ntlv += 1
            continue
        vlen = bank[q]
        q += 1
        if vlen > PT_MAX_VLEN or q + vlen > len(bank):
            return None
        v = int.from_bytes(bank[q:q + vlen], "big")
        q += vlen
        ntlv += 1
        if tag == 0x06:
            s["level"] = v
        elif tag == 0x84:
            s["rate"] = v
        elif tag == 0x85:
            s["frames"] = v
        elif tag == 0x82:
            s["channels"] = v
        elif tag == 0x83:
            s["codec"] = v
        elif tag == 0x88:
            s["data_off"] = v
    return None


def bank_samples(blob):
    """[(frames, rate, data_off, channels, codec)] in bank order, as sfx_bank.c walks them.
    The walk runs to the end of the ABKC file, not the +0x24 field, which understates some
    banks (FE_MB.abk says 0x5c there; the samples and streams clearly live beyond it)."""
    boff = rd32(blob, 0x18)
    bank = blob[boff:]
    out, p, low = [], 0, len(bank)
    while p + PT_HDR < low and p + PT_HDR <= len(bank):
        s = pt_read(bank, p)
        if s is None:
            p += 1
            continue
        low = min(low, s["data_off"])
        out.append(s)
        p += PT_STRIDE
    return bank, out


def eaxa_decode(bank, off, nsamp):
    """EA-XA mono from bank[off:], nsamp samples -- the sfx_bank.c loop."""
    out, prev, prev2, n, d = [], 0, 0, 0, off
    left = len(bank) - off
    while n < nsamp:
        if left < 1:
            break
        if bank[d] == 0xEE:                               # raw s16be + predictor reseed
            take = min(nsamp - n, EAXA_SPS)
            need = 5 + 2 * take
            if left < need:
                break
            for k in range(take):
                v = int.from_bytes(bank[d + 5 + 2 * k:d + 7 + 2 * k], "big", signed=True)
                out.append(v)
            n += take
            prev = int.from_bytes(bank[d + 1:d + 3], "big", signed=True)
            prev2 = int.from_bytes(bank[d + 3:d + 5], "big", signed=True)
            step = min(EAXA_FRAME * 4 + 1 if left >= 61 else left, left)  # EE frame is 61
            step = 61 if left >= 61 else left
            d += step
            left -= step
            continue
        ci, shift = bank[d] >> 4, 20 - (bank[d] & 15)
        if ci > 3 or left < EAXA_FRAME:
            break
        for by in range(1, 15):
            for hi in (1, 0):
                if n >= nsamp:
                    break
                nib = (bank[d + by] >> 4 if hi else bank[d + by]) & 15
                if nib > 7:
                    nib -= 16
                s = ((nib << shift) + prev * C1[ci] + prev2 * C2[ci] + 0x80) >> 8
                s = max(-32768, min(32767, s))
                prev2, prev = prev, s
                out.append(s)
                n += 1
        d += EAXA_FRAME
        left -= EAXA_FRAME
    return out


def traits(pcm, rate):
    n = len(pcm)
    rms = math.sqrt(sum(v * v for v in pcm) / n) / 32768.0
    win = max(1, rate // 100)                             # 10 ms
    env = [math.sqrt(sum(v * v for v in pcm[i:i + win]) / max(1, min(win, n - i))) / 32768.0
           for i in range(0, n, win)]
    peak = max(env) if env else 0.0
    onset = next((k for k, e in enumerate(env) if e >= peak * 0.15), 0) * 0.01
    zc = sum(1 for j in range(1, n) if (pcm[j - 1] < 0) != (pcm[j] < 0))
    tail = min(len(pcm), rate // 10)
    end = sum(abs(v) for v in pcm[-tail:]) / tail / 32768.0 if tail else 0.0
    return dict(rms=rms, dur=n / rate, onset=onset, zcr=zc / (2.0 * n / rate), end=end)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--data", required=True)
    a = ap.parse_args()
    for rel in ("SOUND/FE/FE_MB.abk", "SOUND/GLOBAL/FE_COMMON_MB.abk"):
        path = os.path.join(a.data, rel)
        blob = open(path, "rb").read()
        bank, items = bank_samples(blob)
        print("== %s: %d samples" % (rel, len(items)))
        for i, s in enumerate(items):
            if s["channels"] != 1 or s["codec"] != 0:
                print("%3d  %7d fr %6d Hz  ch %d codec %d (not mono EA-XA)"
                      % (i, s["frames"], s["rate"], s["channels"], s["codec"]))
                continue
            pcm = eaxa_decode(bank, s["data_off"], s["frames"])
            if len(pcm) != s["frames"]:
                print("%3d  %7d fr %6d Hz  (decoded %d)" % (i, s["frames"], s["rate"], len(pcm)))
                continue
            t = traits(pcm, s["rate"])
            print("%3d  %6.3fs  rms %.4f  onset %.2fs  zcr %5.0f Hz  end %.4f  level %d"
                  % (i, t["dur"], t["rms"], t["onset"], t["zcr"], t["end"], s["level"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
