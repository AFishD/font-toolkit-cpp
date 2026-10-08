# ftk — Font Toolkit in C++

A dependency-free **C++17** font toolkit — library, command-line tool and
**plugin** — that reimplements the core of the Python
[fontTools](https://github.com/fonttools/fonttools) as a high-performance,
thread-safe engine.

It is built to be embedded: a stable **C ABI** plus a C++ API, no external
dependencies, and a font model that is immutable after open so one `Font`
object can be shared across threads.

* **Containers** — TTF, OTF, TTC, WOFF, WOFF2 (Brotli decoder vendored)
* **Outlines** — TrueType `glyf` (composites, point matching), CFF, CFF2
  (Type 2 charstrings, subrs, CID, `seac`, blend)
* **Variable fonts** — fvar, avar, STAT, gvar (shared/private point lists,
  packed deltas, IUP, phantom points), HVAR, VVAR, ItemVariationStore
* **Core tables** — head, hhea, maxp, hmtx, OS/2, post, name, cmap (formats
  0/2/4/6/12/13/14), kern, gasp, VORG
* **Layout & color** — GSUB/GPOS/GDEF inspection, pair kerning, COLR/CPAL,
  sbix, CBDT/CBLC, SVG
* **Operations** — TTX-style XML dump, JSON, subsetting (TrueType + CFF/CID),
  WOFF conversion, parallel batch scan, checksum verification
* **Concurrency** — internal thread pool; 1 → 12 threads scales ~4× on a
  3.9 GB / 9.9M glyph corpus

## Build

Requires CMake ≥ 3.16 and a C++17 compiler (verified with MinGW-w64 g++ 8.1
and CMake 4.0 / Ninja; GCC/Clang on Linux and MSVC should work as well).

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Build output: `build/libftk.dll` (or `.so`) — the library / plugin — and
`build/ftk` — the command-line tool. Add `-DFTK_BUILD_DEMO=ON` to also build
[`docs/demo.c`](docs/demo.c).

## Quick start

```bash
ftk info MyFont.ttf                                # family, metrics, tables
ftk outline MyFont.ttf --char U+4E2D --svg         # glyph outline as SVG
ftk var Variable.ttf                               # axes and named instances
ftk dump MyFont.ttf -o font.ttx                    # TTX-style XML
ftk subset MyFont.ttf -o mini.ttf --unicodes 0x41-0x5A,0x4E00-0x4E20
ftk convert MyFont.ttf -o myfont.woff2             # container conversion
ftk scan Fonts -j 12                               # parallel batch scan
```

Using the library from C:

```c
#include "ftk/ftk.h"

ftk_error err;
ftk_font* f = ftk_open_file("MyFont.ttf", 0, &err);

ftk_font_info info;
ftk_get_font_info(f, &info);

uint16_t gid = ftk_glyph_for_unicode(f, 0x4E2D);
size_t n_segs;
float* segs = ftk_glyph_outline(f, gid, NULL, 0, &n_segs);  /* 8 floats per segment */

const double coords[1] = {700.0};                           /* one per fvar axis */
double advance = ftk_glyph_advance(f, gid, coords, 1);

ftk_free(segs);
ftk_font_close(f);
```

A complete, compilable example is [`docs/demo.c`](docs/demo.c); the full API
reference (C ABI, C++ API and CLI) is in [`docs/API.md`](docs/API.md).

## Documentation

| file | contents |
|---|---|
| [`docs/API.md`](docs/API.md) | every API function: C ABI, C++ API, CLI commands |
| [`docs/demo.c`](docs/demo.c) | plugin integration example (open → glyph → subset → scan) |
| [`README.zh-CN.md`](README.zh-CN.md) | 中文说明 |

## License

MIT — see [LICENSE](LICENSE). Third-party code and the fontTools reference are
credited in [THIRD_PARTY.md](THIRD_PARTY.md).
