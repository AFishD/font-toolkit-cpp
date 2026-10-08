// TTX-style XML dump + JSON summary.
#include "ftk/Dump.hpp"
#include "ftk/Layout.hpp"
#include "ftk/Color.hpp"

#include <cmath>
#include <cstdio>
#include <sstream>

namespace ftk {

std::string xmlEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        uint8_t c = (uint8_t)s[i];
        if (c < 0x80) {
            switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            case '\'': out += "&apos;"; break;
            default:
                if (c < 0x20 && c != '\t' && c != '\n' && c != '\r') {
                    // XML 1.0 forbids these even as character references
                    out += "&#xFFFD;";
                } else {
                    out += (char)c;
                }
            }
            i++;
            continue;
        }
        // multi-byte: copy only if it is a valid UTF-8 sequence, else replace
        size_t start = i;
        uint32_t cp = 0;
        size_t need = 0;
        if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; need = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; need = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; need = 3; }
        bool ok = need > 0;
        for (size_t k = 1; ok && k <= need; k++) {
            if (i + k >= s.size() || ((uint8_t)s[i + k] & 0xC0) != 0x80) ok = false;
            else cp = (cp << 6) | ((uint8_t)s[i + k] & 0x3F);
        }
        if (ok) {
            out.append(s, start, need + 1);
            i += need + 1;
        } else {
            out += "&#xFFFD;";
            i++;
        }
    }
    return out;
}

static std::string hexv(uint32_t v, int digits = 0) {
    char buf[32];
    if (digits) snprintf(buf, sizeof buf, "0x%0*X", digits, v);
    else snprintf(buf, sizeof buf, "0x%X", v);
    return buf;
}

static std::string num(double v) {
    if (std::floor(v) == v && std::fabs(v) < 1e15) {
        char buf[32];
        snprintf(buf, sizeof buf, "%lld", (long long)v);
        return buf;
    }
    std::ostringstream o;
    o.precision(6);
    o << v;
    return o.str();
}

static std::string macDate(int64_t t) {
    // seconds since 1904-01-01
    if (t <= 0) return "Thu Jan 1 00:00:00 1904";
    time_t unixT = (time_t)(t - 2082844800);
    struct tm tmv;
#ifdef _WIN32
    if (gmtime_s(&tmv, &unixT) != 0) return "invalid";
#else
    if (!gmtime_r(&unixT, &tmv)) return "invalid";
#endif
    char buf[64];
    // "%e" (space-padded day) is POSIX-only and rejected by some runtimes;
    // "%d" plus this fixup yields the same text fontTools prints.
    strftime(buf, sizeof buf, "%a %b %d %H:%M:%S %Y", &tmv);
    std::string out = buf;
    if (out.size() > 9 && out[8] == '0') out[8] = ' ';
    return out;
}

// ------------------------------------------------------------------ tables

static void dumpHead(std::ostringstream& o, const Head& h) {
    o << "    <tableVersion value=\"" << num(h.majorVersion + h.minorVersion / 10.0) << "\"/>\n";
    o << "    <fontRevision value=\"" << num(h.fontRevision) << "\"/>\n";
    o << "    <checkSumAdjustment value=\"" << hexv(h.checkSumAdjustment, 8) << "\"/>\n";
    o << "    <magicNumber value=\"" << hexv(h.magicNumber, 8) << "\"/>\n";
    o << "    <flags value=\"" << hexv(h.flags, 4) << "\"/>\n";
    o << "    <unitsPerEm value=\"" << h.unitsPerEm << "\"/>\n";
    o << "    <created value=\"" << macDate(h.created) << "\"/>\n";
    o << "    <modified value=\"" << macDate(h.modified) << "\"/>\n";
    o << "    <xMin value=\"" << h.xMin << "\"/>\n";
    o << "    <yMin value=\"" << h.yMin << "\"/>\n";
    o << "    <xMax value=\"" << h.xMax << "\"/>\n";
    o << "    <yMax value=\"" << h.yMax << "\"/>\n";
    o << "    <macStyle value=\"" << hexv(h.macStyle, 4) << "\"/>\n";
    o << "    <lowestRecPPEM value=\"" << h.lowestRecPPEM << "\"/>\n";
    o << "    <fontDirectionHint value=\"" << h.fontDirectionHint << "\"/>\n";
    o << "    <indexToLocFormat value=\"" << h.indexToLocFormat << "\"/>\n";
    o << "    <glyphDataFormat value=\"" << h.glyphDataFormat << "\"/>\n";
}

static void dumpHhea(std::ostringstream& o, const Hhea& h, const char* tag) {
    o << "    <tableVersion value=\"" << num(h.majorVersion + h.minorVersion / 10.0) << "\"/>\n";
    const char* asc = tag[0] == 'v' ? "vertTypoAscender" : "ascender";
    const char* desc = tag[0] == 'v' ? "vertTypoDescender" : "descender";
    const char* gap = tag[0] == 'v' ? "vertTypoLineGap" : "lineGap";
    const char* awm = tag[0] == 'v' ? "advanceHeightMax" : "advanceWidthMax";
    const char* lsb = tag[0] == 'v' ? "minTopSideBearing" : "minLeftSideBearing";
    const char* rsb = tag[0] == 'v' ? "minBottomSideBearing" : "minRightSideBearing";
    const char* ext = tag[0] == 'v' ? "yMaxExtent" : "xMaxExtent";
    const char* nm = tag[0] == 'v' ? "numberOfVMetrics" : "numberOfHMetrics";
    o << "    <" << asc << " value=\"" << h.ascender << "\"/>\n";
    o << "    <" << desc << " value=\"" << h.descender << "\"/>\n";
    o << "    <" << gap << " value=\"" << h.lineGap << "\"/>\n";
    o << "    <" << awm << " value=\"" << h.advanceWidthMax << "\"/>\n";
    o << "    <" << lsb << " value=\"" << h.minLeftSideBearing << "\"/>\n";
    o << "    <" << rsb << " value=\"" << h.minRightSideBearing << "\"/>\n";
    o << "    <" << ext << " value=\"" << h.xMaxExtent << "\"/>\n";
    o << "    <caretSlopeRise value=\"" << h.caretSlopeRise << "\"/>\n";
    o << "    <caretSlopeRun value=\"" << h.caretSlopeRun << "\"/>\n";
    o << "    <caretOffset value=\"" << h.caretOffset << "\"/>\n";
    o << "    <" << nm << " value=\"" << h.numberOfHMetrics << "\"/>\n";
}

static void dumpMaxp(std::ostringstream& o, const Maxp& m) {
    o << "    <tableVersion value=\"" << num(m.majorVersion + m.minorVersion / 10.0) << "\"/>\n";
    o << "    <numGlyphs value=\"" << m.numGlyphs << "\"/>\n";
    if (m.majorVersion == 1) {
        o << "    <maxPoints value=\"" << m.maxPoints << "\"/>\n";
        o << "    <maxContours value=\"" << m.maxContours << "\"/>\n";
        o << "    <maxCompositePoints value=\"" << m.maxCompositePoints << "\"/>\n";
        o << "    <maxCompositeContours value=\"" << m.maxCompositeContours << "\"/>\n";
        o << "    <maxZones value=\"" << m.maxZones << "\"/>\n";
        o << "    <maxTwilightPoints value=\"" << m.maxTwilightPoints << "\"/>\n";
        o << "    <maxStorage value=\"" << m.maxStorage << "\"/>\n";
        o << "    <maxFunctionDefs value=\"" << m.maxFunctionDefs << "\"/>\n";
        o << "    <maxInstructionDefs value=\"" << m.maxInstructionDefs << "\"/>\n";
        o << "    <maxStackElements value=\"" << m.maxStackElements << "\"/>\n";
        o << "    <maxSizeOfInstructions value=\"" << m.maxSizeOfInstructions << "\"/>\n";
        o << "    <maxComponentElements value=\"" << m.maxComponentElements << "\"/>\n";
        o << "    <maxComponentDepth value=\"" << m.maxComponentDepth << "\"/>\n";
    }
}

static void dumpOs2(std::ostringstream& o, const Os2& v) {
    o << "    <version value=\"" << v.version << "\"/>\n";
    o << "    <xAvgCharWidth value=\"" << v.xAvgCharWidth << "\"/>\n";
    o << "    <usWeightClass value=\"" << v.usWeightClass << "\"/>\n";
    o << "    <usWidthClass value=\"" << v.usWidthClass << "\"/>\n";
    o << "    <fsType value=\"" << hexv(v.fsType, 4) << "\"/>\n";
    o << "    <ySubscriptXSize value=\"" << v.ySubscriptXSize << "\"/>\n";
    o << "    <ySubscriptYSize value=\"" << v.ySubscriptYSize << "\"/>\n";
    o << "    <ySubscriptXOffset value=\"" << v.ySubscriptXOffset << "\"/>\n";
    o << "    <ySubscriptYOffset value=\"" << v.ySubscriptYOffset << "\"/>\n";
    o << "    <ySuperscriptXSize value=\"" << v.ySuperscriptXSize << "\"/>\n";
    o << "    <ySuperscriptYSize value=\"" << v.ySuperscriptYSize << "\"/>\n";
    o << "    <ySuperscriptXOffset value=\"" << v.ySuperscriptXOffset << "\"/>\n";
    o << "    <ySuperscriptYOffset value=\"" << v.ySuperscriptYOffset << "\"/>\n";
    o << "    <yStrikeoutSize value=\"" << v.yStrikeoutSize << "\"/>\n";
    o << "    <yStrikeoutPosition value=\"" << v.yStrikeoutPosition << "\"/>\n";
    o << "    <sFamilyClass value=\"" << v.sFamilyClass << "\"/>\n";
    o << "    <panose>\n      <bFamilyType value=\"" << (int)v.panose[0] << "\"/>\n";
    const char* panoseNames[] = {"bSerifStyle", "bWeight", "bProportion", "bContrast",
                                 "bStrokeVariation", "bArmStyle", "bLetterForm",
                                 "bMidline", "bXHeight"};
    for (int i = 0; i < 9; i++)
        o << "      <" << panoseNames[i] << " value=\"" << (int)v.panose[i + 1] << "\"/>\n";
    o << "    </panose>\n";
    o << "    <ulUnicodeRange1 value=\"" << hexv(v.ulUnicodeRange1, 8) << "\"/>\n";
    o << "    <ulUnicodeRange2 value=\"" << hexv(v.ulUnicodeRange2, 8) << "\"/>\n";
    o << "    <ulUnicodeRange3 value=\"" << hexv(v.ulUnicodeRange3, 8) << "\"/>\n";
    o << "    <ulUnicodeRange4 value=\"" << hexv(v.ulUnicodeRange4, 8) << "\"/>\n";
    o << "    <achVendID value=\"" << xmlEscape(v.achVendID) << "\"/>\n";
    o << "    <fsSelection value=\"" << hexv(v.fsSelection, 4) << "\"/>\n";
    o << "    <usFirstCharIndex value=\"" << hexv(v.usFirstCharIndex, 4) << "\"/>\n";
    o << "    <usLastCharIndex value=\"" << hexv(v.usLastCharIndex, 4) << "\"/>\n";
    o << "    <sTypoAscender value=\"" << v.sTypoAscender << "\"/>\n";
    o << "    <sTypoDescender value=\"" << v.sTypoDescender << "\"/>\n";
    o << "    <sTypoLineGap value=\"" << v.sTypoLineGap << "\"/>\n";
    o << "    <usWinAscent value=\"" << v.usWinAscent << "\"/>\n";
    o << "    <usWinDescent value=\"" << v.usWinDescent << "\"/>\n";
    if (v.version >= 1) {
        o << "    <ulCodePageRange1 value=\"" << hexv(v.ulCodePageRange1, 8) << "\"/>\n";
        o << "    <ulCodePageRange2 value=\"" << hexv(v.ulCodePageRange2, 8) << "\"/>\n";
    }
    if (v.version >= 2) {
        o << "    <sxHeight value=\"" << v.sxHeight << "\"/>\n";
        o << "    <sCapHeight value=\"" << v.sCapHeight << "\"/>\n";
        o << "    <usDefaultChar value=\"" << hexv(v.usDefaultChar, 4) << "\"/>\n";
        o << "    <usBreakChar value=\"" << hexv(v.usBreakChar, 4) << "\"/>\n";
        o << "    <usMaxContext value=\"" << v.usMaxContext << "\"/>\n";
    }
    if (v.version >= 5) {
        o << "    <usLowerOpticalPointSize value=\"" << v.usLowerOpticalPointSize << "\"/>\n";
        o << "    <usUpperOpticalPointSize value=\"" << v.usUpperOpticalPointSize << "\"/>\n";
    }
}

static void dumpPost(std::ostringstream& o, const Post& p) {
    o << "    <formatType value=\"" << num(p.version) << "\"/>\n";
    o << "    <italicAngle value=\"" << num(p.italicAngle) << "\"/>\n";
    o << "    <underlinePosition value=\"" << p.underlinePosition << "\"/>\n";
    o << "    <underlineThickness value=\"" << p.underlineThickness << "\"/>\n";
    o << "    <isFixedPitch value=\"" << p.isFixedPitch << "\"/>\n";
    o << "    <minMemType42 value=\"" << p.minMemType42 << "\"/>\n";
    o << "    <maxMemType42 value=\"" << p.maxMemType42 << "\"/>\n";
    o << "    <minMemType1 value=\"" << p.minMemType1 << "\"/>\n";
    o << "    <maxMemType1 value=\"" << p.maxMemType1 << "\"/>\n";
}

static void dumpName(std::ostringstream& o, const NameTable& n) {
    o << "    <format value=\"" << n.format << "\"/>\n";
    for (auto& r : n.records) {
        if (r.platformID == 0 && r.langTag.empty() && r.languageID >= 0x8000) {
            o << "    <namerecord nameID=\"" << r.nameID << "\" platformID=\"0\" platEncID=\""
              << r.encodingID << "\" langID=\"" << hexv(r.languageID, 4) << "\">";
        } else {
            o << "    <namerecord nameID=\"" << r.nameID << "\" platformID=\"" << r.platformID
              << "\" platEncID=\"" << r.encodingID << "\" langID=\"" << hexv(r.languageID, 4) << "\">";
        }
        o << xmlEscape(r.decoded) << "</namerecord>\n";
    }
}

static void dumpCmap(std::ostringstream& o, const Cmap& c, const Font* f) {
    o << "    <version value=\"" << c.version << "\"/>\n";
    for (auto& st : c.subtables) {
        o << "    <cmap_format_" << st.format << " platformID=\"" << st.platformID
          << "\" platEncID=\"" << st.encodingID << "\" language=\"" << st.language << "\">\n";
        for (auto& e : st.map) {
            std::string gname = f ? f->glyphName(e.second) : std::to_string(e.second);
            o << "      <map code=\"" << hexv(e.first) << "\" name=\"" << xmlEscape(gname)
              << "\"/><!-- " << e.second << " -->\n";
        }
        o << "    </cmap_format_" << st.format << ">\n";
    }
    if (c.format14.format == 14) {
        o << "    <cmap_format_14 platformID=\"0\" platEncID=\"5\">\n";
        o << "    </cmap_format_14>\n";
    }
}

static void dumpHmtx(std::ostringstream& o, const Hmtx& h, const Font* f, uint16_t numGlyphs) {
    for (uint16_t g = 0; g < numGlyphs; g++) {
        std::string gname = f ? f->glyphName(g) : std::to_string(g);
        o << "    <mtx name=\"" << xmlEscape(gname) << "\" width=\"" << h.advance(g)
          << "\" lsb=\"" << h.bearing(g) << "\"/>\n";
    }
}

static void dumpGlyf(std::ostringstream& o, const Font& f, const GlyfTable* gt) {
    if (!gt) return;
    for (uint16_t g = 0; g < gt->numGlyphs(); g++) {
        Glyph gl = gt->glyph(g);
        std::string gname = f.glyphName(g);
        if (gl.kind == Glyph::Kind::Empty) {
            o << "    <TTGlyph name=\"" << xmlEscape(gname) << "\" xMin=\"0\" yMin=\"0\" xMax=\"0\" yMax=\"0\"/>\n";
            continue;
        }
        o << "    <TTGlyph name=\"" << xmlEscape(gname) << "\" xMin=\"" << gl.bounds.xMin
          << "\" yMin=\"" << gl.bounds.yMin << "\" xMax=\"" << gl.bounds.xMax << "\" yMax=\""
          << gl.bounds.yMax << "\">\n";
        if (gl.kind == Glyph::Kind::Simple) {
            size_t start = 0;
            for (uint32_t ci = 0; ci < gl.endPts.size(); ci++) {
                o << "      <contour>\n";
                for (size_t p = start; p <= gl.endPts[ci] && p < gl.points.size(); p++) {
                    auto& pt = gl.points[p];
                    o << "        <pt x=\"" << pt.x << "\" y=\"" << pt.y << "\" on=\""
                      << (pt.onCurve ? 1 : 0) << "\"/>\n";
                }
                o << "      </contour>\n";
                start = gl.endPts[ci] + 1;
            }
        } else {
            for (auto& comp : gl.components) {
                std::string cname = f.glyphName(comp.glyph);
                o << "      <component glyphName=\"" << xmlEscape(cname) << "\" x=\""
                  << comp.e << "\" y=\"" << comp.f << "\"";
                if (comp.flags & 0x0008) {
                    o << " scale=\"" << num(comp.a) << "\"";
                } else if (comp.flags & 0x0040) {
                    o << " xscale=\"" << num(comp.a) << "\" yscale=\"" << num(comp.d) << "\"";
                } else if (comp.flags & 0x0080) {
                    o << " scalex=\"" << num(comp.a) << "\" scale01=\"" << num(comp.b)
                      << "\" scale10=\"" << num(comp.c) << "\" scaley=\"" << num(comp.d) << "\"";
                }
                o << "/>\n";
            }
        }
        o << "    </TTGlyph>\n";
    }
}

static void dumpFvar(std::ostringstream& o, const Fvar& f, const NameTable* nm) {
    o << "    <axes>\n";
    for (auto& ax : f.axes) {
        o << "      <axis>\n        <axisTag value=\"" << tagToStr(ax.tag) << "\"/>\n";
        o << "        <minValue value=\"" << num(ax.minValue) << "\"/>\n";
        o << "        <defaultValue value=\"" << num(ax.defaultValue) << "\"/>\n";
        o << "        <maxValue value=\"" << num(ax.maxValue) << "\"/>\n";
        o << "        <nameID value=\"" << ax.nameID << "\"/>\n";
        o << "        <flags value=\"" << hexv(ax.flags, 4) << "\"/>\n";
        o << "      </axis>\n";
    }
    o << "    </axes>\n";
    o << "    <instances>\n";
    for (auto& inst : f.instances) {
        o << "      <instance subfamilyNameID=\"" << inst.nameID << "\">\n";
        o << "        <location>\n";
        for (size_t a = 0; a < f.axes.size() && a < inst.coords.size(); a++)
            o << "          <dimension tag=\"" << tagToStr(f.axes[a].tag) << "\" value=\""
              << num(inst.coords[a]) << "\"/>\n";
        o << "        </location>\n";
        o << "      </instance>\n";
    }
    o << "    </instances>\n";
}

static void dumpStat(std::ostringstream& o, const Stat& s) {
    o << "    <designAxes>\n";
    for (auto& ax : s.axes) {
        o << "      <axis>\n        <tag value=\"" << tagToStr(ax.tag) << "\"/>\n";
        o << "        <nameID value=\"" << ax.nameID << "\"/>\n";
        o << "        <axisOrdering value=\"" << ax.ordering << "\"/>\n";
        o << "      </axis>\n";
    }
    o << "    </designAxes>\n";
    for (auto& v : s.values) {
        o << "    <axisValue>\n      <tag value=\"" << tagToStr(v.tag) << "\"/>\n";
        o << "      <value value=\"" << num(v.value) << "\"/>\n";
        o << "      <valueNameID value=\"" << v.valueNameID << "\"/>\n";
        o << "    </axisValue>\n";
    }
}

static void dumpLayout(std::ostringstream& o, const LayoutTableInfo& t, const Font* f) {
    o << "    <scripts>\n";
    for (auto& sc : t.scripts) {
        o << "      <script tag=\"" << tagToStr(sc.tag) << "\">\n";
        auto dumpLS = [&](const char* kind, const LangSysRec& ls) {
            o << "        <" << kind << (ls.tag ? " tag=\"" + tagToStr(ls.tag) + "\"" : "") << ">\n";
            o << "          <reqFeature index=\"" << ls.reqFeatureIndex << "\"/>\n";
            for (uint16_t fi : ls.featureIndices)
                o << "          <feature index=\"" << fi << "\"/>\n";
            o << "        </" << kind << ">\n";
        };
        if (sc.defaultLangSys.featureIndices.size() || sc.defaultLangSys.reqFeatureIndex != 0xFFFF)
            dumpLS("defaultLangSys", sc.defaultLangSys);
        for (auto& ls : sc.langSys) dumpLS("langSys", ls);
        o << "      </script>\n";
    }
    o << "    </scripts>\n";
    o << "    <features>\n";
    for (auto& ft : t.features) {
        o << "      <feature tag=\"" << tagToStr(ft.tag) << "\">\n";
        for (uint16_t li : ft.lookupIndices)
            o << "        <lookup index=\"" << li << "\"/>\n";
        o << "      </feature>\n";
    }
    o << "    </features>\n";
    o << "    <lookups>\n";
    for (auto& lu : t.lookups) {
        o << "      <lookup type=\"" << lu.type << "\" flag=\"" << hexv(lu.flag, 4) << "\">\n";
        size_t covCount = 0;
        for (auto& cov : lu.coverages)
            covCount += cov.glyphs.size();
        o << "        <!-- " << t.lookupTypeName(lu.type) << ": " << covCount
          << " covered glyphs across " << lu.coverages.size() << " subtables -->\n";
        o << "      </lookup>\n";
    }
    o << "    </lookups>\n";
    (void)f;
}

static void dumpCffSummary(std::ostringstream& o, const CffFont& c, const Font* f) {
    o << "    <header majorVersion=\"" << (int)c.majorVersion() << "\"/>\n";
    o << "    <fontName value=\"" << xmlEscape(c.fontName) << "\"/>\n";
    o << "    <isCID value=\"" << (c.isCID ? 1 : 0) << "\"/>\n";
    o << "    <numGlyphs value=\"" << c.numGlyphs() << "\"/>\n";
    o << "    <numGlobalSubrs value=\"" << c.globalSubrs.count() << "\"/>\n";
    o << "    <numFonts value=\"" << c.fonts.size() << "\"/>\n";
    size_t ls = 0;
    for (size_t i = 0; i < c.fonts.size(); i++)
        if (const CffIndex* idx = c.localSubrs(i)) ls += idx->count();
    o << "    <numLocalSubrs value=\"" << ls << "\"/>\n";
    (void)f;
}

static void dumpKern(std::ostringstream& o, const Kern& k, const Font* f) {
    for (auto& st : k.subtables) {
        o << "    <kernsubtable coverage=\"" << hexv(st.coverage, 4) << "\" format=\""
          << st.format << "\" pairs=\"" << st.pairs.size() << "\">\n";
        size_t n = 0;
        for (auto& p : st.pairs) {
            if (n++ > 200) { o << "      <!-- truncated -->\n"; break; }
            std::string l = f ? f->glyphName(p.left) : std::to_string(p.left);
            std::string r = f ? f->glyphName(p.right) : std::to_string(p.right);
            o << "      <pair l=\"" << xmlEscape(l) << "\" r=\"" << xmlEscape(r) << "\" v=\""
              << p.value << "\"/>\n";
        }
        o << "    </kernsubtable>\n";
    }
}

// ------------------------------------------------------------------ main

static std::string xmlTableName(Tag t) {
    std::string s = tagToStr(t);
    std::string out;
    for (char c : s) {
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.')
            out += c;
        else if (c == ' ')
            out += '_';
        else
            out += '_';
    }
    if (out.empty() || !((out[0] >= 'A' && out[0] <= 'Z') || (out[0] >= 'a' && out[0] <= 'z') || out[0] == '_'))
        out = "t_" + out;
    return out;
}

std::string dumpTtx(const Font& font, const DumpOptions& opt) {
    std::ostringstream o;
    o << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    std::string ver = "\\x00\\x01\\x00\\x00";
    switch (font.sfnt().sfntVersion()) {
    case makeTag('O', 'T', 'T', 'O'): ver = "OTTO"; break;
    case makeTag('t', 'r', 'u', 'e'): ver = "true"; break;
    case makeTag('t', 't', 'c', 'f'): ver = "ttcf"; break;
    default: ver = "\\x00\\x01\\x00\\x00"; break;
    }
    o << "<ttFont sfntVersion=\"" << ver << "\" ttLibVersion=\"ftk-1.0\">\n";

    auto want = [&](Tag t) {
        if (opt.tables.empty()) return true;
        for (uint32_t x : opt.tables)
            if (x == t) return true;
        return false;
    };

    auto tags = font.tableTags();
    for (Tag t : tags) {
        if (!want(t)) continue;
        std::string name = xmlTableName(t);
        if (name == "OS_2") name = "OS_2";
        if (t == tag_Loca && !opt.tables.empty()) continue; // derived from glyf

        o << "  <" << name << ">\n";
        try {
            if (t == tag_Head) dumpHead(o, *font.head());
            else if (t == tag_Hhea) dumpHhea(o, *font.hhea(), "hhea");
            else if (t == tag_Vhea) dumpHhea(o, *font.vhea(), "vhea");
            else if (t == tag_Maxp) dumpMaxp(o, *font.maxp());
            else if (t == tag_Os2) dumpOs2(o, *font.os2());
            else if (t == tag_Post) dumpPost(o, *font.post());
            else if (t == tag_Name) dumpName(o, *font.nameTable());
            else if (t == tag_Cmap) dumpCmap(o, *font.cmap(), &font);
            else if (t == tag_Hmtx) dumpHmtx(o, *font.hmtx(), &font, font.numGlyphs());
            else if (t == tag_Vmtx) dumpHmtx(o, *font.vmtx(), &font, font.numGlyphs());
            else if (t == tag_Fvar) dumpFvar(o, *font.fvar(), font.nameTable());
            else if (t == tag_STAT) dumpStat(o, *font.stat());
            else if (t == tag_GSUB) dumpLayout(o, font.layout()->gsub, &font);
            else if (t == tag_GPOS) dumpLayout(o, font.layout()->gpos, &font);
            else if (t == tag_Kern) dumpKern(o, *font.kern(), &font);
            else if (t == tag_CFF || t == tag_CFF2) dumpCffSummary(o, *font.cff(), &font);
            else if (t == tag_Glyf) {
                if (!opt.skipGlyphs) dumpGlyf(o, font, font.glyf());
                else o << "    <!-- skipped -->\n";
            } else if (t == tag_Loca) {
                if (font.glyf()) {
                    auto& loca = font.glyf()->loca();
                    size_t n = 0;
                    for (uint32_t v : loca) {
                        if (n++ > 100000) break;
                        o << "    <!-- gid " << (n - 1) << ": " << v << " -->\n";
                    }
                }
            } else {
                // raw hex
                size_t len;
                const uint8_t* p = font.rawTable(t, len);
                if (p) {
                    size_t limit = std::min<size_t>(len, 2048);
                    for (size_t i = 0; i < limit; i += 16) {
                        o << "    ";
                        for (size_t k = i; k < std::min(i + 16, limit); k++)
                            o << hexv(p[k], 2).substr(2) << " ";
                        o << "\n";
                    }
                    if (len > limit) o << "    <!-- truncated (" << len << " bytes) -->\n";
                }
            }
        } catch (const Error& e) {
            o << "    <!-- parse error: " << xmlEscape(e.what()) << " -->\n";
        }
        o << "  </" << name << ">\n";
    }
    o << "</ttFont>\n";
    return o.str();
}

// ------------------------------------------------------------------ json

static std::string jsonEscape(const std::string& s) { return xmlEscape(s); }

std::string dumpInfoJson(const Font& font) {
    FontInfo fi = font.info();
    std::ostringstream o;
    o << "{\n";
    o << "  \"path\": \"" << jsonEscape(fi.path) << "\",\n";
    o << "  \"family\": \"" << jsonEscape(fi.family) << "\",\n";
    o << "  \"subfamily\": \"" << jsonEscape(fi.subfamily) << "\",\n";
    o << "  \"fullName\": \"" << jsonEscape(fi.fullName) << "\",\n";
    o << "  \"postScriptName\": \"" << jsonEscape(fi.postScriptName) << "\",\n";
    o << "  \"version\": \"" << jsonEscape(fi.version) << "\",\n";
    o << "  \"flavor\": \"" << fi.flavor << "\",\n";
    o << "  \"numFaces\": " << fi.numFaces << ",\n";
    o << "  \"faceIndex\": " << fi.faceIndex << ",\n";
    o << "  \"unitsPerEm\": " << fi.unitsPerEm << ",\n";
    o << "  \"numGlyphs\": " << fi.numGlyphs << ",\n";
    o << "  \"ascender\": " << fi.ascender << ", \"descender\": " << fi.descender
      << ", \"lineGap\": " << fi.lineGap << ",\n";
    o << "  \"weightClass\": " << fi.weightClass << ", \"widthClass\": " << fi.widthClass << ",\n";
    o << "  \"bold\": " << (fi.isBold ? "true" : "false")
      << ", \"italic\": " << (fi.isItalic ? "true" : "false") << ",\n";
    o << "  \"fixedPitch\": " << (fi.fixedPitch ? "true" : "false") << ",\n";
    o << "  \"italicAngle\": " << num(fi.italicAngle) << ",\n";
    o << "  \"vendor\": \"" << jsonEscape(fi.vendor) << "\",\n";
    o << "  \"bounds\": {\"xMin\": " << fi.bounds.xMin << ", \"yMin\": " << fi.bounds.yMin
      << ", \"xMax\": " << fi.bounds.xMax << ", \"yMax\": " << fi.bounds.yMax << "},\n";
    o << "  \"cmapMappings\": " << fi.cmapMappings << ",\n";
    o << "  \"cmapMin\": " << fi.cmapMin << ", \"cmapMax\": " << fi.cmapMax << ",\n";
    o << "  \"cmapBestFormat\": " << fi.cmapBestFormat << ",\n";
    o << "  \"hasVertical\": " << (fi.hasVertical ? "true" : "false") << ",\n";
    o << "  \"isVariable\": " << (fi.isVariable ? "true" : "false") << ",\n";
    o << "  \"numAxes\": " << fi.numAxes << ", \"numInstances\": " << fi.numInstances << ",\n";
    o << "  \"tables\": [";
    for (size_t i = 0; i < fi.tableTags.size(); i++) {
        if (i) o << ", ";
        o << "\"" << tagToStr(fi.tableTags[i]) << "\"";
    }
    o << "],\n";
    o << "  \"fileSize\": " << fi.fileSize << ",\n";
    o << "  \"checksumsVerified\": " << (fi.checksumsVerified ? "true" : "false") << ",\n";
    o << "  \"checksumsOk\": " << (fi.checksumsOk ? "true" : "false") << "\n";
    o << "}";
    return o.str();
}

std::string dumpInfoText(const Font& font) {
    FontInfo fi = font.info(true); // human-facing: verify checksums
    std::ostringstream o;
    o << "File:          " << fi.path << "\n";
    o << "Family:        " << fi.family << "\n";
    o << "Subfamily:     " << fi.subfamily << "\n";
    o << "Full name:     " << fi.fullName << "\n";
    o << "PostScript:    " << fi.postScriptName << "\n";
    o << "Version:       " << fi.version << "\n";
    o << "Flavor:        " << fi.flavor << "\n";
    o << "Faces:         " << fi.numFaces << (fi.numFaces > 1 ? " (index " + std::to_string(fi.faceIndex) + ")" : "") << "\n";
    o << "Units/em:      " << fi.unitsPerEm << "\n";
    o << "Glyphs:        " << fi.numGlyphs << "\n";
    o << "Ascender:      " << fi.ascender << "\n";
    o << "Descender:     " << fi.descender << "\n";
    o << "Line gap:      " << fi.lineGap << "\n";
    o << "Weight/Width:  " << fi.weightClass << " / " << fi.widthClass << "\n";
    o << "Fixed pitch:   " << (fi.fixedPitch ? "yes" : "no") << "\n";
    o << "Italic angle:  " << num(fi.italicAngle) << "\n";
    o << "Vendor:        " << fi.vendor << "\n";
    o << "Bounds:        " << fi.bounds.xMin << "," << fi.bounds.yMin << " - "
      << fi.bounds.xMax << "," << fi.bounds.yMax << "\n";
    o << "cmap:          " << fi.cmapMappings << " mappings (format " << fi.cmapBestFormat
      << ", U+" << hexv(fi.cmapMin) << "..U+" << hexv(fi.cmapMax) << ")\n";
    o << "Vertical:      " << (fi.hasVertical ? "yes" : "no") << "\n";
    o << "Variable:      " << (fi.isVariable ? "yes" : "no");
    if (fi.isVariable) o << " (" << fi.numAxes << " axes, " << fi.numInstances << " instances)";
    o << "\n";
    o << "Tables:        " << fi.tableTags.size() << "\n";
    o << "File size:     " << fi.fileSize << "\n";
    o << "Checksums:     " << (fi.checksumsOk ? "OK" : "MISMATCH: " + fi.checksumError) << "\n";
    return o.str();
}

} // namespace ftk
