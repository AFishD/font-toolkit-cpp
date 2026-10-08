// ftk CLI — font toolkit command line.
#include "ftk/ftk.h"
#include "ftk/Font.hpp"
#include "ftk/Dump.hpp"
#include "ftk/Subset.hpp"
#include "ftk/Platform.hpp"
#include "ftk/Threads.hpp"
#include "ftk/Layout.hpp"
#include "ftk/Color.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

using namespace ftk;

static void usage() {
    fprintf(stderr,
        "ftk — Font Toolkit (C++ rewrite of fontTools core)\n"
        "\n"
        "Usage: ftk <command> [options]\n"
        "\n"
        "Commands:\n"
        "  info <font>...            font summary (family, metrics, glyphs...)\n"
        "  dump <font> [-o out.xml] [--no-glyphs] [--table TAG]...   TTX-style XML dump\n"
        "  cmap <font> [cp...]       cmap queries; no cps = stats\n"
        "  outline <font> (--gid N | --char U+XXXX | --name NAME) [--svg] [--coords v,...]\n"
        "  kern <font> <leftName> <rightName>\n"
        "  var <font>                variable axes + instances\n"
        "  layout <font>             GSUB/GPOS scripts/features/lookups\n"
        "  metrics <font> (--gid N | --char U+XXXX)\n"
        "  glyphs <font> [--limit N] glyph list (id, name, advance, bounds)\n"
        "  validate <font>...        checksum verification\n"
        "  subset <font> -o out.ttf (--unicodes RANGE | --text TXT | --gids N,... | --all)\n"
        "                           [--keep-layout] [--name-ID] [--vertical]\n"
        "  convert <in> -o out.[ttf|otf|woff]   container conversion\n"
        "  scan <dir>... [--recursive] [--json] [-j N]  parallel batch scan\n"
        "  faces <font>              list faces in a collection\n"
        "  version\n");
}

static std::vector<std::string> args;

static bool hasFlag(const char* name, size_t* idx = nullptr) {
    for (size_t i = 0; i < args.size(); i++)
        if (args[i] == name) {
            if (idx) *idx = i;
            return true;
        }
    return false;
}

static std::string flagValue(const char* name, const std::string& def = "") {
    for (size_t i = 0; i + 1 < args.size(); i++)
        if (args[i] == name) return args[i + 1];
    return def;
}

// parse "0x41", "65", "U+0041", "'A'"
static bool parseCodepoint(const std::string& s, uint32_t& out) {
    if (s.empty()) return false;
    if (s.size() >= 2 && (s.compare(0, 2, "0x") == 0 || s.compare(0, 2, "0X") == 0))
        return sscanf(s.c_str() + 2, "%x", &out) == 1;
    if (s.size() > 2 && (s.compare(0, 2, "U+") == 0 || s.compare(0, 2, "u+") == 0))
        return sscanf(s.c_str() + 2, "%x", &out) == 1;
    if (s[0] == '\'' && s.size() >= 3) {
        size_t i = 1;
        out = utf8DecodeOne(s, i);
        return true;
    }
    if (s.find_first_not_of("0123456789") == std::string::npos) {
        out = (uint32_t)strtoul(s.c_str(), nullptr, 10);
        return true;
    }
    // raw utf-8 char
    size_t i = 0;
    out = utf8DecodeOne(s, i);
    return i == s.size();
}

// parse unicode ranges "4E00-9FFF,3000,41"
static bool parseUnicodeSet(const std::string& s, std::vector<uint32_t>& out) {
    size_t pos = 0;
    while (pos < s.size()) {
        size_t comma = s.find(',', pos);
        if (comma == std::string::npos) comma = s.size();
        std::string part = s.substr(pos, comma - pos);
        size_t dash = part.find('-');
        uint32_t a, b;
        if (dash != std::string::npos) {
            if (!parseCodepoint(part.substr(0, dash), a)) return false;
            if (!parseCodepoint(part.substr(dash + 1), b)) return false;
            if (a > b) return false;
            uint64_t n = (uint64_t)b - a + 1;
            if (n > 0x110000) return false;
            for (uint64_t i = 0; i < n; i++) out.push_back((uint32_t)(a + i));
        } else {
            if (!parseCodepoint(part, a)) return false;
            out.push_back(a);
        }
        pos = comma + 1;
    }
    return true;
}

static int cmdInfo() {
    int n = 0;
    for (auto& path : args) {
        if (path[0] == '-') continue;
        try {
            auto font = Font::openFile(path);
            printf("%s", dumpInfoText(*font).c_str());
        } catch (const Error& e) {
            fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
            n++;
        }
    }
    return n ? 1 : 0;
}

static int cmdDump() {
    std::string out = flagValue("-o");
    bool noGlyphs = hasFlag("--no-glyphs");
    std::vector<uint32_t> tables;
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] == "--table")
            tables.push_back(strToTag(args[i + 1]));
    }
    for (auto& path : args) {
        if (path[0] == '-' || (!out.empty() && args.size() > 2)) continue;
        try {
            auto font = Font::openFile(path);
            DumpOptions opt;
            opt.skipGlyphs = noGlyphs;
            opt.tables = tables;
            std::string xml = dumpTtx(*font, opt);
            if (!out.empty()) {
                if (!writeFile(out, xml.data(), xml.size())) {
                    fprintf(stderr, "cannot write %s\n", out.c_str());
                    return 1;
                }
                fprintf(stderr, "wrote %s (%zu bytes)\n", out.c_str(), xml.size());
            } else {
                fwrite(xml.data(), 1, xml.size(), stdout);
            }
            return 0;
        } catch (const Error& e) {
            fprintf(stderr, "%s: %s\n", path.c_str(), e.what());
            return 1;
        }
    }
    return 1;
}

static int cmdCmap() {
    std::string path = args[0];
    auto font = Font::openFile(path);
    if (args.size() == 1) {
        const Cmap* c = font->cmap();
        if (!c) {
            fprintf(stderr, "no cmap\n");
            return 1;
        }
        printf("subtables: %zu\n", c->subtables.size());
        for (auto& st : c->subtables)
            printf("  platform %u encoding %u format %u: %zu mappings\n",
                   st.platformID, st.encodingID, st.format, st.map.size());
        const CmapSubtable* best = c->best();
        if (best)
            printf("best: platform %u encoding %u format %u (%zu mappings)\n",
                   best->platformID, best->encodingID, best->format, best->map.size());
        return 0;
    }
    for (size_t i = 1; i < args.size(); i++) {
        uint32_t cp;
        if (!parseCodepoint(args[i], cp)) {
            fprintf(stderr, "bad codepoint: %s\n", args[i].c_str());
            return 1;
        }
        uint16_t gid = font->glyphForUnicode(cp);
        printf("U+%04X -> gid %u (%s)\n", cp, gid, font->glyphName(gid).c_str());
    }
    return 0;
}

static int cmdOutline() {
    std::string path = args[0];
    auto font = Font::openFile(path);
    uint16_t gid = 0;
    std::string g = flagValue("--gid");
    std::string ch = flagValue("--char");
    std::string nm = flagValue("--name");
    std::vector<double> coords;
    std::string cs = flagValue("--coords");
    if (!cs.empty()) {
        size_t pos = 0;
        while (pos < cs.size()) {
            size_t comma = cs.find(',', pos);
            if (comma == std::string::npos) comma = cs.size();
            coords.push_back(atof(cs.substr(pos, comma - pos).c_str()));
            pos = comma + 1;
        }
    }
    if (!g.empty()) gid = (uint16_t)strtoul(g.c_str(), nullptr, 0);
    else if (!ch.empty()) {
        uint32_t cp;
        parseCodepoint(ch, cp);
        gid = font->glyphForUnicode(cp);
    } else if (!nm.empty()) gid = font->glyphForName(nm);

    GlyphData gd = font->glyph(gid, coords.empty() ? nullptr : &coords);
    printf("glyph %u (%s): %zu segments, %zu points, advance %g, bounds [%g %g %g %g]\n",
           gid, gd.name.c_str(), gd.path.cmds.size(), gd.path.totalPoints(),
           gd.advance, gd.path.bounds().xMin, gd.path.bounds().yMin,
           gd.path.bounds().xMax, gd.path.bounds().yMax);
    if (hasFlag("--svg")) {
        printf("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 -1200 1200 1200\">\n");
        printf("<path d=\"%s\"/></svg>\n", gd.path.toSvg(1, 0, 0, true).c_str());
    } else if (hasFlag("--segments")) {
        for (auto& c : gd.path.cmds) {
            switch (c.op) {
            case PathOp::MoveTo: printf("M %g %g\n", c.x0, c.y0); break;
            case PathOp::LineTo: printf("L %g %g\n", c.x0, c.y0); break;
            case PathOp::QuadTo: printf("Q %g %g %g %g\n", c.x0, c.y0, c.x1, c.y1); break;
            case PathOp::CubicTo: printf("C %g %g %g %g %g %g\n", c.x0, c.y0, c.x1, c.y1, c.x2, c.y2); break;
            case PathOp::Close: printf("Z\n"); break;
            }
        }
    }
    return 0;
}

static int cmdKern() {
    if (args.size() < 3) {
        fprintf(stderr, "usage: ftk kern <font> <leftGlyph> <rightGlyph>\n");
        return 1;
    }
    auto font = Font::openFile(args[0]);
    uint16_t l = font->glyphForName(args[1]);
    uint16_t r = font->glyphForName(args[2]);
    printf("kern(%s[%u], %s[%u]) = %g\n", args[1].c_str(), l, args[2].c_str(), r,
           font->kerning(l, r));
    return 0;
}

static int cmdVar() {
    auto font = Font::openFile(args[0]);
    const Fvar* fv = font->fvar();
    if (!fv || fv->axes.empty()) {
        printf("not a variable font\n");
        return 0;
    }
    printf("axes (%zu):\n", fv->axes.size());
    const NameTable* nm = font->nameTable();
    for (auto& ax : fv->axes) {
        std::string name = ax.name;
        if (name.empty() && nm) name = nm->get(ax.nameID);
        printf("  %-6s %-20s min %g default %g max %g (nameID %u)\n",
               tagToStr(ax.tag).c_str(), name.c_str(),
               ax.minValue, ax.defaultValue, ax.maxValue, ax.nameID);
    }
    printf("instances (%zu):\n", fv->instances.size());
    for (auto& in : fv->instances) {
        std::string name = nm ? nm->get(in.nameID) : std::string();
        std::string loc;
        for (size_t a = 0; a < fv->axes.size() && a < in.coords.size(); a++)
            loc += " " + tagToStr(fv->axes[a].tag) + "=" + std::to_string(in.coords[a]);
        printf("  %-30s%s\n", name.c_str(), loc.c_str());
    }
    if (const Stat* st = font->stat()) {
        printf("STAT axes (%zu), values (%zu)\n", st->axes.size(), st->values.size());
        for (auto& v : st->values)
            printf("  %s = %g (%s)\n", tagToStr(v.tag).c_str(), v.value, v.name.c_str());
    }
    return 0;
}

static int cmdLayout() {
    auto font = Font::openFile(args[0]);
    const LayoutTables* lt = font->layout();
    auto dumpTable = [&](const char* name, const LayoutTableInfo& t) {
        printf("%s (%zu scripts, %zu features, %zu lookups)\n", name, t.scripts.size(),
               t.features.size(), t.lookups.size());
        for (auto& sc : t.scripts)
            printf("  script %s: %zu langsys\n", tagToStr(sc.tag).c_str(), sc.langSys.size());
        for (auto& ft : t.features)
            printf("  feature %s: %zu lookups\n", tagToStr(ft.tag).c_str(), ft.lookupIndices.size());
        for (size_t i = 0; i < t.lookups.size() && i < 40; i++)
            printf("  lookup %zu: type %u (%s) flag 0x%04X\n", i, t.lookups[i].type,
                   t.lookupTypeName(t.lookups[i].type).c_str(), t.lookups[i].flag);
    };
    if (lt->hasGsub) dumpTable("GSUB", lt->gsub);
    if (lt->hasGpos) dumpTable("GPOS", lt->gpos);
    if (lt->hasGdef) printf("GDEF: glyphClassDef %s varStore %s\n",
                            lt->gdef.hasGlyphClassDef ? "yes" : "no",
                            lt->gdef.hasVarStore ? "yes" : "no");
    if (!lt->hasGsub && !lt->hasGpos && !lt->hasGdef) printf("no layout tables\n");
    return 0;
}

static int cmdMetrics() {
    auto font = Font::openFile(args[0]);
    std::string g = flagValue("--gid");
    std::string ch = flagValue("--char");
    uint16_t gid = 0;
    if (!g.empty()) gid = (uint16_t)strtoul(g.c_str(), nullptr, 0);
    else if (!ch.empty()) {
        uint32_t cp;
        parseCodepoint(ch, cp);
        gid = font->glyphForUnicode(cp);
    }
    GlyphData gd = font->glyph(gid);
    printf("glyph %u (%s)\n", gid, gd.name.c_str());
    printf("advance:  %g\n", gd.advance);
    printf("lsb:      %g\n", gd.lsb);
    if (gd.hasWidth) printf("CFF width: %g\n", gd.width);
    printf("bounds:   [%d %d %d %d]\n", gd.bounds.xMin, gd.bounds.yMin, gd.bounds.xMax, gd.bounds.yMax);
    return 0;
}

static int cmdGlyphs() {
    auto font = Font::openFile(args[0]);
    int limit = 20;
    std::string lim = flagValue("--limit");
    if (!lim.empty()) limit = atoi(lim.c_str());
    uint16_t n = font->numGlyphs();
    printf("%u glyphs:\n", n);
    for (uint16_t i = 0; i < n && (int)i < limit; i++) {
        GlyphData gd = font->glyph(i);
        printf("  %5u  %-30s adv %5g  [%d %d %d %d]\n", i, gd.name.c_str(), gd.advance,
               gd.bounds.xMin, gd.bounds.yMin, gd.bounds.xMax, gd.bounds.yMax);
    }
    return 0;
}

static int cmdValidate() {
    int bad = 0;
    for (auto& path : args) {
        if (path[0] == '-') continue;
        try {
            auto font = Font::openFile(path);
            std::string err;
            bool ok = font->verifyChecksums(&err);
            printf("%s: %s%s\n", path.c_str(), ok ? "OK" : "FAIL",
                   ok ? "" : (" — " + err).c_str());
            if (!ok) bad++;
        } catch (const Error& e) {
            printf("%s: ERROR %s\n", path.c_str(), e.what());
            bad++;
        }
    }
    return bad ? 1 : 0;
}

static int cmdSubset() {
    std::string path = args[0];
    std::string out = flagValue("-o");
    if (out.empty()) {
        fprintf(stderr, "subset requires -o output\n");
        return 1;
    }
    auto font = Font::openFile(path);
    SubsetPlan plan;
    std::string uni = flagValue("--unicodes");
    std::string text = flagValue("--text");
    std::string gids = flagValue("--gids");
    if (!uni.empty() && !parseUnicodeSet(uni, plan.unicodes)) {
        fprintf(stderr, "bad --unicodes\n");
        return 1;
    }
    if (!text.empty()) {
        size_t i = 0;
        while (i < text.size())
            plan.unicodes.push_back(utf8DecodeOne(text, i));
    }
    if (!gids.empty()) {
        size_t pos = 0;
        while (pos < gids.size()) {
            size_t comma = gids.find(',', pos);
            if (comma == std::string::npos) comma = gids.size();
            plan.gids.push_back((uint16_t)strtoul(gids.substr(pos, comma - pos).c_str(), nullptr, 0));
            pos = comma + 1;
        }
    }
    plan.keepEverything = hasFlag("--all");
    plan.keepLayout = hasFlag("--keep-layout");
    plan.keepVertical = hasFlag("--vertical");
    try {
        auto t0 = std::chrono::steady_clock::now();
        auto bytes = subsetFont(*font, plan);
        auto t1 = std::chrono::steady_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (!writeFile(out, bytes.data(), bytes.size())) {
            fprintf(stderr, "cannot write %s\n", out.c_str());
            return 1;
        }
        printf("wrote %s (%zu bytes, %u glyphs kept of %u)\n", out.c_str(), bytes.size(),
               (unsigned)0, font->numGlyphs());
        // internal timing (excludes process start-up), used by the benchmark
        fprintf(stderr, "subset: %zu bytes, %zu codepoints in %.2f ms\n", bytes.size(),
                plan.unicodes.size() + plan.gids.size(), ms);
        return 0;
    } catch (const Error& e) {
        fprintf(stderr, "subset failed: %s\n", e.what());
        return 1;
    }
}

static int cmdConvert() {
    std::string path = args[0];
    std::string out = flagValue("-o");
    if (out.empty()) {
        fprintf(stderr, "convert requires -o output (.ttf/.otf/.woff)\n");
        return 1;
    }
    auto font = Font::openFile(path);
    std::string lower = toLowerAscii(out);
    if (lower.size() >= 5 && lower.compare(lower.size() - 5, 5, ".woff") == 0) {
        auto bytes = sfntToWoff(font->sfnt().resolvedBytes());
        if (!writeFile(out, bytes.data(), bytes.size())) return 1;
        printf("wrote %s (%zu bytes)\n", out.c_str(), bytes.size());
        return 0;
    }
    // plain sfnt out
    auto& bytes = font->sfnt().resolvedBytes();
    if (!writeFile(out, bytes.data(), bytes.size())) return 1;
    printf("wrote %s (%zu bytes)\n", out.c_str(), bytes.size());
    return 0;
}

static int cmdScan() {
    bool recursive = hasFlag("--recursive") || hasFlag("-r");
    bool json = hasFlag("--json");
    bool outlines = hasFlag("--outlines"); // extract every glyph outline (CPU bound)
    std::string jobs = flagValue("-j");
    int maxThreads = jobs.empty() ? 0 : atoi(jobs.c_str());

    std::vector<std::string> files;
    for (auto& a : args) {
        if (a[0] == '-') continue;
        // A plain file argument is used as-is; directories are walked.
        std::vector<std::string> found = fileExists(a) ? std::vector<std::string>{a}
                                                       : listFiles(a, recursive);
        for (auto& f : found) {
            std::string lower = toLowerAscii(f);
            if (lower.size() >= 4 &&
                (lower.compare(lower.size() - 4, 4, ".ttf") == 0 ||
                 lower.compare(lower.size() - 4, 4, ".otf") == 0 ||
                 lower.compare(lower.size() - 4, 4, ".ttc") == 0 ||
                 lower.compare(lower.size() - 5, 5, ".woff") == 0 ||
                 lower.compare(lower.size() - 5, 5, ".woff2") == 0))
                files.push_back(f);
        }
    }
    unsigned nthreads = maxThreads > 0 ? (unsigned)maxThreads : ThreadPool::shared().size();
    fprintf(stderr, "scanning %zu files with %u threads...\n", files.size(), nthreads);

    auto t0 = std::chrono::steady_clock::now();
    std::atomic<size_t> okCount{0}, failCount{0};
    std::atomic<size_t> totalGlyphs{0}, totalMappings{0};
    std::atomic<size_t> totalSegments{0}, totalPoints{0};

    auto work = [&](size_t lo, size_t hi) {
        for (size_t i = lo; i < hi; i++) {
            try {
                auto font = Font::openFile(files[i]);
                FontInfo fi = font->info();
                okCount.fetch_add(1);
                totalGlyphs.fetch_add(fi.numGlyphs);
                totalMappings.fetch_add(fi.cmapMappings);
                if (outlines) {
                    size_t segs = 0, pts = 0;
                    uint16_t n = font->numGlyphs();
                    Path scratch; // reused per font: no per-glyph allocation
                    for (uint16_t g = 0; g < n; g++) {
                        font->glyphPathInto(g, scratch);
                        segs += scratch.cmds.size();
                        pts += scratch.totalPoints();
                    }
                    totalSegments.fetch_add(segs);
                    totalPoints.fetch_add(pts);
                }
                if (json) {
                    printf("{\"file\": \"%s\", \"ok\": true, \"info\": %s}\n",
                           files[i].c_str(), dumpInfoJson(*font).c_str());
                } else if (!outlines) {
                    fprintf(stderr, "OK   %s\n", baseName(files[i]).c_str());
                }
            } catch (const Error& e) {
                failCount.fetch_add(1);
                if (json)
                    printf("{\"file\": \"%s\", \"ok\": false, \"error\": \"%s\"}\n",
                           files[i].c_str(), e.what());
                else
                    fprintf(stderr, "FAIL %s: %s\n", baseName(files[i]).c_str(), e.what());
            }
        }
    };

    if (maxThreads > 0 && (unsigned)maxThreads != ThreadPool::shared().size()) {
        ThreadPool pool((unsigned)maxThreads);
        pool.parallelFor(files.size(), 1, work);
    } else {
        parallelFor(files.size(), 1, work);
    }
    auto t1 = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    fprintf(stderr, "done: %zu ok, %zu failed, %zu glyphs, %zu cmap mappings",
            okCount.load(), failCount.load(), totalGlyphs.load(), totalMappings.load());
    if (outlines)
        fprintf(stderr, ", %zu segments, %zu points", totalSegments.load(), totalPoints.load());
    fprintf(stderr, " in %.1f ms (%.1f fonts/s, %.1f mglyph/s)\n", ms,
            ms > 0 ? files.size() * 1000.0 / ms : 0.0,
            ms > 0 ? totalGlyphs.load() / ms / 1000.0 : 0.0);
    return failCount.load() ? 1 : 0;
}

// Machine-readable dump for automated cross-validation against fontTools.
// ftk json <font> [--cmap] [--advances] [--outlines gid,gid,...] [--kerning]
//                [--coords v1,v2] [--vmetrics] [--names] [--kernpairs N]
static int cmdJson() {
    if (args.empty()) {
        fprintf(stderr, "usage: ftk json <font> [--cmap] [--advances] [--outlines gids] ...\n");
        return 1;
    }
    std::string path = args[0];
    auto font = Font::openFile(path);
    FontInfo fi = font->info();

    std::vector<double> coords;
    std::string cs = flagValue("--coords");
    if (!cs.empty()) {
        size_t pos = 0;
        while (pos < cs.size()) {
            size_t comma = cs.find(',', pos);
            if (comma == std::string::npos) comma = cs.size();
            coords.push_back(atof(cs.substr(pos, comma - pos).c_str()));
            pos = comma + 1;
        }
    }
    const std::vector<double>* cp = coords.empty() ? nullptr : &coords;

    std::ostringstream o;
    o.precision(9);
    o << "{";
    o << "\"family\":\"" << xmlEscape(fi.family) << "\"";
    o << ",\"subfamily\":\"" << xmlEscape(fi.subfamily) << "\"";
    o << ",\"postScriptName\":\"" << xmlEscape(fi.postScriptName) << "\"";
    o << ",\"numGlyphs\":" << fi.numGlyphs;
    o << ",\"unitsPerEm\":" << fi.unitsPerEm;
    o << ",\"outline\":" << (int)fi.outline;
    o << ",\"numFaces\":" << fi.numFaces;
    o << ",\"flavor\":\"" << fi.flavor << "\"";

    if (hasFlag("--advances")) {
        o << ",\"advances\":[";
        uint16_t n = font->numGlyphs();
        for (uint16_t g = 0; g < n; g++) {
            if (g) o << ",";
            o << font->advance(g, cp);
        }
        o << "]";
    }
    if (hasFlag("--cmap")) {
        o << ",\"cmap\":{";
        const Cmap* c = font->cmap();
        if (c) {
            auto entries = c->bestEntries();
            for (size_t i = 0; i < entries.size(); i++) {
                if (i) o << ",";
                o << "\"" << entries[i].first << "\":" << entries[i].second;
            }
        }
        o << "}";
    }
    std::string og = flagValue("--outlines");
    if (!og.empty()) {
        o << ",\"outlines\":{";
        size_t pos = 0;
        bool first = true;
        while (pos < og.size()) {
            size_t comma = og.find(',', pos);
            if (comma == std::string::npos) comma = og.size();
            uint16_t gid = (uint16_t)strtoul(og.substr(pos, comma - pos).c_str(), nullptr, 0);
            Path p = font->glyphPath(gid, cp);
            if (!first) o << ",";
            first = false;
            o << "\"" << gid << "\":[";
            for (size_t i = 0; i < p.cmds.size(); i++) {
                const PathCmd& cmd = p.cmds[i];
                if (i) o << ",";
                o << "[" << (int)cmd.op;
                switch (cmd.op) {
                case PathOp::MoveTo:
                case PathOp::LineTo:
                    o << "," << cmd.x0 << "," << cmd.y0;
                    break;
                case PathOp::QuadTo:
                    o << "," << cmd.x0 << "," << cmd.y0 << "," << cmd.x1 << "," << cmd.y1;
                    break;
                case PathOp::CubicTo:
                    o << "," << cmd.x0 << "," << cmd.y0 << "," << cmd.x1 << "," << cmd.y1
                      << "," << cmd.x2 << "," << cmd.y2;
                    break;
                default:
                    break;
                }
                o << "]";
            }
            o << "]";
            pos = comma + 1;
        }
        o << "}";
    }
    if (hasFlag("--names")) {
        o << ",\"names\":[";
        uint16_t n = font->numGlyphs();
        for (uint16_t g = 0; g < n; g++) {
            if (g) o << ",";
            o << "\"" << xmlEscape(font->glyphName(g)) << "\"";
        }
        o << "]";
    }
    if (hasFlag("--kernpairs")) {
        std::string kp = flagValue("--kernpairs");
        int n = atoi(kp.c_str());
        o << ",\"kerning\":[";
        uint16_t ng = font->numGlyphs();
        int emitted = 0;
        for (uint16_t l = 0; l < ng && emitted < n; l++) {
            for (uint16_t r = 0; r < ng && emitted < n; r++) {
                double k = font->kerning(l, r);
                if (k != 0) {
                    if (emitted) o << ",";
                    o << "[" << l << "," << r << "," << k << "]";
                    emitted++;
                }
            }
        }
        o << "]";
    }
    if (hasFlag("--vmetrics")) {
        o << ",\"vadvances\":[";
        uint16_t n = font->numGlyphs();
        for (uint16_t g = 0; g < n; g++) {
            if (g) o << ",";
            o << font->verticalAdvance(g);
        }
        o << "]";
    }
    o << "}";
    printf("%s\n", o.str().c_str());
    return 0;
}

static int cmdFaces() {
    auto font = Font::openFile(args[0]);
    int n = Font::numFacesInFile(args[0]);
    printf("%d face(s)\n", n);
    for (int i = 0; i < n && i < 32; i++) {
        try {
            auto face = Font::openFile(args[0], (uint32_t)i);
            FontInfo fi = face->info();
            printf("  face %d: %s %s (%u glyphs)\n", i, fi.family.c_str(),
                   fi.subfamily.c_str(), fi.numGlyphs);
        } catch (const Error& e) {
            printf("  face %d: error %s\n", i, e.what());
        }
    }
    return 0;
}

int main(int argc, char** argv) {
    enableUtf8Console();
    std::vector<std::string> wargs = utf8CommandLineArgs(argc, argv);
    if (wargs.size() < 2) {
        usage();
        return 1;
    }
    std::string cmd = wargs[1];
    for (size_t i = 2; i < wargs.size(); i++) args.push_back(wargs[i]);

    try {
        if (cmd == "info") return cmdInfo();
        if (cmd == "dump") return cmdDump();
        if (cmd == "cmap") return cmdCmap();
        if (cmd == "outline") return cmdOutline();
        if (cmd == "kern") return cmdKern();
        if (cmd == "var") return cmdVar();
        if (cmd == "layout") return cmdLayout();
        if (cmd == "metrics") return cmdMetrics();
        if (cmd == "glyphs") return cmdGlyphs();
        if (cmd == "validate") return cmdValidate();
        if (cmd == "subset") return cmdSubset();
        if (cmd == "convert") return cmdConvert();
        if (cmd == "scan") return cmdScan();
        if (cmd == "json") return cmdJson();
        if (cmd == "faces") return cmdFaces();
        if (cmd == "version") {
            printf("ftk %s (fontTools C++ rewrite)\n", ftk_version());
            return 0;
        }
    } catch (const Error& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    } catch (const std::exception& e) {
        fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    usage();
    return 1;
}
