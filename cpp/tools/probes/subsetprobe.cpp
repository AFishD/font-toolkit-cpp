// CFF subset round-trip diagnostic.
#include "ftk/Font.hpp"
#include "ftk/Subset.hpp"
#include "ftk/Platform.hpp"

#include <cstdio>
#include <vector>

using namespace ftk;

static void dumpDict(const char* label, const Dict& d) {
    printf("%s: %zu entries\n", label, d.entries.size());
    for (auto& e : d.entries) {
        printf("   op %d", e.op);
        if (e.op == 12) printf("/%d", e.op2);
        printf(" operands=%zu:", e.operands.size());
        for (auto& o : e.operands)
            printf(" %s", o.isInt ? std::to_string(o.i).c_str() : std::to_string(o.d).c_str());
        printf("\n");
    }
}

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string path = argc > 1 ? argv[1] : "Fonts/DearJoefour.otf";
    auto font = Font::openFile(path);
    const CffFont* c = font->cff();
    if (!c) {
        printf("no CFF\n");
        return 1;
    }
    dumpDict("original topDict", c->topDict);
    printf("original: numGlyphs=%u charstrings=%zu charset=%zu fonts=%zu isCID=%d\n",
           c->numGlyphs(), c->charStrings.count(), c->charsetSIDs.size(), c->fonts.size(),
           c->isCID ? 1 : 0);
    for (size_t i = 0; i < c->fonts.size(); i++) {
        printf("  font %zu: privOffset=%u privSize=%u subrs=%lld\n", i,
               c->fonts[i].privOffset, c->fonts[i].privSize,
               (long long)c->fonts[i].priv.subrs);
    }

    SubsetPlan plan;
    plan.unicodes = {0x41, 0x61, 0x20};
    std::vector<uint8_t> bytes;
    try {
        bytes = subsetFont(*font, plan);
    } catch (const Error& e) {
        printf("subset error: %s\n", e.what());
        return 1;
    }
    // write next to the CWD (or into $TMPDIR when set) instead of a fixed path
    const char* tmp = getenv("TEMP");
    if (!tmp) tmp = getenv("TMPDIR");
    std::string out = tmp ? (std::string(tmp) + "/ftk-subset-probe.otf")
                          : std::string("ftk-subset-probe.otf");
    writeFile(out, bytes.data(), bytes.size());
    printf("subset written: %zu bytes\n", bytes.size());

    auto sub = Font::openFile(out);
    const CffFont* sc = sub->cff();
    if (!sc) {
        printf("reopened: NO CFF (parse failed)\n");
        return 1;
    }
    dumpDict("subset topDict", sc->topDict);
    printf("subset: numGlyphs=%u charstrings=%zu charset=%zu fonts=%zu isCID=%d\n",
           sc->numGlyphs(), sc->charStrings.count(), sc->charsetSIDs.size(), sc->fonts.size(),
           sc->isCID ? 1 : 0);
    for (size_t i = 0; i < sc->fonts.size(); i++) {
        printf("  font %zu: privOffset=%u privSize=%u subrs=%lld\n", i,
               sc->fonts[i].privOffset, sc->fonts[i].privSize,
               (long long)sc->fonts[i].priv.subrs);
    }
    // draw all subset glyphs
    for (uint16_t g = 0; g < sub->numGlyphs(); g++) {
        Path p = sub->glyphPath(g);
        printf("  glyph %u: %zu segs, advance %g\n", g, p.cmds.size(), sub->advance(g));
    }
    return 0;
}
