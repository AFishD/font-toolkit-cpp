// Layout tables implementation.
#include "ftk/Layout.hpp"

#include <algorithm>

namespace ftk {

// ---------------------------------------------------------------- helpers

void Coverage::parse(Reader r) {
    uint16_t fmt = r.u16();
    if (fmt == 1) {
        uint16_t count = r.u16();
        glyphs.reserve(count);
        for (uint16_t i = 0; i < count && r.remaining() >= 2; i++)
            glyphs.push_back(r.u16());
    } else if (fmt == 2) {
        uint16_t rangeCount = r.u16();
        for (uint16_t i = 0; i < rangeCount && r.remaining() >= 6; i++) {
            uint16_t start = r.u16(), end = r.u16();
            uint16_t startCoverageIndex = r.u16();
            (void)startCoverageIndex;
            if (start > end) continue;
            // guard against absurd ranges
            if ((uint32_t)(end - start) > 0xFFFF) continue;
            for (uint32_t g = start; g <= end; g++)
                glyphs.push_back((uint16_t)g);
        }
    }
}

bool Coverage::covers(uint16_t g) const {
    return std::binary_search(glyphs.begin(), glyphs.end(), g);
}

void ClassDef::parse(Reader r) {
    uint16_t fmt = r.u16();
    if (fmt == 1) {
        uint16_t start = r.u16();
        uint16_t count = r.u16();
        for (uint16_t i = 0; i < count; i++)
            ranges.emplace_back(start + i, start + i);
        classes.reserve(count);
        for (uint16_t i = 0; i < count && r.remaining() >= 2; i++)
            classes.push_back(r.u16());
    } else if (fmt == 2) {
        uint16_t rangeCount = r.u16();
        for (uint16_t i = 0; i < rangeCount && r.remaining() >= 6; i++) {
            uint16_t start = r.u16(), end = r.u16();
            uint16_t cls = r.u16();
            ranges.emplace_back(start, end);
            classes.push_back(cls);
        }
    }
}

uint16_t ClassDef::classOf(uint16_t g) const {
    // linear scan ok for structural use; binary over sorted ranges if needed
    for (size_t i = 0; i < ranges.size(); i++)
        if (g >= ranges[i].first && g <= ranges[i].second)
            return classes[i];
    return 0;
}

// ---------------------------------------------------------------- parse

static void parseScriptList(Reader r, uint32_t off, std::vector<ScriptRec>& scripts) {
    if (off + 2 > r.size()) return;
    Reader sr = r.subAt(off, r.size() - off);
    uint16_t count = sr.u16();
    struct Rec { Tag tag; uint16_t offset; };
    std::vector<Rec> recs;
    for (uint16_t i = 0; i < count && sr.remaining() >= 6; i++) {
        Rec r2;
        r2.tag = sr.u32();
        r2.offset = sr.u16();
        recs.push_back(r2);
    }
    for (auto& e : recs) {
        ScriptRec sc;
        sc.tag = e.tag;
        size_t sOff = off + e.offset;
        if (sOff + 4 > r.size()) continue;
        Reader scr = r.subAt(sOff, r.size() - sOff);
        uint16_t defaultLSOff = scr.u16();
        uint16_t langSysCount = scr.u16();
        struct LS { Tag tag; uint16_t off; };
        std::vector<LS> lsRecs;
        for (uint16_t i = 0; i < langSysCount && scr.remaining() >= 6; i++) {
            LS l;
            l.tag = scr.u32();
            l.off = scr.u16();
            lsRecs.push_back(l);
        }
        auto parseLangSys = [&](Reader lr, LangSysRec& out2) {
            lr.u16(); // lookupOrderOffset
            out2.reqFeatureIndex = lr.u16();
            uint16_t fi = lr.u16();
            for (uint16_t k = 0; k < fi && lr.remaining() >= 2; k++)
                out2.featureIndices.push_back(lr.u16());
        };
        if (defaultLSOff) {
            size_t lOff = sOff + defaultLSOff;
            if (lOff + 6 <= r.size())
                parseLangSys(r.subAt(lOff, r.size() - lOff), sc.defaultLangSys);
        }
        for (auto& l : lsRecs) {
            LangSysRec lr;
            lr.tag = l.tag;
            size_t lOff = sOff + l.off;
            if (lOff + 6 <= r.size())
                parseLangSys(r.subAt(lOff, r.size() - lOff), lr);
            sc.langSys.push_back(std::move(lr));
        }
        scripts.push_back(std::move(sc));
    }
}

static void parseFeatureList(Reader r, uint32_t off, std::vector<FeatureRec>& features) {
    if (off + 2 > r.size()) return;
    Reader fr = r.subAt(off, r.size() - off);
    uint16_t count = fr.u16();
    struct Rec { Tag tag; uint16_t offset; };
    std::vector<Rec> recs;
    for (uint16_t i = 0; i < count && fr.remaining() >= 6; i++) {
        Rec e;
        e.tag = fr.u32();
        e.offset = fr.u16();
        recs.push_back(e);
    }
    for (auto& e : recs) {
        FeatureRec f;
        f.tag = e.tag;
        f.offset = off + e.offset;
        size_t fOff = f.offset;
        if (fOff + 4 > r.size()) continue;
        Reader r2 = r.subAt(fOff, r.size() - fOff);
        r2.u16(); // featureParams
        uint16_t li = r2.u16();
        for (uint16_t k = 0; k < li && r2.remaining() >= 2; k++)
            f.lookupIndices.push_back(r2.u16());
        features.push_back(std::move(f));
    }
}

void LayoutTableInfo::parse(Reader r) {
    tableTag = 0;
    majorVersion = r.u16();
    minorVersion = r.u16();
    uint16_t scriptListOffset = r.u16();
    uint16_t featureListOffset = r.u16();
    uint16_t lookupListOffset = r.u16();
    if (minorVersion >= 1 && r.remaining() >= 4)
        featureVariationsOffset = r.u32();

    parseScriptList(r, scriptListOffset, scripts);
    parseFeatureList(r, featureListOffset, features);

    // lookup list
    if ((size_t)lookupListOffset + 2 <= r.size()) {
        Reader lr = r.subAt(lookupListOffset, r.size() - lookupListOffset);
        uint16_t count = lr.u16();
        std::vector<uint16_t> offs;
        for (uint16_t i = 0; i < count && lr.remaining() >= 2; i++)
            offs.push_back(lr.u16());
        for (uint16_t off : offs) {
            LookupRec lu;
            size_t lOff = lookupListOffset + off;
            if (lOff + 6 > r.size()) continue;
            Reader r2 = r.subAt(lOff, r.size() - lOff);
            lu.type = r2.u16();
            lu.flag = r2.u16();
            uint16_t subCount = r2.u16();
            for (uint16_t k = 0; k < subCount && r2.remaining() >= 2; k++) {
                uint16_t subOff = r2.u16();
                lu.subtableOffsets.push_back((uint32_t)(lOff + subOff));
            }
            if ((lu.flag & 0x0010) && r2.remaining() >= 2) // useMarkFilteringSet
                lu.markFilteringSet = r2.u16();
            for (uint32_t so : lu.subtableOffsets) {
                if (so + 2 <= r.size())
                    parseSubtable(r, so, lu.type);
            }
            lookups.push_back(std::move(lu));
        }
    }
}

std::string LayoutTableInfo::lookupTypeName(uint16_t type) const {
    if (tableTag == tag_GSUB) {
        switch (type) {
        case 1: return "Single substitution";
        case 2: return "Multiple substitution";
        case 3: return "Alternate substitution";
        case 4: return "Ligature substitution";
        case 5: return "Contextual substitution";
        case 6: return "Chained contexts substitution";
        case 7: return "Extension substitution";
        case 8: return "Reverse chaining context single substitution";
        }
    } else if (tableTag == tag_GPOS) {
        switch (type) {
        case 1: return "Single adjustment";
        case 2: return "Pair adjustment";
        case 3: return "Cursive attachment";
        case 4: return "MarkToBase attachment";
        case 5: return "MarkToLigature attachment";
        case 6: return "MarkToMark attachment";
        case 7: return "Context positioning";
        case 8: return "Chained contexts positioning";
        case 9: return "Extension positioning";
        }
    }
    return "Unknown";
}

void LayoutTableInfo::parseSubtable(Reader r, uint32_t off, uint16_t type) {
    if (off + 6 > r.size()) return;
    Reader sr = r.subAt(off, r.size() - off);
    uint16_t fmt = sr.u16();
    uint16_t covOff = sr.u16();

    auto covAt = [&](uint32_t subOff, uint16_t rel) -> Coverage {
        Coverage c;
        if (rel && subOff + rel + 2 <= r.size())
            c.parse(r.subAt(subOff + rel, r.size() - (subOff + rel)));
        return c;
    };

    if (tableTag == tag_GSUB) {
        if (type == 1) {
            Coverage cov = covAt(off, covOff);
            if (fmt == 1) {
                int16_t delta = sr.i16();
                for (uint16_t g : cov.glyphs)
                    singleSubs[g] = (uint16_t)(g + delta);
            } else if (fmt == 2) {
                uint16_t count = sr.u16();
                size_t i = 0;
                for (uint16_t g : cov.glyphs) {
                    if (i >= count) break;
                    Reader rr = r.subAt(off + 6 + i * 2, 2);
                    singleSubs[g] = rr.u16();
                    i++;
                }
            }
        } else if (type == 3 && fmt == 1) {
            Coverage cov = covAt(off, covOff);
            uint16_t count = sr.u16();
            size_t i = 0;
            for (uint16_t g : cov.glyphs) {
                if (i >= count) break;
                Reader rr = r.subAt(off + 6 + i * 2, 2);
                uint16_t altSetOff = rr.u16();
                std::vector<uint16_t> alts;
                if (altSetOff && off + altSetOff + 2 <= r.size()) {
                    Reader ar = r.subAt(off + altSetOff, r.size() - (off + altSetOff));
                    uint16_t n = ar.u16();
                    for (uint16_t k = 0; k < n && ar.remaining() >= 2; k++)
                        alts.push_back(ar.u16());
                }
                alternates[g] = std::move(alts);
                i++;
            }
        } else if (type == 8 && fmt == 1) {
            // reverse chain single substitution
            Coverage cov = covAt(off, covOff);
            sr.u16(); // backtrackCoverageCount skipped reading below
            // re-read properly:
            Reader r2 = r.subAt(off, r.size() - off);
            r2.u16(); r2.u16(); // format, coverageOffset
            uint16_t backtrackCount = r2.u16();
            for (uint16_t i = 0; i < backtrackCount && r2.remaining() >= 2; i++)
                r2.u16();
            uint16_t aheadCount = r2.u16();
            for (uint16_t i = 0; i < aheadCount && r2.remaining() >= 2; i++)
                r2.u16();
            uint16_t glyphCount = r2.u16();
            size_t i = 0;
            for (uint16_t g : cov.glyphs) {
                if (i >= glyphCount || r2.remaining() < 2) break;
                reverseSubs[g] = r2.u16();
                i++;
            }
        }
    } else if (tableTag == tag_GPOS && type == 2) {
        if (fmt == 1) {
            Coverage cov = covAt(off, covOff);
            uint16_t vf1 = sr.u16(), vf2 = sr.u16();
            size_t recSize = 2 + 2 * __builtin_popcount(vf1) + 2 * __builtin_popcount(vf2);
            (void)recSize;
            auto valSize = [](uint16_t vf) { return (size_t)__builtin_popcount(vf) * 2; };
            size_t vs1 = valSize(vf1), vs2 = valSize(vf2);
            uint16_t pairSetCount = sr.u16();
            size_t gi = 0;
            for (uint16_t g1 : cov.glyphs) {
                if (gi >= pairSetCount || sr.remaining() < 2) break;
                uint16_t psOff = sr.u16();
                gi++;
                if (!psOff || off + psOff + 2 > r.size()) continue;
                Reader ps = r.subAt(off + psOff, r.size() - (off + psOff));
                uint16_t pvCount = ps.u16();
                for (uint16_t k = 0; k < pvCount; k++) {
                    if (ps.remaining() < 2 + vs1 + vs2) break;
                    uint16_t g2 = ps.u16();
                    // read value1 records; xAdvance is bit0 of vf1
                    int16_t xAdvance = 0;
                    if (vf1 & 0x0001)
                        xAdvance = ps.i16();
                    else
                        xAdvance = 0;
                    ps.skip(vs1 - ((vf1 & 0x0001) ? 2 : 0));
                    ps.skip(vs2);
                    kernPairs[{g1, g2}] = xAdvance;
                }
            }
        } else if (fmt == 2) {
            Coverage cov = covAt(off, covOff);
            (void)cov;
            PairPos2 pp;
            uint16_t vf1 = sr.u16(), vf2 = sr.u16();
            if (!(vf1 & 0x0001)) return; // need xAdvance
            uint16_t cd1Off = sr.u16(), cd2Off = sr.u16();
            uint16_t c1Count = sr.u16(), c2Count = sr.u16();
            pp.class2Count = c2Count;
            if (cd1Off && off + cd1Off < r.size())
                pp.classDef1.parse(r.subAt(off + cd1Off, r.size() - (off + cd1Off)));
            if (cd2Off && off + cd2Off < r.size())
                pp.classDef2.parse(r.subAt(off + cd2Off, r.size() - (off + cd2Off)));
            size_t recWords = __builtin_popcount(vf1) + __builtin_popcount(vf2);
            for (uint16_t i = 0; i < c1Count; i++) {
                for (uint16_t j = 0; j < c2Count; j++) {
                    if (sr.remaining() < recWords * 2) break;
                    // value1 xAdvance is first field of record i*c2+j when vf1&1
                    int16_t xAdv = sr.i16();
                    pp.value1Matrix.push_back(xAdv);
                    sr.skip((recWords - 1) * 2);
                }
            }
            pairPos2.push_back(std::move(pp));
        }
    }
}

int16_t LayoutTableInfo::kerning(uint16_t left, uint16_t right) const {
    auto it = kernPairs.find({left, right});
    if (it != kernPairs.end()) return it->second;
    for (auto& pp : pairPos2) {
        uint16_t c1 = pp.classDef1.classOf(left);
        if (c1 == 0) continue;
        uint16_t c2 = pp.classDef2.classOf(right);
        if (c2 >= pp.class2Count) c2 = 0;
        if ((size_t)c1 * pp.class2Count + c2 < pp.value1Matrix.size())
            return pp.value1Matrix[(size_t)c1 * pp.class2Count + c2];
    }
    return 0;
}

void GdefInfo::parse(Reader r) {
    uint16_t major = r.u16(), minor = r.u16();
    (void)major; (void)minor;
    uint16_t glyphClassDefOff = r.u16();
    uint16_t attachListOff = r.u16();
    uint16_t ligCaretListOff = r.u16();
    uint16_t markAttachClassDefOff = r.u16();
    uint32_t markGlyphSetsOff = 0;
    if (minor >= 2 && r.remaining() >= 4)
        markGlyphSetsOff = r.u32();
    if (minor >= 3 && r.remaining() >= 4) {
        uint32_t varStore = r.u32();
        if (varStore) hasVarStore = true;
        varStoreOffset = varStore;
    }
    if (glyphClassDefOff && glyphClassDefOff < r.size()) {
        hasGlyphClassDef = true;
        glyphClassDef.parse(r.subAt(glyphClassDefOff, r.size() - glyphClassDefOff));
    }
    if (markAttachClassDefOff && markAttachClassDefOff < r.size()) {
        hasMarkAttachClassDef = true;
        markAttachClassDef.parse(r.subAt(markAttachClassDefOff, r.size() - markAttachClassDefOff));
    }
    (void)attachListOff; (void)ligCaretListOff; (void)markGlyphSetsOff;
}

void LayoutTables::parse(Reader gdefR, Reader gsubR, Reader gposR) {
    if (gdefR) {
        hasGdef = true;
        gdef.parse(gdefR);
    }
    if (gsubR) {
        hasGsub = true;
        gsub.tableTag = tag_GSUB;
        gsub.parse(gsubR);
    }
    if (gposR) {
        hasGpos = true;
        gpos.tableTag = tag_GPOS;
        gpos.parse(gposR);
    }
}

int16_t LayoutTables::kerning(uint16_t left, uint16_t right) const {
    int16_t k = gpos.kerning(left, right);
    if (k != 0) return k;
    return gsub.kerning(left, right); // (no kerning in GSUB; returns 0)
}

} // namespace ftk
