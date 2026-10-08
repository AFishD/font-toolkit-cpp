// Variable font metrics probe (HVAR/VVAR/gvar phantom diagnostics).
#include "ftk/Font.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace ftk;

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    std::string path = argc > 1 ? argv[1] : "build/varfix/TestSparseCFF2VF.ttf";
    std::vector<double> coords;
    for (int i = 2; i < argc; i++) coords.push_back(atof(argv[i]));

    auto font = Font::openFile(path);
    const Fvar* fv = font->fvar();
    printf("axes:");
    if (fv)
        for (auto& a : fv->axes) printf(" %s(%g..%g..%g)", tagToStr(a.tag).c_str(),
                                        a.minValue, a.defaultValue, a.maxValue);
    printf("\n");

    const Hvar* hv = font->hvar();
    printf("HVAR: %s\n", hv ? "present" : "absent");
    if (hv) {
        printf("  store: format=%u regions=%zu dataItems=%zu\n", hv->store.format,
               hv->store.regions.size(), hv->store.dataItems.size());
        printf("  advanceMap: format=%u entries=%zu\n", hv->advanceMap.format,
               hv->advanceMap.entries.size());
        for (size_t i = 0; i < hv->advanceMap.entries.size() && i < 8; i++)
            printf("    gid %zu -> varIdx %u (outer %u inner %u)\n", i,
                   hv->advanceMap.entries[i], hv->advanceMap.entries[i] >> 16,
                   hv->advanceMap.entries[i] & 0xFFFF);
        for (size_t d = 0; d < hv->store.dataItems.size(); d++) {
            printf("    dataItem %zu: itemCount=%u regions=%zu rows=%zu\n", d,
                   hv->store.dataItems[d].itemCount, hv->store.dataItems[d].regionIndexes.size(),
                   hv->store.dataItems[d].deltas.size());
        }
    }
    std::vector<double> norm = font->normalizeCoords(coords);
    printf("normalized:");
    for (double v : norm) printf(" %g", v);
    printf("\n");

    uint16_t n = font->numGlyphs();
    for (uint16_t g = 0; g < n && g < 6; g++) {
        double base = 0;
        if (const Hmtx* hm = font->hmtx()) base = hm->advance(g);
        double delta = hv ? hv->advanceDelta(g, norm) : 0.0;
        printf("gid %u: base=%g delta=%g total=%g\n", g, base, delta, font->advance(g, &coords));
    }
    if (const Gvar* gv = font->gvar()) {
        printf("gvar: axisCount=%u glyphCount=%u sharedTuples=%zu\n", gv->axisCount,
               gv->glyphCount, gv->sharedTuples.size());
        for (uint16_t g = 0; g < n && g < 6; g++)
            printf("  gid %u hasVariation=%d\n", g, gv->hasGlyphVariation(g) ? 1 : 0);
    }
    return 0;
}
