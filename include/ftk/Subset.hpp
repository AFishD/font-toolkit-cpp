// ftk: font subsetting — build a new font keeping a subset of glyphs.
// Supports TrueType (glyf) and CFF/CFF2 outlines.
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Font.hpp"

#include <string>
#include <vector>

namespace ftk {

struct SubsetPlan {
    std::vector<uint32_t> unicodes;    // codepoints to keep
    std::vector<uint16_t> gids;        // glyph ids to keep
    std::vector<std::string> names;    // glyph names to keep
    bool keepEverything = false;       // retain all glyphs (still rebuilds)
    bool keepLayout = false;           // keep GSUB/GPOS/GDEF/kern etc. raw
    bool keepNameTable = true;
    bool keepOs2 = true;
    bool keepPostNames = false;        // keep post v2 glyph names (not recommended)
    bool keepVertical = false;         // keep vhea/vmtx/VORG
    std::vector<uint32_t> dropTables;  // extra tags to drop
};

// Returns subset font bytes (plain sfnt). Throws ftk::Error on failure.
std::vector<uint8_t> subsetFont(const Font& font, const SubsetPlan& plan);

// Container conversion: ttf/otf <-> woff (store-mode deflate).
std::vector<uint8_t> sfntToWoff(const std::vector<uint8_t>& sfntBytes);

} // namespace ftk
