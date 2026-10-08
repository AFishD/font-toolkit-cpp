/*
 * ftk.h — Font Toolkit C API (plugin ABI).
 *
 * A self-contained, thread-safe, high-performance font processing library.
 * Load ftk.dll/ftk.so as a plugin and call these functions from any language
 * with a C FFI.
 *
 * Conventions:
 *  - All functions are thread-safe; font objects are immutable after open
 *    and may be shared across threads without locking.
 *  - Errors: functions returning pointers return NULL on failure and set the
 *    thread-local error string (ftk_last_error). Functions returning buffers
 *    return NULL similarly.
 *  - Memory: buffers returned by the library must be freed with ftk_free().
 *    Font objects with ftk_font_close().
 *
 * Part of the Font_Toolkit C++ rewrite. MIT-style license.
 */
#ifndef FTK_H
#define FTK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _WIN32
#ifdef FTK_BUILDING_DLL
#define FTK_API __declspec(dllexport)
#else
#define FTK_API __declspec(dllimport)
#endif
#else
#define FTK_API __attribute__((visibility("default")))
#endif

/* ---------------------------------------------------------------- version */

FTK_API const char* ftk_version(void);
FTK_API int ftk_thread_pool_size(void);        /* default worker count */
FTK_API void ftk_set_thread_pool_size(int n);  /* 0 = hardware concurrency */
FTK_API const char* ftk_last_error(void);      /* thread-local error string */

/* ------------------------------------------------------------------ font */

typedef struct ftk_font ftk_font;

typedef struct ftk_error {
    int32_t code;       /* 0 = ok */
    char message[512];
} ftk_error;

/* Opens a font from a file (UTF-8 path). faceIndex selects a face in TTC. */
FTK_API ftk_font* ftk_open_file(const char* path_utf8, uint32_t face_index, ftk_error* err);
/* Opens from memory: copies the data (caller keeps ownership). */
FTK_API ftk_font* ftk_open_memory(const void* data, size_t size, uint32_t face_index, ftk_error* err);
/* Number of faces in a font file (-1 on error). */
FTK_API int32_t ftk_num_faces(const char* path_utf8);
FTK_API void ftk_font_close(ftk_font* f);

/* ------------------------------------------------------------------ info */

typedef struct ftk_font_info {
    char family[256];
    char subfamily[256];
    char full_name[512];
    char postscript_name[512];
    char version[128];
    uint16_t units_per_em;
    uint16_t num_glyphs;
    int32_t num_faces;
    uint32_t face_index;
    int32_t outline_format;      /* 0=none 1=TrueType 2=CFF 3=CFF2 */
    int32_t container;          /* 0=unknown 1=sfnt 2=ttc 3=woff 4=woff2 */
    int16_t ascender, descender, line_gap;
    uint16_t weight_class, width_class;
    int32_t is_bold, is_italic, is_fixed_pitch, is_variable, has_vertical;
    int32_t num_axes, num_instances;
    uint64_t num_cmap_mappings;
    uint32_t cmap_min, cmap_max;
    int32_t cmap_best_format;
    uint64_t file_size;
    int32_t checksums_ok;
} ftk_font_info;

FTK_API int ftk_get_font_info(ftk_font* f, ftk_font_info* out);

/* ---------------------------------------------------------------- glyphs */

FTK_API uint16_t ftk_glyph_for_unicode(ftk_font* f, uint32_t codepoint);
FTK_API uint32_t ftk_unicode_for_glyph(ftk_font* f, uint16_t gid);
/* Writes up to buf_cap glyph ids for the given codepoints. Returns needed. */
FTK_API int32_t ftk_glyphs_for_unicodes(ftk_font* f, const uint32_t* cps, int32_t n, uint16_t* out_gids);
/* Glyph name (into caller buffer). Returns length needed. */
FTK_API int32_t ftk_glyph_name(ftk_font* f, uint16_t gid, char* buf, int32_t buf_cap);

/* Outline in float segments, 8 floats (32 bytes) per segment:
 *   [0] op as float (0=move 1=line 2=quad 3=cubic 4=close)
 *   [1] reserved (0.0)
 *   [2] x0  [3] y0     (current/start point)
 *   [4] x1  [5] y1     (control point; unused ops have 0)
 *   [6] x2  [7] y2     (end point)
 * user_coords: optional variable-axis coordinates (fvar order), may be NULL.
 * Free the returned buffer with ftk_free(). */
FTK_API float* ftk_glyph_outline(ftk_font* f, uint16_t gid,
                                 const double* user_coords, int32_t n_coords,
                                 size_t* out_n_segs);
FTK_API double ftk_glyph_advance(ftk_font* f, uint16_t gid,
                                 const double* user_coords, int32_t n_coords);
FTK_API double ftk_kerning(ftk_font* f, uint16_t left, uint16_t right);
FTK_API void ftk_glyph_bounds(ftk_font* f, uint16_t gid,
                              const double* user_coords, int32_t n_coords,
                              float* out_bounds /* xMin,yMin,xMax,yMax */);

/* -------------------------------------------------------------- variable */

typedef struct ftk_axis {
    char tag[8];
    char name[128];
    double min_value, default_value, max_value;
    uint16_t name_id;
    uint16_t flags;
} ftk_axis;

typedef struct ftk_instance {
    char name[256];
    uint16_t name_id;
    uint16_t flags;
    int32_t num_coords;
    double coords[16];
} ftk_instance;

FTK_API int32_t ftk_var_axes(ftk_font* f, ftk_axis* out, int32_t cap);
FTK_API int32_t ftk_var_instances(ftk_font* f, ftk_instance* out, int32_t cap);

/* --------------------------------------------------------------- tables */

FTK_API int32_t ftk_table_tags(ftk_font* f, uint32_t* out_tags, int32_t cap);
/* Raw table copy; caller frees with ftk_free. */
FTK_API uint8_t* ftk_table_data(ftk_font* f, uint32_t tag, size_t* out_size);
FTK_API int ftk_verify_checksums(ftk_font* f);

/* ----------------------------------------------------------------- dump */

/* TTX-style XML dump. Caller frees with ftk_free. */
FTK_API char* ftk_dump_ttx(ftk_font* f, int skip_glyphs, const uint32_t* only_tags, int32_t n_tags);
/* JSON one-liner summary. Caller frees with ftk_free. */
FTK_API char* ftk_dump_json(ftk_font* f);

/* --------------------------------------------------------------- subset */

typedef struct ftk_subset_plan {
    const uint32_t* unicodes;
    int32_t num_unicodes;
    const uint16_t* gids;
    int32_t num_gids;
    const char* const* names;     /* array of char* glyph names */
    int32_t num_names;
    int32_t keep_everything;
    int32_t keep_layout;
    int32_t keep_name_table;
    int32_t keep_os2;
    int32_t keep_vertical;
} ftk_subset_plan;

/* Returns subset sfnt bytes; caller frees with ftk_free. */
FTK_API uint8_t* ftk_subset(ftk_font* f, const ftk_subset_plan* plan, size_t* out_size);

/* --------------------------------------------------------------- convert */

FTK_API uint8_t* ftk_sfnt_to_woff(const uint8_t* sfnt_data, size_t size, size_t* out_size);

/* ------------------------------------------------------------------ cmap */

/* All mappings from the best unicode cmap: pairs of (cp, gid) u32s. */
FTK_API uint32_t* ftk_cmap_mappings(ftk_font* f, size_t* out_n_pairs);

/* ------------------------------------------------------------- batch ops */

typedef void (*ftk_scan_callback)(const char* path_utf8, const char* json_info,
                                  int ok, void* user);

/* Scans a directory (optionally recursive) on the internal thread pool,
 * producing a JSON info line per font file. Returns number of files scanned. */
FTK_API int64_t ftk_scan_dir(const char* dir_utf8, int recursive, int max_threads,
                             ftk_scan_callback cb, void* user);

/* ------------------------------------------------------------- utilities */

FTK_API void ftk_free(void* p);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FTK_H */
