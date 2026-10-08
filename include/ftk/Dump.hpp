// ftk: TTX-style XML dump + JSON summary.
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Font.hpp"

#include <string>
#include <vector>

namespace ftk {

struct DumpOptions {
    bool skipGlyphs = false;     // don't dump glyf/CFF glyph details
    std::vector<uint32_t> tables; // dump only these tags (empty = all)
    bool jsonInfo = false;       // emit font info as JSON instead
};

std::string dumpTtx(const Font& font, const DumpOptions& opt = {});
std::string dumpInfoJson(const Font& font);
std::string dumpInfoText(const Font& font);

std::string xmlEscape(const std::string& s);

} // namespace ftk
