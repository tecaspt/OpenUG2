#!/usr/bin/env python3
"""List the nodes of a retail .fng layout package (screens and HUDs), the way OpenUG2 reads them.

Every caption, list row and icon the game shows is a node in one of these packages. The game
finds the node it wants by name hash and reads the authored position / size / colour / sample
text from it, so a dump of those fields answers "where is the slot the game uses for X" without
a C probe. Reader ports, in the order the data is touched:

  src/asset_chunks.h    asset_chunks_walk_depth -> asset_chunks_walk (chunk framing)
  src/ugui/ugui_codec.c ug_decompress           -> jdlz_decode / huff_decode
  src/hud_race.c        package_chunk           -> package_bodies (0x30203 / 0x30210 chunks,
                                                   per-chunk decode, synthetic 0x30203 wrap)
  src/hud_asset.c       archives / selected / ha_header_name / blocks / ha_object /
                        ha_hierarchy / ha_package_select / ha_point
  src/ugui/ugui_assets.c assets_open / add_package -> which data files hold packages, and the
                        first-package-of-a-name-wins rule (by name hash, so case-insensitive)

Packages live *inside* bundle files, never as loose .fng files: the frontend bundles
(FRONTEND/FrontB.lzc, GLOBAL/GlobalB.lzc or GLOBALB.BUN, GLOBAL/GLOBALA.BUN) hold the screens and
GLOBAL/InGameRace.bun / InGameDrag.bun / InGameDrift.bun / InGameFreeRoam.bun hold the HUDs
(src/hud.c:527, src/ugui/ugui_assets.c:203-241). Names repeat across files - HUD_SingleRace.fng is
both the free-roam HUD and the circuit HUD, and the post-race screens are pooled into several
bundles - so --list shows one line per occurrence and a dump says which file it read.

A text row also carries the LANGUAGES/English.bin string behind it when one exists, resolved by
task M's reader (tools/lang_key_audit.py, imported rather than copied): either the object's own
"SH" label key, which is what the game substitutes at draw time, or - when the sample text is
itself the label - the entry that sample's hash selects. See label_of.

  python3 -I tools/fng_dump.py --data "$OPENUG2_DATA" --list
  python3 -I tools/fng_dump.py --data "$OPENUG2_DATA" --package HUD_Drag.fng [--json OUT.json]
  python3 -I tools/fng_dump.py --data "$OPENUG2_DATA" --find-sample "Shift"

Exit code: 0 = the request was served, 2 = usage or data error (an unknown package prints close
matches). Two deliberate differences from the game reader, both toward a more useful dump: one
rejected package chunk aborts the whole file there in the game (src/hud_race.c:package_chunk sets
`bad`), while this tool records a note and carries on with the file's other chunks; and ha_point
composes in double where the game composes in float, so a composed point can differ from the drawn
position by ~1e-6 logical units. Everything else - framing, decode, field offsets, hierarchy, the
"exactly one PkHd of that name" selection rule - is the C code's.
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

MASK32 = 0xFFFFFFFF
HA_LIMIT = 4096                            # src/hud_asset.h:7
HA_DEPTH = 64                              # src/hud_asset.h:8
CHUNK_PKG_RAW = 0x00030203                 # UG_CHUNK_PKG_RAW, src/ugui/ugui_bundle.h:45
CHUNK_PKG_HASHED = 0x00030210              # UG_CHUNK_PKG_HASHED, src/ugui/ugui_bundle.h:46
FEN_E7 = bytes((0x46, 0x45, 0x6E, 0xE7))   # "FEn" + 0xE7: package body magic, src/hud_asset.c:40
FOBE = b"FOb\xea"                          # object wrapper magic, src/hud_asset.c:15
CODEC_MAX_OUT = 512 * 1024 * 1024          # UG_CODEC_MAX_OUT, src/ugui/ugui_codec.h:36
PACKAGE_MAX_OUT = 4 * 1024 * 1024          # the bomb bound in src/hud_race.c:package_chunk
SAMPLE_WIDTH = 40                          # the brief's row: sample text cut to 40 characters

KIND_NAMES = {1: "image", 2: "text", 5: "group"}    # HA_KIND_*, src/hud_asset.h:96
JUST_NAMES = {0: "left", 1: "centre", 2: "right"}   # ha_text_start, src/hud_asset.c:120


class FngError(Exception):
    """A malformed input or an unusable data root: reported as a message, never a traceback."""


def u16(buf, off):
    return struct.unpack_from("<H", buf, off)[0]


def u32(buf, off):
    return struct.unpack_from("<I", buf, off)[0]


def f32(buf, off):
    return struct.unpack_from("<f", buf, off)[0]


def f32v(value):
    """Round to the float32 the C reader stores, so --json stays close to the drawn numbers."""
    if not math.isfinite(value):
        return value
    return struct.unpack("<f", struct.pack("<f", value))[0]


def ha_hash(text):
    """Port of ha_hash (src/hud_asset.c:11): ASCII upper-case, h = 33*h + c."""
    h = MASK32
    for byte in text.encode("utf-8", "surrogateescape"):
        if 0x61 <= byte <= 0x7A:
            byte -= 0x20
        h = (h * 33 + byte) & MASK32
    return h


def cstr(buf, off, extent):
    """Port of namecopy (src/hud_asset.c:11): NUL-terminated, None when there is no NUL."""
    if extent <= 0:
        return None
    zone = buf[off:off + extent]
    end = zone.find(b"\x00")
    if end < 0:
        return None
    return zone[:end].decode("latin-1")


# ---- codecs: port of src/ugui/ugui_codec.c --------------------------------------------------

def jdlz_decode(inb):
    """Port of ug_jdlz_decode (src/ugui/ugui_codec.c:25)."""
    if len(inb) < 16 or inb[:4] != b"JDLZ":
        raise FngError("jdlz: bad header")
    dec, enc = struct.unpack_from("<II", inb, 8)
    if dec > CODEC_MAX_OUT:
        raise FngError("jdlz: absurd decoded size")
    end = len(inb)
    if 16 <= enc < len(inb):          # ignore trailing padding
        end = enc
    out = bytearray()
    lit = bytearray()
    pos = 16
    n = 0
    fa, fb = 1, 1                     # flag registers; 1 means "empty"
    while n < dec:
        if fa == 1:
            if pos >= end:
                raise FngError("jdlz: input ends before decoded size is reached")
            fa = inb[pos] + 256
            pos += 1
        if fb == 1:
            if pos >= end:
                raise FngError("jdlz: input ends before decoded size is reached")
            fb = inb[pos] + 256
            pos += 1
        bit_a = fa & 1
        fa >>= 1
        if not bit_a:
            if pos >= end:
                raise FngError("jdlz: input ends before decoded size is reached")
            lit.append(inb[pos])
            pos += 1
            n += 1
            continue
        if pos + 2 > end:
            raise FngError("jdlz: input ends before decoded size is reached")
        p, q = inb[pos], inb[pos + 1]
        pos += 2
        bit_b = fb & 1
        fb >>= 1
        if bit_b:
            dist = (p & 15) + 1
            cnt = ((p >> 4) * 256 + q) + 3
        else:
            dist = ((p >> 5) * 256 + q) + 17
            cnt = (p & 31) + 3
        if dist > n:
            raise FngError("jdlz: match distance before start of output")
        if cnt > dec - n:
            raise FngError("jdlz: copy runs past decoded size")
        if lit:
            out += lit
            del lit[:]
        if dist <= cnt:               # overlapping run: repeat the distance block
            src = bytes(out[-dist:])
            out += (src * (-(-cnt // dist)))[:cnt]
        else:
            out += bytes(out[len(out) - dist:len(out) - dist + cnt])
        n += cnt
    if lit:
        out += lit
    return bytes(out)


class _Bits(object):
    """Port of the Bits bit reader (src/ugui/ugui_codec.c:72): MSB first, 0 once past the end."""

    def __init__(self, data, nbits):
        self.data = data
        self.nbits = nbits
        self.pos = 0
        self.over = False

    def get(self):
        if self.pos >= self.nbits:
            self.over = True
            return 0
        value = (self.data[self.pos >> 3] >> (7 - (self.pos & 7))) & 1
        self.pos += 1
        return value

    def take(self, count):
        value = 0
        for _ in range(count):
            value = (value << 1) | self.get()
        return value

    def varnum(self):
        """z zero bits then a 1; read z+2 bits v; v + 2^(z+2) - 4 (src/ugui/ugui_codec.c:84)."""
        zeros = 0
        while not self.get():
            if self.over or zeros > 24:
                self.over = True
                return 0
            zeros += 1
        v = self.take(zeros + 2)
        return (v + (1 << (zeros + 2)) - 4) & MASK32


def huff_decode(inb):
    """Port of ug_huff_decode (src/ugui/ugui_codec.c:91)."""
    if len(inb) < 16 or inb[:4] != b"HUFF":
        raise FngError("huff: bad header")
    dec_hdr, blen = struct.unpack_from("<II", inb, 8)
    avail = len(inb) - 16
    if blen and blen < avail:
        avail = blen
    bits = _Bits(inb[16:16 + avail], avail * 8)

    method = bits.take(16)
    if (method & 0xFF) != 0xFB or not (0x30 <= (method >> 8) <= 0x35):
        raise FngError("huff: unsupported method")
    if method & 0x0100:
        bits.take(24)
    dec = bits.take(24)
    esc = bits.take(8)
    if dec == 0:
        dec = dec_hdr
    if dec > CODEC_MAX_OUT:
        raise FngError("huff: absurd decoded size")

    cnt = [0] * 17
    full = 0
    maxw = 0
    for w in range(1, 17):
        nw = bits.varnum()
        if bits.over or nw > 256:
            raise FngError("huff: bad code length histogram")
        cnt[w] = nw
        full = full * 2 + nw
        maxw = w
        if (full << (16 - w)) >= 65536:
            break
    if (full << (16 - maxw)) != 65536:
        raise FngError("huff: code space is not full")
    nsym = sum(cnt[1:])
    if nsym > 256:
        raise FngError("huff: too many symbols")

    syms = []
    used = [False] * 256
    prev = 255
    for _ in range(nsym):
        k = bits.varnum()
        if bits.over or k >= 256 - len(syms):
            raise FngError("huff: bad symbol list")
        cur = prev
        for _ in range(k + 1):
            cur = (cur + 1) & 255
            while used[cur]:
                cur = (cur + 1) & 255
        used[cur] = True
        syms.append(cur)
        prev = cur

    first_code = [0] * 17
    first_idx = [0] * 17
    acc = 0
    idx = 0
    for w in range(1, 17):
        first_code[w] = acc >> (16 - w)
        first_idx[w] = idx
        acc = (acc + (cnt[w] << (16 - w))) & MASK32       # uint32 accumulator, as in the C
        idx += cnt[w]

    out = bytearray(dec)
    n = 0
    ended = False
    while n < dec and not ended:
        code = 0
        sym = -1
        for w in range(1, 17):
            code = (code << 1) | bits.get()
            if cnt[w] and code >= first_code[w] and code - first_code[w] < cnt[w]:
                sym = syms[first_idx[w] + (code - first_code[w])]
                break
        if sym < 0 or bits.over:
            raise FngError("huff: corrupt or truncated stream")
        if sym != esc:
            out[n] = sym
            n += 1
            continue
        run = bits.varnum()
        if bits.over:
            raise FngError("huff: truncated stream")
        if run > 0:
            if n == 0 or run > dec - n:
                raise FngError("huff: bad run")
            out[n:n + run] = bytes([out[n - 1]]) * run
            n += run
        elif bits.get():
            ended = True
        else:
            if n >= dec:
                break
            out[n] = bits.take(8)
            n += 1
    if n != dec:
        raise FngError("huff: stream ended before decoded size")
    method_hi = method >> 8
    passes = 2 if method_hi == 0x34 else (1 if method_hi == 0x32 else 0)
    for _ in range(passes):
        for i in range(1, n):
            out[i] = (out[i] + out[i - 1]) & 0xFF
    return bytes(out[:n])


def decompress(inb):
    """Port of ug_decompress: the input back unchanged when no codec names it."""
    magic = inb[:4]
    if magic == b"JDLZ":
        return jdlz_decode(inb)
    if magic == b"HUFF":
        return huff_decode(inb)
    return bytes(inb)


# ---- chunk framing: port of src/asset_chunks.h ----------------------------------------------

def asset_chunks_walk(buf, beg, end, visit, depth=0):
    """Port of asset_chunks_walk_depth: the visitor returns nonzero to descend."""
    if depth >= 64:
        return False
    at = beg
    while at < end:
        if end - at < 8:
            return False
        tag, size = struct.unpack_from("<II", buf, at)
        payload = at + 8
        if size > end - payload:
            return False
        nxt = payload + size
        if visit(buf, tag, payload, nxt) and not asset_chunks_walk(buf, payload, nxt, visit,
                                                                  depth + 1):
            return False
        at = nxt
    return True


# ---- the package reader: port of src/hud_asset.c --------------------------------------------

class Node(object):
    """HaNode (src/hud_asset.h:12) plus the "SH" label hash the HUD reader has no use for but
    src/ugui/ugui_fng.c:186 does; fields the package never wrote keep memset's zeros."""

    __slots__ = ("kind", "name", "id", "parent_id", "parent", "has_parent", "appearance",
                 "valid", "resource", "justification", "color", "pivot", "position",
                 "quaternion", "size", "uv", "sample", "label_hash", "has_label_hash")

    def __init__(self):
        self.kind = 0
        self.name = 0
        self.id = 0
        self.parent_id = 0
        self.parent = -1
        self.has_parent = 0
        self.appearance = 0
        self.valid = 1
        self.resource = 0
        self.justification = 0
        self.color = [0, 0, 0, 0]
        self.pivot = [0.0, 0.0, 0.0]
        self.position = [0.0, 0.0, 0.0]
        self.quaternion = [0.0, 0.0, 0.0, 0.0]
        self.size = [0.0, 0.0, 0.0]
        self.uv = [0.0, 0.0, 0.0, 0.0]
        self.sample = None
        self.label_hash = 0
        self.has_label_hash = 0


class Resource(object):
    """HaResource (src/hud_asset.h:10). kind 1 is an image, 2 a font (src/ugui/ugui_scene.c)."""

    __slots__ = ("id", "kind", "name")

    def __init__(self, rid, kind, name):
        self.id = rid
        self.kind = kind
        self.name = name


class Package(object):
    """HaPackage (src/hud_asset.h:18)."""

    def __init__(self):
        self.nodes = []
        self.resources = []
        self.resource_count = 0
        self.diagnostics = 0


def ha_object(buf, off, n):
    """Port of ha_object (src/hud_asset.c:13).

    Returns (identity, node). identity 0 means the C code drops the object: either the framing
    broke or the object carries no "OP" field, which is the only place id and name come from.
    """
    node = Node()
    if n < 8 or buf[off:off + 4] != FOBE or u32(buf, off + 4) > n - 8:
        return 0, node
    end = 8 + u32(buf, off + 4)
    at = 8
    identity = 0
    appearance_extent = 0
    while at < end:
        if end - at < 8:
            return 0, node
        z = u32(buf, off + at + 4)
        if z > end - at - 8:
            return 0, node
        if buf[off + at:off + at + 4] == b"ObjD":
            q = at + 8
            e = q + z
            while q < e:
                if e - q < 4:
                    return 0, node
                v = u16(buf, off + q + 2)
                b = off + q + 4
                if v > e - q - 4:
                    return 0, node
                field = buf[off + q:off + q + 2]
                if field == b"Ot":
                    if v < 4:
                        return 0, node
                    node.kind = u32(buf, b)
                elif field == b"Oh":
                    if v < 4:
                        return 0, node
                    node.name = u32(buf, b)
                elif field == b"OP":
                    if v < 16:
                        return 0, node
                    node.id = u32(buf, b)
                    node.name = u32(buf, b + 4)
                    node.resource = u16(buf, b + 12)
                    identity = 1
                elif field == b"PA":
                    if v < 4:
                        return 0, node
                    node.has_parent = 1
                    node.parent_id = u32(buf, b)
                elif field == b"Sj":
                    if v < 4:
                        return 0, node
                    node.justification = u32(buf, b) & 0xFFFF
                elif field == b"SH":
                    # src/ugui/ugui_fng.c:186: the key whose language-table string replaces the
                    # sample when the screen is drawn (ug_scene_draw, ug_strtab_or).
                    if v >= 4:
                        node.label_hash = u32(buf, b)
                        node.has_label_hash = 1
                elif field == b"St":
                    if v % 2:
                        return 0, node
                    k = 0
                    while k < v:
                        if u16(buf, b + k) == 0:
                            break
                        k += 2
                    if k == v:
                        return 0, node
                    node.sample = "".join(
                        chr(u16(buf, b + 2 * j)) if u16(buf, b + 2 * j) < 128 else "?"
                        for j in range(k // 2))
                elif field == b"SA":
                    appearance_extent = v
                    need = 84 if node.kind == 1 else 68
                    if v < need:
                        return 0, node
                    for j in range(4):
                        if u32(buf, b + 4 * j) > 255:
                            return 0, node
                    node.color = [u32(buf, b + 8), u32(buf, b + 4), u32(buf, b), u32(buf, b + 12)]
                    for j in range(3):
                        node.pivot[j] = f32(buf, b + 16 + 4 * j)
                        node.position[j] = f32(buf, b + 28 + 4 * j)
                        node.size[j] = f32(buf, b + 56 + 4 * j)
                        if not (math.isfinite(node.pivot[j]) and math.isfinite(node.position[j])
                                and math.isfinite(node.size[j])):
                            return 0, node
                    for j in range(4):
                        node.quaternion[j] = f32(buf, b + 40 + 4 * j)
                        if not math.isfinite(node.quaternion[j]):
                            return 0, node
                        node.uv[j] = f32(buf, b + 68 + 4 * j) if v >= 84 else 0.0
                        if not math.isfinite(node.uv[j]):
                            return 0, node
                    node.appearance = 1
                q += 4 + v
        at += 8 + z
    if node.kind == 1 and node.appearance and appearance_extent < 84:
        return 0, node
    return identity, node


def ha_hierarchy(pkg):
    """Port of ha_hierarchy (src/hud_asset.c:41); returns the C function's ok flag.

    Parent links are by stored id, not by position: a duplicate id invalidates both nodes, a
    parent_id that resolves to nothing invalidates the child, and a chain of HA_DEPTH nodes is
    taken to be a cycle.
    """
    ok = True
    count = len(pkg.nodes)
    for i in range(count):
        node = pkg.nodes[i]
        node.parent = -1
        for j in range(i):
            if pkg.nodes[j].id == node.id:
                node.valid = 0
                pkg.nodes[j].valid = 0
                ok = False
        if node.has_parent:
            for j in range(count):        # no break: the last id match wins, as in the C
                if pkg.nodes[j].id == node.parent_id:
                    node.parent = j
            if node.parent < 0:
                node.valid = 0
                ok = False
    for i in range(count):
        j = i
        depth = 0
        while j >= 0:
            if depth >= HA_DEPTH or not pkg.nodes[j].valid:
                pkg.nodes[i].valid = 0
                ok = False
                break
            depth += 1
            j = pkg.nodes[j].parent
    if not ok:
        pkg.diagnostics += 1
    return ok


def blocks(buf, beg, n, pkg, mode):
    """Port of blocks (src/hud_asset.c:44): mode 0 = region, 1 = resources, 2 = objects."""
    at = 0
    pool = None
    req = None
    pooln = 0
    reqn = 0
    while at < n:
        if n - at < 8:
            return False
        z = u32(buf, beg + at + 4)
        if z > n - at - 8:
            return False
        b = beg + at + 8
        tag = buf[beg + at:beg + at + 4]
        if mode == 1:
            if tag == b"RsNm":
                pool = b
                pooln = z
            elif tag == b"RsRq":
                req = b
                reqn = z
        elif mode == 2:
            if tag == FOBE:
                if len(pkg.nodes) >= HA_LIMIT:
                    return False
                identity, node = ha_object(buf, b - 8, z + 8)
                if identity:
                    pkg.nodes.append(node)
                else:
                    pkg.diagnostics += 1
        elif mode == 0 and (tag == b"Res\xcc" or tag == b"Obj\xcc"):
            if not blocks(buf, b, z, pkg, 1 if tag == b"Res\xcc" else 2):
                return False
        at += 8 + z

    if mode == 1 and req is not None:
        if pkg.resources:
            return False
        if reqn < 4:
            return False
        count = u32(buf, req)
        if count > HA_LIMIT or count > (reqn - 4) // 24 or pool is None:
            return False
        pkg.resource_count = count
        for i in range(count):
            r = req + 4 + i * 24
            off = u32(buf, r + 4)
            rid = u32(buf, r)
            rkind = u32(buf, r + 8)
            name = cstr(buf, pool + off, pooln - off) if off < pooln else None
            if name is None:
                pkg.diagnostics += 1
                rkind = MASK32            # the C marks an unresolvable resource this way
            pkg.resources.append(Resource(rid, rkind, name))
    return True


def ha_header_name(buf, off, n, name):
    """Port of ha_header_name (src/hud_asset.c:50): -1 malformed, 0 not it, 1 that is the name."""
    if n < 24:
        return -1
    names = u32(buf, off + 16)
    paths = u32(buf, off + 20)
    if not names or names > n - 24 or paths > n - 24 - names:
        return -1
    zone = buf[off + 24:off + 24 + names]
    end = zone.find(b"\x00")
    if end < 0:
        return -1
    target = name.encode("utf-8", "surrogateescape")
    return 1 if len(target) == end and zone[:end] == target else 0


def header_name_at(buf, off, n):
    """The stored name of a PkHd chunk (the bounds ha_header_name uses), or None."""
    if n < 24:
        return None
    names = u32(buf, off + 16)
    paths = u32(buf, off + 20)
    if not names or names > n - 24 or paths > n - 24 - names:
        return None
    return cstr(buf, off + 24, names)


def selected(buf, beg, n, name):
    """Port of selected (src/hud_asset.c:52): the name must appear exactly once in the region."""
    at = 0
    found = 0
    while at < n:
        if n - at < 8:
            return -1
        z = u32(buf, beg + at + 4)
        if z > n - at - 8:
            return -1
        if buf[beg + at:beg + at + 4] == b"PkHd":
            r = ha_header_name(buf, beg + at + 8, z, name)
            if r < 0:
                return -1
            if r:
                found += 1
        at += 8 + z
    if found == 1:
        return 1
    return -1 if found > 1 else 0


def region_names(buf, beg, n):
    """Every stored PkHd name in a package region; None when a header is malformed.

    This is selected() without the name filter, for --list: the C reader would only answer
    whether one name matches, and it rejects the whole region on a malformed header.
    """
    names = []
    at = 0
    while at < n:
        if n - at < 8:
            return None
        z = u32(buf, beg + at + 4)
        if z > n - at - 8:
            return None
        if buf[beg + at:beg + at + 4] == b"PkHd":
            name = header_name_at(buf, beg + at + 8, z)
            if name is None:
                return None
            names.append(name)
        at += 8 + z
    return names


def archives(buf, beg, end, name, match, depth=0):
    """Port of archives (src/hud_asset.c:55); `match` is a one-element holder, as in the C."""
    if depth > HA_DEPTH:
        return False
    at = beg
    while at < end:
        if end - at < 8:
            return False
        t = u32(buf, at)
        z = u32(buf, at + 4)
        if z > end - at - 8:
            return False
        b = at + 8
        if t == CHUNK_PKG_RAW:
            if z < 8 or buf[b:b + 4] != FEN_E7 or u32(buf, b + 4) > z - 8:
                return False
            extent = u32(buf, b + 4)
            r = selected(buf, b + 8, extent, name)
            if r < 0:
                return False
            if r:
                if match[0] is not None:
                    return False          # two regions claiming one name: the C refuses both
                match[0] = (b + 8, extent)
        elif t & 0x80000000:
            if not archives(buf, b, b + z, name, match, depth + 1):
                return False
        at = b + z
    return True


def ha_package_select(buf, beg, end, name):
    """Port of ha_package_select (src/hud_asset.c:60): the named package, or None."""
    pkg = Package()
    match = [None]
    if not archives(buf, beg, end, name, match) or match[0] is None:
        return None
    off, extent = match[0]
    if not blocks(buf, off, extent, pkg, 0):
        return None
    ha_hierarchy(pkg)                     # the C ignores the flag and only counts diagnostics
    return pkg


def ha_point(pkg, index, start=(0.0, 0.0, 0.0), angle=0.0):
    """Port of ha_point (src/hud_asset.c:66): `start` composed through the node and its parents.

    Composing the origin (0, 0, 0) gives the point the node's own content is drawn around, in the
    package's logical space (the HUD's is 640x480 centred on 0,0 per ha_fit). A node whose object
    carries no SA field composes through zeros, exactly as the C reader does.
    """
    out = [f32v(float(v)) for v in start]
    i = int(index)
    depth = 0
    while i >= 0 and depth < HA_DEPTH:
        node = pkg.nodes[i]
        scale = node.size if node.kind == 5 else [1.0, 1.0, 1.0]
        v = [f32v((out[j] - node.pivot[j]) * scale[j]) for j in range(3)]
        x, y, z, w = node.quaternion
        norm = x * x + y * y + z * z + w * w
        a = f32v(2.0 / norm) if norm > 0 else 0.0
        r = [
            (1 - a * (y * y + z * z)) * v[0] + a * (x * y - z * w) * v[1] + a * (x * z + y * w) * v[2],
            a * (x * y + z * w) * v[0] + (1 - a * (x * x + z * z)) * v[1] + a * (y * z - x * w) * v[2],
            a * (x * z - y * w) * v[0] + a * (y * z + x * w) * v[1] + (1 - a * (x * x + y * y)) * v[2],
        ]
        if depth == 0 and angle:
            cos_a, sin_a = math.cos(angle), math.sin(angle)
            rx = r[0]
            r[0] = cos_a * rx - sin_a * r[1]
            r[1] = sin_a * rx + cos_a * r[1]
        for j in range(3):
            out[j] = f32v(node.position[j] + node.pivot[j] + r[j])
        depth += 1
        i = node.parent
    return out


# ---- bundle level: src/hud_race.c:package_chunk, src/ugui/ugui_assets.c ----------------------

def resolve_ci(base, rel):
    """Port of resolve_ci (src/ugui/ugui_assets.c:18): case-insensitive, an exact name wins."""
    cur = base
    for part in rel.split("/"):
        if not part:
            continue
        if not os.path.isdir(cur):
            return None
        try:
            entries = os.listdir(cur)
        except OSError:
            return None
        real = None
        for name in entries:
            if name.lower() == part.lower():
                if real is None or name == part:
                    real = name
        if real is None:
            return None
        cur = os.path.join(cur, real)
    return cur if os.path.isfile(cur) else None


# The files the game reads packages from, in the order it reads them: assets_open
# (src/ugui/ugui_assets.c:197-241) walks the frontend and global bundles, then the language
# table, then the in-game bundles (Race, Drag and Drift are concatenated in that order there),
# and hud_init (src/hud.c:527) adds the free-roam HUD bundle. Within one candidate slot the
# first path that exists wins, which is why a .lzc listed before a .BUN shadows it. The other
# files hud.c reads (GLOBAL/HUD_CustomTextures_*.bin, TRACKS/TRACKMAP4000.BIN) only ever feed
# the texture finder, never a package, so they are not scanned; --file covers them if needed.
CANDIDATE_SLOTS = (
    ("FRONTEND/FRONTB.LZC.bacc", "FRONTEND/FrontB.lzc"),
    ("GLOBAL/GLOBALB.LZC.bacc", "GLOBAL/GlobalB.lzc", "GLOBAL/GLOBALB.BUN"),
    ("GLOBAL/GLOBALA.BUN.bacc", "GLOBAL/GLOBALA.BUN"),
    ("LANGUAGES/%(language)s.bin.bacc", "LANGUAGES/%(language)s.bin"),
    ("LANGUAGES/LanguageTextures.bin",),
    ("GLOBAL/InGameCommon.lzc", "GLOBAL/InGameCommon.bun"),
    ("GLOBAL/InGameRace.lzc", "GLOBAL/InGameRace.bun"),
    ("GLOBAL/InGameDrag.lzc", "GLOBAL/InGameDrag.bun"),
    ("GLOBAL/InGameDrift.lzc", "GLOBAL/InGameDrift.bun"),
    ("GLOBAL/InGameFreeRoam.bun",),
)


def candidate_files(data_root, language="English"):
    """One path per candidate slot, first spelling that exists, as the C loaders pick them."""
    picked = []
    for slot in CANDIDATE_SLOTS:
        rels = [rel % {"language": language} for rel in slot]
        for rel in rels:
            path = resolve_ci(data_root, rel)
            if path:
                picked.append((rels[0] if rel == rels[0] else rel, path))
                break
    return picked


def package_bodies(buf):
    """Every package chunk in a bundle buffer, decoded: ([(offset, tag, body)], notes).

    Mirrors src/hud_race.c:package_chunk - 0x30210 carries a 4-byte hash prefix, either tag may
    be JDLZ or HUFF encoded, and a decoded body over PACKAGE_MAX_OUT or under 8 bytes is refused.
    Where the C aborts the whole file on one bad chunk this records a note and continues, so one
    unreadable chunk cannot hide the rest of a bundle.
    """
    found = []
    notes = []

    def visit(data, tag, p, e):
        if tag != CHUNK_PKG_RAW and tag != CHUNK_PKG_HASHED:
            return bool(tag & 0x80000000)
        body = data[p + (4 if tag == CHUNK_PKG_HASHED else 0):e]
        magic = body[:4]
        if magic in (b"JDLZ", b"HUFF"):
            name = magic.decode("latin-1")
            if len(body) < 12:
                notes.append("chunk at 0x%x: short %s header" % (p - 8, name))
                return False
            if magic == b"JDLZ" and u32(body, 8) > PACKAGE_MAX_OUT:
                notes.append("chunk at 0x%x: %s declares %d bytes, over the 4 MB limit"
                             % (p - 8, name, u32(body, 8)))
                return False
            if magic == b"HUFF":
                at = 18 + (3 if (len(body) > 16 and (body[16] & 1)) else 0)
                if len(body) < at + 3:
                    notes.append("chunk at 0x%x: short %s stream size" % (p - 8, name))
                    return False
                declared = (body[at] << 16) | (body[at + 1] << 8) | body[at + 2]
                if declared > PACKAGE_MAX_OUT:
                    notes.append("chunk at 0x%x: %s declares %d bytes, over the 4 MB limit"
                                 % (p - 8, name, declared))
                    return False
            try:
                body = decompress(body)
            except FngError as exc:
                notes.append("chunk at 0x%x: %s" % (p - 8, exc))
                return False
        if len(body) > PACKAGE_MAX_OUT or len(body) < 8:
            notes.append("chunk at 0x%x: package body of %d bytes refused" % (p - 8, len(body)))
            return False
        found.append((p - 8, tag, bytes(body)))
        return False

    if not asset_chunks_walk(buf, 0, len(buf), visit):
        notes.append("chunk walk stopped early: the file's framing is malformed")
    return found, notes


def package_blobs(buf):
    """(offset, tag, name, wrapped) per selectable package in a bundle buffer, plus notes.

    Each body is wrapped in a synthetic 0x30203 header exactly as src/hud_race.c does, so
    ha_package_select sees the framing it expects. A name stored twice in one package is
    dropped here too: selected() refuses it (-1) rather than guess.
    """
    out = []
    notes = []
    bodies, body_notes = package_bodies(buf)
    notes.extend(body_notes)
    for offset, tag, body in bodies:
        wrapped = struct.pack("<II", CHUNK_PKG_RAW, len(body)) + body
        if len(body) < 8 or body[:4] != FEN_E7:
            notes.append("package chunk at 0x%x: no FEn\\xe7 region" % offset)
            continue
        extent = u32(body, 4)
        names = region_names(wrapped, 16, extent)
        if names is None:
            notes.append("package chunk at 0x%x: malformed PkHd header" % offset)
            continue
        if not names:
            notes.append("package chunk at 0x%x: no PkHd header" % offset)
            continue
        for name in sorted(set(names)):
            if names.count(name) != 1:
                notes.append("package chunk at 0x%x: %s is stored %d times, not selectable"
                             % (offset, name, names.count(name)))
                continue
            out.append((offset, tag, name, wrapped))
    return out, notes


class Catalog(object):
    """Every package the game can reach under one data root, with the file it came from."""

    def __init__(self, data_root, language="English", files=None):
        self.data_root = data_root
        self.language = language
        self.notes = []
        self.files = []            # [(rel, path)] actually read, in loader order
        if files:
            picked = []
            for rel in files:
                path = rel if os.path.isabs(rel) else resolve_ci(data_root, rel)
                if path is None or not os.path.isfile(path):
                    raise FngError("--file %s not found" % rel)
                abs_path = os.path.abspath(path)
                shown = os.path.relpath(abs_path, os.path.abspath(data_root))
                picked.append((shown if not shown.startswith("..") else abs_path, abs_path))
            self.files = picked
            return
        self.files = candidate_files(data_root, language)
        if not self.files:
            raise FngError("no package bundle found under %s (looked for %s)"
                           % (data_root, ", ".join(CANDIDATE_SLOTS[0])))

    def blobs(self):
        """Decoded package blobs of every readable file, cached on first use."""
        if not hasattr(self, "_blobs"):
            self._blobs = []
            for rel, path in self.files:
                try:
                    with open(path, "rb") as handle:
                        raw = handle.read()
                    if raw[:4] in (b"JDLZ", b"HUFF"):     # ug_bundle_read decompresses the file
                        raw = decompress(raw)
                    entries, notes = package_blobs(raw)
                except (FngError, OSError, struct.error, ValueError) as exc:
                    self.notes.append("%s: %s" % (rel, exc))
                    continue
                del raw
                self.notes.extend("%s: %s" % (rel, note) for note in notes)
                for offset, tag, name, wrapped in entries:
                    self._blobs.append((rel, offset, tag, name, wrapped))
        return self._blobs

    def names(self):
        """[(name, rel, offset)] for every occurrence, in loader order."""
        return [(name, rel, offset) for rel, offset, tag, name, _ in self.blobs()]

    def select(self, wrapped, name):
        """The parsed package of one blob, or None when the reader refuses it."""
        try:
            return ha_package_select(wrapped, 0, len(wrapped), name)
        except (struct.error, ValueError, IndexError):
            return None

    def language_table(self):
        """{key: text} from LANGUAGES/<language>.bin, or None. Reads task M's loader."""
        if hasattr(self, "_table"):
            return self._table
        self._table = None
        path = resolve_ci(self.data_root, "LANGUAGES/%s.bin" % self.language)
        if path is None:
            self.notes.append("no LANGUAGES/%s.bin: label samples stay unresolved" % self.language)
            return None
        try:
            with open(path, "rb") as handle:
                buf = handle.read()
            offsets = lang_key_audit.strtab_offsets(buf)
            if not offsets:
                raise lang_key_audit.StrTabError("no string table chunk")
            # English.bin carries exactly one table; the first is the one ug_strtab_load filled from.
            self._table = lang_key_audit.load_string_table(buf, offsets[0],
                                                           os.path.basename(path))[2]
        except (lang_key_audit.StrTabError, OSError, struct.error, ValueError) as exc:
            self.notes.append("LANGUAGES/%s.bin: %s: samples stay unresolved" % (self.language, exc))
        return self._table


# ---- rows and output -------------------------------------------------------------------------

def num(value):
    """A short fixed-point spelling that keeps a trailing .0, for the table."""
    text = "%.4f" % value
    if "." in text:
        text = text.rstrip("0")
        if text.endswith("."):
            text += "0"
    return "0.0" if text in ("-0", "0", "-0.0") else text


def quoted(text):
    """The row's quoted sample, cut to SAMPLE_WIDTH characters (JSON keeps the whole string)."""
    if text is None:
        return ""
    cut = text[:SAMPLE_WIDTH]
    return json.dumps(cut, ensure_ascii=False) + ("..." if len(text) > SAMPLE_WIDTH else "")


def label_of(node, table):
    """(string, how) for the language-table entry behind a node, or (None, None).

    Two spellings of "this text is a label" exist in retail data and both are reported:

    "SH"   - the object carries an explicit label key, which is what the game substitutes at draw
             time (src/ugui/ugui_fng.c:186 stores it, src/ugui/ugui_scene.c:577 looks it up with
             ug_strtab_or and falls back to the sample when the key is absent from the table).
    "sample" - the authored sample text *is* the label: hashing it the way the table keys are
             hashed (ug_hash_upper, tools/lang_key_audit.py) finds the entry. The game does not
             do this lookup, so it is reported as the label the string stands for, not as the
             text the screen shows.

    686 nodes resolve by "SH" and 282 by "sample" in the retail set; a node with a key that the
    table does not hold draws its sample text verbatim.
    """
    if not table:
        return None, None
    if node.has_label_hash:
        text = table.get(node.label_hash)
        return (text, "SH") if text is not None else (None, None)
    if node.sample:
        text = table.get(lang_key_audit.ug_hash_upper(node.sample))
        return (text, "sample") if text is not None else (None, None)
    return None, None


def rows_for(pkg, table=None):
    """One dict per node, in file order: the row the brief asks for, and the --json payload."""
    rows = []
    for index, node in enumerate(pkg.nodes):
        alias = None
        res_kind = None
        if node.resource < len(pkg.resources):
            alias = pkg.resources[node.resource].name
            res_kind = pkg.resources[node.resource].kind
        flags = []
        if not node.appearance:
            flags.append("no-SA")          # no authored appearance: the zeros below are absent
        if not node.valid:
            flags.append("invalid")        # ha_hierarchy rejected it (duplicate id, or bad parent)
        if node.resource and alias is None:
            flags.append("no-alias")
        point = ha_point(pkg, index)
        sample = node.sample
        label, label_src = label_of(node, table)
        rows.append({
            "index": index,
            "kind": node.kind,
            "kind_name": KIND_NAMES.get(node.kind, "kind%d" % node.kind),
            "name_hash": "0x%08x" % node.name,
            "name_id": "0x%08x" % node.id,
            "parent": node.parent if node.parent >= 0 else None,
            "flags": flags,
            "justification": node.justification,
            "justification_name": JUST_NAMES.get(node.justification, "bits%d" % node.justification),
            "resource": node.resource,
            "resource_kind": res_kind,
            "resource_alias": alias,
            "position": list(node.position),
            "size": list(node.size),
            "color": list(node.color),
            "point": point,
            "sample": None if sample is None else sample[:SAMPLE_WIDTH],
            "sample_full": sample,
            "label_hash": "0x%08x" % node.label_hash if node.has_label_hash else None,
            "label_src": label_src,          # "SH" | "sample" | None: how label_text was found
            "label_text": label,
        })
    return rows


COLUMNS = (
    ("index", "#", False), ("kind_name", "kind", False), ("name_hash", "name", False),
    ("flags", "flags", False), ("parent", "parent", False),
    ("justification_name", "just", False), ("resource_alias", "alias", False),
    ("pos", "pos x", False), ("posy", "pos y", False), ("posz", "pos z", False),
    ("sizex", "size x", False), ("sizey", "size y", False),
    ("cr", "r", False), ("cg", "g", False), ("cb", "b", False), ("ca", "a", False),
    ("ptx", "pt x", False), ("pty", "pt y", False), ("ptz", "pt z", False),
    ("sample", "sample", False), ("label", "english", False),
)

# The colour columns, in the order ha_object stores them (src/hud_asset.c:22).
COLOR_KEYS = {"cr": 0, "cg": 1, "cb": 2, "ca": 3}


def cell(row, key):
    """One cell of the table; absent appearance fields read '-' so a real 0 cannot be invented."""
    if key == "pos":
        return "-" if "-" in row["_noapp"] else num(row["position"][0])
    if key == "posy":
        return "-" if "-" in row["_noapp"] else num(row["position"][1])
    if key == "posz":
        return "-" if "-" in row["_noapp"] else num(row["position"][2])
    if key == "sizex":
        return "-" if "-" in row["_noapp"] else num(row["size"][0])
    if key == "sizey":
        return "-" if "-" in row["_noapp"] else num(row["size"][1])
    if key in COLOR_KEYS:
        if "-" in row["_noapp"]:
            return "-"
        return str(row["color"][COLOR_KEYS[key]])
    if key == "ptx":
        return num(row["point"][0])
    if key == "pty":
        return num(row["point"][1])
    if key == "ptz":
        return num(row["point"][2])
    if key == "sample":
        return quoted(row["sample_full"])
    if key == "label":
        return quoted(row["label_text"])
    if key == "parent":
        return "-" if row["parent"] is None else str(row["parent"])
    if key == "flags":
        return ",".join(row["flags"])
    return str(row[key])


def render_table(rows, out=None):
    """Aligned text rows; the same data --json writes.

    The stream is resolved when the function runs rather than defaulting to the one that happened
    to be attached at import time, so a caller can redirect sys.stdout (the tests do).
    """
    out = out if out is not None else sys.stdout
    # A node with no SA field stores no appearance at all, so those cells read '-' rather than
    # the zeros the reader left in them.
    prepared = [dict(row, _noapp=["-"] if "no-SA" in row["flags"] else [""]) for row in rows]
    cells = [[cell(row, key) for key, _, _ in COLUMNS] for row in prepared]
    widths = [max(len(head), max((len(r[i]) for r in cells), default=0))
              for i, (_, head, _) in enumerate(COLUMNS)]

    def emit(parts):
        out.write("  ".join(s if i == len(COLUMNS) - 1 else s.ljust(w)
                            for i, (s, w) in enumerate(zip(parts, widths))).rstrip() + "\n")

    emit([head for _, head, _ in COLUMNS])
    for parts in cells:
        emit(parts)


def write_json(path, payload):
    try:
        parent = os.path.dirname(os.path.abspath(path))
        os.makedirs(parent, exist_ok=True)
        with open(path, "w", encoding="utf-8") as handle:
            json.dump(payload, handle, ensure_ascii=False, indent=1)
            handle.write("\n")
    except OSError as exc:
        raise FngError("cannot write %s: %s" % (path, exc))


def print_notes(catalog, out=None):
    """Reader notes on stderr; the stream is resolved at call time so it can be redirected."""
    out = out if out is not None else sys.stderr
    for note in catalog.notes:
        out.write("note: %s\n" % note)


def cmd_list(catalog):
    """One line per package occurrence, then the counts."""
    names = catalog.names()
    for name, rel, offset in sorted(names, key=lambda row: (row[0].lower(), row[1], row[2])):
        print("%-38s %-34s 0x%x" % (name, rel, offset))
    distinct = {}
    for name, rel, offset in names:
        distinct.setdefault(name.lower(), []).append((name, rel))
    print("packages: %d occurrences, %d distinct names, in %d of %d files scanned"
          % (len(names), len(distinct),
             len(set(rel for _, rel, _ in names)), len(catalog.files)))
    for pairs in sorted(distinct.values()):
        if len(pairs) > 1:
            print("note: %s is also in %s"
                  % (pairs[0][0], ", ".join(rel for _, rel in pairs[1:])))
    return 0


def find_name(catalog, name):
    """Every occurrence of a package name: exact first, then the case-insensitive key the game
    uses (ug_assets_package hashes the upper-cased name, src/ugui/ugui_assets.c:ug_assets_package)."""
    exact = [row for row in catalog.blobs() if row[3] == name]
    if exact:
        return exact
    folded = name.lower()
    return [row for row in catalog.blobs() if row[3].lower() == folded]


def cmd_package(catalog, name, text_only, json_out):
    hits = find_name(catalog, name)
    if not hits:
        known = sorted(set(row[3] for row in catalog.blobs()))
        close = difflib.get_close_matches(name, known, n=5, cutoff=0.4)
        sys.stderr.write("data error: no package named %s (%d packages known)\n"
                         % (name, len(known)))
        if close:
            sys.stderr.write("close matches: %s\n" % ", ".join(close))
        return 2
    rel, offset, tag, stored, wrapped = hits[0]
    pkg = catalog.select(wrapped, stored)
    if pkg is None:
        sys.stderr.write("data error: %s in %s at 0x%x is not readable\n" % (stored, rel, offset))
        return 2
    table = catalog.language_table()
    rows = rows_for(pkg, table)
    kept = [row for row in rows if not text_only or row["kind"] == 2]
    print("package %s  file %s  chunk 0x%x (%s)  nodes %d (%d text, %d image, %d group)  "
          "resources %d  diagnostics %d"
          % (stored, rel, offset,
             "hashed" if tag == CHUNK_PKG_HASHED else "raw",
             len(pkg.nodes),
             sum(1 for r in rows if r["kind"] == 2), sum(1 for r in rows if r["kind"] == 1),
             sum(1 for r in rows if r["kind"] == 5), len(pkg.resources), pkg.diagnostics))
    if text_only:
        print("text nodes only (%d of %d)" % (len(kept), len(rows)))
    if len(hits) > 1:
        print("note: the name is also stored in %s"
              % ", ".join("%s at 0x%x" % (row[1], row[2]) for row in hits[1:]))
    render_table(kept)
    print("rows: %d%s" % (len(kept), " of %d nodes" % len(rows) if text_only else ""))
    if json_out:
        write_json(json_out, kept)
    return 0


def cmd_find_sample(catalog, needle, text_only):
    folded = needle.lower()
    table = catalog.language_table()
    hits = 0
    for rel, offset, tag, name, wrapped in catalog.blobs():
        pkg = catalog.select(wrapped, name)
        if pkg is None:
            catalog.notes.append("%s: %s at 0x%x is not readable" % (rel, name, offset))
            continue
        for row in rows_for(pkg, table):
            sample = row["sample_full"] or ""
            label = row["label_text"] or ""
            if folded not in sample.lower() and folded not in label.lower():
                continue
            if text_only and row["kind"] != 2:
                continue
            print("%-34s %-30s %5d  %-5s %s  %s%s"
                  % (rel, name, row["index"], row["kind_name"], row["name_hash"],
                     quoted(sample), ('  -> "%s"' % label) if label else ""))
            hits += 1
    print("nodes: %d match %r in %d packages scanned" % (hits, needle, len(catalog.blobs())))
    return 0


USAGE = """usage:
  fng_dump.py --data "$OPENUG2_DATA" --list
  fng_dump.py --data "$OPENUG2_DATA" --package HUD_Drag.fng [--text-only] [--json OUT.json]
  fng_dump.py --data "$OPENUG2_DATA" --find-sample "Shift"
  fng_dump.py --data "$OPENUG2_DATA" --file GLOBAL/InGameDrag.bun --package HUD_Drag.fng"""


def main(argv=None):
    ap = argparse.ArgumentParser(prog="fng_dump.py",
                                 description=__doc__.splitlines()[0],
                                 epilog=USAGE)
    ap.add_argument("--data", required=True, help="retail data root ($OPENUG2_DATA)")
    goal = ap.add_mutually_exclusive_group(required=True)
    goal.add_argument("--list", action="store_true",
                      help="every package name the game can reach, with its file")
    goal.add_argument("--package", metavar="NAME", help="dump one package's nodes")
    goal.add_argument("--find-sample", metavar="TEXT",
                      help="nodes whose sample text (or resolved label) contains TEXT, all packages")
    ap.add_argument("--text-only", action="store_true", help="keep text nodes only")
    ap.add_argument("--json", dest="json_out", metavar="OUT.json",
                    help="also write the rows as JSON here")
    ap.add_argument("--file", action="append", default=[], metavar="PATH",
                    help="scan only this bundle file (repeatable); default: the files the game reads")
    ap.add_argument("--language", default="English",
                    help="language table for label samples, default English")
    args = ap.parse_args(argv)

    if not os.path.isdir(args.data):
        sys.stderr.write("data error: %s is not a directory\n" % args.data)
        return 2
    try:
        catalog = Catalog(args.data, args.language, args.file)
        if args.list:
            code = cmd_list(catalog)
        elif args.package:
            code = cmd_package(catalog, args.package, args.text_only, args.json_out)
        else:
            code = cmd_find_sample(catalog, args.find_sample, args.text_only)
        print_notes(catalog)
        return code
    except FngError as exc:
        sys.stderr.write("data error: %s\n" % exc)
        return 2


if __name__ == "__main__":
    sys.exit(main())
