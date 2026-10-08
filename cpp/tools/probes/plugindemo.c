/* External plugin demo: uses only the stable C ABI (ftk.h) and links against
 * libftk.dll, exactly as a host application embedding ftk would.
 *
 * Build (MinGW), from the build/ directory:
 *   gcc -std=c11 -I../cpp/include -o plugin_demo.exe \
 *       ../cpp/tools/probes/plugindemo.c -L. -lftk
 * Run from build/ (libftk.dll must be next to the executable).
 */
/* MinGW: use the ISO-C compliant printf so %llu/%zu format checking works. */
#if defined(_WIN32) && !defined(__USE_MINGW_ANSI_STDIO)
#define __USE_MINGW_ANSI_STDIO 1
#endif

#include "ftk/ftk.h"

#include <stdio.h>
#include <string.h>

static void on_font(const char* path, const char* json_info, int ok, void* user) {
    int* count = (int*)user;
    (*count)++;
    if (*count <= 3)
        printf("  scan callback: %s (ok=%d, %lu bytes of json)\n", path, ok,
               (unsigned long)(json_info ? strlen(json_info) : 0));
}

int main(int argc, char** argv) {
    const char* path = argc > 1 ? argv[1] : "../Fonts/SimHei.ttf";
    const char* dir = argc > 2 ? argv[2] : NULL;

    printf("ftk plugin ABI %s (pool %d threads)\n", ftk_version(), ftk_thread_pool_size());

    ftk_error err;
    memset(&err, 0, sizeof err);
    ftk_font* f = ftk_open_file(path, 0, &err);
    if (!f) {
        fprintf(stderr, "open failed: %s\n", err.message);
        return 1;
    }

    ftk_font_info info;
    if (ftk_get_font_info(f, &info) != 0) {
        fprintf(stderr, "info failed: %s\n", err.message);
        ftk_font_close(f);
        return 1;
    }
    printf("  family      : %s\n", info.family);
    printf("  outline fmt : %d (0=none 1=TrueType 2=CFF 3=CFF2)\n", info.outline_format);
    printf("  container   : %d (0=unknown 1=sfnt 2=ttc 3=woff 4=woff2)\n", info.container);
    printf("  glyphs      : %u   units/em: %u   cmap: %llu mappings\n", info.num_glyphs,
           info.units_per_em, (unsigned long long)info.num_cmap_mappings);
    printf("  variable    : %d axes, %d instances\n", info.num_axes, info.num_instances);
    printf("  checksums   : %s\n", info.checksums_ok ? "ok" : "mismatch");

    /* codepoint -> gid -> outline (8 floats per segment: op + 6 coords) */
    uint16_t gid = ftk_glyph_for_unicode(f, 0x4E2D);
    size_t nseg = 0;
    float* segs = ftk_glyph_outline(f, gid, NULL, 0, &nseg);
    printf("  gid %u (U+4E2D): %zu segments, advance %.1f\n", gid, nseg,
           ftk_glyph_advance(f, gid, NULL, 0));
    if (segs && nseg > 0) {
        const float* s = segs;             /* first segment */
        printf("  first segment: op=%d start=(%g,%g) ctrl=(%g,%g) end=(%g,%g)\n",
               (int)s[0], s[2], s[3], s[4], s[5], s[6], s[7]);
        ftk_free(segs);
    } else if (segs) {
        ftk_free(segs);
    }

    /* glyph name */
    char name[128];
    int need = ftk_glyph_name(f, gid, name, (int)sizeof name);
    printf("  glyph name  : %s (needs %d bytes)\n", need > 0 ? name : "?", need);

    /* variable instance: one coordinate per fvar axis, in fvar order */
    if (info.num_axes > 0) {
        ftk_axis axes[8];
        int32_t naxes = ftk_var_axes(f, axes, 8);
        const double coords[1] = {700.0};
        printf("  first axis  : %s (%g..%g..%g)\n", naxes > 0 ? axes[0].tag : "?",
               naxes > 0 ? axes[0].min_value : 0, naxes > 0 ? axes[0].default_value : 0,
               naxes > 0 ? axes[0].max_value : 0);
        printf("  advance @700: %.1f\n", ftk_glyph_advance(f, gid, coords, 1));
    }

    /* TTX-style dump of selected tables (NULL tag list = all tables) */
    size_t xml_len = 0;
    char* xml = ftk_dump_ttx(f, 1 /* skip glyphs */, NULL, 0);
    if (xml) {
        printf("  ttx dump    : %zu bytes\n", xml_len = strlen(xml));
        ftk_free(xml);
    }

    /* JSON summary */
    char* js = ftk_dump_json(f);
    if (js) {
        printf("  json        : %.60s...\n", js);
        ftk_free(js);
    }

    /* subsetting this font, then re-opening the result from memory */
    {
        uint32_t cps[] = {0x41, 0x42, 0x43, 0x4E2D};
        ftk_subset_plan plan;
        memset(&plan, 0, sizeof plan);
        plan.unicodes = cps;
        plan.num_unicodes = 4;
        plan.keep_name_table = 1;
        plan.keep_os2 = 1;
        size_t out_len = 0;
        uint8_t* out = ftk_subset(f, &plan, &out_len);
        if (out) {
            ftk_font* sub = ftk_open_memory(out, out_len, 0, &err);
            if (sub) {
                ftk_font_info si;
                ftk_get_font_info(sub, &si);
                printf("  subset      : %zu bytes -> %u glyphs (host re-read OK)\n",
                       out_len, si.num_glyphs);
                ftk_font_close(sub);
            } else {
                printf("  subset      : host re-read failed: %s\n", err.message);
            }
            ftk_free(out);
        } else {
            printf("  subset      : failed: %s\n", err.message);
        }
    }

    ftk_font_close(f);

    /* parallel directory scan through the plugin entry point */
    if (dir) {
        int count = 0;
        int64_t n = ftk_scan_dir(dir, 0, 8, on_font, &count);
        printf("  scan        : %lld files, %d callbacks\n", (long long)n, count);
    }

    printf("plugin ABI demo OK\n");
    return 0;
}
