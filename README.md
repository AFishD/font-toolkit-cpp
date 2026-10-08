# ftk — Font Toolkit in C++

A dependency-free **C++17** font-processing library, command-line tool and
**plugin** that reimplements the core of the Python
[fontTools](https://github.com/fonttools/fonttools) toolkit as a
high-performance, thread-safe engine.

Built to be embedded: a stable **C ABI** (`ftk.h`) plus a C++ API, no external
dependencies, and a font model that stays immutable after open so a single
`Font` can be shared across threads.

```
┌────────────┐   C ABI (ftk.h)   ┌──────────────────────────────────────┐
│ host app   │ ────────────────► │ libftk: sfnt, glyf, CFF/CFF2, gvar,  │
│ (any lang) │ ◄──────────────── │ HVAR, layout, color, subset, dump    │
└────────────┘   threads         └──────────────────────────────────────┘
```

## Highlights

| area | support |
|---|---|
| Containers | TTF, OTF, **TTC**, WOFF, **WOFF2** (Brotli decoder vendored) |
| Outlines | TrueType `glyf` (composites, point matching), **CFF**, **CFF2** (Type2 charstrings, subrs, CID, `seac`, blend) |
| Variable fonts | fvar, avar, STAT, **gvar** (shared/private point lists, packed deltas, IUP, phantom points), HVAR, VVAR, ItemVariationStore |
| Other tables | head, hhea, maxp, hmtx, OS/2, post, name, cmap (0/2/4/6/12/13/14), kern, gasp, VORG |
| Layout / color | GSUB/GPOS/GDEF inspection, GPOS pair kerning, COLR/CPAL, sbix, CBDT/CBLC, SVG |
| Operations | TTX-style XML dump, JSON, **subsetting** (TrueType + CFF/CID), WOFF conversion, parallel batch scan, checksum verification |
| Interfaces | `libftk.dll` (C ABI), C++ API, `ftk` CLI, diagnostic probes |

## Build

Requirements: CMake ≥ 3.16 and a C++17 compiler (verified with MinGW-w64
g++ 8.1 and CMake 4.0 / Ninja; MSVC and GCC/Clang on Linux should work too —
no platform-specific build steps).

```bash
cmake -S cpp -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Artifacts: `build/libftk.dll` (or `.so`), `build/ftk` (CLI), `build/ftk_tests`.

## Quick start

```bash
ftk info Fonts/MyFont.ttf
ftk outline Fonts/MyFont.ttf --char U+4E2D --svg
ftk var Fonts/Variable.ttf                     # axes and named instances
ftk dump Fonts/MyFont.ttf -o font.ttx
ftk subset Fonts/MyFont.ttf -o mini.ttf --unicodes 0x41-0x5A,0x4E00-0x4E20
ftk convert Fonts/MyFont.ttf -o myfont.woff2
ftk scan Fonts -j 12                           # parallel batch scan
```

Plugin (C):

```c
#include "ftk/ftk.h"

ftk_error err;
ftk_font* f = ftk_open_file("MyFont.ttf", 0, &err);
ftk_font_info info;
ftk_get_font_info(f, &info);

uint16_t gid = ftk_glyph_for_unicode(f, 0x4E2D);
size_t n_segs;
float* segs = ftk_glyph_outline(f, gid, NULL, 0, &n_segs);   /* 8 floats/segment */
const double coords[1] = {700.0};                            /* one per fvar axis */
double advance = ftk_glyph_advance(f, gid, coords, 1);

ftk_free(segs);
ftk_font_close(f);
```

See [`cpp/tools/probes/plugindemo.c`](cpp/tools/probes/plugindemo.c) for a
complete compiled example, and [`cpp/README.md`](cpp/README.md) for the full
CLI, C++ API and ABI reference.

## Validation

Cross-validated against Python fontTools 4.43 on a 528-font corpus
(3.88 GB, 9.9M glyphs):

| suite | result |
|---|---|
| outlines + cmap + advances per font | **511 exact**, 9 differences (all diagnosed, see below), 8 fonts fontTools cannot load |
| TTX dump values | 60/60 |
| WOFF/WOFF2 round-trip | 8/8 |
| subsetting round-trip | 10/10 |
| variable fonts (fvar/avar/gvar/HVAR) | 9/12 fixtures exact |
| unit tests | pass |

The 9 differences are not crashes and not regressions:

* **6 fonts** — contours whose last point is off-curve: ftk draws the closing
  curve through the implied on-curve point (spec / FreeType behaviour), while
  fontTools' outline conversion drops it.
* **3 fonts** — composite glyphs using point matching whose argument points do
  not exist (malformed data): ftk applies no translation, fontTools guesses one.

Full evidence and reproduction commands: [`docs/VALIDATION.md`](docs/VALIDATION.md).

## Performance

AMD Ryzen 5 7600X (6 cores / 12 threads), Windows, Release build. Identical
work on both sides, verified by matching segment/point totals
(`python cpp/tools/bench_vs_python.py`):

| workload | Python fontTools 1 proc | Python 12 procs | ftk 1 thread | ftk 12 threads |
|---|---|---|---|---|
| parse + info + cmap (523 fonts, 3.87 GB) | 17.52 s | 4.04 s | 3.20 s (**5.5×**) | **1.03 s (17.1×)** |
| every glyph outline (522K glyphs, 46.6M segments) | 164.80 s | 35.53 s | 1.31 s (**126×**) | **0.47 s (349×)** |
| subset 6 fonts (300 codepoints) | 1.45 s | — | **0.03 s (48×)** | — |

ftk's own thread scaling on the corpus: 181 → 785 fonts/s (`ftk scan`) and
0.37 → 1.45 M glyphs/s (full outline extraction) from 1 to 12 threads.

## Repository layout

```
cpp/
  include/ftk/       public headers (ftk.h = stable C ABI, *.hpp = C++ API)
  src/support/       buffers, inflate, Brotli, threads, platform, Unicode
  src/sfnt/          sfnt / ttc / woff / woff2 containers
  src/tables/        core OpenType tables
  src/glyf/          loca/glyf, contours, composite resolution
  src/cff/           CFF/CFF2 INDEX, DICT, charset, FDArray, Type2 interpreter
  src/var/           fvar/avar/STAT/ItemVariationStore/gvar/HVAR/VVAR
  src/layout/        GSUB/GPOS/GDEF inspection and kerning
  src/color/         COLR/CPAL/sbix/CBDT-CBLC/SVG
  src/dump/          TTX-style XML and JSON
  src/subset/        subsetter and WOFF writer
  src/api/           Font facade, C ABI implementation
  src/cli/           ftk command line
  tests/             unit + corpus tests
  tools/             validation and benchmark scripts, diagnostic probes
  third_party/brotli vendored Brotli decoder (MIT)
docs/
  VALIDATION.md      full validation report and performance data
```

The test corpus and the Python reference sources are **not** part of this
repository (size and licensing); the validation scripts expect a font directory
(`FTK_FONTS`, default `./Fonts`) and, for the variable-font fixtures, a
fontTools source checkout (`FTK_TTX_ROOT`).

## License

MIT — see [LICENSE](LICENSE). Vendored third-party code and the fontTools
reference are credited in [THIRD_PARTY.md](THIRD_PARTY.md).
