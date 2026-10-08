#!/usr/bin/env python3
"""Print ftk vs fontTools outline segments for one glyph (debugging aid)."""
import json
import subprocess
import sys

from fontTools.ttLib import TTFont
from fontTools.pens.recordingPen import DecomposingRecordingPen

from _paths import FTK, FONTS  # noqa: E402


def ftk_segs(path, gid, coords=None):
    cmd = [FTK, "json", path, "--outlines", str(gid)]
    if coords:
        cmd += ["--coords", coords]
    out = subprocess.run(cmd, capture_output=True, text=True)
    if out.returncode != 0:
        print("ftk error:", out.stderr)
        return None
    data = json.loads(out.stdout)
    return data["outlines"].get(str(gid))


def ref_segs(font, gid):
    order = font.getGlyphOrder()
    name = order[gid]
    glyphset = font.getGlyphSet()
    pen = DecomposingRecordingPen(glyphset)
    glyphset[name].draw(pen)
    segs = []
    for op, args in pen.value:
        if op == "moveTo":
            segs.append([0, args[0][0], args[0][1]])
        elif op == "lineTo":
            segs.append([1, args[0][0], args[0][1]])
        elif op == "qCurveTo":
            pts = list(args)
            if pts[-1] is None:
                pts = pts[:-1]
            for i in range(len(pts) - 1):
                c = pts[i]
                if i + 2 == len(pts):
                    end = pts[i + 1]
                else:
                    nxt = pts[i + 1]
                    end = ((c[0] + nxt[0]) / 2.0, (c[1] + nxt[1]) / 2.0)
                segs.append([2, c[0], c[1], end[0], end[1]])
        elif op == "curveTo":
            pts = list(args)
            for i in range(0, len(pts) - 2, 3):
                segs.append([3] + [v for p in pts[i:i + 3] for v in p])
        elif op == "closePath":
            segs.append([4])
    return segs, name


def main():
    path = sys.argv[1]
    gid = int(sys.argv[2])
    coords = sys.argv[3] if len(sys.argv) > 3 else None
    font = TTFont(path, lazy=True)
    mine = ftk_segs(path, gid, coords)
    theirs, name = ref_segs(font, gid)
    print(f"{path} gid {gid} ({name}): ftk {len(mine) if mine else None} segs, "
          f"fontTools {len(theirs)} segs")
    n = max(len(mine or []), len(theirs))
    for i in range(n):
        a = (mine or [])[i] if mine and i < len(mine) else None
        b = theirs[i] if i < len(theirs) else None
        marker = "  "
        if a is None or b is None:
            marker = "**"
        else:
            if len(a) != len(b) or int(a[0]) != int(b[0]):
                marker = "**"
            else:
                for k in range(1, len(a)):
                    if abs(float(a[k]) - float(b[k])) > 0.02:
                        marker = "**"
                        break
        print(f"{marker} [{i:3d}] ftk={a}  ref={b}")
    # raw glyph data for TrueType
    if "glyf" in font:
        g = font["glyf"][name]
        print("glyf:", "composite" if g.isComposite() else "simple",
              "contours:", getattr(g, "numberOfContours", None),
              "points:", len(g.coordinates) if hasattr(g, "coordinates") else None)
        if hasattr(g, "coordinates"):
            print("coords:", list(g.coordinates)[:20])
            print("flags :", list(g.flags)[:20] if hasattr(g, "flags") else None)
        if g.isComposite():
            for c in g.components:
                print("  comp:", c.glyphName, getattr(c, "x", None), getattr(c, "y", None),
                      getattr(c, "transform", None), "flags", hex(c.flags))


if __name__ == "__main__":
    main()
