#!/usr/bin/env python3
"""Benchmark ftk (C++) against Python fontTools on identical workloads.

Workloads (both sides do exactly the same work on the same font files):
  info      : open font, read head/maxp, enumerate the best cmap
  outlines  : draw every glyph into a counting pen (composites decomposed)
  subset    : keep a fixed set of codepoints, rebuild the font, re-read it

Comparisons:
  Python single process  vs  Python multiprocessing (N workers)
  vs  ftk -j 1           vs  ftk -j N

ftk timings use the internal counter printed by `ftk scan` (excludes process
start-up); Python timings are measured in-process around the same work.

Usage:
  python bench_vs_python.py [--count 20] [--outline count] [--jobs 12]
                            [--fonts DIR] [--ftk PATH] [--repeats 3]
                            [--workloads info,outlines]
"""
import argparse
import concurrent.futures as futures
import logging
import os
import subprocess
import sys
import time

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:  # noqa: BLE001
    pass

# fontTools logs "created timestamp seems very low" for ~20 % of the corpus;
# keep the benchmark output readable.
logging.getLogger("fontTools").setLevel(logging.ERROR)

from fontTools.pens.basePen import BasePen  # noqa: E402
from fontTools.ttLib import TTFont  # noqa: E402
from _paths import FTK, FONTS  # noqa: E402


class CountingPen(BasePen):
    """Counts segments; BasePen.addComponent decomposes composites."""

    __slots__ = ("segs", "pts")

    def __init__(self, glyphSet):
        super().__init__(glyphSet)
        self.segs = 0
        self.pts = 0

    def _moveTo(self, pt):
        self.segs += 1
        self.pts += 1

    def _lineTo(self, pt):
        self.segs += 1
        self.pts += 1

    def _curveToOne(self, p1, p2, p3):
        self.segs += 1
        self.pts += 3

    def _qCurveToOne(self, p1, p2):
        self.segs += 1
        self.pts += 2

    def _closePath(self):
        self.segs += 1


# ------------------------------------------------------------------ workloads

def work_info(path):
    font = TTFont(path, lazy=True, fontNumber=0)
    upem = font["head"].unitsPerEm
    n = font["maxp"].numGlyphs
    cmap = font.getBestCmap() or {}
    font.close()
    return (n, len(cmap), upem)


def work_outlines(path):
    font = TTFont(path, lazy=True, fontNumber=0)
    gs = font.getGlyphSet()
    pen = CountingPen(gs)
    n_glyphs = 0
    segs = pts = 0
    for name in font.getGlyphOrder():
        before_s, before_p = pen.segs, pen.pts
        gs[name].draw(pen)
        n_glyphs += 1
        segs += pen.segs - before_s
        pts += pen.pts - before_p
    font.close()
    return (n_glyphs, segs, pts)


def work_subset(path, unicodes):
    from fontTools import subset

    font = TTFont(path, lazy=True, fontNumber=0)
    options = subset.Options()
    options.layout_features = []
    options.name_IDs = ["*"]
    options.notdef_outline = True
    options.recalc_bounds = True
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(unicodes=unicodes)
    subsetter.subset(font)
    from io import BytesIO

    buf = BytesIO()
    font.save(buf)
    size = buf.tell()
    buf.seek(0)
    check = TTFont(buf, lazy=True)
    n = check["maxp"].numGlyphs
    check.close()
    font.close()
    return size, n


WORKLOADS = {"info": work_info, "outlines": work_outlines}


# ---------------------------------------------------------------------- ftk

def ftk_scan(ftk, paths, threads, outlines):
    # plain scan = open + info + full cmap enumeration (same work as work_info);
    # --outlines additionally extracts every glyph outline.
    cmd = [ftk, "scan"]
    if outlines:
        cmd.append("--outlines")
    cmd += paths
    if threads:
        cmd += ["-j", str(threads)]
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                         errors="replace")
    line = ""
    for l in (res.stderr or "").splitlines():
        if l.startswith("done:"):
            line = l
    if not line:
        raise RuntimeError(f"ftk scan failed: {(res.stderr or '')[:200]}")
    # "done: N ok, M failed, G glyphs, C cmap mappings[, S segments, P points] in X ms"
    ms = float(line.split(" in ")[1].split(" ms")[0])
    segs = pts = 0
    if ", " in line and "segments" in line:
        part = line.split(", ")
        for p in part:
            if p.endswith(" segments"):
                segs = int(p.split()[0])
            if "points" in p:
                pts = int(p.split()[0])
    return ms, segs, pts, line


def chunk_paths(paths, max_chars=8000):
    """Windows caps the command line at 32k chars: batch the file list."""
    chunks, cur, n = [], [], 0
    for p in paths:
        if cur and n + len(p) + 1 > max_chars:
            chunks.append(cur)
            cur, n = [], 0
        cur.append(p)
        n += len(p) + 1
    if cur:
        chunks.append(cur)
    return chunks


def ftk_timing(ftk, paths, threads, outlines, repeats):
    chunks = chunk_paths(paths)
    best = None
    for _ in range(repeats):
        total_ms = 0.0
        segs = pts = 0
        for ch in chunks:
            ms, s, p, _line = ftk_scan(ftk, ch, threads, outlines)
            total_ms += ms
            segs += s
            pts += p
        if best is None or total_ms < best[0]:
            best = (total_ms, segs, pts, f"{len(chunks)} chunk(s)")
    return best


# --------------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fonts", default=FONTS)
    ap.add_argument("--ftk", default=FTK)
    ap.add_argument("--count", type=int, default=20, help="fonts to benchmark")
    ap.add_argument("--jobs", type=int, default=12, help="ftk/python worker count")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--workloads", default="info,outlines")
    ap.add_argument("--subset-unicodes", type=int, default=0,
                    help="if >0, also benchmark subsetting keeping N codepoints")
    args = ap.parse_args()

    names = sorted(n for n in os.listdir(args.fonts)
                   if n.lower().endswith((".ttf", ".otf", ".ttc")))
    step = max(1, len(names) // args.count)
    picked = names[::step][: args.count]
    all_paths = [os.path.join(args.fonts, n) for n in picked]

    # Both sides must do the same work on the same fonts: drop files fontTools
    # cannot load (a handful of the corpus fonts have broken cmaps) and report.
    paths, unloadable = [], []
    for p in all_paths:
        try:
            work_info(p)
            paths.append(p)
        except Exception as exc:  # noqa: BLE001
            unloadable.append((os.path.basename(p), str(exc)[:60]))
    if unloadable:
        print(f"excluded {len(unloadable)} font(s) that python fontTools cannot load:")
        for n, e in unloadable[:5]:
            print(f"   - {n}: {e}")
    total_mb = sum(os.path.getsize(p) for p in paths) / (1024 * 1024)
    print(f"corpus sample: {len(paths)} fonts, {total_mb:.1f} MB")
    print(f"CPUs: {os.cpu_count()} logical, ftk threads: {args.jobs}, "
          f"python: {sys.version.split()[0]}/fontTools "
          f"{__import__('fontTools').version}")
    print()

    results = {}

    for wl in args.workloads.split(","):
        wl = wl.strip()
        fn = WORKLOADS[wl]
        print(f"### workload: {wl}")

        # ---- python, single process
        t0 = time.perf_counter()
        py_res = [fn(p) for p in paths]
        py1 = time.perf_counter() - t0
        py_totals = [sum(r[i] for r in py_res) for i in range(len(py_res[0]))]

        # ---- python, multiprocessing
        t0 = time.perf_counter()
        with futures.ProcessPoolExecutor(max_workers=args.jobs) as ex:
            py_res_mp = list(ex.map(fn, paths))
        pyN = time.perf_counter() - t0
        assert len(py_res_mp) == len(py_res)

        # ---- ftk
        if wl == "outlines":
            ftk1 = ftk_timing(args.ftk, paths, 1, True, args.repeats)
            ftkN = ftk_timing(args.ftk, paths, args.jobs, True, args.repeats)
            ftk_segs, ftk_pts = ftk1[1], ftk1[2]
            py_segs, py_pts = py_totals[0], py_totals[1]
        else:
            ftk1 = ftk_timing(args.ftk, paths, 1, False, args.repeats)
            ftkN = ftk_timing(args.ftk, paths, args.jobs, False, args.repeats)
            ftk_segs = ftk_pts = py_segs = py_pts = 0

        glyphs = sum(r[0] for r in py_res)
        cmap = sum(r[1] for r in py_res) if wl == "info" else 0

        def secs(v):
            return f"{v*1000:8.1f} ms" if v < 1.0 else f"{v:8.2f} s "

        print(f"  python 1 proc  : {secs(py1)}")
        print(f"  python {args.jobs:2d} procs : {secs(pyN)}")
        print(f"  ftk -j 1       : {secs(ftk1[0]/1000.0)}")
        print(f"  ftk -j {args.jobs:<2d}      : {secs(ftkN[0]/1000.0)}")
        print(f"  speedup  ftk -j {args.jobs} / python 1 proc   : "
              f"{py1 / (ftkN[0]/1000.0):8.1f}x")
        print(f"  speedup  ftk -j {args.jobs} / python {args.jobs} procs: "
              f"{pyN / (ftkN[0]/1000.0):8.1f}x")
        print(f"  speedup  ftk -j 1    / python 1 proc   : "
              f"{py1 / (ftk1[0]/1000.0):8.1f}x")
        if wl == "outlines":
            py_segs, py_pts = py_totals[1], py_totals[2]
            print(f"  work done: {glyphs:,} glyphs on both sides; "
                  f"python {py_segs:,} segments / {py_pts:,} points, "
                  f"ftk {ftk_segs:,} / {ftk_pts:,}")
        elif wl == "info":
            print(f"  work done: {glyphs:,} glyphs and {cmap:,} cmap mappings "
                  f"on both sides")
        print()
        results[wl] = dict(py1=py1, pyN=pyN, ftk1=ftk1[0] / 1000.0,
                           ftkN=ftkN[0] / 1000.0)

    # ---- subsetting (optional; python side is slow, so few fonts)
    if args.subset_unicodes > 0:
        n = args.subset_unicodes
        cps = []
        cp = 0x4E00
        while len(cps) < n:
            cps.append(cp)
            cp += 1
        sub_paths = paths[:6]
        print(f"### workload: subset (keep {n} codepoints, {len(sub_paths)} fonts)")
        t0 = time.perf_counter()
        for p in sub_paths:
            work_subset(p, cps)
        py1 = time.perf_counter() - t0

        t0 = time.perf_counter()
        out_size = 0
        ftk_internal_ms = 0.0
        for p in sub_paths:
            out = os.path.join(os.path.dirname(args.ftk), "bench_sub.ttf")
            r = subprocess.run([args.ftk, "subset", p, "-o", out,
                                "--unicodes", ",".join("0x%X" % c for c in cps)],
                               capture_output=True, text=True, encoding="utf-8",
                               errors="replace")
            if r.returncode != 0:
                print("  ftk subset failed:", (r.stderr or "")[:120])
                break
            out_size = os.path.getsize(out)
            for l in (r.stderr or "").splitlines():
                if l.startswith("subset:") and " in " in l:
                    ftk_internal_ms += float(l.split(" in ")[1].split(" ms")[0])
        ftk1 = time.perf_counter() - t0
        print(f"  python 1 proc   : {py1:8.2f} s")
        print(f"  ftk wall clock  : {ftk1:8.2f} s   speedup {py1/ftk1:6.1f}x "
              f"(includes {len(sub_paths)} process launches)")
        if ftk_internal_ms > 0:
            print(f"  ftk internal    : {ftk_internal_ms/1000.0:8.2f} s   "
                  f"speedup {py1/(ftk_internal_ms/1000.0):6.1f}x (subset only)")
        print(f"  last subset size: {out_size:,} bytes")
        print()

    return 0


def jobs_label(n):
    return f"-j {n}"


if __name__ == "__main__":
    sys.exit(main())
