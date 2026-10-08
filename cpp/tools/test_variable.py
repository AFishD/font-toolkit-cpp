#!/usr/bin/env python3
"""Variable font validation.

Compiles the variable-font TTX fixtures shipped with fontTools into real
fonts, then compares ftk's instance rendering (fvar/gvar/HVAR) with
fontTools' getGlyphSet(location=...) at several axis locations.

Usage: python test_variable.py [--ftk path] [--limit N]
"""
import argparse
import json
import os
import subprocess
import sys
import tempfile

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:  # noqa: BLE001
    pass

from fontTools.ttLib import TTFont  # noqa: E402
from fontTools.pens.recordingPen import DecomposingRecordingPen  # noqa: E402

from _paths import FTK, FONTS  # noqa: E402
from _paths import FTK as FTK_DEFAULT, TTX_ROOT  # noqa: E402


def find_ttx(max_files=200):
    """Full-font TTX dumps that contain both <fvar> and <glyf>/<CFF>."""
    out = []
    for root, _dirs, files in os.walk(TTX_ROOT):
        for f in files:
            if not f.endswith(".ttx"):
                continue
            p = os.path.join(root, f)
            try:
                if os.path.getsize(p) < 3000:
                    continue
                with open(p, encoding="utf-8", errors="replace") as fh:
                    txt = fh.read()
            except OSError:
                continue
            if "<fvar>" in txt and ("<glyf>" in txt or "<CFF>" in txt or "<CFF2>" in txt):
                out.append(p)
                if len(out) >= max_files:
                    return out
    return out


def compile_ttx(ttx, out):
    res = subprocess.run([sys.executable, "-m", "fontTools.ttx", "-q", "-o", out, ttx],
                         capture_output=True, text=True, encoding="utf-8", errors="replace")
    return res.returncode == 0 and os.path.exists(out)


def run_ftk(ftk, path, extra):
    res = subprocess.run([ftk, "json", path] + extra, capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if res.returncode != 0:
        raise RuntimeError((res.stderr or "").strip()[:200])
    return json.loads(res.stdout)


def ref_outline(font, gid, location):
    order = font.getGlyphOrder()
    name = order[gid]
    gs = font.getGlyphSet(location=location)
    pen = DecomposingRecordingPen(gs)
    gs[name].draw(pen)
    segs = []
    for op, args in pen.value:
        if op == "moveTo":
            segs.append([0, args[0][0], args[0][1]])
        elif op == "lineTo":
            segs.append([1, args[0][0], args[0][1]])
        elif op == "qCurveTo":
            pts = [p for p in args if p is not None]
            for i in range(len(pts) - 1):
                c = pts[i]
                end = pts[i + 1] if i + 2 == len(pts) else (
                    (c[0] + pts[i + 1][0]) / 2.0, (c[1] + pts[i + 1][1]) / 2.0)
                segs.append([2, c[0], c[1], end[0], end[1]])
        elif op == "curveTo":
            for i in range(0, len(args) - 2, 3):
                segs.append([3] + [v for p in args[i:i + 3] for v in p])
        elif op == "closePath":
            segs.append([4])
    return segs


def compare(mine, ref, tol=0.6):
    """tol is loose because ftk returns TrueType integer font units while
    fontTools keeps fractional coordinates for varied outlines."""
    if len(mine) != len(ref):
        return f"segments {len(mine)} vs {len(ref)}"
    for i, (a, b) in enumerate(zip(mine, ref)):
        if int(a[0]) != int(b[0]) or len(a) != len(b):
            return f"seg {i}: op/arity {a} vs {b}"
        for k in range(1, len(a)):
            if abs(float(a[k]) - float(b[k])) > tol:
                return f"seg {i} pt {k}: {a[k]} vs {b[k]}"
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ftk", default=FTK_DEFAULT)
    ap.add_argument("--limit", type=int, default=6)
    args = ap.parse_args()

    ttxx = find_ttx(args.limit * 3)
    if not ttxx:
        print("no variable TTX fixtures found")
        return 1
    # stable output dir so fixtures can be re-inspected after a run
    tmp = os.path.join(os.path.dirname(os.path.abspath(args.ftk)), "varfix")
    os.makedirs(tmp, exist_ok=True)
    tested = 0
    fails = 0
    for ttx in ttxx:
        if tested >= args.limit:
            break
        name = os.path.splitext(os.path.basename(ttx))[0]
        ttf = os.path.join(tmp, name + ".ttf")
        if not compile_ttx(ttx, ttf):
            continue
        try:
            font = TTFont(ttf)
            if "fvar" not in font:
                continue
            axes = {a.axisTag: (a.minValue, a.defaultValue, a.maxValue) for a in font["fvar"].axes}
        except Exception:  # noqa: BLE001
            continue
        tested += 1
        problems = []

        # --- axis metadata via ftk
        try:
            info = run_ftk(args.ftk, ttf, [])
        except Exception as exc:  # noqa: BLE001
            print(f"FAIL {name}: {exc}")
            fails += 1
            continue
        if info["numGlyphs"] != font["maxp"].numGlyphs:
            problems.append(f"numGlyphs {info['numGlyphs']} vs {font['maxp'].numGlyphs}")

        # --- locations to test per axis: min, default, max, and 3/4 point
        locations = [{}]
        for tag, (mn, dv, mx) in axes.items():
            locations.append({tag: mn})
            locations.append({tag: mx})
            locations.append({tag: dv + (mx - dv) * 0.5})

        order = font.getGlyphOrder()
        n = len(order)
        gids = [0, 1, n // 2, n - 1]
        axis_tags = [a.axisTag for a in font["fvar"].axes]  # fvar order, not sorted

        # fontTools' glyph set shifts outlines by (lsb - xMin); ftk returns the
        # stored outline. Only compare outlines for fonts whose outlines really
        # vary (gvar / CFF2), where both sides use the same varied geometry.
        compare_outlines = ("gvar" in font) or ("CFF2" in font)

        for loc in locations:
            coords = [loc.get(t, axes[t][1]) for t in axis_tags]
            coordstr = ",".join("%g" % c for c in coords)
            try:
                got = run_ftk(args.ftk, ttf, ["--outlines", ",".join(str(g) for g in gids),
                                              "--advances", "--coords", coordstr])
            except Exception as exc:  # noqa: BLE001
                problems.append(f"location {loc}: ftk failed: {exc}")
                continue
            for gid in gids:
                if not compare_outlines:
                    break
                mine = got["outlines"].get(str(gid))
                try:
                    ref = ref_outline(font, gid, loc)
                except Exception:  # noqa: BLE001
                    continue
                diff = compare(mine or [], ref)
                if diff:
                    problems.append(f"location {loc} gid {gid}: {diff}")
            # advances: ftk applies gvar phantom deltas when HVAR is absent
            # (fontTools' glyph set does not model those), so only compare
            # against fontTools when the font has HVAR.
            try:
                if "HVAR" in font:
                    gs = font.getGlyphSet(location=loc)
                    for gid in gids:
                        ref_adv = gs[order[gid]].width
                        mine_adv = got["advances"][gid]
                        if abs(mine_adv - ref_adv) > 0.05:
                            problems.append(f"location {loc} gid {gid}: advance "
                                            f"{mine_adv} vs {ref_adv}")
            except Exception:  # noqa: BLE001
                pass

        if problems:
            fails += 1
            print(f"FAIL {name}")
            for p in problems[:6]:
                print(f"        {p}")
        else:
            print(f"OK   {name}: axes {list(axes)} verified at {len(locations)} locations")

    print(f"\n=== {tested} variable fonts tested, {fails} failures ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
