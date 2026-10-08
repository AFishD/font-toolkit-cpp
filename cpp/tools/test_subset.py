#!/usr/bin/env python3
"""Subsetting round-trip test.

For each font: create a subset with ftk, then verify with fontTools that
 * the subset loads and has the expected glyph count,
 * requested codepoints map to glyphs with the same outlines and advances as
   in the original font,
 * the subset passes the library's own checksum validation.

Usage: python test_subset.py [fonts_dir] [--ftk path] [--count N]
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

from fontTools.ttLib import TTFont, TTCollection  # noqa: E402
from fontTools.pens.recordingPen import DecomposingRecordingPen  # noqa: E402
from _paths import FTK, FONTS  # noqa: E402


def ftk_outlines(ftk, path, gids):
    res = subprocess.run([ftk, "json", path, "--outlines", ",".join(str(g) for g in gids)],
                         capture_output=True, text=True, encoding="utf-8", errors="replace")
    if res.returncode != 0:
        raise RuntimeError((res.stderr or "").strip())
    return json.loads(res.stdout)["outlines"]


def pen_segs(font, name):
    pen = DecomposingRecordingPen(font.getGlyphSet())
    if "glyf" in font:
        font["glyf"][name].draw(pen, font["glyf"], 0)
    elif "CFF " in font:
        font["CFF "].cff.topDictIndex[0].CharStrings[name].draw(pen)
    elif "CFF2" in font:
        font["CFF2"].cff.topDictIndex[0].CharStrings[name].draw(pen)
    else:
        return []
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fonts_dir", nargs="?",
                    default=FONTS)
    ap.add_argument("--ftk", default=FTK)
    ap.add_argument("--count", type=int, default=10)
    args = ap.parse_args()

    names = sorted(os.listdir(args.fonts_dir))
    picks = []
    # a spread: CJK TrueType, Latin TrueType, CFF
    for want in ["SimHei.ttf", "Arial.ttf", "Calibri.ttf", "DearJoefour.otf",
                 "STFangsong.ttf", "KaiTi.ttf", "Segoe Print.ttf", "EVA.ttf",
                 "Optima Regular.ttf", "Walter Turncoat.ttf"]:
        if want in names:
            picks.append(want)
    for n in names:
        if len(picks) >= args.count:
            break
        if n.lower().endswith((".ttf", ".otf")) and n not in picks:
            picks.append(n)
    picks = picks[: args.count]

    tmp = tempfile.mkdtemp(prefix="ftk_subset_")
    fails = 0
    for name in picks:
        src = os.path.join(args.fonts_dir, name)
        try:
            font = TTFont(src, lazy=True)
            cmap = font.getBestCmap()
            gsub = font.getGlyphSet()
        except Exception as exc:  # noqa: BLE001
            print(f"SKIP {name}: {exc}")
            continue

        # choose a few codepoints that exist in the font
        cps = []
        for cp in [0x41, 0x61, 0x4E2D, 0x6587, 0x20, 0x2014, 0xE9, 0x4E00, 0x3002]:
            if cp in cmap:
                cps.append(cp)
        if not cps:
            print(f"SKIP {name}: no test codepoints")
            continue

        uni = ",".join(hex(cp) for cp in cps)
        out = os.path.join(tmp, os.path.splitext(name)[0] + "-sub.ttf")
        res = subprocess.run([args.ftk, "subset", src, "-o", out, "--unicodes", uni],
                             capture_output=True, text=True, encoding="utf-8", errors="replace")
        if res.returncode != 0:
            print(f"FAIL {name}: subset failed: {(res.stderr or '').strip()[:160]}")
            fails += 1
            continue

        problems = []
        try:
            sub = TTFont(out)
            subcmap = sub.getBestCmap()
        except Exception as exc:  # noqa: BLE001
            print(f"FAIL {name}: subset not loadable by fontTools: {exc}")
            fails += 1
            continue

        # every requested codepoint must be present
        for cp in cps:
            if cp not in subcmap:
                problems.append(f"U+{cp:04X} missing in subset cmap")
        if "maxp" in sub and sub["maxp"].numGlyphs > font["maxp"].numGlyphs:
            problems.append("subset has more glyphs than the original")

        # outlines + advances must match the original for kept codepoints
        nchecked = 0
        for cp in cps:
            if cp not in subcmap or cp not in cmap:
                continue
            gname_sub = subcmap[cp]
            gname_src = cmap[cp]
            a = pen_segs(sub, gname_sub)
            b = pen_segs(font, gname_src)
            if len(a) != len(b):
                problems.append(f"U+{cp:04X}: {len(a)} vs {len(b)} segments")
                continue
            for i, (x, y) in enumerate(zip(a, b)):
                if int(x[0]) != int(y[0]) or len(x) != len(y):
                    problems.append(f"U+{cp:04X} seg {i}: op/arity differs")
                    break
                if any(abs(float(p) - float(q)) > 0.02 for p, q in zip(x[1:], y[1:])):
                    problems.append(f"U+{cp:04X} seg {i}: coords differ ({x} vs {y})")
                    break
            # advance
            if "hmtx" in sub and "hmtx" in font:
                adv_sub = sub["hmtx"][gname_sub][0]
                adv_src = font["hmtx"][gname_src][0]
                if adv_sub != adv_src:
                    problems.append(f"U+{cp:04X}: advance {adv_sub} vs {adv_src}")
            nchecked += 1

        val = subprocess.run([args.ftk, "validate", out], capture_output=True, text=True,
                             encoding="utf-8", errors="replace")
        if "OK" not in (val.stdout or ""):
            problems.append(f"checksum validation: {(val.stdout or '').strip()[:80]}")

        # also check the subset can be re-read for every glyph
        try:
            sub2 = TTFont(out)
            for g in range(sub2["maxp"].numGlyphs):
                gname = sub2.getGlyphOrder()[g]
                pen_segs(sub2, gname)
        except Exception as exc:  # noqa: BLE001
            problems.append(f"glyph walk failed: {exc}")

        if problems:
            print(f"FAIL {name} ({nchecked} codes checked)")
            for p in problems[:6]:
                print(f"        {p}")
            fails += 1
        else:
            print(f"OK   {name}: {sub['maxp'].numGlyphs} glyphs, "
                  f"{len(cps)} codepoints verified, {os.path.getsize(out)} bytes")

    print(f"\n=== {len(picks)} fonts tested, {fails} failures ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
