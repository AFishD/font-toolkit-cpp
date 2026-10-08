// ftk: OpenType layout tables — GDEF/GSUB/GPOS structural inspection,
// GPOS pair kerning, GSUB single-substitution extraction.
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Bytes.hpp"

#include <map>
#include <string>
#include <vector>

namespace ftk {

struct Coverage {
    std::vector<uint16_t> glyphs; // sorted
    void parse(Reader r);
    bool covers(uint16_t g) const;
};

struct ClassDef {
    // format 1: start glyph + class values; format 2: ranges
    std::vector<std::pair<uint16_t, uint16_t>> ranges; // [first, last]
    std::vector<uint16_t> classes;
    void parse(Reader r);
    uint16_t classOf(uint16_t g) const;
};

struct LangSysRec {
    Tag tag = 0;
    uint16_t reqFeatureIndex = 0xFFFF;
    std::vector<uint16_t> featureIndices;
};

struct ScriptRec {
    Tag tag = 0;
    LangSysRec defaultLangSys;
    std::vector<LangSysRec> langSys;
};

struct FeatureRec {
    Tag tag = 0;
    std::vector<uint16_t> lookupIndices;
    uint32_t offset = 0;
};

struct LookupRec {
    uint16_t type = 0;
    uint16_t flag = 0;
    uint16_t markFilteringSet = 0;
    std::vector<uint32_t> subtableOffsets;
    std::vector<Coverage> coverages; // per subtable (best-effort)
};

struct PairPos2 {
    // format 2 subtable info for on-demand kerning
    ClassDef classDef1, classDef2;
    std::vector<int16_t> value1Matrix; // [class1 * nClass2] xAdvance (vf1 xAdvance only)
    uint16_t class2Count = 0;
};

struct LayoutTableInfo {
    Tag tableTag = 0;
    uint16_t majorVersion = 1, minorVersion = 0;
    std::vector<ScriptRec> scripts;
    std::vector<FeatureRec> features;
    std::vector<LookupRec> lookups;
    uint32_t featureVariationsOffset = 0;

    // GPOS type 2 pair kerning (format 1 enumerated; format 2 on demand)
    std::map<std::pair<uint16_t, uint16_t>, int16_t> kernPairs;
    std::vector<PairPos2> pairPos2;

    // GSUB type 1 single substitutions (in => out)
    std::map<uint16_t, uint16_t> singleSubs;
    // GSUB type 3 alternates (glyph -> list of alternates)
    std::map<uint16_t, std::vector<uint16_t>> alternates;
    // GSUB type 8 reverse chains: substitutions applied in reverse order
    std::map<uint16_t, uint16_t> reverseSubs;

    void parse(Reader r);
    int16_t kerning(uint16_t left, uint16_t right) const;
    std::string lookupTypeName(uint16_t type) const;

private:
    void parseSubtable(Reader r, uint32_t off, uint16_t type);
};

struct GdefInfo {
    bool hasGlyphClassDef = false;
    ClassDef glyphClassDef;
    bool hasMarkAttachClassDef = false;
    ClassDef markAttachClassDef;
    uint32_t varStoreOffset = 0;
    bool hasVarStore = false;
    void parse(Reader r);
};

class LayoutTables {
public:
    bool hasGdef = false, hasGsub = false, hasGpos = false;
    GdefInfo gdef;
    LayoutTableInfo gsub, gpos;

    void parse(Reader gdefR, Reader gsubR, Reader gposR);
    int16_t kerning(uint16_t left, uint16_t right) const;
};

} // namespace ftk
