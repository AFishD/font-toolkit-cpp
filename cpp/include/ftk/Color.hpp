// ftk: color & bitmap tables — COLR (v0/v1 structural), CPAL, sbix,
// CBDT/CBLC (EBDT/EBLC legacy), SVG documents.
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Bytes.hpp"

#include <string>
#include <vector>

namespace ftk {

struct ColrLayer {
    uint16_t glyph = 0;
    uint16_t paletteIndex = 0xFFFF;
};

struct ColrBaseGlyph {
    uint16_t glyph = 0;
    uint16_t firstLayerIndex = 0;
    uint16_t numLayers = 0;
};

struct Colr {
    uint16_t version = 0;
    std::vector<ColrBaseGlyph> baseGlyphs; // v0
    std::vector<ColrLayer> layers;         // v0
    // v1 presence info
    bool hasClipList = false;
    bool hasLayerList = false;
    uint32_t baseGlyphListOffset = 0, layerListOffset = 0, clipListOffset = 0;
    uint32_t varIdxMapOffset = 0, varStoreOffset = 0;
    size_t numBaseGlyphPaintRecords = 0;
    size_t numLayerV1Records = 0;
    size_t numClipRecords = 0;

    void parse(Reader r);
};

struct Cpal {
    uint16_t version = 0;
    uint16_t numPaletteEntries = 0;
    uint16_t numPalettes = 0;
    uint16_t numColorRecords = 0;
    std::vector<uint32_t> colorRecords; // 0xBBGGRRFF (as stored, BGRA order)
    std::vector<uint16_t> colorRecordIndices;
    std::vector<uint32_t> paletteTypes;
    std::vector<uint16_t> paletteLabels, paletteEntryLabels;

    void parse(Reader r);
    // returns 0xRRGGBBAA
    uint32_t color(uint16_t palette, uint16_t entry) const;
};

struct SbixGlyph {
    uint16_t gid = 0;
    int16_t originOffsetX = 0, originOffsetY = 0;
    std::string graphicType;
    std::vector<uint8_t> data;
};

struct SbixStrikeInfo {
    uint16_t ppem = 0;
    uint16_t ppi = 72;
    uint32_t dataStart = 0; // absolute offset of glyph data array
    std::vector<uint32_t> offsets; // numGlyphs+1, absolute
};

struct Sbix {
    uint16_t version = 1;
    uint16_t flags = 0;
    std::vector<SbixStrikeInfo> strikes;
    const uint8_t* tableData = nullptr;
    size_t tableSize = 0;

    void parse(Reader r, uint16_t numGlyphs);
    std::vector<SbixGlyph> glyphsOfStrike(size_t strikeIdx, uint16_t numGlyphs) const;
};

struct CbdtGlyph {
    uint16_t gid = 0;
    uint8_t format = 0;
    uint8_t height = 0, width = 0;
    int8_t bearingX = 0, bearingY = 0;
    uint8_t advance = 0;
    std::vector<uint8_t> data; // raw payload (PNG for formats 17/18/19)
    bool isPng = false;
};

struct Cbdt {
    struct BitmapSize {
        uint16_t ppemX = 0, ppemY = 0;
        uint8_t bitDepth = 1;
        uint8_t flags = 0;
        uint32_t indexSubTableArrayOffset = 0;
        uint32_t numberOfIndexSubTables = 0;
        // glyph ranges covered
        uint16_t firstGlyph = 0, lastGlyph = 0;
        // resolved glyph->(offset,length) via subtables
        std::vector<std::tuple<uint16_t, uint32_t, uint32_t>> glyphLocations; // gid, off, len
    };
    uint16_t majorVersion = 2, minorVersion = 0;
    std::vector<BitmapSize> sizes;
    std::vector<uint8_t> cbdtData;

    void parse(Reader cblc, Reader cbdt, uint16_t numGlyphs);
    std::vector<CbdtGlyph> glyphsOfStrike(size_t sizeIdx) const;
};

class ColorTables {
public:
    bool hasColr = false, hasCpal = false, hasSbix = false, hasCbdt = false, hasSvg = false;
    Colr colr;
    Cpal cpal;
    Sbix sbix;
    Cbdt cbdt;
    std::vector<uint8_t> svgRaw;

    void parse(Reader colrR, Reader cpalR, Reader sbixR, Reader cblcR, Reader cbdtR, Reader svgR,
               uint16_t numGlyphs);

    struct SvgDoc {
        uint16_t firstGlyph = 0, lastGlyph = 0;
        std::vector<uint8_t> svg;
    };
    std::vector<SvgDoc> svgDocuments() const;
};

} // namespace ftk
