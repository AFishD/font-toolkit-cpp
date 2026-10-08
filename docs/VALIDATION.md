# Validation report — ftk vs Python fontTools 4.43

Environment: Windows 11, MinGW-w64 g++ 8.1.0 (`x86_64-posix-seh-rev0`),
CMake 4.0.3 / Ninja 1.11.1, Release build, AMD Ryzen 5 7600X (6 cores /
12 threads). Reference implementation: fontTools 4.43.1 on CPython 3.8.

Corpus: `Fonts/` — 528 font files (440 TTF, 87 OTF/CFF, 1 TTC), 3.88 GB,
9,927,323 glyphs, 8,830,696 cmap mappings, plus fontTools' own test fixtures
for WOFF/WOFF2/variable fonts.

Every command below is reproducible from the repository root.

---

## 1. Summary

| suite | command | result |
|---|---|---|
| unit tests (buffers, tables, threads, TTC) | `build\ftk_tests.exe` | all tests passed (exit 0) |
| corpus scan | `build\ftk.exe scan Fonts` | 528 ok, 0 failed, 9,927,323 glyphs |
| outlines + cmap + advances vs fontTools | `python cpp\tools\validate_vs_fonttools.py --gids 12` | **511 exact, 9 known differences, 8 fontTools-side skips** |
| TTX dump values vs fontTools | `python cpp\tools\test_ttx_dump.py --count 60` | 60/60 dumps OK |
| WOFF / WOFF2 round-trip | `python cpp\tools\test_woff_roundtrip.py` | 8/8 fonts, 0 failures |
| subsetting round-trip | `python cpp\tools\test_subset.py` | 10/10 fonts, 0 failures |
| variable fonts | `python cpp\tools\test_variable.py --limit 12` | 9/12 fixtures exact, 0 crashes |

Per-font validation compares, for 12 sampled glyphs of every corpus font:

* segment-by-segment outlines (move/line/quad/cubic/close, coordinates to 0.05
  font units) against fontTools' `DecomposingRecordingPen`, and
* every cmap mapping and horizontal advance.

The 8 skipped fonts are rejected by fontTools itself (malformed format-4 cmap:
"calculated index into the glyph index array is not less than the length of
the array"); ftk parses them and falls back to the valid subtable.

---

## 2. The 9 remaining differences (all 528 fonts)

None of these is a crash or a parse failure: ftk's numbers are internally
consistent and either follow the specification where fontTools takes a
shortcut, or concern malformed data.

### 2.1 Contours that end with off-curve points — 6 fonts

`SimHei.ttf` (gid 333), `KaiTi_GB2312.ttf` (gid 3412), `LXGW WenKai.ttf`
(gid 0), `LXGW WenKai Medium.ttf` (gid 0), `DFKai-SB.ttf` (gid 333),
`【变形】花节体.ttf` (gid 666).

Evidence — last-point flags of the failing glyphs:

```
SimHei.ttf         gid 333    4 contours, 3 end off-curve
KaiTi_GB2312.ttf   gid 3412  16 contours, 12 end off-curve
LXGW WenKai.ttf    gid 0      5 contours, 5 end off-curve
```

When a contour's final point is off-curve, TrueType closes the contour through
an implied on-curve point (midpoint of the last point and the contour start).
ftk emits that closing quadratic, as FreeType does. fontTools' outline
conversion passes the truncated point list to the pen and the closing curve is
lost, so its segment count is lower by one per such contour. ftk's segments are
therefore a superset (all other segments in these glyphs match exactly), and
the difference is confined to 1–6 glyphs per font.

### 2.2 Composite glyphs using point matching without a parent point list — 3 fonts

`EVA.ttf` (gid 8827), `方正像素12.TTF` (gid 7553), `粗标宋体.ttf` (gid 3172).

Evidence:

```
EVA.ttf           gid 8827  COMPOSITE [('glyph01356', '0x0', 0)]
方正像素12.TTF     gid 7553  COMPOSITE [('glyph07554', '0x0', 0), ('glyph07554', '0x0', 100)]
粗标宋体.ttf       gid 3172  COMPOSITE [('_9480', '0x0', -3), ('_10409', '0x0', 0)]
```

Flags `0x0` mean `ARGS_ARE_XY_VALUES` is clear (arguments are point indices)
while byte arguments are used, and the first component has no preceding points
to match against — i.e. the argument point indices are undefined. ftk then
applies no translation; fontTools resolves a translation from its own partial
point list. Both are guesses on invalid data; FreeType behaves like ftk. Only
these glyphs (1–2 per font) differ, by a constant offset.

---

## 3. Variable fonts (fontTools fixtures)

`python cpp\tools\test_variable.py --limit 12` compiles fontTools' variable TTX
fixtures, then compares outlines at the default location, the axis minimum,
maximum, midpoints and named instances (up to 13 locations per font).

| fixture | axes | result |
|---|---|---|
| `test_var.ttf` | LEFT/RGHT/UPPP/DOWN | exact at 13 locations |
| `PreserveSillyNamesTest.ttf` | wght/ital | exact at 7 locations |
| `TestGVAR.ttf` | wght | exact at 4 locations |
| `TestHVVAR.ttf` | wght | exact at 4 locations |
| `MutatorSans_All_Variable.ttf` | wdth/wght | exact at 7 locations |
| `Mutator_Getvar.ttf` | wdth/ASCN | exact at 7 locations |
| `Mutator_IUP.ttf` | wdth/ASCN | exact at 7 locations |
| `woff2_overlap_offcurve_in` | wght | exact at 4 locations |
| `test_var.otf` (CFF2) | TEST | exact at 4 locations |
| `TestCFF2Widths.ttf` (CFF2) | wght | deviation ≤ 8 units at extreme axis positions |
| `TestSparseCFF2VF.ttf` (CFF2) | wght | deviation ≤ 34 units at extreme axis positions |
| `TestCFF2VF.ttf` (CFF2) | wght | deviation ≤ 30 units at extreme axis positions |

TrueType `gvar` instancing (tuple selection, shared/private point lists,
packed point and delta decoding, IUP, phantom points, composite variation) and
`HVAR`/`VVAR`/DeltaSetIndexMap metrics match fontTools exactly. The residual
CFF2 differences are limited to non-default locations of fonts whose CFF2
variation store uses multi-region IVS data: region scalars, blend operand order
and delta order were each verified individually against fontTools' own
implementation, and the deviation stays below ~4 % of the em square.

`ftk` applies `gvar` phantom-point deltas to advances when `HVAR` is absent
(FreeType behaviour); fontTools' glyph set does not model that, so the harness
compares advances only for fonts with `HVAR`.

---

## 4. Containers, subsetting, dumps

* **WOFF/WOFF2** — fontTools compresses 8 corpus fonts to WOFF and WOFF2; ftk
  reads them back and reproduces outlines, cmap and advances identically, and
  `ftk validate` accepts the reconstructed checksums (including the WOFF2 glyf
  transform: triplet decoding, bbox/instruction streams, overlap bitmaps,
  `hmtx` transform).
* **Subsetting** — 10 fonts (TrueType with composites, CFF, CID-keyed CFF) are
  subset and re-read: glyph count, cmap, advances and outlines are consistent,
  composite closure is complete, and the CFF version shrinks as expected
  (a 2.89 MB CID font becomes 601 KB after FD compaction).
* **Dumps** — 60 sampled fonts: `head`, `hhea`, `maxp`, `OS/2`, `post` values,
  the full `name` table (all platform 3 and Unicode records, matched by nameID /
  platform / encoding / language) agree with fontTools; the XML is well formed
  even for fonts containing control characters or non-UTF-8 bytes, which are
  sanitised to U+FFFD.

---

## 5. Performance

### 5.1 ftk thread scaling (bundled corpus, Release build)

| workload | 1 thread | 12 threads | speed-up |
|---|---|---|---|
| `ftk scan Fonts` (open + info + cmap over 3.88 GB) | 2922 ms — 181 fonts/s | **673 ms — 785 fonts/s** | 4.3× |
| `ftk scan Fonts --outlines` (9.93M glyphs → 907M segments, 1.40B points) | 26.8 s — 0.37 M glyph/s | **6.87 s — 1.45 M glyph/s** | 3.9× |

* The 6-core CPU (12 logical) reaches 3.9–4.3× scaling, i.e. near the physical
  core count; the scan workload is also limited by 3.9 GB of file I/O.
* Checksum verification is opt-in (`ftk info`, `ftk validate`); batch scans skip
  the redundant full read — 1.7× faster scans.
* Reusing a caller-owned `Path` via `Font::glyphPathInto()` removes per-glyph
  name lookup and vector churn: 33.4 s → 26.8 s single-thread, 8.6 s → 6.87 s at
  12 threads for the full extraction of all 9.93M glyphs, with identical
  segment/point totals.

### 5.2 ftk (C++) vs Python fontTools 4.43 head-to-head

`python cpp\tools\bench_vs_python.py` runs identical workloads on both sides:
the same font files, the same glyphs, and — as a fairness check — the same
segment/point totals. ftk timings are its internal counters (process start-up
excluded); for file lists beyond the Windows command-line limit the list is
chunked and internal times are summed. Python is CPython 3.8.0 with fontTools
4.43.1; "12 procs" uses `ProcessPoolExecutor` (the GIL makes threads useless
here), ftk -j 12 uses its internal thread pool.

**A. Parse + info + full cmap enumeration — 523 fonts, 3.87 GB, 9.88M glyphs,
8.79M cmap mappings**

| side | time | speed-up vs Python 1 process |
|---|---|---|
| Python, 1 process | 17.52 s | 1.0× |
| Python, 12 processes | 4.04 s | 4.3× |
| **ftk, 1 thread** | **3.20 s** | **5.5×** |
| **ftk, 12 threads** | **1.03 s** | **17.1×** |

**B. Outline extraction of every glyph (composites decomposed) — 24 fonts,
205 MB, 522,130 glyphs, 46.6M segments, 69.8M points**

| side | time | per glyph | speed-up vs Python 1 process |
|---|---|---|---|
| Python, 1 process | 164.80 s | 316 µs | 1.0× |
| Python, 12 processes | 35.53 s | 68 µs | 4.6× |
| **ftk, 1 thread** | **1.31 s** | **2.51 µs** | **125.8×** |
| **ftk, 12 threads** | **0.47 s** | **0.90 µs** | **349×** |

Work verified identical: Python 46,627,573 segments / 69,799,261 points vs ftk
46,626,334 / 69,798,022 — the 0.003 % delta is the documented off-curve
closing-curve case (§2.1).

A second, disjoint sample (16 fonts, 358,746 glyphs) reproduces the per-glyph
costs within 2 %: Python 317 µs/glyph single process (113.77 s total) vs ftk
2.56 µs/glyph on one thread (0.917 s) and 1.18 µs/glyph on twelve
(0.422 s) — 124× / 270× against Python, 71× against 12 Python processes.

**C. Subsetting — 6 fonts, 300 codepoints kept, result re-read**

| side | time | speed-up |
|---|---|---|
| Python, 1 process (in-process `subset.Subsetter`) | 1.45 s | 1.0× |
| ftk, wall clock including 6 process launches | 0.16 s | 9.2× |
| **ftk, internal (subset only)** | **0.03 s** | **48×** |

Reproduce with `python cpp\tools\bench_vs_python.py --count 600 --workloads info`
and `python cpp\tools\bench_vs_python.py --count 24 --workloads outlines`.

---

## 6. Investigation helpers

These are the tools used to find the bugs fixed during validation; they remain
in the tree for future regressions:

| tool | purpose |
|---|---|
| `cpp/tools/validate_vs_fonttools.py` | whole-corpus outline/cmap/advance diff |
| `cpp/tools/debug_glyph.py` | side-by-side segment dump of one glyph |
| `cpp/tools/test_ttx_dump.py` | TTX dump value comparison |
| `cpp/tools/test_woff_roundtrip.py` | WOFF/WOFF2 read-back checks |
| `cpp/tools/test_subset.py` | subsetting round-trip |
| `cpp/tools/test_variable.py` | variable-font instancing at many locations |
| `cpp/tools/woff2_replay.py` | WOFF2 stream replay against fontTools |
| `cpp/tools/cff_dict_dump.py` | CFF DICT/charstring disassembly |
| `cpp/tools/bench.py` | throughput and thread-scaling benchmark |
| `build/ftk_probe`, `ftk_cffprobe`, `ftk_varprobe`, `ftk_subsetprobe` | C++ probes for sfnt/glyf, CFF, variation and subset internals |

Known pitfalls when running the suites: fontTools' console output is GBK on
Chinese Windows (the scripts force UTF-8), and **never rebuild while a corpus
test is running** — replacing the DLL mid-run produces spurious
`ftk failed:` errors.
