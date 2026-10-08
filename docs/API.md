# ftk API reference

Three ways to use the toolkit:

1. [C ABI](#1-c-abi-plugin) — `include/ftk/ftk.h`, the stable plugin interface
   (any language with a C FFI). Everything below is `extern "C"`.
2. [C++ API](#2-c-api) — the C++ classes the CLI and the C ABI are built on.
3. [Command line](#3-command-line) — the `ftk` executable.

Working example of the whole C surface: [`demo.c`](demo.c).
Conventions used throughout:

* a `ftk_font*` is immutable once opened and **safe to share between threads**;
* functions returning a pointer/`NULL` report the reason via `ftk_last_error()`
  (thread-local) or through the `ftk_error*` out-parameter of the open calls;
* any buffer the library returns must be released with `ftk_free()`; a font with
  `ftk_font_close()`.

---

## 1. C ABI (plugin)

### Library-wide

| function | purpose |
|---|---|
| `const char* ftk_version(void)` | library version string, e.g. `"1.0.0"` |
| `int ftk_thread_pool_size(void)` | current worker-thread count of the internal pool |
| `void ftk_set_thread_pool_size(int n)` | resize the pool (`0` = hardware concurrency) |
| `const char* ftk_last_error(void)` | thread-local message for the last failing call |

### Opening and closing

```c
ftk_font* ftk_open_file(const char* path_utf8, uint32_t face_index, ftk_error* err);
ftk_font* ftk_open_memory(const void* data, size_t size, uint32_t face_index, ftk_error* err);
int32_t   ftk_num_faces(const char* path_utf8);   /* -1 on error */
void      ftk_font_close(ftk_font* f);
```

`path_utf8` is UTF-8 (or ASCII); `face_index` selects a face inside a TTC.
`ftk_open_memory` copies the data, so the caller keeps ownership of its buffer.
Opening auto-detects sfnt, TTC, WOFF and WOFF2.

### Font information

```c
typedef struct ftk_font_info {
    char     family[256], subfamily[256], full_name[512], postscript_name[512], version[128];
    uint16_t units_per_em, num_glyphs;
    int32_t  num_faces, face_index;
    int32_t  outline_format;      /* 0=none 1=TrueType 2=CFF 3=CFF2 */
    int32_t  container;           /* 0=unknown 1=sfnt 2=ttc 3=woff 4=woff2 */
    int16_t  ascender, descender, line_gap;
    uint16_t weight_class, width_class;
    int32_t  is_bold, is_italic, is_fixed_pitch, is_variable, has_vertical;
    int32_t  num_axes, num_instances;
    uint64_t num_cmap_mappings;   /* mappings in the best Unicode subtable */
    uint32_t cmap_min, cmap_max;
    int32_t  cmap_best_format;    /* 4, 12, ... */
    uint64_t file_size;
    int32_t  checksums_ok;
} ftk_font_info;

int ftk_get_font_info(ftk_font* f, ftk_font_info* out);   /* 0 = ok */
```

This single call verifies the table checksums and summarises the font, so it is
the recommended first call after opening.

### Glyph lookup and names

| function | purpose |
|---|---|
| `uint16_t ftk_glyph_for_unicode(ftk_font*, uint32_t cp)` | codepoint → glyph id (`0` = `.notdef`) |
| `uint32_t ftk_unicode_for_glyph(ftk_font*, uint16_t gid)` | reverse lookup (best effort) |
| `int32_t ftk_glyphs_for_unicodes(ftk_font*, const uint32_t* cps, int32_t n, uint16_t* out)` | batch lookup; returns the number of ids needed |
| `int32_t ftk_glyph_name(ftk_font*, uint16_t gid, char* buf, int32_t cap)` | glyph name into a caller buffer; returns the length needed |

### Outlines, metrics, kerning

```c
float* ftk_glyph_outline(ftk_font* f, uint16_t gid,
                         const double* user_coords, int32_t n_coords,
                         size_t* out_n_segs);
double ftk_glyph_advance(ftk_font* f, uint16_t gid,
                         const double* user_coords, int32_t n_coords);
void   ftk_glyph_bounds(ftk_font* f, uint16_t gid,
                        const double* user_coords, int32_t n_coords,
                        float* out_bounds /* xMin,yMin,xMax,yMax */);
double ftk_kerning(ftk_font* f, uint16_t left, uint16_t right);
```

`user_coords` holds one coordinate per fvar axis, **in fvar order**, and may be
`NULL` for the default instance (see `ftk_var_axes`). Composite glyphs are
decomposed; advances follow HVAR/gvar variation.

The outline is returned as `out_n_segs` segments of **8 floats (32 bytes)** each:

```
[0] op   0=move 1=line 2=quadratic 3=cubic 4=close
[1] reserved (0.0)
[2] x0  [3] y0    current / start point
[4] x1  [5] y1    control point (0 for line/close)
[6] x2  [7] y2    end point
```

### Variable fonts

```c
typedef struct ftk_axis {
    char tag[8], name[128];
    double min_value, default_value, max_value;
    uint16_t name_id, flags;
} ftk_axis;

typedef struct ftk_instance {
    char name[256];
    uint16_t name_id, flags;
    int32_t num_coords;
    double coords[16];
} ftk_instance;

int32_t ftk_var_axes(ftk_font* f, ftk_axis* out, int32_t cap);        /* count needed */
int32_t ftk_var_instances(ftk_font* f, ftk_instance* out, int32_t cap);
```

Both return the number of records needed; call once with `cap = 0` (or a small
buffer) to size, then again with a large enough buffer.

### Raw tables and checksums

| function | purpose |
|---|---|
| `int32_t ftk_table_tags(ftk_font*, uint32_t* out, int32_t cap)` | table tags as 4-byte big-endian values |
| `uint8_t* ftk_table_data(ftk_font*, uint32_t tag, size_t* out_size)` | copy of one raw table |
| `int ftk_verify_checksums(ftk_font*)` | `0` when every checksum matches |

### Dump

```c
char* ftk_dump_ttx(ftk_font* f, int skip_glyphs, const uint32_t* only_tags, int32_t n_tags);
char* ftk_dump_json(ftk_font* f);
```

`skip_glyphs` omits per-glyph data (much faster on large CJK fonts);
`only_tags`/`n_tags` restrict the dump to selected tables (`NULL`/`0` = all).
Both return NUL-terminated text.

### Subsetting

```c
typedef struct ftk_subset_plan {
    const uint32_t* unicodes;      int32_t num_unicodes;
    const uint16_t* gids;          int32_t num_gids;
    const char* const* names;      int32_t num_names;
    int32_t keep_everything;       /* 1 = keep all glyphs (re-encode only) */
    int32_t keep_layout;           /* retain GSUB/GPOS/GDEF                */
    int32_t keep_name_table;
    int32_t keep_os2;
    int32_t keep_vertical;
} ftk_subset_plan;

uint8_t* ftk_subset(ftk_font* f, const ftk_subset_plan* plan, size_t* out_size);
```

Returns complete sfnt bytes for the subset (TrueType with composite closure and
CFF/CID are supported; CFF2 fonts are rejected with an error instead of
producing a broken font). Re-open the result with `ftk_open_memory`.

### Conversion and cmap

| function | purpose |
|---|---|
| `uint8_t* ftk_sfnt_to_woff(const uint8_t* sfnt, size_t size, size_t* out_size)` | wrap sfnt bytes in a WOFF container |
| `uint32_t* ftk_cmap_mappings(ftk_font*, size_t* out_n_pairs)` | all mappings of the best Unicode cmap as `(codepoint, gid)` pairs |

### Parallel batch scan

```c
typedef void (*ftk_scan_callback)(const char* path_utf8, const char* json_info,
                                  int ok, void* user);

int64_t ftk_scan_dir(const char* dir_utf8, int recursive, int max_threads,
                     ftk_scan_callback cb, void* user);
```

Walks a directory on the internal thread pool and reports every file it tries:
`ok = 1` with a JSON info line, `ok = 0` for non-fonts or failures. The callback
runs on worker threads, so it must be thread-safe. Returns the number of files
scanned.

---

## 2. C++ API

### `ftk::Font` — `include/ftk/Font.hpp`

```cpp
#include "ftk/Font.hpp"
using namespace ftk;

auto font = Font::openFile("MyFont.ttf");          // or Font::open(bytes, ttcIndex)
FontInfo fi = font->info(true);                    // true = verify checksums

uint16_t gid = font->glyphForUnicode(0x4E2D);
GlyphData g  = font->glyph(gid);                   // path, advance, bounds, name
double adv   = font->advance(gid);

std::vector<double> coords = {700.0};              // fvar axis order
Path p = font->glyphPath(gid, &coords);

Path scratch;                                      // reuse in hot loops
font->glyphPathInto(gid, scratch, &coords);
```

| group | members |
|---|---|
| open | `open(data, ttcIndex)`, `openFile(path, ttcIndex)`, `numFacesInFile(path)` |
| info | `info(computeChecksums)`, `path()`, `fileBytes()`, `tableTags()`, `hasTable(tag)`, `rawTable(tag, len)`, `rawTableCopy(tag)`, `verifyChecksums(&err)` |
| tables | `head()`, `hhea()`, `maxp()`, `hmtx()`, `os2()`, `post()`, `nameTable()`, `cmap()`, `gasp()`, `kern()`, `vhea()`, `vmtx()`, `vorg()`, `layout()`, `color()` |
| glyphs | `numGlyphs()`, `glyphForUnicode(cp)`, `unicodeForGlyph(gid)`, `glyphName(gid)`, `glyphForName(name)`, `glyphIdsForUnicodes(cps)`, `compositeClosure(gids, keep)` |
| geometry | `glyph(gid, coords)`, `glyphPath(...)`, `glyphPathInto(gid, out, coords)`, `glyphBounds(...)`, `advance(...)`, `verticalAdvance(...)`, `kerning(l, r)` |
| variation | `isVariable()`, `defaultCoords()`, `normalizeCoords(userCoords)`, `fvar()`, `avar()`, `stat()`, `gvar()`, `hvar()`, `vvar()` |

`Font` is immutable after construction: all table caches are internally
synchronised, so one instance can be shared by any number of threads.

### `ftk::Path` — `include/ftk/Glyphs.hpp`

Segment list in font units; `PathOp` is `MoveTo/LineTo/QuadTo/CubicTo/Close`.
Members: `cmds` (`std::vector<PathCmd>`), `moves`, `empty()`, `totalPoints()`,
`totalCurves()`, `bounds()`, `toSvg(scale, dx, dy, flipY)`.

### Other headers

| header | contents |
|---|---|
| `ftk/Bytes.hpp` | bounds-checked big-endian `Reader`/`Writer`, `Tag` helpers |
| `ftk/Threads.hpp` | `ThreadPool`, `ThreadPool::shared()`, `parallelFor(n, grain, fn)` |
| `ftk/Dump.hpp` | `dumpTtx`, `dumpInfoJson`, `dumpInfoText` |
| `ftk/Subset.hpp` | `SubsetPlan`, `subsetFont(font, plan)` |
| `ftk/Sfnt.hpp` | container parsing (`SfntFont`, `ContainerType`) |
| `ftk/Cff.hpp`, `ftk/VarTables.hpp`, `ftk/Layout.hpp`, `ftk/Color.hpp` | table-level APIs |

```cpp
#include "ftk/Threads.hpp"
parallelFor(files.size(), 1, [&](size_t lo, size_t hi) {
    for (size_t i = lo; i < hi; i++) { auto f = Font::openFile(files[i]); /* ... */ }
});
```

Errors are thrown as `ftk::Error` (derived from `std::runtime_error`).

---

## 3. Command line

```
ftk info <font>...                       font summary (family, metrics, tables)
ftk dump <font> [-o out.xml] [--no-glyphs] [--table TAG]...
ftk cmap <font> [cp...]                  cmap subtables / queries
ftk outline <font> --gid N|--char U+XXXX|--name NAME [--svg] [--segments] [--coords v,v]
ftk metrics <font> --gid N|--char U+XXXX
ftk glyphs <font> [--limit N]            glyph list
ftk kern <font> <left> <right>           kerning between two glyphs
ftk var <font>                           axes, named instances, STAT values
ftk layout <font>                        GSUB/GPOS scripts, features, lookups
ftk json <font> [--cmap] [--advances] [--outlines gids] [--names] [--coords v,v]
ftk subset <font> -o out.ttf --unicodes RANGES|--text TXT|--gids N,...|--all
           [--keep-layout] [--vertical]
ftk convert <in> -o out.[ttf|otf|woff]   container conversion
ftk scan <dir|font>... [-r] [--json] [--outlines] [-j N]
ftk validate <font>...                   checksum verification
ftk faces <font>                         faces inside a collection
ftk version
```

Notes:

* `--coords` takes one value per fvar axis **in fvar order** (`ftk var` lists the
  order); pass the default value to leave an axis unchanged.
* `--gids`, `--unicodes` and `--text` may be combined when subsetting;
  `--unicodes` accepts ranges such as `0x41-0x5A,0x4E00-0x4E20`.
* `ftk scan` reports throughput on stderr and returns a non-zero exit code when
  any font fails to parse.
