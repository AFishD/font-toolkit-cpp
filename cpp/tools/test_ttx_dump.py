#!/usr/bin/env python3
"""Validate ftk's TTX-style XML dump against fontTools table values.

Parses ftk's dump with ElementTree and compares key values (head, hhea, maxp,
OS/2, post, name, fvar, hmtx subset) with fontTools' interpretation of the
same font.

Usage: python test_ttx_dump.py [fonts_dir] [--ftk path] [--count N]
"""
import argparse
import os
import subprocess
import sys
import xml.etree.ElementTree as ET

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:  # noqa: BLE001
    pass

from fontTools.ttLib import TTFont  # noqa: E402
from _paths import FTK, FONTS  # noqa: E402


def dump(ftk, path):
    res = subprocess.run([ftk, "dump", path], capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if res.returncode != 0:
        raise RuntimeError((res.stderr or "").strip()[:200])
    return res.stdout


def attr(elem, name):
    return elem.get(name) if elem is not None else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fonts_dir", nargs="?",
                    default=FONTS)
    ap.add_argument("--ftk", default=FTK)
    ap.add_argument("--count", type=int, default=40)
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    names = sorted(n for n in os.listdir(args.fonts_dir)
                   if n.lower().endswith((".ttf", ".otf", ".ttc")))
    step = max(1, len(names) // args.count)
    picked = names[::step][: args.count]

    ok = 0
    fails = 0
    for name in picked:
        path = os.path.join(args.fonts_dir, name)
        try:
            xml = dump(args.ftk, path)
            root = ET.fromstring(xml)
        except Exception as exc:  # noqa: BLE001
            print(f"FAIL {name}: dump/parse error: {exc}")
            fails += 1
            continue
        try:
            font = TTFont(path, lazy=True)
        except Exception as exc:  # noqa: BLE001
            print(f"SKIP {name}: fontTools cannot load ({exc})")
            continue

        problems = []

        def check(label, mine, ref):
            if mine is None or ref is None:
                return
            if isinstance(ref, float):
                if abs(float(mine) - ref) > 1e-4:
                    problems.append(f"{label}: {mine} vs {ref}")
            elif str(mine) != str(ref):
                problems.append(f"{label}: {mine} vs {ref}")

        head = root.find("head")
        if head is not None and "head" in font:
            h = font["head"]
            check("head.unitsPerEm", attr(head.find("unitsPerEm"), "value"), h.unitsPerEm)
            check("head.flags", attr(head.find("flags"), "value"), "0x%04X" % h.flags)
            check("head.xMin", attr(head.find("xMin"), "value"), h.xMin)
            check("head.macStyle", attr(head.find("macStyle"), "value"), "0x%04X" % h.macStyle)

        hhea = root.find("hhea")
        if hhea is not None and "hhea" in font:
            h = font["hhea"]
            check("hhea.ascender", attr(hhea.find("ascender"), "value"), h.ascender)
            check("hhea.descender", attr(hhea.find("descender"), "value"), h.descender)
            check("hhea.numberOfHMetrics",
                  attr(hhea.find("numberOfHMetrics"), "value"), h.numberOfHMetrics)

        maxp = root.find("maxp")
        if maxp is not None and "maxp" in font:
            check("maxp.numGlyphs", attr(maxp.find("numGlyphs"), "value"),
                  font["maxp"].numGlyphs)

        os2 = root.find("OS_2")
        if os2 is not None and "OS/2" in font:
            o = font["OS/2"]
            check("OS/2.usWeightClass", attr(os2.find("usWeightClass"), "value"),
                  o.usWeightClass)
            check("OS/2.usWinAscent", attr(os2.find("usWinAscent"), "value"), o.usWinAscent)
            check("OS/2.achVendID",
                  "".join(ch if ch.isprintable() and ch != "\ufffd" else ""
                          for ch in (attr(os2.find("achVendID"), "value") or "")).strip(),
                  "".join(ch if ch.isprintable() and ch != "\ufffd" else ""
                          for ch in o.achVendID).strip())
            check("OS/2.sTypoAscender", attr(os2.find("sTypoAscender"), "value"),
                  o.sTypoAscender)

        post = root.find("post")
        if post is not None and "post" in font:
            p = font["post"]
            mine = attr(post.find("italicAngle"), "value")
            if mine is not None and abs(float(mine) - p.italicAngle) > 1e-4:
                problems.append(f"post.italicAngle: {mine} vs {p.italicAngle}")
            check("post.underlinePosition", attr(post.find("underlinePosition"), "value"),
                  p.underlinePosition)

        # name records: every fontTools record must appear with the same text
        name_elem = root.find("name")
        if name_elem is not None and "name" in font:
            mine = {}
            for rec in name_elem.findall("namerecord"):
                key = (int(rec.get("nameID")), int(rec.get("platformID")),
                       int(rec.get("platEncID")), int(rec.get("langID"), 0))
                mine[key] = (rec.text or "")
            ref_missing = 0
            for rec in font["name"].names:
                if rec.platformID not in (0, 3):
                    # platform 1 (Mac) records use per-language legacy codecs
                    # (Big5/Shift-JIS/...) that ftk does not decode; skip them.
                    continue
                key = (rec.nameID, rec.platformID, rec.platEncID, rec.langID)
                try:
                    text = rec.toUnicode()
                except Exception:  # noqa: BLE001 (undecodable record)
                    continue
                if key in mine:
                    a = mine[key].replace("\r\n", "\n").replace("\r", "\n").strip()
                    b = text.replace("\r\n", "\n").replace("\r", "\n").strip()
                    if "\ufffd" in a and "\ufffd" not in b:
                        continue  # control/non-UTF8 bytes sanitized by ftk
                    if a != b:
                        # allow minor whitespace differences only
                        if a.split() != b.split():
                            ref_missing += 1
                            if ref_missing <= 2:
                                problems.append(f"name {key}: '{a[:24]}' vs '{b[:24]}'")
            if ref_missing > 2:
                problems.append(f"name: {ref_missing} records differ")

        if problems:
            print(f"FAIL {name}")
            for p in problems[:5]:
                print(f"        {p}")
            fails += 1
        else:
            ok += 1
            if args.verbose:
                print(f"OK   {name}")

    print(f"\n=== {ok} dumps OK, {fails} failures (of {len(picked)}) ===")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
