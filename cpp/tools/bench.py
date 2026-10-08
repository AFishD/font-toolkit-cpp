#!/usr/bin/env python3
"""ftk performance benchmark: parsing throughput, outline extraction and
multithread scaling over a font corpus.

Usage: python bench.py [fonts_dir] [--ftk path] [--threads 1,2,4,8,auto]
"""
import argparse
import os
import subprocess
import sys
import time
from _paths import FTK, FONTS  # noqa: E402

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:  # noqa: BLE001
    pass


def collect(fonts_dir, limit=0):
    files = []
    for name in sorted(os.listdir(fonts_dir)):
        if name.lower().endswith((".ttf", ".otf", ".ttc", ".woff", ".woff2")):
            files.append(os.path.join(fonts_dir, name))
    return files[:limit] if limit else files


def run_scan(ftk, fonts_dir, threads, recursive=False):
    env = dict(os.environ)
    cmd = [ftk, "scan", fonts_dir] + (["--recursive"] if recursive else [])
    if threads:
        cmd += ["-j", str(threads)]
    t0 = time.perf_counter()
    res = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8",
                         errors="replace", env=env)
    dt = time.perf_counter() - t0
    line = [l for l in (res.stderr or "").splitlines() if l.startswith("done:")]
    return dt, (line[0] if line else (res.stderr or "").strip()[:120])


def bench_single_font(ftk, path, reps=20):
    """Time a full parse + outline extraction across all glyphs (info path)."""
    t0 = time.perf_counter()
    for _ in range(reps):
        subprocess.run([ftk, "json", path, "--advances"], capture_output=True,
                       text=True, encoding="utf-8", errors="replace")
    return (time.perf_counter() - t0) / reps


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("fonts_dir", nargs="?",
                    default=FONTS)
    ap.add_argument("--ftk", default=FTK)
    ap.add_argument("--threads", default="1,2,4,8,0")
    args = ap.parse_args()

    files = collect(args.fonts_dir)
    total_mb = sum(os.path.getsize(f) for f in files) / (1024 * 1024)
    print(f"corpus: {len(files)} fonts, {total_mb:.1f} MB")
    print()
    print(f"{'threads':>8} | {'time (s)':>9} | {'fonts/s':>9} | {'MB/s':>8}")
    print("-" * 44)
    for t in args.threads.split(","):
        n = int(t)
        dt, summary = run_scan(args.ftk, args.fonts_dir, n)
        fps = len(files) / dt if dt else 0
        mbs = total_mb / dt if dt else 0
        label = "auto" if n == 0 else str(n)
        print(f"{label:>8} | {dt:9.3f} | {fps:9.1f} | {mbs:8.1f}")
    print()
    print("summary lines:")
    for t in ("1", "0"):
        _, s = run_scan(args.ftk, args.fonts_dir, int(t))
        print(f"  -j {t}: {s}")

    # single-font latency: parse + load every glyph advance/cmap
    big = max(files, key=os.path.getsize)
    t = bench_single_font(args.ftk, big, reps=10)
    print(f"\nlatency: '{os.path.basename(big)}' ({os.path.getsize(big)/1048576:.1f} MB) "
          f"full parse+metrics: {t*1000:.1f} ms/run (process spawn included)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
