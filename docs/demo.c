/* demo.c — plugin integration example for ftk.
 *
 * Shows every part of the C ABI in a realistic host-application flow:
 * open a font, inspect it, pull glyph outlines and metrics (including a
 * variable instance), read raw tables, dump XML/JSON, subset the font and
 * re-open the result from memory, and run a parallel directory scan.
 *
 * Build with the project (recommended):
 *     cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DFTK_BUILD_DEMO=ON
 *     cmake --build build
 *     build/ftk_demo MyFont.ttf [font-directory]
 *
 * Or standalone (the shared library must be next to the executable):
 *     gcc -std=c11 -Iinclude -o demo demo.c -Lbuild -lftk        # MinGW
 *     cc  -std=c11 -Iinclude -o demo demo.c -Lbuild -lftk        # Linux
 *
 * API reference: docs/API.md
 */
#if defined(_WIN32) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1 /* ISO-C printf checking under MinGW */
#endif

#include "ftk/ftk.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------ scan */

/* Called on thread-pool workers for every file ftk_scan_dir() visits. */
static void on_font(const char* path, const char* json_info, int ok, void* user) {
    int* count = (int*)user;
    (*count)++;
    if (*count <= 3)
        printf("    %-44s ok=%d (%lu bytes of JSON)\n", path, ok,
               (unsigned long)(json_info ? strlen(json_info) : 0));
}

/* ------------------------------------------------------------------ main */

int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <font.ttf|otf|ttc|woff|woff2> [directory-to-scan]\n",
                argv[0]);
        return 2;
    }
    const char* path = argv[1];
    const char* dir = argc > 2 ? argv[2] : NULL;

    printf("ftk %s — plugin ABI demo\n", ftk_version());
    printf("thread pool: %d workers (set with ftk_set_thread_pool_size)\n\n",
           ftk_thread_pool_size());

    /* ---------------------------------------------------------- open */
    ftk_error err;
    memset(&err, 0, sizeof err);
    ftk_font* font = ftk_open_file(path, 0 /* face index in a TTC */, &err);
    if (!font) {
        fprintf(stderr, "cannot open %s: %s\n", path, err.message);
        return 1;
    }

    /* ---------------------------------------------------------- info */
    ftk_font_info info;
    if (ftk_get_font_info(font, &info) != 0) {
        fprintf(stderr, "ftk_get_font_info failed: %s\n", ftk_last_error());
        ftk_font_close(font);
        return 1;
    }
    printf("file        : %s\n", path);
    printf("family      : %s (%s)\n", info.family, info.subfamily);
    printf("version     : %s\n", info.version);
    printf("glyphs      : %u   units/em: %u   faces: %d\n", info.num_glyphs,
           info.units_per_em, info.num_faces);
    printf("outline fmt : %d (0=none 1=TrueType 2=CFF 3=CFF2)\n", info.outline_format);
    printf("container   : %d (0=unknown 1=sfnt 2=ttc 3=woff 4=woff2)\n", info.container);
    printf("cmap        : %llu mappings (U+%04X..U+%04X, best format %d)\n",
           (unsigned long long)info.num_cmap_mappings, info.cmap_min, info.cmap_max,
           info.cmap_best_format);
    printf("metrics     : ascender %d, descender %d, line gap %d\n",
           info.ascender, info.descender, info.line_gap);
    printf("checksums   : %s\n", info.checksums_ok ? "ok" : "MISMATCH");

    /* ------------------------------------------------------- glyphs */
    uint32_t cp = 0x4E2D; /* CJK "middle" if present, else .notdef */
    uint16_t gid = ftk_glyph_for_unicode(font, cp);

    char name[128];
    int name_len = ftk_glyph_name(font, gid, name, (int)sizeof name);
    printf("\nglyph       : U+%04X -> gid %u (%s)\n", cp, gid,
           name_len > 0 ? name : "?");

    size_t n_segs = 0;
    float* segs = ftk_glyph_outline(font, gid, NULL, 0, &n_segs);
    if (segs && n_segs) {
        printf("outline     : %zu segments, advance %.1f, bounds", n_segs,
               ftk_glyph_advance(font, gid, NULL, 0));
        float b[4];
        ftk_glyph_bounds(font, gid, NULL, 0, b);
        printf(" [%g %g %g %g]\n", b[0], b[1], b[2], b[3]);
        /* segments are 8 floats: op, reserved, (x0,y0), (x1,y1), (x2,y2) */
        printf("first seg   : op=%d start=(%g,%g) ctrl=(%g,%g) end=(%g,%g)\n",
               (int)segs[0], segs[2], segs[3], segs[4], segs[5], segs[6], segs[7]);
        ftk_free(segs);
    } else {
        printf("outline     : empty (missing codepoint?)\n");
        ftk_free(segs);
    }

    printf("kerning     : gid %u/%u = %.1f\n", gid, gid,
           ftk_kerning(font, gid, gid));

    /* ----------------------------------------------------- variable */
    if (info.is_variable) {
        ftk_axis axes[8];
        int32_t n_axes = ftk_var_axes(font, axes, 8);
        printf("\nvariable    : %d axes, %d named instances\n", n_axes,
               info.num_instances);
        for (int32_t i = 0; i < n_axes && i < 8; i++)
            printf("  axis %-4s %-24s %g .. %g .. %g\n", axes[i].tag, axes[i].name,
                   axes[i].min_value, axes[i].default_value, axes[i].max_value);

        if (n_axes > 0) {
            /* one coordinate per axis, in fvar order (see ftk_var_axes) */
            double coords[8] = {0};
            ftk_axis all[8];
            int32_t got = ftk_var_axes(font, all, 8);
            for (int32_t i = 0; i < got && i < 8; i++) coords[i] = all[i].max_value;
            printf("  advance at maximum: %.1f  (default %.1f)\n",
                   ftk_glyph_advance(font, gid, coords, got),
                   ftk_glyph_advance(font, gid, NULL, 0));
        }
    }

    /* -------------------------------------------------------- tables */
    uint32_t tags[128];
    int32_t n_tags = ftk_table_tags(font, tags, 128);
    printf("\ntables      : %d", n_tags);
    for (int32_t i = 0; i < n_tags; i++) {
        char tag[5];
        tag[0] = (char)(tags[i] >> 24);
        tag[1] = (char)(tags[i] >> 16);
        tag[2] = (char)(tags[i] >> 8);
        tag[3] = (char)tags[i];
        tag[4] = 0;
        printf(" %s", tag);
    }
    printf("\n");
    printf("checksums   : %s\n",
           ftk_verify_checksums(font) == 0 ? "verified" : "verification failed");

    size_t head_len = 0;
    uint8_t* head = ftk_table_data(font, 0x68656164 /* 'head' */, &head_len);
    printf("raw 'head'  : %s (%lu bytes)\n", head ? "available" : "missing",
           (unsigned long)head_len);
    ftk_free(head);

    /* ---------------------------------------------------------- dumps */
    char* json = ftk_dump_json(font);
    if (json) {
        printf("JSON dump   : %.48s...\n", json);
        ftk_free(json);
    }
    char* xml = ftk_dump_ttx(font, 1 /* skip glyphs */, NULL, 0);
    if (xml) {
        printf("TTX dump    : %lu bytes\n", (unsigned long)strlen(xml));
        ftk_free(xml);
    }

    /* ------------------------------------------------------- subsetting */
    {
        const uint32_t keep[] = {0x41, 0x42, 0x43, 0x4E2D}; /* A B C 中 */
        ftk_subset_plan plan;
        memset(&plan, 0, sizeof plan);
        plan.unicodes = keep;
        plan.num_unicodes = 4;
        plan.keep_name_table = 1;
        plan.keep_os2 = 1;

        size_t sub_len = 0;
        uint8_t* sub = ftk_subset(font, &plan, &sub_len);
        if (sub) {
            printf("\nsubset      : %lu bytes from a %llu byte font\n",
                   (unsigned long)sub_len,
                   (unsigned long long)info.file_size);
            /* the result is a complete font: re-open it straight from memory */
            ftk_font* reopened = ftk_open_memory(sub, sub_len, 0, &err);
            if (reopened) {
                ftk_font_info si;
                ftk_get_font_info(reopened, &si);
                printf("re-open     : %u glyphs, %s\n", si.num_glyphs, si.family);
                ftk_font_close(reopened);
            } else {
                printf("re-open     : failed (%s)\n", err.message);
            }
            ftk_free(sub);
        } else {
            printf("\nsubset      : failed (%s)\n", ftk_last_error());
        }
    }

    ftk_font_close(font);

    /* --------------------------------------------- parallel directory scan */
    if (dir) {
        int seen = 0;
        printf("\nscanning %s on %d threads:\n", dir, ftk_thread_pool_size());
        int64_t files = ftk_scan_dir(dir, 0 /* recursive */, 8 /* max threads */,
                                     on_font, &seen);
        printf("  %lld files visited, %d callbacks\n", (long long)files, seen);
    }

    printf("\ndemo finished\n");
    return 0;
}
