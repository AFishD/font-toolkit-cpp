// C ABI implementation (plugin entry points).
#include "ftk/ftk.h"
#include "ftk/Font.hpp"
#include "ftk/Dump.hpp"
#include "ftk/Subset.hpp"
#include "ftk/Platform.hpp"
#include "ftk/Threads.hpp"

#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

using namespace ftk;

struct ftk_font {
    std::unique_ptr<Font> font;
};

static thread_local std::string g_lastError;

static void setErr(ftk_error* err, const std::string& msg) {
    g_lastError = msg;
    if (err) {
        err->code = 1;
        snprintf(err->message, sizeof err->message, "%s", msg.c_str());
    }
}

const char* ftk_version() { return "1.0.0"; }

int ftk_thread_pool_size() { return (int)ThreadPool::shared().size(); }

void ftk_set_thread_pool_size(int) {
    // The shared pool is created once; changing size requires restart.
}

const char* ftk_last_error() { return g_lastError.c_str(); }

ftk_font* ftk_open_file(const char* path_utf8, uint32_t face_index, ftk_error* err) {
    try {
        auto f = Font::openFile(path_utf8 ? path_utf8 : "", face_index);
        auto* wrap = new ftk_font;
        wrap->font = std::move(f);
        if (err) { err->code = 0; err->message[0] = 0; }
        return wrap;
    } catch (const std::exception& e) {
        setErr(err, e.what());
        return nullptr;
    } catch (...) {
        setErr(err, "unknown error");
        return nullptr;
    }
}

ftk_font* ftk_open_memory(const void* data, size_t size, uint32_t face_index, ftk_error* err) {
    try {
        std::vector<uint8_t> copy((const uint8_t*)data, (const uint8_t*)data + size);
        auto f = Font::open(std::move(copy), face_index);
        auto* wrap = new ftk_font;
        wrap->font = std::move(f);
        if (err) { err->code = 0; err->message[0] = 0; }
        return wrap;
    } catch (const std::exception& e) {
        setErr(err, e.what());
        return nullptr;
    } catch (...) {
        setErr(err, "unknown error");
        return nullptr;
    }
}

int32_t ftk_num_faces(const char* path_utf8) {
    if (!path_utf8) return -1;
    return Font::numFacesInFile(path_utf8);
}

void ftk_font_close(ftk_font* f) {
    delete f;
}

static void copyStr(char* dst, size_t cap, const std::string& s) {
    snprintf(dst, cap, "%s", s.c_str());
}

int ftk_get_font_info(ftk_font* f, ftk_font_info* out) {
    if (!f || !out) return -1;
    try {
        FontInfo fi = f->font->info(true); // plugin callers expect verified checksums
        memset(out, 0, sizeof *out);
        copyStr(out->family, sizeof out->family, fi.family);
        copyStr(out->subfamily, sizeof out->subfamily, fi.subfamily);
        copyStr(out->full_name, sizeof out->full_name, fi.fullName);
        copyStr(out->postscript_name, sizeof out->postscript_name, fi.postScriptName);
        copyStr(out->version, sizeof out->version, fi.version);
        out->units_per_em = fi.unitsPerEm;
        out->num_glyphs = fi.numGlyphs;
        out->num_faces = fi.numFaces;
        out->face_index = fi.faceIndex;
        out->outline_format = (int32_t)fi.outline;
        out->container = (int32_t)fi.container;
        out->ascender = fi.ascender;
        out->descender = fi.descender;
        out->line_gap = fi.lineGap;
        out->weight_class = fi.weightClass;
        out->width_class = fi.widthClass;
        out->is_bold = fi.isBold;
        out->is_italic = fi.isItalic;
        out->is_fixed_pitch = fi.fixedPitch;
        out->is_variable = fi.isVariable;
        out->has_vertical = fi.hasVertical;
        out->num_axes = (int32_t)fi.numAxes;
        out->num_instances = (int32_t)fi.numInstances;
        out->num_cmap_mappings = fi.cmapMappings;
        out->cmap_min = fi.cmapMin;
        out->cmap_max = fi.cmapMax;
        out->cmap_best_format = fi.cmapBestFormat;
        out->file_size = fi.fileSize;
        out->checksums_ok = fi.checksumsOk;
        return 0;
    } catch (const std::exception& e) {
        g_lastError = e.what();
        return -1;
    }
}

uint16_t ftk_glyph_for_unicode(ftk_font* f, uint32_t cp) {
    if (!f) return 0;
    return f->font->glyphForUnicode(cp);
}

uint32_t ftk_unicode_for_glyph(ftk_font* f, uint16_t gid) {
    if (!f) return 0;
    return f->font->unicodeForGlyph(gid);
}

int32_t ftk_glyphs_for_unicodes(ftk_font* f, const uint32_t* cps, int32_t n, uint16_t* out_gids) {
    if (!f) return -1;
    std::vector<uint32_t> v(cps, cps + n);
    auto gids = f->font->glyphIdsForUnicodes(v);
    if (out_gids)
        for (int32_t i = 0; i < n; i++) out_gids[i] = gids[i];
    return n;
}

int32_t ftk_glyph_name(ftk_font* f, uint16_t gid, char* buf, int32_t cap) {
    if (!f) return -1;
    std::string n = f->font->glyphName(gid);
    if (buf && cap > 0) snprintf(buf, cap, "%s", n.c_str());
    return (int32_t)n.size() + 1;
}

float* ftk_glyph_outline(ftk_font* f, uint16_t gid,
                         const double* user_coords, int32_t n_coords,
                         size_t* out_n_segs) {
    if (!f || !out_n_segs) return nullptr;
    try {
        std::vector<double> coords;
        if (user_coords && n_coords > 0)
            coords.assign(user_coords, user_coords + n_coords);
        Path p = f->font->glyphPath(gid, coords.empty() ? nullptr : &coords);
        size_t n = p.cmds.size();
        if (n == 0) {
            *out_n_segs = 0;
            return (float*)malloc(0);
        }
        // 8 floats per segment: [op, 0, x0,y0,x1,y1,x2,y2]
        float* buf = (float*)malloc(n * 8 * sizeof(float));
        if (!buf) return nullptr;
        for (size_t i = 0; i < n; i++) {
            const PathCmd& c = p.cmds[i];
            float* q = buf + i * 8;
            q[0] = (float)(int)c.op;
            q[1] = 0;
            q[2] = c.x0; q[3] = c.y0;
            q[4] = c.x1; q[5] = c.y1;
            q[6] = c.x2; q[7] = c.y2;
        }
        *out_n_segs = n;
        return buf;
    } catch (const std::exception& e) {
        g_lastError = e.what();
        return nullptr;
    }
}

double ftk_glyph_advance(ftk_font* f, uint16_t gid, const double* user_coords, int32_t n_coords) {
    if (!f) return 0;
    std::vector<double> coords;
    if (user_coords && n_coords > 0)
        coords.assign(user_coords, user_coords + n_coords);
    return f->font->advance(gid, coords.empty() ? nullptr : &coords);
}

double ftk_kerning(ftk_font* f, uint16_t left, uint16_t right) {
    if (!f) return 0;
    return f->font->kerning(left, right);
}

void ftk_glyph_bounds(ftk_font* f, uint16_t gid, const double* user_coords, int32_t n_coords,
                      float* out_bounds) {
    if (!f || !out_bounds) return;
    std::vector<double> coords;
    if (user_coords && n_coords > 0)
        coords.assign(user_coords, user_coords + n_coords);
    FBounds b = f->font->glyphBoundsF(gid, coords.empty() ? nullptr : &coords);
    out_bounds[0] = b.xMin;
    out_bounds[1] = b.yMin;
    out_bounds[2] = b.xMax;
    out_bounds[3] = b.yMax;
}

int32_t ftk_var_axes(ftk_font* f, ftk_axis* out, int32_t cap) {
    if (!f) return -1;
    const Fvar* fv = f->font->fvar();
    if (!fv) return 0;
    if (out) {
        for (size_t i = 0; i < fv->axes.size() && (int32_t)i < cap; i++) {
            const FvarAxis& a = fv->axes[i];
            snprintf(out[i].tag, sizeof out[i].tag, "%s", tagToStr(a.tag).c_str());
            snprintf(out[i].name, sizeof out[i].name, "%s", a.name.c_str());
            out[i].min_value = a.minValue;
            out[i].default_value = a.defaultValue;
            out[i].max_value = a.maxValue;
            out[i].name_id = a.nameID;
            out[i].flags = a.flags;
        }
    }
    return (int32_t)fv->axes.size();
}

int32_t ftk_var_instances(ftk_font* f, ftk_instance* out, int32_t cap) {
    if (!f) return -1;
    const Fvar* fv = f->font->fvar();
    if (!fv) return 0;
    if (out) {
        for (size_t i = 0; i < fv->instances.size() && (int32_t)i < cap; i++) {
            const FvarInstance& in = fv->instances[i];
            snprintf(out[i].name, sizeof out[i].name, "%s", in.name.c_str());
            out[i].name_id = in.nameID;
            out[i].flags = in.flags;
            out[i].num_coords = (int32_t)in.coords.size();
            for (size_t k = 0; k < in.coords.size() && k < 16; k++)
                out[i].coords[k] = in.coords[k];
        }
    }
    return (int32_t)fv->instances.size();
}

int32_t ftk_table_tags(ftk_font* f, uint32_t* out_tags, int32_t cap) {
    if (!f) return -1;
    auto tags = f->font->tableTags();
    if (out_tags)
        for (size_t i = 0; i < tags.size() && (int32_t)i < cap; i++)
            out_tags[i] = tags[i];
    return (int32_t)tags.size();
}

uint8_t* ftk_table_data(ftk_font* f, uint32_t tag, size_t* out_size) {
    if (!f || !out_size) return nullptr;
    auto data = f->font->rawTableCopy(tag);
    *out_size = data.size();
    if (data.empty()) return nullptr;
    uint8_t* buf = (uint8_t*)malloc(data.size());
    memcpy(buf, data.data(), data.size());
    return buf;
}

int ftk_verify_checksums(ftk_font* f) {
    if (!f) return -1;
    // 0 = all checksums verify, 1 = mismatch (matches the other int-returning
    // entry points, which also use 0 for success)
    return f->font->verifyChecksums(nullptr) ? 0 : 1;
}

char* ftk_dump_ttx(ftk_font* f, int skip_glyphs, const uint32_t* only_tags, int32_t n_tags) {
    if (!f) return nullptr;
    try {
        DumpOptions opt;
        opt.skipGlyphs = skip_glyphs != 0;
        if (only_tags && n_tags > 0)
            opt.tables.assign(only_tags, only_tags + n_tags);
        std::string s = dumpTtx(*f->font, opt);
        char* buf = (char*)malloc(s.size() + 1);
        memcpy(buf, s.data(), s.size() + 1);
        return buf;
    } catch (const std::exception& e) {
        g_lastError = e.what();
        return nullptr;
    }
}

char* ftk_dump_json(ftk_font* f) {
    if (!f) return nullptr;
    try {
        std::string s = dumpInfoJson(*f->font);
        char* buf = (char*)malloc(s.size() + 1);
        memcpy(buf, s.data(), s.size() + 1);
        return buf;
    } catch (const std::exception& e) {
        g_lastError = e.what();
        return nullptr;
    }
}

uint8_t* ftk_subset(ftk_font* f, const ftk_subset_plan* plan, size_t* out_size) {
    if (!f || !plan || !out_size) return nullptr;
    try {
        SubsetPlan p;
        p.unicodes.assign(plan->unicodes, plan->unicodes + plan->num_unicodes);
        p.gids.assign(plan->gids, plan->gids + plan->num_gids);
        for (int32_t i = 0; i < plan->num_names; i++)
            p.names.push_back(plan->names[i]);
        p.keepEverything = plan->keep_everything != 0;
        p.keepLayout = plan->keep_layout != 0;
        p.keepNameTable = plan->keep_name_table != 0;
        p.keepOs2 = plan->keep_os2 != 0;
        p.keepVertical = plan->keep_vertical != 0;
        auto bytes = subsetFont(*f->font, p);
        *out_size = bytes.size();
        uint8_t* buf = (uint8_t*)malloc(bytes.size() ? bytes.size() : 1);
        if (bytes.size()) memcpy(buf, bytes.data(), bytes.size());
        return buf;
    } catch (const std::exception& e) {
        g_lastError = e.what();
        return nullptr;
    }
}

uint8_t* ftk_sfnt_to_woff(const uint8_t* sfnt_data, size_t size, size_t* out_size) {
    if (!sfnt_data || !out_size) return nullptr;
    try {
        auto bytes = sfntToWoff(std::vector<uint8_t>(sfnt_data, sfnt_data + size));
        *out_size = bytes.size();
        uint8_t* buf = (uint8_t*)malloc(bytes.size());
        memcpy(buf, bytes.data(), bytes.size());
        return buf;
    } catch (const std::exception& e) {
        g_lastError = e.what();
        return nullptr;
    }
}

uint32_t* ftk_cmap_mappings(ftk_font* f, size_t* out_n_pairs) {
    if (!f || !out_n_pairs) return nullptr;
    const Cmap* c = f->font->cmap();
    if (!c) {
        *out_n_pairs = 0;
        return (uint32_t*)malloc(0);
    }
    auto entries = c->bestEntries();
    uint32_t* buf = (uint32_t*)malloc(entries.size() * 2 * sizeof(uint32_t));
    for (size_t i = 0; i < entries.size(); i++) {
        buf[2 * i] = entries[i].first;
        buf[2 * i + 1] = entries[i].second;
    }
    *out_n_pairs = entries.size();
    return buf;
}

int64_t ftk_scan_dir(const char* dir_utf8, int recursive, int max_threads,
                     ftk_scan_callback cb, void* user) {
    if (!dir_utf8) return -1;
    auto files = listFiles(dir_utf8, recursive != 0);
    std::atomic<int64_t> nScanned{0};
    std::atomic<int64_t> next{0};

    size_t workers = ThreadPool::shared().size();
    if (max_threads > 0 && (size_t)max_threads < workers) workers = (size_t)max_threads;
    if (workers == 0) workers = 1;

    // chunked: one file per task (tasks are cheap)
    std::vector<std::function<void()>> jobs;
    jobs.reserve(files.size());
    for (size_t i = 0; i < files.size(); i++) {
        jobs.emplace_back([&, i] {
            const std::string& path = files[i];
            std::string json;
            int ok = 0;
            try {
                auto font = Font::openFile(path, 0);
                json = dumpInfoJson(*font);
                ok = 1;
            } catch (const std::exception&) {
                ok = 0;
            } catch (...) {
                ok = 0;
            }
            if (cb) cb(path.c_str(), ok ? json.c_str() : "", ok, user);
            nScanned.fetch_add(1);
        });
    }
    ThreadPool::shared().parallelFor(jobs.size(), 1, [&](size_t lo, size_t hi) {
        for (size_t k = lo; k < hi; k++) jobs[k]();
    });
    return nScanned.load();
}

void ftk_free(void* p) { free(p); }
