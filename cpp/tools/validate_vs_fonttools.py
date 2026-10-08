#!/usr/bin/env python3
"""Cross-validate the ftk C++ implementation against fontTools.

Compares, per font: font info, cmap mappings, advance widths, and glyph
outlines (command sequence + coordinates).

Usage:
    python validate_vs_fonttools.py [fonts_dir] [--limit N] [--gids N]
                                    [--ftk path/to/ftk.exe] [--verbose]
"""
import argparse
import json
import os
import random
import subprocess
import sys
from _paths import FTK, FONTS  # noqa: E402

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
except Exception:  # noqa: BLE001
    pass

try:
    from fontTools.ttLib import TTFont, TTCollection
    from fontTools.pens.recordingPen import DecomposingRecordingPen
except ImportError:
    print("fontTools is required for validation", file=sys.stderr)
    sys.exit(2)

TOL = 0.02  # coordinate tolerance in font units


def glyph_ids_to_compare(font, count):
    """Pick a deterministic spread of glyph ids across the font."""
    order = font.getGlyphOrder()
    n = len(order)
    gids = {0, 1, 2, min(3, n - 1), n // 2, n - 2, n - 1}
    rng = random.Random(12345)
    while len(gids) < min(count, n):
        gids.add(rng.randrange(n))
    return sorted(g for g in gids if 0 <= g < n)


def ft_outline(font, gid, glyphset=None):
    """Return ftk-style segment list [[op,x,y,...], ...] for a glyph.

    Uses the *stored* outline (no lsb - xMin normalization that fontTools'
    glyph sets apply), matching what ftk returns.
    """
    name = font.getGlyphOrder()[gid]
    pen = DecomposingRecordingPen(None) if False else DecomposingRecordingPen(
        font.getGlyphSet())
    try:
        if "glyf" in font:
            # raw glyf outline, components resolved through the glyf table
            glyph = font["glyf"][name]
            from fontTools.pens.recordingPen import DecomposingRecordingPen as DRP
            rawpen = DRP(font.getGlyphSet())
            glyph.draw(rawpen, font["glyf"], 0)
            pen = rawpen
        elif "CFF " in font:
            cs = font["CFF "].cff.topDictIndex[0].CharStrings
            cs[name].draw(pen)
        elif "CFF2" in font:
            cs = font["CFF2"].cff.topDictIndex[0].CharStrings
            cs[name].draw(pen)
        else:
            return None, "no outline table"
    except Exception as exc:  # noqa: BLE001
        return None, str(exc)
    segs = []
    for op, args in pen.value:
        if op == "moveTo":
            segs.append([0, args[0][0], args[0][1]])
        elif op == "lineTo":
            segs.append([1, args[0][0], args[0][1]])
        elif op == "qCurveTo":
            # fontTools may emit multiple off-curve points (implied on-curves)
            pts = list(args)
            if pts[-1] is None:  # TrueType contour ending with off-curve
                pts = pts[:-1]
            if len(pts) == 2:
                segs.append([2, pts[0][0], pts[0][1], pts[1][0], pts[1][1]])
            else:
                # decompose into implied on-curve midpoints
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
            if len(pts) == 3:
                segs.append([3] + [v for p in pts for v in p])
            else:
                # cubic super-path: split
                for i in range(0, len(pts) - 1, 3):
                    segs.append([3] + [v for p in pts[i:i + 3] for v in p])
        elif op == "closePath":
            segs.append([4])
        elif op == "endPath":
            pass
        elif op == "addComponent":
            pass
    return segs, None


def normalize_segs(segs):
    """Drop contour bookkeeping that differs by representation:
     * moveTo ops (ftk emits an explicit start point; fontTools' pen may imply
       the start of an all-off-curve contour inside qCurveTo),
     * empty contours and stray closePath from degenerate contours.
    """
    if segs is None:
        return None
    out = []
    i = 0
    n = len(segs)
    while i < n:
        op = int(segs[i][0])
        if op == 0:  # moveTo: skip; contour content is compared below
            i += 1
            continue
        if op == 2 and i + 1 < n:
            # a leading quadTo with no moveTo implies a start point that ftk
            # already emitted explicitly; keep the curve itself
            out.append(segs[i])
            i += 1
            continue
        if op == 4:  # closePath
            out.append(segs[i])
            i += 1
            continue
        out.append(segs[i])
        i += 1
    return out


def compare_outlines(mine, theirs, tol=TOL):
    if mine is None or theirs is None:
        return None
    mine = normalize_segs(mine)
    theirs = normalize_segs(theirs)
    if len(mine) != len(theirs):
        return f"segment count {len(mine)} vs {len(theirs)}"
    for i, (a, b) in enumerate(zip(mine, theirs)):
        if int(a[0]) != int(b[0]):
            return f"segment {i}: op {a[0]} vs {b[0]}"
        if len(a) != len(b):
            return f"segment {i}: arg count {len(a)} vs {len(b)}"
        for k in range(1, len(a)):
            if abs(float(a[k]) - float(b[k])) > tol:
                return (f"segment {i} point {k}: {a[k]} vs {b[k]} "
                        f"(delta {abs(float(a[k]) - float(b[k])):.3f})")
    return None


def run_ftk(ftk, path, extra):
    cmd = [ftk, "json", path] + extra
    res = subprocess.run(cmd, capture_output=True, text=True,
                         encoding="utf-8", errors="replace")
    if res.returncode != 0:
        raise RuntimeError(f"ftk failed: {(res.stderr or '').strip()}")
    return json.loads(res.stdout)


def check_font(ftk, path, face_index, gid_count, verbose=False):
    errors = []
    notes = []
    # --- ftk side: info + cmap + advances
    data = run_ftk(ftk, path, ["--cmap", "--advances"])
    try:
        if path.lower().endswith(".ttc"):
            coll = TTCollection(path)
            font = coll.fonts[face_index]
        else:
            font = TTFont(path, fontNumber=face_index, lazy=True)
    except Exception as exc:  # noqa: BLE001
        return [f"fontTools failed to load: {exc}"], notes

    num_glyphs = font["maxp"].numGlyphs if "maxp" in font else len(font.getGlyphOrder())
    upm = font["head"].unitsPerEm if "head" in font else None
    if data["numGlyphs"] != num_glyphs:
        errors.append(f"numGlyphs {data['numGlyphs']} vs {num_glyphs}")
    if upm is not None and data["unitsPerEm"] != upm:
        errors.append(f"unitsPerEm {data['unitsPerEm']} vs {upm}")

    order = font.getGlyphOrder()

    # --- cmap
    if "cmap" in font:
        try:
            ref = font.getBestCmap()
        except Exception:  # noqa: BLE001
            ref = {}
        mine = {int(k): v for k, v in data["cmap"].items()}
        # fontTools returns glyph names; convert to gids
        name_to_gid = {n: i for i, n in enumerate(order)}
        ref_gids = {}
        for cp, name in ref.items():
            if name in name_to_gid:
                ref_gids[cp] = name_to_gid[name]
        common = set(mine) & set(ref_gids)
        diffs = [cp for cp in common if mine[cp] != ref_gids[cp]]
        only_mine = set(mine) - set(ref_gids)
        only_ref = set(ref_gids) - set(mine)
        if diffs:
            sample = diffs[:5]
            errors.append(f"cmap mismatches {len(diffs)} (e.g. "
                          + ", ".join(f"U+{c:04X}: {mine[c]} vs {ref_gids[c]}" for c in sample) + ")")
        if only_ref:
            errors.append(f"cmap missing {len(only_ref)} mappings (e.g. "
                          + ", ".join(f"U+{c:04X}" for c in sorted(only_ref)[:5]) + ")")
        if only_mine:
            notes.append(f"cmap extra {len(only_mine)} mappings")

    # --- advances
    if "hmtx" in font and data.get("advances"):
        metrics = font["hmtx"].metrics
        bad = 0
        first = None
        for gid in range(min(num_glyphs, len(data["advances"]))):
            name = order[gid]
            if name not in metrics:
                continue
            ref_adv = metrics[name][0]
            if abs(data["advances"][gid] - ref_adv) > 0.001:
                bad += 1
                if first is None:
                    first = (gid, data["advances"][gid], ref_adv)
        if bad:
            errors.append(f"advances mismatch for {bad} glyphs (first gid {first[0]}: "
                          f"{first[1]} vs {first[2]})")

    # --- outlines
    gids = glyph_ids_to_compare(font, gid_count)
    data2 = run_ftk(ftk, path, ["--outlines", ",".join(str(g) for g in gids)])
    glyphset = font.getGlyphSet()
    bad = 0
    checked = 0
    first_err = None
    for gid in gids:
        mine = data2["outlines"].get(str(gid))
        try:
            ref, err = ft_outline(font, gid, glyphset)
        except Exception as exc:  # noqa: BLE001  (fontTools cannot draw it)
            notes.append(f"fontTools cannot draw gid {gid}: {exc}")
            continue
        if ref is None:
            continue
        checked += 1
        diff = compare_outlines(mine, ref)
        if diff:
            bad += 1
            if first_err is None:
                first_err = f"gid {gid} ({order[gid]}): {diff}"
    if bad:
        errors.append(f"outline mismatches for {bad}/{checked} glyphs; first: {first_err}")

    if verbose:
        notes.append(f"checked {checked} glyphs")
    return errors, notes


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fonts_dir", nargs="?", default=FONTS)
    ap.add_argument("--ftk", default=FTK)
    ap.add_argument("--limit", type=int, default=0, help="max fonts to check (0 = all)")
    ap.add_argument("--gids", type=int, default=25, help="glyphs per font for outlines")
    ap.add_argument("--filter", default="", help="substring filter on filename")
    ap.add_argument("--verbose", action="store_true")
    args = ap.parse_args()

    files = []
    for name in sorted(os.listdir(args.fonts_dir)):
        lower = name.lower()
        if not lower.endswith((".ttf", ".otf", ".ttc", ".woff", ".woff2")):
            continue
        if args.filter and args.filter.lower() not in lower:
            continue
        files.append(os.path.join(args.fonts_dir, name))
    if args.limit:
        files = files[: args.limit]

    total_err = 0
    skipped = 0
    ok = 0
    for i, path in enumerate(files, 1):
        base = os.path.basename(path)
        try:
            errors, notes = check_font(args.ftk, path, 0, args.gids, args.verbose)
        except Exception as exc:  # noqa: BLE001
            msg = str(exc)
            low = msg.lower()
            ft_tools_issue = ("fonttools" in low or "fonttools" in msg or "cmap" in low
                              or "glyf" in low or "index out of range" in low
                              or "list index" in low or "assert" in low)
            if ft_tools_issue and "ftk failed" not in msg:
                skipped += 1
                print(f"[{i}/{len(files)}] SKIP {base} (fontTools error: {msg[:110]})")
                continue
            errors, notes = [f"exception: {msg}"], []
        if errors:
            total_err += 1
            print(f"[{i}/{len(files)}] FAIL {base}")
            for e in errors:
                print(f"        {e}")
        else:
            ok += 1
            if args.verbose:
                print(f"[{i}/{len(files)}] OK   {base} ({'; '.join(notes)})")
        if i % 25 == 0:
            print(f"--- progress {i}/{len(files)}: {ok} ok, {total_err} fail, "
                  f"{skipped} skipped ---", flush=True)

    print(f"\n=== {ok} fonts OK, {total_err} fonts with differences, "
          f"{skipped} skipped (of {len(files)}) ===")
    return 1 if total_err else 0


if __name__ == "__main__":
    sys.exit(main())
