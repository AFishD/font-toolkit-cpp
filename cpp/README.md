# ftk — Font Toolkit (C++ rewrite of the fontTools core)

A dependency-free C++17 font-processing library, CLI and **plugin** (stable C ABI)
that re-implements the core of the Python `fontTools` toolkit as a
high-performance, thread-safe engine.

* Formats: **TTF / OTF / TTC / WOFF / WOFF2** (Brotli decoder vendored)
* Outlines: **TrueType `glyf`** (composites, point matching) and
  **CFF / CFF2** (Type2 charstrings, subrs, CID, seac, CFF2 blend)
* Variable fonts: **fvar / avar / STAT / gvar (with IUP) / HVAR / VVAR / ItemVariationStore**
* Metrics, cmap (formats 0/2/4/6/12/13/14), name, OS/2, post, kern, VORG, gasp
* Layout inspection (GSUB / GPOS / GDEF, pair kerning), color/bitmap (COLR, CPAL, sbix, CBDT/CBLC, SVG)
* TTX-style XML dump, JSON summaries, subsetting (TrueType + CFF/CID), container conversion
* Internal thread pool: parallel batch scanning/dumping, immutable fonts shareable across threads

---

## 1. Build

Requirements: CMake ≥ 3.16, a C++17 compiler, Ninja or Make. No external libraries
(the Brotli decoder used for WOFF2 is vendored under `third_party/brotli`, MIT).

```powershell
cmake -S cpp -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Artifacts (in `build/`):

| artifact | purpose |
|---|---|
| `libftk.dll` + `libftk.dll.a` | the library / plugin (Windows) |
| `ftk.exe` | command line tool |
| `ftk_tests.exe` | unit + corpus tests (`ctest`) |

Verified with MinGW-w64 g++ 8.1 (x86_64-posix-seh) and CMake 4.0 / Ninja 1.11.
On MinGW the library is built with `-Wl,--export-all-symbols` so both the C ABI
and the C++ API are linkable.

---

## 2. Command line

```
ftk info <font>...                          summary (family, metrics, glyphs, tables)
ftk dump <font> [-o out.xml] [--no-glyphs] [--table TAG]...   TTX-style XML
ftk cmap <font> [cp...]                     cmap subtables / queries
ftk outline <font> --gid N|--char U+XXXX|--name NAME [--svg] [--segments] [--coords v,v]
ftk metrics <font> --gid N|--char U+XXXX
ftk glyphs <font> [--limit N]
ftk kern <font> <leftName> <rightName>
ftk var <font>                              axes, named instances, STAT values
ftk layout <font>                           GSUB/GPOS scripts, features, lookups
ftk json <font> [--cmap] [--advances] [--outlines gids] [--names] [--coords v,v]
ftk subset <font> -o out.ttf --unicodes RANGES|--text TXT|--gids N,...|--all
           [--keep-layout] [--vertical]
ftk convert <in> -o out.[ttf|otf|woff]
ftk scan <dir>... [-r] [--json] [-j N]      parallel batch scan
ftk validate <font>...                      checksum verification
ftk faces <font>                            list faces of a collection
```

Examples:

```powershell
build\ftk.exe info "Fonts\SimHei.ttf"
build\ftk.exe outline "Fonts\SimHei.ttf" --char U+4E2D --svg
build\ftk.exe var "Fonts\Variable.ttf"
build\ftk.exe outline "Fonts\Variable.ttf" --gid 12 --coords 700      # fvar axis order
build\ftk.exe subset "Fonts\SimHei.ttf" -o sub.ttf --unicodes 0x41-0x5A,0x4E00-0x4E20
build\ftk.exe convert "Fonts\SimHei.ttf" -o simhei.woff2
build\ftk.exe scan Fonts -j 8
```

`--coords` takes one value per fvar axis, **in fvar axis order** (use `ftk var` to list it).

---

## 3. C++ API

```cpp
#include "ftk/Font.hpp"
using namespace ftk;

auto font = Font::openFile("font.ttf");          // or open(bytes), openFile(path, ttcIndex)
FontInfo info = font->info();

uint16_t gid = font->glyphForUnicode(0x4E2D);
GlyphData g = font->glyph(gid);                  // path, advance, bounds, name
printf("%zu segments, advance %g\n", g.path.cmds.size(), g.advance);

// variable instance (fvar axis order)
std::vector<double> coords = {700.0};
Path p = font->glyphPath(gid, &coords);
double adv = font->advance(gid, &coords);
```

Font objects are immutable after open: all table caches are guarded internally,
so a single `Font` can be shared across threads without external locking.

Parallel batch work uses the library thread pool:

```cpp
#include "ftk/Threads.hpp"
parallelFor(files.size(), 1, [&](size_t lo, size_t hi) {
    for (size_t i = lo; i < hi; i++) { auto f = Font::openFile(files[i]); /* ... */ }
});
```

---

## 4. Plugin ABI (`include/ftk/ftk.h`)

A stable C API, callable from any language with a C FFI. Fonts are opaque
handles; functions return NULL / non-zero on failure and fill `ftk_error`;
buffers must be released with `ftk_free()`.

```c
#include "ftk/ftk.h"

ftk_error err;
ftk_font* f = ftk_open_file("font.ttf", 0, &err);   /* face index for TTC */

ftk_font_info info;                                 /* char[256] strings + metrics */
ftk_get_font_info(f, &info);
uint16_t gid = ftk_glyph_for_unicode(f, 0x4E2D);

size_t n_segs = 0;
float* segs = ftk_glyph_outline(f, gid, NULL, 0, &n_segs);  /* 8 floats/segment */
double adv  = ftk_glyph_advance(f, gid, NULL, 0);

const double coords[1] = {700.0};                   /* one per fvar axis */
double adv700 = ftk_glyph_advance(f, gid, coords, 1);

char* xml = ftk_dump_ttx(f, 1 /*skip glyphs*/, NULL, 0);     /* TTX-style XML */

ftk_subset_plan plan = {0};
plan.unicodes = (const uint32_t[]){0x41, 0x4E2D};
plan.num_unicodes = 2;
plan.keep_os2 = plan.keep_name_table = 1;
size_t sub_len = 0;
uint8_t* sub = ftk_subset(f, &plan, &sub_len);       /* sfnt bytes of the subset */

ftk_scan_dir("Fonts", 0, 8, on_font_cb, user_data);  /* parallel scan, JSON per font */

ftk_free(segs); ftk_free(xml); ftk_free(sub);
ftk_font_close(f);
```

Every segment is 8 floats — `[op, 0, x0, y0, x1, y1, x2, y2]` — where `op` is a
float (`0 move, 1 line, 2 quadratic, 3 cubic, 4 close`), `(x0,y0)` the current
point, `(x1,y1)` the control point and `(x2,y2)` the end point. A complete,
compilable example is `cpp/tools/probes/plugindemo.c`, built as
`build/ftk_plugindemo.exe`:

```powershell
build\ftk_plugindemo.exe Fonts\SimHei.ttf Fonts
```

---

## 5. Validation

The implementation is cross-validated against Python fontTools 4.43 on the
530-font corpus in `Fonts/` plus fontTools' own test fixtures.
Detailed, reproducible results: **`docs/VALIDATION.md`**.

| test | command | result |
|---|---|---|
| unit tests | `build\ftk_tests.exe` | all tests passed |
| corpus parse/scan | `build\ftk.exe scan Fonts` | 528 fonts, 0 failures, 9.93M glyphs, 8.83M cmap mappings |
| outlines / cmap / advances vs fontTools | `python cpp\tools\validate_vs_fonttools.py` | see `docs/VALIDATION.md` |
| TTX dump values vs fontTools | `python cpp\tools\test_ttx_dump.py` | 60/60 dumps OK |
| WOFF/WOFF2 round-trip (fontTools writes, ftk reads) | `python cpp\tools\test_woff_roundtrip.py` | 8/8 fonts, outlines/cmap/advances identical |
| subset round-trip | `python cpp\tools\test_subset.py` | 16/16 fonts (TrueType + CFF + CID) |
| variable fonts (fvar/avar/gvar/HVAR) | `python cpp\tools\test_variable.py` | 9/12 fixtures exact (3 CFF2 blend cases differ slightly) |
| performance | `python cpp\tools\bench.py` | see below |

Helper scripts for investigation: `cpp/tools/debug_glyph.py`,
`cpp/tools/cff_dict_dump.py`, `cpp/tools/woff2_replay.py`,
probes under `cpp/tools/probes/` (`ftk_probe`, `ftk_cffprobe`, `ftk_varprobe`, `ftk_subsetprobe`).

### Performance

Measured on the bundled corpus (528 fonts, 3.88 GB, 9.93M glyphs) on an
AMD Ryzen 5 7600X (6 cores / 12 threads), Windows, Release build:

| workload | 1 thread | 12 threads | speed-up |
|---|---|---|---|
| `ftk scan` (open + info + cmap) | 2922 ms (181 fonts/s) | **673 ms (785 fonts/s)** | 4.3× |
| `ftk scan --outlines` (9.93M glyphs → 907M segments, 1.40B points) | 26.8 s (0.37 M glyph/s) | **6.87 s (1.45 M glyph/s)** | 3.9× |

Against **Python fontTools 4.43** on identical work
(`python cpp\tools\bench_vs_python.py`, full detail in `docs/VALIDATION.md`):

| workload | Python 1 process | Python 12 processes | ftk 1 thread | ftk 12 threads |
|---|---|---|---|---|
| parse + info + cmap, 523 fonts / 3.87 GB | 17.52 s | 4.04 s | 3.20 s (**5.5×**) | **1.03 s (17.1×)** |
| every glyph outline, 522K glyphs / 46.6M segments | 164.80 s | 35.53 s | 1.31 s (**126×**) | **0.47 s (349×)** |
| subset 6 fonts, 300 codepoints | 1.45 s | — | **0.03 s (48×)** | — |

* single 45.5 MB font: full parse + metrics in ~56 ms (process start included);
* checksum verification is opt-in (`ftk info` / `ftk validate`), so batch scans
  skip a redundant full read of every table — 1.7× faster scans;
* `Font` objects are immutable and shareable across threads; batch tools use the
  built-in pool (`-j N`), and `glyphPathInto()` reuses a caller-owned `Path`
  buffer to avoid per-glyph allocation (20–25 % faster bulk extraction).

---

## 6. Known limitations and deliberate differences

Full evidence and reproduction commands: `docs/VALIDATION.md`.

* **Contours whose last point is off-curve**: ftk draws the closing quadratic
  through the implied on-curve point (spec/FreeType behaviour); fontTools'
  outline conversion drops it, so segment counts can differ by one per such
  contour. Affects 6 of 528 corpus fonts, 1–6 glyphs each; all other segments
  in those glyphs match exactly.
* **Composite glyphs with point matching (`ARGS_ARE_XY_VALUES` clear) whose
  argument points do not exist** (undefined/malformed data): ftk applies no
  translation, fontTools derives one from its partial point list, FreeType
  behaves like ftk. Affects 3 of 528 corpus fonts, 1–2 glyphs each.
* **CFF2 blend at non-default locations**: for variable fonts whose CFF2
  variation store uses multi-region IVS data, blended outlines can differ from
  fontTools by up to ~4 % of the em square at extreme axis positions (region
  scalars, delta order and operand order were verified individually). Default
  locations are exact, and TrueType `gvar` instancing matches fontTools exactly.
* `gvar` phantom-point deltas are applied to advances when `HVAR` is absent
  (FreeType behaviour); fontTools' glyph set does not model that.
* CFF2 fonts are inspected/instanced but **not subset** (CFF1/CID subsetting is
  supported; CFF2 subsetting is rejected rather than producing a broken font).
* `gasp`, `DSIG`, `MATH`, AAT (`morx`/`mort`) tables are parsed only structurally
  (presence/existence) and copied verbatim, not interpreted.
* Color fonts: COLR v0 layers and CPAL palettes are fully decoded; COLR v1
  paint graphs and CBDT/EBDT bitmaps are enumerated structurally.
* `hdmx`, `PCLT`, `VDMX`, `LTSH` are treated as opaque tables.
* No GSUB/GPOS *shaping* engine: layout tables are inspected (scripts, features,
  lookups, GPOS pair kerning extraction, GSUB single substitutions).
* `name` records on the Macintosh platform are decoded as Mac Roman; other Mac
  script encodings (Big5, Shift-JIS, GB2312, …) are not decoded — render such
  records as raw bytes (Windows/Unicode records are fully decoded).
* Single-face WOFF2 only: WOFF2 files that wrap a TTC are rejected with a clear error.

---

## 7. Layout

```
cpp/
  include/ftk/          public headers (ftk.h = C ABI, *.hpp = C++ API)
  src/support/          buffers, inflate, brotli, threads, platform, unicode
  src/sfnt/             sfnt/ttc/woff/woff2 containers
  src/tables/           head/hhea/maxp/hmtx/OS2/post/name/cmap/kern/gasp/VORG
  src/glyf/             loca/glyf, contours, composite resolution
  src/cff/              CFF/CFF2 INDEX/DICT/charset/FDArray/Type2 interpreter
  src/var/              fvar/avar/STAT/IVS/gvar/HVAR/VVAR
  src/layout/           GDEF/GSUB/GPOS inspection + kerning
  src/color/            COLR/CPAL/sbix/CBDT-CBLC/SVG
  src/dump/             TTX-style XML + JSON
  src/subset/           subsetter + WOFF writer
  src/api/              Font facade, C ABI implementation
  src/cli/              ftk command line
  tools/                validation/benchmark scripts and probes
  tests/                unit + corpus tests
  third_party/brotli/   vendored Brotli decoder (MIT)
```

License: MIT (the vendored Brotli decoder keeps its own MIT license,
see `third_party/brotli/LICENSE`).
