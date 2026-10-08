// ftk: core sfnt tables — head, hhea, maxp, hmtx/vmtx, OS/2, post, name,
// cmap, gasp, kern, VORG, cvt/fpgm/prep.
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Bytes.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ftk {

struct Bounds {
    int16_t xMin = 0, yMin = 0, xMax = 0, yMax = 0;
    bool valid = false;
};

// ---------------------------------------------------------------- head
struct Head {
    uint16_t majorVersion = 1, minorVersion = 0;
    double fontRevision = 1.0;
    uint32_t checkSumAdjustment = 0, magicNumber = 0x5F0F3CF5;
    uint16_t flags = 0;
    uint16_t unitsPerEm = 1000;
    int64_t created = 0, modified = 0;
    int16_t xMin = 0, yMin = 0, xMax = 0, yMax = 0;
    uint16_t macStyle = 0, lowestRecPPEM = 8;
    int16_t fontDirectionHint = 2, indexToLocFormat = 0, glyphDataFormat = 0;

    void parse(Reader r);
};

// ---------------------------------------------------------------- hhea / vhea
struct Hhea {
    uint16_t majorVersion = 1, minorVersion = 0;
    int16_t ascender = 0, descender = 0, lineGap = 0;
    uint16_t advanceWidthMax = 0;
    int16_t minLeftSideBearing = 0, minRightSideBearing = 0;
    int16_t xMaxExtent = 0;
    int16_t caretSlopeRise = 1, caretSlopeRun = 0, caretOffset = 0;
    int16_t reserved[4] = {0, 0, 0, 0};
    int16_t metricDataFormat = 0;
    uint16_t numberOfHMetrics = 0;
    bool vertical = false; // true when parsed from vhea

    void parse(Reader r);
};

// ---------------------------------------------------------------- maxp
struct Maxp {
    uint16_t majorVersion = 1, minorVersion = 0;
    uint16_t numGlyphs = 0;
    // v1.0 fields
    uint16_t maxPoints = 0, maxContours = 0, maxCompositePoints = 0, maxCompositeContours = 0;
    uint16_t maxZones = 2, maxTwilightPoints = 0, maxStorage = 0, maxFunctionDefs = 0;
    uint16_t maxInstructionDefs = 0, maxStackElements = 0, maxSizeOfInstructions = 0;
    uint16_t maxComponentElements = 0, maxComponentDepth = 0;

    void parse(Reader r);
};

// ---------------------------------------------------------------- hmtx / vmtx
struct Hmtx {
    std::vector<uint16_t> advances; // advanceWidth (or height for vmtx)
    std::vector<int16_t> bearings;  // lsb / tsb
    bool vertical = false;

    void parse(Reader r, uint16_t numberOfMetrics, uint16_t numGlyphs);

    uint16_t advance(uint16_t gid, uint16_t numGlyphs = 0) const {
        (void)numGlyphs;
        if (advances.empty()) return 0;
        if (gid < advances.size()) return advances[gid];
        return advances.back();
    }
    int16_t bearing(uint16_t gid) const {
        if (gid < bearings.size()) return bearings[gid];
        return bearings.empty() ? 0 : bearings.back();
    }
};

// ---------------------------------------------------------------- OS/2
struct Os2 {
    uint16_t version = 0;
    int16_t xAvgCharWidth = 0;
    uint16_t usWeightClass = 400, usWidthClass = 5;
    uint16_t fsType = 0;
    int16_t ySubscriptXSize = 0, ySubscriptYSize = 0, ySubscriptXOffset = 0, ySubscriptYOffset = 0;
    int16_t ySuperscriptXSize = 0, ySuperscriptYSize = 0, ySuperscriptXOffset = 0, ySuperscriptYOffset = 0;
    int16_t yStrikeoutSize = 0, yStrikeoutPosition = 0;
    int16_t sFamilyClass = 0;
    uint8_t panose[10] = {0};
    uint32_t ulUnicodeRange1 = 0, ulUnicodeRange2 = 0, ulUnicodeRange3 = 0, ulUnicodeRange4 = 0;
    std::string achVendID;
    uint16_t fsSelection = 0;
    uint16_t usFirstCharIndex = 0, usLastCharIndex = 0;
    int16_t sTypoAscender = 0, sTypoDescender = 0, sTypoLineGap = 0;
    uint16_t usWinAscent = 0, usWinDescent = 0;
    uint32_t ulCodePageRange1 = 0, ulCodePageRange2 = 0;
    int16_t sxHeight = 0, sCapHeight = 0;
    uint16_t usDefaultChar = 0, usBreakChar = 0, usMaxContext = 0;
    uint16_t usLowerOpticalPointSize = 0, usUpperOpticalPointSize = 0;

    void parse(Reader r);
};

// ---------------------------------------------------------------- post
struct Post {
    double version = 3.0;
    double italicAngle = 0;
    int16_t underlinePosition = 0, underlineThickness = 0;
    uint32_t isFixedPitch = 0;
    uint32_t minMemType42 = 0, maxMemType42 = 0, minMemType1 = 0, maxMemType1 = 0;

    // v2.0 glyph names
    std::vector<uint32_t> indices;          // gid -> index (258 std + extraNames)
    std::vector<std::string> extraNames;    // pascal strings after index array

    void parse(Reader r);
    std::string glyphName(uint16_t gid) const; // "" when not available
};

const char* standardMacGlyphName(uint16_t i);
size_t numStandardMacGlyphNames();

// ---------------------------------------------------------------- name
struct NameRecord {
    uint16_t platformID, encodingID, languageID, nameID;
    uint16_t length, offset; // in string storage
    std::string decoded;    // UTF-8
    std::string langTag;    // for platform 0 language-tag records
};

struct NameTable {
    uint16_t format = 0;
    std::vector<NameRecord> records;
    std::vector<uint8_t> storage;

    void parse(Reader r);
    // Best-effort lookup: prefer Windows Unicode, then Mac Roman, then any.
    std::string get(uint16_t nameID) const;
    std::vector<std::string> getAll(uint16_t nameID) const;
    const NameRecord* bestRecord(uint16_t nameID) const;

    static std::string langTagFor(uint16_t lid,
                                  const std::vector<std::pair<uint16_t, std::string>>& tags);
};

// name IDs
enum NameId : uint16_t {
    NAME_COPYRIGHT = 0, NAME_FAMILY = 1, NAME_SUBFAMILY = 2, NAME_UNIQUEID = 3,
    NAME_FULLNAME = 4, NAME_VERSION = 5, NAME_POSTSCRIPT = 6, NAME_TRADEMARK = 7,
    NAME_MANUFACTURER = 8, NAME_DESIGNER = 9, NAME_DESCRIPTION = 10,
    NAME_VENDOR_URL = 11, NAME_DESIGNER_URL = 12, NAME_LICENSE = 13,
    NAME_LICENSE_URL = 14, NAME_TYPO_FAMILY = 16, NAME_TYPO_SUBFAMILY = 17,
    NAME_COMPAT_FULL = 18, NAME_SAMPLE_TEXT = 19, NAME_POSTSCRIPT_CID = 20,
    NAME_WWS_FAMILY = 21, NAME_WWS_SUBFAMILY = 22, NAME_LIGHT_BACKGROUND = 23,
    NAME_DARK_BACKGROUND = 24, NAME_VARIATIONS_PS_PREFIX = 25,
};

// ---------------------------------------------------------------- cmap
struct CmapSubtable {
    uint16_t platformID = 0, encodingID = 0, format = 0, language = 0;
    uint32_t offset = 0;
    bool isUnicode = false;
    bool isSymbol = false;
    // decoded maps (for formats 0/4/6/12/13)
    std::map<uint32_t, uint16_t> map; // codepoint -> gid
    // format 14 (Unicode Variation Sequences)
    std::vector<std::pair<uint32_t, uint32_t>> uvs; // (base cp, selector) -> via uvsMap
    std::map<std::pair<uint32_t, uint32_t>, uint16_t> uvsMap;

    void parse(Reader r);

    uint16_t glyph(uint32_t cp) const;         // 0 if not mapped
    bool nextRange(uint32_t cp, uint32_t& outCp, uint16_t& outGid) const; // first entry >= cp
    std::vector<std::pair<uint32_t, uint16_t>> entries() const;
};

struct Cmap {
    uint16_t version = 0;
    std::vector<CmapSubtable> subtables;
    CmapSubtable format14; // variation sequences, if present

    void parse(Reader r);
    // fontTools-style getBestCmap selection
    const CmapSubtable* best() const;
    uint16_t glyph(uint32_t cp) const; // via best
    std::vector<std::pair<uint32_t, uint16_t>> bestEntries() const;
    // all subtables lookup (first hit)
    uint16_t glyphAny(uint32_t cp) const;
};

// ---------------------------------------------------------------- gasp
struct GaspRecord { uint16_t rangeMaxPPEM; uint16_t rangeGaspBehavior; };
struct Gasp {
    uint16_t version = 0;
    std::vector<GaspRecord> records;
    void parse(Reader r);
};

// ---------------------------------------------------------------- kern
struct KernPair { uint16_t left, right; int16_t value; };
struct KernSubtable {
    uint16_t version = 0, length = 0, coverage = 0, format = 0;
    bool apple = false; // version 1.0 (Apple) vs 0 (Microsoft)
    std::vector<KernPair> pairs; // format 0
    uint16_t nTables = 0; // for apple format
};
struct Kern {
    uint16_t version = 0;
    std::vector<KernSubtable> subtables;
    void parse(Reader r);
    int16_t kern(uint16_t left, uint16_t right) const; // first subtable with a hit
};

// ---------------------------------------------------------------- VORG
struct VORG {
    uint16_t majorVersion = 1, minorVersion = 0;
    int16_t defaultVertOriginY = 0;
    std::vector<std::pair<uint16_t, int16_t>> vertOriginYMetrics; // gid -> y
    void parse(Reader r);
    int16_t originY(uint16_t gid) const;
};

// raw passthrough tables we don't interpret
struct RawTable {
    Tag tag = 0;
    std::vector<uint8_t> data;
};

} // namespace ftk
