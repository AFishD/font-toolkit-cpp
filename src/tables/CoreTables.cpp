// Core table parsers.
#include "ftk/Tables.hpp"
#include "ftk/Platform.hpp"

#include <algorithm>
#include <cmath>

namespace ftk {

// ---------------------------------------------------------------- head

void Head::parse(Reader r) {
    majorVersion = r.u16();
    minorVersion = r.u16();
    fontRevision = r.fixed16();
    checkSumAdjustment = r.u32();
    magicNumber = r.u32();
    if (magicNumber != 0x5F0F3CF5) fail("head: bad magic number");
    flags = r.u16();
    unitsPerEm = r.u16();
    if (unitsPerEm == 0) fail("head: unitsPerEm is zero");
    created = r.i64();
    modified = r.i64();
    xMin = r.i16(); yMin = r.i16(); xMax = r.i16(); yMax = r.i16();
    macStyle = r.u16();
    lowestRecPPEM = r.u16();
    fontDirectionHint = r.i16();
    indexToLocFormat = r.i16();
    glyphDataFormat = r.i16();
}

// ---------------------------------------------------------------- hhea

void Hhea::parse(Reader r) {
    majorVersion = r.u16();
    minorVersion = r.u16();
    ascender = r.i16();
    descender = r.i16();
    lineGap = r.i16();
    advanceWidthMax = r.u16();
    minLeftSideBearing = r.i16();
    minRightSideBearing = r.i16();
    xMaxExtent = r.i16();
    caretSlopeRise = r.i16();
    caretSlopeRun = r.i16();
    caretOffset = r.i16();
    for (int i = 0; i < 4; i++) reserved[i] = r.i16();
    metricDataFormat = r.i16();
    numberOfHMetrics = r.u16();
}

// ---------------------------------------------------------------- maxp

void Maxp::parse(Reader r) {
    majorVersion = r.u16();
    minorVersion = r.u16();
    numGlyphs = r.u16();
    if (majorVersion == 1) {
        maxPoints = r.u16(); maxContours = r.u16();
        maxCompositePoints = r.u16(); maxCompositeContours = r.u16();
        maxZones = r.u16(); maxTwilightPoints = r.u16();
        maxStorage = r.u16(); maxFunctionDefs = r.u16();
        maxInstructionDefs = r.u16(); maxStackElements = r.u16();
        maxSizeOfInstructions = r.u16(); maxComponentElements = r.u16();
        maxComponentDepth = r.u16();
    }
}

// ---------------------------------------------------------------- hmtx

void Hmtx::parse(Reader r, uint16_t numberOfMetrics, uint16_t numGlyphs) {
    if (numGlyphs == 0) numGlyphs = 0xFFFF;
    if (numberOfMetrics > numGlyphs) numberOfMetrics = numGlyphs;
    // Also clamp by available bytes
    size_t availPairs = r.remaining() / 4;
    if (numberOfMetrics > availPairs) numberOfMetrics = (uint16_t)availPairs;

    advances.reserve(numberOfMetrics);
    bearings.reserve(numberOfMetrics);
    for (uint16_t i = 0; i < numberOfMetrics; i++) {
        advances.push_back(r.u16());
        bearings.push_back(r.i16());
    }
    size_t extra = numGlyphs > numberOfMetrics ? numGlyphs - numberOfMetrics : 0;
    extra = std::min<size_t>(extra, r.remaining() / 2);
    for (size_t i = 0; i < extra; i++)
        bearings.push_back(r.i16());
}

// ---------------------------------------------------------------- OS/2

void Os2::parse(Reader r) {
    version = r.u16();
    xAvgCharWidth = r.i16();
    usWeightClass = r.u16();
    usWidthClass = r.u16();
    fsType = r.u16();
    ySubscriptXSize = r.i16(); ySubscriptYSize = r.i16();
    ySubscriptXOffset = r.i16(); ySubscriptYOffset = r.i16();
    ySuperscriptXSize = r.i16(); ySuperscriptYSize = r.i16();
    ySuperscriptXOffset = r.i16(); ySuperscriptYOffset = r.i16();
    yStrikeoutSize = r.i16(); yStrikeoutPosition = r.i16();
    sFamilyClass = r.i16();
    r.copyTo(panose, 10);
    ulUnicodeRange1 = r.u32(); ulUnicodeRange2 = r.u32();
    ulUnicodeRange3 = r.u32(); ulUnicodeRange4 = r.u32();
    char vid[4];
    r.copyTo(vid, 4);
    achVendID.assign(vid, vid + 4);
    fsSelection = r.u16();
    usFirstCharIndex = r.u16(); usLastCharIndex = r.u16();
    sTypoAscender = r.i16(); sTypoDescender = r.i16(); sTypoLineGap = r.i16();
    usWinAscent = r.u16(); usWinDescent = r.u16();
    if (version >= 1 && r.remaining() >= 8) {
        ulCodePageRange1 = r.u32(); ulCodePageRange2 = r.u32();
    }
    if (version >= 2 && r.remaining() >= 10) {
        sxHeight = r.i16(); sCapHeight = r.i16();
        usDefaultChar = r.u16(); usBreakChar = r.u16(); usMaxContext = r.u16();
    }
    if (version >= 5 && r.remaining() >= 4) {
        usLowerOpticalPointSize = r.u16(); usUpperOpticalPointSize = r.u16();
    }
}

// ---------------------------------------------------------------- post

static const char* kStdMacNames[] = {
    ".notdef", ".null", "nonmarkingreturn", "space", "exclam", "quotedbl", "numbersign",
    "dollar", "percent", "ampersand", "quotesingle", "parenleft", "parenright", "asterisk",
    "plus", "comma", "hyphen", "period", "slash", "zero", "one", "two", "three", "four",
    "five", "six", "seven", "eight", "nine", "colon", "semicolon", "less", "equal",
    "greater", "question", "at", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K",
    "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
    "bracketleft", "backslash", "bracketright", "asciicircum", "underscore", "grave",
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q",
    "r", "s", "t", "u", "v", "w", "x", "y", "z", "braceleft", "bar", "braceright",
    "asciitilde", "Adieresis", "Aring", "Ccedilla", "Eacute", "Ntilde", "Odieresis",
    "Udieresis", "aacute", "agrave", "acircumflex", "adieresis", "atilde", "aring",
    "ccedilla", "eacute", "egrave", "ecircumflex", "edieresis", "iacute", "igrave",
    "icircumflex", "idieresis", "ntilde", "oacute", "ograve", "ocircumflex", "odieresis",
    "otilde", "uacute", "ugrave", "ucircumflex", "udieresis", "dagger", "degree",
    "cent", "sterling", "section", "bullet", "paragraph", "germandbls", "registered",
    "copyright", "trademark", "acute", "dieresis", "notequal", "AE", "Oslash",
    "infinity", "plusminus", "lessequal", "greaterequal", "yen", "mu", "partialdiff",
    "summation", "product", "pi", "integral", "ordfeminine", "ordmasculine", "Omega",
    "ae", "oslash", "questiondown", "exclamdown", "logicalnot", "radical", "florin",
    "approxequal", "Delta", "guillemotleft", "guillemotright", "ellipsis", "nonbreakingspace",
    "Agrave", "Atilde", "Otilde", "OE", "oe", "endash", "emdash", "quotedblleft",
    "quotedblright", "quoteleft", "quoteright", "divide", "lozenge", "ydieresis",
    "Ydieresis", "fraction", "currency", "guilsinglleft", "guilsinglright", "fi", "fl",
    "daggerdbl", "periodcentered", "quotesinglbase", "quotedblbase", "perthousand",
    "Acircumflex", "Ecircumflex", "Aacute", "Edieresis", "Egrave", "Iacute",
    "Icircumflex", "Idieresis", "Igrave", "Oacute", "Ocircumflex", "apple", "Ograve",
    "Uacute", "Ucircumflex", "Ugrave", "dotlessi", "circumflex", "tilde", "macron",
    "breve", "dotaccent", "ring", "cedilla", "hungarumlaut", "ogonek", "caron",
    "Lslash", "lslash", "Scaron", "scaron", "Zcaron", "zcaron", "brokenbar", "Eth",
    "eth", "Yacute", "yacute", "Thorn", "thorn", "minus", "multiply", "onesuperior",
    "twosuperior", "threesuperior", "onehalf", "onequarter", "threequarters",
    "franc", "Gbreve", "gbreve", "Idotaccent", "Scedilla", "scedilla", "Cacute",
    "cacute", "Ccaron", "ccaron", "dcroat"
};
static const size_t kNumStdMacNames = sizeof(kStdMacNames) / sizeof(kStdMacNames[0]);

const char* standardMacGlyphName(uint16_t i) {
    if (i < kNumStdMacNames) return kStdMacNames[i];
    return "";
}
size_t numStandardMacGlyphNames() { return kNumStdMacNames; }

void Post::parse(Reader r) {
    uint32_t verRaw = r.u32();
    version = (float)((int32_t)verRaw) / 65536.0;
    italicAngle = r.fixed16();
    underlinePosition = r.i16();
    underlineThickness = r.i16();
    isFixedPitch = r.u32();
    minMemType42 = r.u32(); maxMemType42 = r.u32();
    minMemType1 = r.u32(); maxMemType1 = r.u32();

    if (verRaw == 0x00020000) {
        uint16_t numGlyphs = r.u16();
        indices.reserve(numGlyphs);
        for (uint16_t i = 0; i < numGlyphs; i++)
            indices.push_back(r.u16());
        while (!r.eof()) {
            uint8_t len = r.u8();
            if (r.remaining() < len) break;
            extraNames.push_back(r.str(len));
        }
    }
}

std::string Post::glyphName(uint16_t gid) const {
    uint32_t verRaw = (uint32_t)std::lround(version * 65536.0);
    if (verRaw == 0x00020000) {
        if (gid >= indices.size()) return std::string();
        uint32_t idx = indices[gid];
        if (idx < 258) return standardMacGlyphName((uint16_t)idx);
        uint32_t e = idx - 258;
        if (e < extraNames.size()) return extraNames[e];
        return std::string();
    }
    if (verRaw == 0x00010000) {
        if (gid < 258) return standardMacGlyphName(gid);
        return std::string();
    }
    return std::string(); // v3: no names
}

// ---------------------------------------------------------------- name

void NameTable::parse(Reader r) {
    format = r.u16();
    uint16_t count = r.u16();
    uint16_t stringOffset = r.u16();

    struct RawRec { uint16_t pid, eid, lid, nid, len, off; };
    std::vector<RawRec> recs;
    for (uint16_t i = 0; i < count; i++) {
        RawRec q;
        q.pid = r.u16(); q.eid = r.u16(); q.lid = r.u16(); q.nid = r.u16();
        q.len = r.u16(); q.off = r.u16();
        recs.push_back(q);
    }
    std::vector<std::pair<uint16_t, std::string>> langTags; // (index, tag)
    if (format == 1) {
        uint16_t numLangTags = r.u16();
        for (uint16_t i = 0; i < numLangTags; i++)
            langTags.emplace_back(r.u16(), std::string());
    }

    // String storage: [stringOffset, table end)
    size_t storeBase = std::min<size_t>(stringOffset, r.size());
    const uint8_t* store = r.data() + storeBase;
    size_t storeSize = r.size() - storeBase;
    storage.assign(store, store + storeSize);

    // decode language tags (UTF-16BE, length-prefixed)
    for (auto& lt : langTags) {
        size_t p = lt.first;
        if (p + 2 > storage.size()) continue;
        uint16_t len = (uint16_t)((storage[p] << 8) | storage[p + 1]);
        if (p + 2 + len * 2 <= storage.size() + 1 && p + 2 + (size_t)len * 2 <= storage.size()) {
            lt.second = utf16BEToUtf8(storage.data() + p + 2, (size_t)len * 2);
        }
    }

    for (auto& q : recs) {
        NameRecord rec;
        rec.platformID = q.pid;
        rec.encodingID = q.eid;
        rec.languageID = q.lid;
        rec.nameID = q.nid;
        rec.length = q.len;
        rec.offset = q.off;
        if (q.off > storage.size()) continue;
        size_t len = std::min<size_t>(q.len, storage.size() - q.off);
        const uint8_t* p = storage.data() + q.off;
        if (q.pid == 0) {
            rec.langTag = langTagFor(q.lid, langTags);
            if (q.eid == 4 || q.eid == 6)
                rec.decoded = ""; // UTF-32BE — rare; drop
            else
                rec.decoded = utf16BEToUtf8(p, len);
        } else if (q.pid == 3) {
            if (q.eid == 1 || q.eid == 3 || q.eid == 5 || q.eid == 10)
                rec.decoded = utf16BEToUtf8(p, len);
            else if (q.eid == 4)
                rec.decoded = ""; // UTF-32BE
            else
                rec.decoded = windows1252ToUtf8(p, len);
        } else if (q.pid == 1) {
            rec.decoded = macRomanToUtf8(p, len);
        } else {
            rec.decoded = windows1252ToUtf8(p, len);
        }
        records.push_back(std::move(rec));
    }
}

std::string NameTable::langTagFor(uint16_t lid, const std::vector<std::pair<uint16_t, std::string>>& tags) {
    for (auto& t : tags)
        if (t.first == lid) return t.second;
    return std::string();
}

const NameRecord* NameTable::bestRecord(uint16_t nameID) const {
    const NameRecord* best = nullptr;
    int bestScore = -1;
    for (auto& r : records) {
        if (r.nameID != nameID || r.decoded.empty()) continue;
        int score;
        if (r.platformID == 3 && r.encodingID == 1 && r.languageID == 0x409) score = 100;
        else if (r.platformID == 3 && r.encodingID == 1) score = 90;
        else if (r.platformID == 3 && r.encodingID == 10) score = 85;
        else if (r.platformID == 0) score = 80;
        else if (r.platformID == 3) score = 60;
        else if (r.platformID == 1 && r.languageID == 0) score = 50;
        else score = 10;
        if (score > bestScore) { bestScore = score; best = &r; }
    }
    return best;
}

std::string NameTable::get(uint16_t nameID) const {
    const NameRecord* r = bestRecord(nameID);
    return r ? r->decoded : std::string();
}

std::vector<std::string> NameTable::getAll(uint16_t nameID) const {
    std::vector<std::string> out;
    for (auto& r : records)
        if (r.nameID == nameID && !r.decoded.empty())
            out.push_back(r.decoded);
    return out;
}

// ---------------------------------------------------------------- cmap

void CmapSubtable::parse(Reader r) {
    format = r.u16();
    switch (format) {
    case 0: {
        r.u16(); // length
        language = r.u16();
        for (int i = 0; i < 256 && r.remaining() >= 1; i++) {
            uint8_t g = r.u8();
            if (g) map[i] = g;
        }
        break;
    }
    case 2: {
        r.u16(); // length
        language = r.u16();
        uint16_t subHeaderKeys[256];
        uint32_t maxSub = 0;
        size_t subBase = r.offset(); // start of subHeaderKeys[]
        for (int i = 0; i < 256; i++) {
            subHeaderKeys[i] = (uint16_t)(r.u16() / 8);
            if (subHeaderKeys[i] > maxSub) maxSub = subHeaderKeys[i];
        }
        for (uint32_t b = 0; b < 256; b++) {
            uint32_t s = subHeaderKeys[b];
            size_t subPos = subBase + (size_t)s * 8;
            if (subPos + 8 > r.size()) continue;
            Reader sr = r.subAt(subPos, 8);
            uint16_t firstCode = sr.u16();
            uint16_t entryCount = sr.u16();
            int16_t idDelta = sr.i16();
            uint16_t idRangeOffset = sr.u16();
            for (uint16_t k = 0; k < entryCount; k++) {
                uint16_t low = (uint16_t)(firstCode + k);
                // glyph index: located at subPos+6 (idRangeOffset field) + idRangeOffset + 2k
                size_t gpos = subPos + 6 + idRangeOffset + 2 * (size_t)k;
                if (gpos + 2 > r.size()) continue;
                uint16_t gid = (uint16_t)((r.data()[gpos] << 8) | r.data()[gpos + 1]);
                if (gid == 0) continue;
                uint16_t out = (uint16_t)(gid + idDelta);
                uint32_t cp = (s == 0) ? b : ((b << 8) | low);
                map[cp] = out;
            }
        }
        break;
    }
    case 4: {
        r.u16(); // length
        language = r.u16();
        uint16_t segCountX2 = r.u16();
        uint16_t segCount = segCountX2 / 2;
        r.u16(); r.u16(); r.u16(); // searchRange, entrySelector, rangeShift
        if ((uint64_t)segCount * 2 + 2 > r.remaining()) fail("cmap4: truncated segment arrays");
        std::vector<uint32_t> endCode(segCount), startCode(segCount);
        std::vector<int16_t> idDelta(segCount);
        std::vector<uint16_t> idRangeOffset(segCount);
        for (auto& e : endCode) e = r.u16();
        r.u16(); // reservedPad
        for (auto& s : startCode) s = r.u16();
        for (auto& d : idDelta) d = r.i16();
        for (auto& o : idRangeOffset) o = r.u16();
        size_t idRangeArrayBase = r.offset(); // glyphIdArray start

        for (uint16_t seg = 0; seg < segCount; seg++) {
            uint32_t sc = startCode[seg], ec = std::min<uint32_t>(endCode[seg], 0x10FFFF);
            if (sc > ec) continue;
            if (ec == 0xFFFF && sc == 0xFFFF) continue;
            for (uint32_t cp = sc;; cp++) {
                if (idRangeOffset[seg] == 0) {
                    map[cp] = (uint16_t)(cp + idDelta[seg]);
                } else {
                    // idRangeOffset is relative to the idRangeOffset[seg] field address
                    size_t fieldAddr = idRangeArrayBase - (size_t)segCount * 2 + (size_t)seg * 2;
                    size_t pos = fieldAddr + idRangeOffset[seg] + 2 * (cp - sc);
                    if (pos + 2 <= r.size()) {
                        uint16_t gid = (uint16_t)((r.data()[pos] << 8) | r.data()[pos + 1]);
                        if (gid) map[cp] = (uint16_t)(gid + idDelta[seg]);
                    }
                }
                if (cp == ec) break;
            }
        }
        break;
    }
    case 6: {
        r.u16(); // length
        language = r.u16();
        uint16_t firstCode = r.u16();
        uint16_t entryCount = r.u16();
        for (uint16_t i = 0; i < entryCount && r.remaining() >= 2; i++) {
            uint16_t gid = r.u16();
            if (gid) map[firstCode + i] = gid;
        }
        break;
    }
    case 12:
    case 13: {
        r.u16();          // reserved
        r.u32();          // length
        language = r.u32();
        uint32_t nGroups = r.u32();
        if ((uint64_t)nGroups * 12 > (uint64_t)r.remaining())
            nGroups = (uint32_t)(r.remaining() / 12);
        for (uint32_t g = 0; g < nGroups; g++) {
            uint32_t start = r.u32(), end = r.u32(), val = r.u32();
            if (start > 0x10FFFF || end < start || end > 0x10FFFF) continue;
            if (format == 12) {
                for (uint32_t cp = start; cp <= end; cp++)
                    if (val + (cp - start) != 0) // value 0 = unmapped for format 12 when result is 0? keep 0 mapping out
                        map[cp] = (uint16_t)(val + (cp - start));
            } else {
                if (val == 0) continue;
                for (uint32_t cp = start; cp <= end; cp++)
                    map[cp] = (uint16_t)val;
            }
        }
        break;
    }
    case 14: {
        r.u32(); // length
        uint32_t numVarSelectorRecords = r.u32();
        for (uint32_t i = 0; i < numVarSelectorRecords && r.remaining() >= 11; i++) {
            uint32_t varSelector = r.u24();
            uint32_t defaultUVSOffset = r.u32();
            uint32_t nonDefaultUVSOffset = r.u32();
            if (defaultUVSOffset && defaultUVSOffset < r.size()) {
                Reader d = r.subAt(defaultUVSOffset, r.size() - defaultUVSOffset);
                uint32_t numRanges = d.u32();
                for (uint32_t k = 0; k < numRanges && d.remaining() >= 4; k++) {
                    uint32_t start = d.u24();
                    uint8_t additionalCount = d.u8();
                    for (uint32_t cp = start; cp <= start + additionalCount && cp <= 0x10FFFF; cp++) {
                        auto it = map.find(cp);
                        uvsMap[{cp, varSelector}] = it != map.end() ? it->second : 0;
                    }
                }
            }
            if (nonDefaultUVSOffset && nonDefaultUVSOffset < r.size()) {
                Reader d = r.subAt(nonDefaultUVSOffset, r.size() - nonDefaultUVSOffset);
                uint32_t numUVSMappings = d.u32();
                for (uint32_t k = 0; k < numUVSMappings && d.remaining() >= 5; k++) {
                    uint32_t unicodeValue = d.u24();
                    uint16_t gid = d.u16();
                    uvsMap[{unicodeValue, varSelector}] = gid;
                }
            }
        }
        break;
    }
    default:
        break;
    }
}

uint16_t CmapSubtable::glyph(uint32_t cp) const {
    auto it = map.find(cp);
    return it != map.end() ? it->second : 0;
}

std::vector<std::pair<uint32_t, uint16_t>> CmapSubtable::entries() const {
    return std::vector<std::pair<uint32_t, uint16_t>>(map.begin(), map.end());
}

void Cmap::parse(Reader r) {
    version = r.u16();
    uint16_t numTables = r.u16();
    struct Rec { uint16_t pid, eid; uint32_t off; };
    std::vector<Rec> recs;
    for (uint16_t i = 0; i < numTables; i++) {
        Rec r2;
        r2.pid = r.u16();
        r2.eid = r.u16();
        r2.off = r.u32();
        recs.push_back(r2);
    }
    for (auto& e : recs) {
        if (e.off >= r.size()) continue;
        CmapSubtable st;
        st.platformID = e.pid;
        st.encodingID = e.eid;
        st.offset = e.off;
        st.isUnicode = (e.pid == 0) ||
                       (e.pid == 3 && (e.eid == 1 || e.eid == 10 || e.eid == 3 || e.eid == 5));
        st.isSymbol = (e.pid == 3 && e.eid == 0) || (e.pid == 1 && e.eid == 0);
        Reader sr = r.subAt(e.off, r.size() - e.off);
        st.parse(sr);
        if (st.format == 14)
            format14 = std::move(st);
        else
            subtables.push_back(std::move(st));
    }
}

const CmapSubtable* Cmap::best() const {
    auto score = [](int pid, int eid, int fmt) -> int {
        if (pid == 3 && eid == 10 && fmt == 12) return 1000;
        if (pid == 0 && eid == 4 && fmt == 12) return 990;
        if (pid == 0 && eid == 6 && fmt == 12) return 980;
        if (pid == 0 && eid == 6 && fmt == 4) return 970;
        if (pid == 0 && eid == 4 && fmt == 4) return 960;
        if (pid == 3 && eid == 1 && fmt == 12) return 950;
        if (pid == 3 && eid == 10 && fmt == 4) return 940;
        if (pid == 3 && eid == 1 && fmt == 4) return 930;
        if (pid == 0 && eid == 6 && fmt == 6) return 920;
        if (pid == 0 && eid == 4 && fmt == 6) return 910;
        if (pid == 3 && eid == 1 && fmt == 6) return 900;
        if (pid == 0 && eid == 0 && fmt == 4) return 890;
        if (pid == 0 && eid == 0 && fmt == 6) return 880;
        if (pid == 0 && eid == 0 && fmt == 0) return 870;
        if (pid == 3 && eid == 1 && fmt == 0) return 860;
        if (pid == 3 && eid == 0 && fmt == 4) return 800;
        if (pid == 3 && eid == 0 && fmt == 0) return 790;
        if (pid == 3 && eid == 0 && fmt == 12) return 780;
        if (pid == 3 && eid == 0 && fmt == 6) return 770;
        return 0;
    };
    const CmapSubtable* bestSt = nullptr;
    int bestScore = 0;
    for (auto& st : subtables) {
        if (st.map.empty()) continue;
        int s = score(st.platformID, st.encodingID, st.format);
        if (s > bestScore) { bestScore = s; bestSt = &st; }
    }
    return bestSt;
}

uint16_t Cmap::glyph(uint32_t cp) const {
    if (const CmapSubtable* b = best()) {
        uint16_t g = b->glyph(cp);
        if (g) return g;
    }
    return glyphAny(cp);
}

uint16_t Cmap::glyphAny(uint32_t cp) const {
    for (auto& st : subtables) {
        uint16_t g = st.glyph(cp);
        if (g) return g;
    }
    return 0;
}

std::vector<std::pair<uint32_t, uint16_t>> Cmap::bestEntries() const {
    if (const CmapSubtable* b = best()) return b->entries();
    return {};
}

// ---------------------------------------------------------------- gasp

void Gasp::parse(Reader r) {
    version = r.u16();
    uint16_t num = r.u16();
    for (uint16_t i = 0; i < num && r.remaining() >= 4; i++) {
        GaspRecord rec;
        rec.rangeMaxPPEM = r.u16();
        rec.rangeGaspBehavior = r.u16();
        records.push_back(rec);
    }
}

// ---------------------------------------------------------------- kern

void Kern::parse(Reader r) {
    if (r.remaining() < 4) return;
    uint16_t major = r.u16();
    r.u16(); // minorVersion
    version = major;
    bool apple = major == 1;
    if (apple) {
        uint32_t nTables = r.u32();
        for (uint32_t t = 0; t < nTables && r.remaining() >= 8; t++) {
            size_t start = r.offset();
            KernSubtable st;
            st.apple = true;
            st.length = r.u16();
            st.coverage = r.u16();
            st.format = (uint16_t)(st.coverage >> 8);
            if (st.format == 0) {
                uint16_t nPairs = r.u16();
                r.skip(6);
                for (uint16_t p = 0; p < nPairs && r.remaining() >= 6; p++) {
                    KernPair kp{r.u16(), r.u16(), r.i16()};
                    st.pairs.push_back(kp);
                }
            }
            subtables.push_back(std::move(st));
            if (st.length >= 8) r.seek(start + st.length);
        }
    } else {
        uint16_t nTables = r.u16();
        for (uint16_t t = 0; t < nTables && r.remaining() >= 6; t++) {
            size_t start = r.offset();
            KernSubtable st;
            st.version = r.u16();
            st.length = r.u16();
            st.coverage = r.u16();
            st.format = (uint16_t)(st.coverage & 0xFF);
            if (st.format == 0) {
                uint16_t nPairs = r.u16();
                r.skip(6);
                for (uint16_t p = 0; p < nPairs && r.remaining() >= 6; p++) {
                    KernPair kp{r.u16(), r.u16(), r.i16()};
                    st.pairs.push_back(kp);
                }
            }
            subtables.push_back(std::move(st));
            if (st.length >= 6) r.seek(start + st.length);
        }
    }
}

int16_t Kern::kern(uint16_t left, uint16_t right) const {
    for (auto& st : subtables) {
        if (st.pairs.empty()) continue;
        auto it = std::lower_bound(st.pairs.begin(), st.pairs.end(), std::make_pair(left, right),
                                   [](const KernPair& a, const std::pair<uint16_t, uint16_t>& b) {
                                       return a.left != b.first ? a.left < b.first : a.right < b.second;
                                   });
        if (it != st.pairs.end() && it->left == left && it->right == right)
            return it->value;
    }
    return 0;
}

// ---------------------------------------------------------------- VORG

void VORG::parse(Reader r) {
    majorVersion = r.u16();
    minorVersion = r.u16();
    defaultVertOriginY = r.i16();
    uint16_t count = r.u16();
    for (uint16_t i = 0; i < count && r.remaining() >= 4; i++) {
        uint16_t gid = r.u16();
        int16_t y = r.i16();
        vertOriginYMetrics.emplace_back(gid, y);
    }
}

int16_t VORG::originY(uint16_t gid) const {
    auto it = std::lower_bound(vertOriginYMetrics.begin(), vertOriginYMetrics.end(), gid,
                               [](const std::pair<uint16_t, int16_t>& a, uint16_t g) { return a.first < g; });
    if (it != vertOriginYMetrics.end() && it->first == gid) return it->second;
    return defaultVertOriginY;
}

} // namespace ftk
