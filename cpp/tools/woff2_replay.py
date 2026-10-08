#!/usr/bin/env python3
"""Replay the ftk WOFF2 glyf-transform algorithm in Python and compare the
per-glyph results (contours, points, triplet bytes, instruction length) with
fontTools' own reconstruction. Prints the first divergent glyph.
"""
import struct
import sys

import brotli
from fontTools.ttLib import TTFont
from fontTools.ttLib.woff2 import WOFF2Reader, woff2KnownTags


def read_base128(d, p):
    v = 0
    while True:
        b = d[p]
        p += 1
        v = (v << 7) | (b & 0x7F)
        if not (b & 0x80):
            return v, p


def read255(d, p):
    code = d[p]
    p += 1
    if code == 253:
        return struct.unpack(">H", d[p:p + 2])[0], p + 2
    if code == 255:
        return d[p] + 253 * 2, p + 1
    if code == 254:
        return d[p] + 253, p + 1
    return code, p


def main():
    path = sys.argv[1]
    data = open(path, "rb").read()
    (numTables, totalCompressedSize) = struct.unpack(">H", data[12:14])[0], struct.unpack(">I", data[20:24])[0]
    pos = 48
    entries = []
    for _ in range(numTables):
        flags = data[pos]
        pos += 1
        idx = flags & 0x3F
        tv = (flags >> 6) & 3
        tag = data[pos:pos + 4].decode("latin1") if idx == 63 else woff2KnownTags[idx]
        if idx == 63:
            pos += 4
        orig, pos = read_base128(data, pos)
        trans = None
        if (tag in ("glyf", "loca") and tv != 3) or (tag == "hmtx" and tv != 0):
            trans, pos = read_base128(data, pos)
        entries.append((tag, orig, trans))
    raw = brotli.decompress(data[pos:pos + totalCompressedSize])

    o = 0
    glyf = None
    for tag, orig, trans in entries:
        ln = trans if trans is not None else orig
        if tag == "glyf":
            glyf = raw[o:o + ln]
        o += ln
    assert glyf is not None

    opt, numGlyphs, indexFormat = struct.unpack(">HHH", glyf[2:8])
    sizes = struct.unpack(">7I", glyf[8:36])
    (nContourSize, nPointsSize, flagSize, glyphSize, compSize, bboxSize, instrSize) = sizes
    p = 36
    nContourS = glyf[p:p + nContourSize]; p += nContourSize
    nPointsS = glyf[p:p + nPointsSize]; p += nPointsSize
    flagS = glyf[p:p + flagSize]; p += flagSize
    glyphS = glyf[p:p + glyphSize]; p += glyphSize
    compS = glyf[p:p + compSize]; p += compSize
    bboxS = glyf[p:p + bboxSize]; p += bboxSize
    instrS = glyf[p:p + instrSize]; p += instrSize

    bboxBitmapSize = ((numGlyphs + 31) >> 5) << 2

    # ---- reference from fontTools
    font = TTFont(path)
    order = font.getGlyphOrder()
    ref = []
    for gid in range(numGlyphs):
        g = font["glyf"][order[gid]]
        npts = len(g.coordinates) if hasattr(g, "coordinates") and g.coordinates is not None else 0
        ninstr = len(g.program.getBytecode()) if hasattr(g, "program") else 0
        ref.append((g.numberOfContours, npts, ninstr))

    # ---- my algorithm replay
    gp = 0          # glyphStream cursor
    ip = 0          # instructionStream cursor
    fp = 0          # flagStream cursor
    np_ = 0         # nPointsStream cursor
    cp = 0          # compositeStream cursor
    diffs = 0
    for gid in range(numGlyphs):
        ncont = struct.unpack(">h", nContourS[2 * gid:2 * gid + 2])[0]
        if ncont == 0:
            mine = (0, 0, 0)
        elif ncont > 0:
            total = 0
            for _ in range(ncont):
                v, np_ = read255(nPointsS, np_)
                total += v
            for i in range(total):
                flag = flagS[fp]; fp += 1
                flag &= 0x7F
                nBytes = 1 if flag < 84 else 2 if flag < 120 else 3 if flag < 124 else 4
                gp += nBytes
            ilen, gp = read255(glyphS, gp)
            ip += ilen
            mine = (ncont, total, ilen)
        else:
            # composite: cannot easily replay without component parsing; approximate
            mine = (ncont, 0, 0)
        if gid < len(ref) and mine[0] != ref[gid][0]:
            print(f"gid {gid}: contours mine {mine[0]} ref {ref[gid][0]}")
            diffs += 1
        elif gid < len(ref) and mine[0] > 0 and (mine[1] != ref[gid][1] or mine[2] != ref[gid][2]):
            print(f"gid {gid}: contours {mine[0]}  points mine {mine[1]} ref {ref[gid][1]}"
                  f"  instr mine {mine[2]} ref {ref[gid][2]}")
            diffs += 1
        if diffs >= 8:
            break
    print(f"streams: instr used {ip}/{instrSize}, glyph used {gp}/{glyphSize}, flags {fp}/{flagSize}")
    print(f"total glyphs {numGlyphs}; first divergence reported above (diffs={diffs})")


if __name__ == "__main__":
    main()
