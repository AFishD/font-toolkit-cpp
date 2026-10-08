// ftk: CFF / CFF2 — INDEX, DICTs, charsets, FDArray/FDSelect, and the
// Type2 charstring interpreter. Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Bytes.hpp"
#include "ftk/Glyphs.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ftk {

const char* cffStandardString(uint32_t sid); // SID < 391, else nullptr
size_t cffStandardStringCount();

// CFF INDEX (v1 and CFF2 layout)
class CffIndex {
public:
    void parse(Reader r, bool cff2);
    size_t count() const { return count_; }
    bool empty() const { return count_ == 0; }
    // item i data (pointer into underlying buffer) and length
    const uint8_t* itemData(size_t i, size_t& len) const;
    std::vector<uint8_t> itemCopy(size_t i) const;
    uint32_t offsetOf(size_t i) const { return offs_[i]; }     // absolute in buffer
    uint32_t dataStart() const { return dataStart_; }
    uint32_t endOffset() const; // absolute offset just past last item

private:
    size_t count_ = 0;
    std::vector<uint32_t> offs_; // count+1, absolute offsets in buffer
    uint32_t dataStart_ = 0;
    const uint8_t* buf_ = nullptr;
    size_t bufSize_ = 0;
};

// DICT operand + entries
struct DictNumber {
    bool isInt = true;
    int64_t i = 0;
    double d = 0;
};

struct DictEntry {
    std::vector<DictNumber> operands;
    uint8_t op = 0;
    uint8_t op2 = 0; // valid when op == 12
};

struct Dict {
    std::vector<DictEntry> entries;
    void parse(Reader r, bool cff2);
    const DictEntry* find(uint8_t op) const;
    const DictEntry* find2(uint8_t op, uint8_t op2) const;
    // numeric operand getters (with default when absent)
    int64_t asInt(const DictEntry* e, size_t idx, int64_t def) const;
    double asNum(const DictEntry* e, size_t idx, double def) const;
};

// Private DICT keys used
struct PrivateDict {
    int64_t subrs = -1;         // 19, offset relative to private dict start
    int64_t defaultWidthX = 0;  // 20
    int64_t nominalWidthX = 0;  // 21
    int64_t vsindex = 0;        // 22 (CFF2)
    void parse(Reader r, bool cff2 = false);
};

struct CffFontDict {
    Dict dict;
    double matrix[6] = {0.001, 0, 0, 0.001, 0, 0};
    bool hasMatrix = false;
    uint32_t privOffset = 0, privSize = 0;
    PrivateDict priv;
    // raw bytes of private dict + local subrs region (for subset copying)
    const uint8_t* privData = nullptr;
    size_t privDataSize = 0;
};

struct SeacComponent {
    std::string glyphName;
    double dx = 0, dy = 0;
};

struct CffGlyphInfo {
    Path path;
    double width = 0;
    bool hasWidth = false;
    std::vector<SeacComponent> seac; // when endchar seac used
    size_t hintCount = 0;
    bool ok = true;
};

// Region scalar provider for CFF2 blend (implemented by VarStore wrapper).
class BlendRegions {
public:
    virtual ~BlendRegions() = default;
    // fill scalars for each region of the given IVS (vsindex)
    virtual const std::vector<float>& scalars(uint16_t vsindex) = 0;
};

class CffFont {
public:
    void parse(Reader r);

    bool isCFF2() const { return cff2_; }
    uint8_t majorVersion() const { return hdrMajor_; }

    // top dict values
    Dict topDict;
    double topMatrix[6] = {0.001, 0, 0, 0.001, 0, 0};
    bool hasTopMatrix = false;
    std::string fontName; // from Name INDEX (CFF1)
    int64_t cidRegistrySid = -1, cidOrderingSid = -1, cidSupplement = 0;
    bool isCID = false;
    int64_t charStringsOffset = 0;
    int64_t charsetOffset = 0; // -1 => predefined
    int64_t encodingOffset = 0;
    int64_t fdArrayOffset = -1, fdSelectOffset = -1;
    int64_t varStoreOffset = -1;
    int64_t privOffset = 0, privSize = 0; // non-CID

    CffIndex nameIndex, topDictIndex, stringIndex, globalSubrs, charStrings, fdArrayIndex;

    // charset: gid -> SID (predefined resolved too)
    std::vector<uint32_t> charsetSIDs;
    bool customCharset = false;

    // fonts (FDArray or single)
    std::vector<CffFontDict> fonts;

    // FDSelect
    bool hasFDSelect = false;
    std::vector<uint8_t> fdSelect0;                    // format 0
    std::vector<std::pair<uint32_t, uint8_t>> fdSelect3; // format 3 ranges

    uint16_t fdOfGlyph(uint16_t gid) const;

    // local subrs of font fd (parses lazily, thread-safe via const parse into cache)
    const CffIndex* localSubrs(size_t fdIndex) const;

    uint16_t numGlyphs() const { return (uint16_t)charStrings.count(); }

    std::string sidString(uint32_t sid) const;          // standard + custom
    std::string glyphName(uint16_t gid) const;          // charset + strings
    uint32_t glyphSID(uint16_t gid) const;

    // Charstring interpreter. Thread-safe. provider enables CFF2 blending.
    CffGlyphInfo interpret(uint16_t gid, BlendRegions* provider = nullptr) const;

    // raw charstring bytes for gid
    const uint8_t* charstringBytes(uint16_t gid, size_t& len) const;

    // for dump: parse charset/encoding raw info
    int charsetFormat = -1;

private:
    void parseCharset(Reader r);
    mutable std::vector<std::unique_ptr<CffIndex>> localSubrs_;
    mutable std::vector<bool> localSubrsParsed_;
    const uint8_t* buf_ = nullptr;
    size_t bufSize_ = 0;
    bool cff2_ = false;
    uint8_t hdrMajor_ = 1;
};

// seac via StandardEncoding
std::string standardEncodingName(uint8_t code);

} // namespace ftk
