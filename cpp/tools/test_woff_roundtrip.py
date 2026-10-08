#!/usr/bin/env python3
"""Round-trip test: compress fonts to WOFF/WOFF2 with fontTools, then verify
the ftk C++ reader reproduces the original outlines, advances and cmap.

Usage: python test_woff_roundtrip.py [fonts_dir] [--ftk path] [--count N]
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
from _paths import FTK, FONTS  # noqa: E402


def run_ftk(ftk, path, extra):
    res = subprocess.run([ftk, "json", path] + extra, capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if res.returncode != 0:
        raise RuntimeError(f"ftk failed on {os.path.basename(path)}: "
                           f"{(res.stderr or '').strip()}")
    return json.loads(res.stdout)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fonts_dir", nargs="?",
                    default=FONTS)
    ap.add_argument("--ftk", default=FTK)
    ap.add_argument("--count", type=int, default=8)
    args = ap.parse_args()

    # pick a spread of fonts: pure TrueType, CFF, TTC, CJK, composites
    prefs = ["SimHei.ttf", "Arial.ttf", "STFangsong.ttf", "DearJoefour.otf",
             "方正大标宋_GBK.ttf", "PingFang SC Regular.ttf", "Calibri.ttf",
             "Arphic", "KaiTi", "DFHSMincho-W5.TTC"]
    names = sorted(os.listdir(args.fonts_dir))
    picked = []
    for want in prefs:
        for n in names:
            if want.lower() in n.lower() and n not in picked:
                picked.append(n)
                break
    for n in names:
        if len(picked) >= args.count:
            break
        if n.lower().endswith((".ttf", ".otf", ".ttc")) and n not in picked:
            picked.append(n)
    picked = picked[: args.count]

    tmp = tempfile.mkdtemp(prefix="ftk_woff_")
    fails = 0
    for name in picked:
        src = os.path.join(args.fonts_dir, name)
        entry = {}
        try:
            if name.lower().endswith(".ttc"):
                font = TTCollection(src).fonts[0]
            else:
                font = TTFont(src)
            base = run_ftk(args.ftk, src, ["--cmap", "--advances"])
            gids = [0, 1, 2, len(font.getGlyphOrder()) // 2,
                    len(font.getGlyphOrder()) - 1]
            gidstr = ",".join(str(g) for g in gids)
            base_out = run_ftk(args.ftk, src, ["--outlines", gidstr])
        except Exception as exc:  # noqa: BLE001
            print(f"SKIP {name}: {exc}")
            continue

        for flavor, ext in (("woff", ".woff"), ("woff2", ".woff2")):
            out = os.path.join(tmp, os.path.splitext(name)[0] + ext)
            try:
                font.flavor = flavor
                font.save(out)
                font.flavor = None
            except Exception as exc:  # noqa: BLE001
                print(f"SKIP {name} {flavor}: fontTools cannot write it ({exc})")
                continue
            label = f"{name} [{flavor}]"
            try:
                got = run_ftk(args.ftk, out, ["--cmap", "--advances"])
                got_out = run_ftk(args.ftk, out, ["--outlines", gidstr])
            except Exception as exc:  # noqa: BLE001
                print(f"FAIL {label}: {exc}")
                fails += 1
                continue

            problems = []
            if got["numGlyphs"] != base["numGlyphs"]:
                problems.append(f"numGlyphs {got['numGlyphs']} vs {base['numGlyphs']}")
            if got["unitsPerEm"] != base["unitsPerEm"]:
                problems.append("unitsPerEm differs")
            if got["cmap"] != base["cmap"]:
                a = {int(k): v for k, v in got["cmap"].items()}
                b = {int(k): v for k, v in base["cmap"].items()}
                diff = [cp for cp in set(a) & set(b) if a[cp] != b[cp]]
                problems.append(f"cmap differs ({len(diff)} mismatches, "
                                f"{len(set(a) - set(b))} extra, {len(set(b) - set(a))} missing)")
            adv = base["advances"]
            if len(got["advances"]) != len(adv):
                problems.append("advance count differs")
            else:
                bad = [(i, got["advances"][i], adv[i]) for i in range(len(adv))
                       if abs(got["advances"][i] - adv[i]) > 0.001]
                if bad:
                    problems.append(f"advances differ for {len(bad)} glyphs "
                                    f"(first {bad[0]})")
            for g in gids:
                a = got_out["outlines"].get(str(g))
                b = base_out["outlines"].get(str(g))
                if a is None or b is None:
                    problems.append(f"gid {g}: missing outline")
                    break
                if len(a) != len(b):
                    problems.append(f"gid {g}: {len(a)} vs {len(b)} segments")
                    break
                for i, (x, y) in enumerate(zip(a, b)):
                    if int(x[0]) != int(y[0]) or len(x) != len(y):
                        problems.append(f"gid {g} seg {i}: op/arity differs")
                        break
                    if any(abs(float(p) - float(q)) > 0.02 for p, q in zip(x[1:], y[1:])):
                        problems.append(f"gid {g} seg {i}: coords differ")
                        break
            # checksum verification of the decompressed sfnt
            val = subprocess.run([args.ftk, "validate", out], capture_output=True,
                                 text=True, encoding="utf-8", errors="replace")
            if "OK" not in (val.stdout or ""):
                problems.append(f"checksum validation: {(val.stdout or '').strip()[:60]}")

            if problems:
                print(f"FAIL {label}")
                for p in problems:
                    print(f"        {p}")
                fails += 1
            else:
                size = os.path.getsize(out)
                print(f"OK   {label} ({size} bytes)")

    print(f"\n=== {len(picked)} fonts tested, {fails} failures ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
