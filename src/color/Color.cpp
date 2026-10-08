// Color & bitmap tables implementation.
#include "ftk/Color.hpp"

#include <algorithm>
#include <tuple>

namespace ftk {

// ---------------------------------------------------------------- COLR

void Colr::parse(Reader r) {
    version = r.u16();
    if (version == 0) {
        uint32_t numBaseGlyphRecords = r.u16();
        uint32_t baseGlyphRecordsOffset = r.u32();
        uint32_t layersOffset = r.u32();
        uint16_t numLayerRecords = r.u16();

        if (baseGlyphRecordsOffset + (uint64_t)numBaseGlyphRecords * 6 <= r.size()) {
            Reader br = r.subAt(baseGlyphRecordsOffset, (size_t)numBaseGlyphRecords * 6);
            for (uint32_t i = 0; i < numBaseGlyphRecords; i++) {
                ColrBaseGlyph bg;
                bg.glyph = br.u16();
                bg.firstLayerIndex = br.u16();
                bg.numLayers = br.u16();
                baseGlyphs.push_back(bg);
            }
        }
        if (layersOffset + (uint64_t)numLayerRecords * 4 <= r.size()) {
            Reader lr = r.subAt(layersOffset, (size_t)numLayerRecords * 4);
            for (uint16_t i = 0; i < numLayerRecords; i++) {
                ColrLayer l;
                l.glyph = lr.u16();
                l.paletteIndex = lr.u16();
                layers.push_back(l);
            }
        }
    } else if (version == 1) {
        uint32_t numBaseGlyphRecords = r.u32();
        (void)numBaseGlyphRecords;
        baseGlyphListOffset = r.u32();
        layerListOffset = r.u32();
        clipListOffset = r.u32();
        varIdxMapOffset = r.u32();
        varStoreOffset = r.u32();
        hasLayerList = layerListOffset != 0;
        hasClipList = clipListOffset != 0;

        if (baseGlyphListOffset && baseGlyphListOffset + 4 <= r.size()) {
            Reader br = r.subAt(baseGlyphListOffset, r.size() - baseGlyphListOffset);
            numBaseGlyphPaintRecords = br.u32();
        }
        if (layerListOffset && layerListOffset + 4 <= r.size()) {
            Reader lr = r.subAt(layerListOffset, r.size() - layerListOffset);
            numLayerV1Records = lr.u32();
        }
        if (clipListOffset && clipListOffset + 5 <= r.size()) {
            Reader cr = r.subAt(clipListOffset, r.size() - clipListOffset);
            cr.u8(); // format
            numClipRecords = cr.u32();
        }
    }
}

// ---------------------------------------------------------------- CPAL

void Cpal::parse(Reader r) {
    version = r.u16();
    numPaletteEntries = r.u16();
    numPalettes = r.u16();
    numColorRecords = r.u16();
    uint32_t colorRecordsArrayOffset = r.u32();
    uint32_t colorRecordIndicesOffset = r.u32();

    if (colorRecordIndicesOffset && (uint64_t)numPalettes * 2 + colorRecordIndicesOffset <= r.size()) {
        Reader ir = r.subAt(colorRecordIndicesOffset, (size_t)numPalettes * 2);
        for (uint16_t i = 0; i < numPalettes; i++)
            colorRecordIndices.push_back(ir.u16());
    }
    if (colorRecordsArrayOffset && (uint64_t)numColorRecords * 4 + colorRecordsArrayOffset <= r.size()) {
        Reader cr = r.subAt(colorRecordsArrayOffset, (size_t)numColorRecords * 4);
        for (uint16_t i = 0; i < numColorRecords; i++) {
            uint8_t b = cr.u8(), g = cr.u8(), rr = cr.u8(), a = cr.u8();
            colorRecords.push_back((uint32_t(rr) << 24) | (uint32_t(g) << 16) | (uint32_t(b) << 8) | a);
        }
    }
    if (version == 1 && r.remaining() >= 12) {
        uint32_t paletteTypesArrayOffset = r.u32();
        uint32_t paletteLabelsArrayOffset = r.u32();
        uint32_t paletteEntryLabelsArrayOffset = r.u32();
        if (paletteTypesArrayOffset && (uint64_t)numPalettes * 4 + paletteTypesArrayOffset <= r.size()) {
            Reader tr = r.subAt(paletteTypesArrayOffset, (size_t)numPalettes * 4);
            for (uint16_t i = 0; i < numPalettes; i++)
                paletteTypes.push_back(tr.u32());
        }
        if (paletteLabelsArrayOffset && (uint64_t)numPalettes * 2 + paletteLabelsArrayOffset <= r.size()) {
            Reader lr = r.subAt(paletteLabelsArrayOffset, (size_t)numPalettes * 2);
            for (uint16_t i = 0; i < numPalettes; i++)
                paletteLabels.push_back(lr.u16());
        }
        if (paletteEntryLabelsArrayOffset && (uint64_t)numPaletteEntries * 2 + paletteEntryLabelsArrayOffset <= r.size()) {
            Reader lr = r.subAt(paletteEntryLabelsArrayOffset, (size_t)numPaletteEntries * 2);
            for (uint16_t i = 0; i < numPaletteEntries; i++)
                paletteEntryLabels.push_back(lr.u16());
        }
    }
}

uint32_t Cpal::color(uint16_t palette, uint16_t entry) const {
    if (palette >= colorRecordIndices.size()) return 0;
    uint16_t baseIdx = colorRecordIndices[palette];
    if ((uint32_t)baseIdx + entry >= colorRecords.size()) return 0;
    return colorRecords[baseIdx + entry];
}

// ---------------------------------------------------------------- sbix

void Sbix::parse(Reader r, uint16_t numGlyphs) {
    tableData = r.data();
    tableSize = r.size();
    version = r.u16();
    flags = r.u16();
    uint32_t numStrikes = r.u32();
    std::vector<uint32_t> strikeOffsets;
    for (uint32_t i = 0; i < numStrikes && r.remaining() >= 4; i++)
        strikeOffsets.push_back(r.u32());

    for (uint32_t off : strikeOffsets) {
        if (off + 4 > r.size()) continue;
        SbixStrikeInfo s;
        size_t strikeStart = off;
        Reader sr = r.subAt(off, r.size() - off);
        s.ppem = sr.u16();
        s.ppi = sr.u16();
        s.dataStart = (uint32_t)strikeStart;
        for (uint16_t g = 0; g <= numGlyphs && sr.remaining() >= 4; g++) {
            uint32_t o = sr.u32();
            s.offsets.push_back(o ? (uint32_t)(strikeStart + o) : 0);
        }
        strikes.push_back(std::move(s));
    }
}

std::vector<SbixGlyph> Sbix::glyphsOfStrike(size_t strikeIdx, uint16_t numGlyphs) const {
    std::vector<SbixGlyph> out;
    if (strikeIdx >= strikes.size() || !tableData) return out;
    const SbixStrikeInfo& s = strikes[strikeIdx];
    for (uint16_t g = 0; g < numGlyphs && (size_t)g + 1 < s.offsets.size(); g++) {
        uint32_t a = s.offsets[g], b = s.offsets[g + 1];
        if (a == 0 || b < a || b > tableSize) continue;
        Reader gr(tableData + a, b - a);
        if (gr.remaining() < 8) continue;
        SbixGlyph sg;
        sg.gid = g;
        sg.originOffsetX = gr.i16();
        sg.originOffsetY = gr.i16();
        sg.graphicType = gr.str(4);
        sg.data = gr.take(gr.remaining());
        out.push_back(std::move(sg));
    }
    return out;
}

// ---------------------------------------------------------------- CBDT/CBLC

void Cbdt::parse(Reader cblc, Reader cbdt, uint16_t numGlyphs) {
    cbdtData.assign(cbdt.data(), cbdt.data() + cbdt.size());
    (void)numGlyphs;
    if (cblc.size() < 6) return;
    Reader r(cblc);
    majorVersion = r.u8();
    minorVersion = r.u8();
    uint32_t numSizes = r.u32();
    for (uint32_t i = 0; i < numSizes && r.remaining() >= 48; i++) {
        BitmapSize bs;
        bs.indexSubTableArrayOffset = r.u32();
        r.u32(); // indexTablesSize
        bs.numberOfIndexSubTables = r.u32();
        r.u32(); // colorRef
        r.skip(24); // hori + vert sbitLineMetrics
        bs.ppemX = r.u16();
        bs.ppemY = r.u16();
        bs.bitDepth = r.u8();
        bs.flags = r.u8();

        // index subtable array
        if (bs.indexSubTableArrayOffset + (uint64_t)bs.numberOfIndexSubTables * 8 > cblc.size())
            continue;
        Reader arr = cblc.subAt(bs.indexSubTableArrayOffset,
                                cblc.size() - bs.indexSubTableArrayOffset);
        for (uint32_t k = 0; k < bs.numberOfIndexSubTables && arr.remaining() >= 8; k++) {
            uint16_t firstGlyph = arr.u16();
            uint16_t lastGlyph = arr.u16();
            uint32_t subOff = arr.u32();
            bs.firstGlyph = k == 0 ? firstGlyph : std::min<uint16_t>(bs.firstGlyph, firstGlyph);
            bs.lastGlyph = std::max(bs.lastGlyph, lastGlyph);

            size_t abs = bs.indexSubTableArrayOffset + subOff;
            if (abs + 8 > cblc.size()) continue;
            Reader sub = cblc.subAt(abs, cblc.size() - abs);
            uint16_t indexFormat = sub.u16();
            sub.u16(); // imageDataFormat
            uint16_t imageFormat = sub.u16();
            uint32_t imageDataOffset = sub.u32();

            if (indexFormat == 1 || indexFormat == 3) {
                size_t n = (size_t)(lastGlyph - firstGlyph) + 2;
                std::vector<uint32_t> offs;
                for (size_t q = 0; q < n; q++) {
                    uint32_t v = indexFormat == 1 ? sub.u32() : sub.u16();
                    offs.push_back(v);
                    if (sub.remaining() < (indexFormat == 1 ? 4 : 2)) break;
                }
                for (size_t q = 0; q + 1 < offs.size(); q++) {
                    if (offs[q] == 0) continue;
                    uint16_t gid = firstGlyph + (uint16_t)q;
                    uint32_t o1 = imageDataOffset + offs[q];
                    uint32_t o2 = imageDataOffset + offs[q + 1];
                    if (o2 < o1 || o2 > cbdtData.size()) continue;
                    bs.glyphLocations.emplace_back(gid, o1, o2 - o1);
                }
            } else if (indexFormat == 2) {
                uint32_t imageSize = sub.u32();
                sub.skip(8); // bigMetrics
                for (uint16_t gid = firstGlyph; gid <= lastGlyph; gid++) {
                    uint32_t o1 = imageDataOffset + (uint32_t)(gid - firstGlyph) * imageSize;
                    if (o1 + imageSize > cbdtData.size()) break;
                    bs.glyphLocations.emplace_back(gid, o1, imageSize);
                }
            } else if (indexFormat == 4) {
                uint32_t numGlyphsThis = sub.u32();
                std::vector<std::pair<uint16_t, uint32_t>> glyphArr;
                for (uint32_t q = 0; q <= numGlyphsThis && sub.remaining() >= 4; q++)
                    glyphArr.emplace_back(sub.u16(), sub.u16());
                for (uint32_t q = 0; q + 1 < glyphArr.size(); q++) {
                    uint16_t gid = glyphArr[q].first;
                    uint32_t o1 = imageDataOffset + glyphArr[q].second;
                    uint32_t o2 = imageDataOffset + glyphArr[q + 1].second;
                    if (o2 < o1 || o2 > cbdtData.size()) continue;
                    bs.glyphLocations.emplace_back(gid, o1, o2 - o1);
                }
            } else if (indexFormat == 5) {
                uint32_t imageSize = sub.u32();
                sub.skip(8); // bigMetrics
                uint32_t numGlyphsThis = sub.u32();
                for (uint32_t q = 0; q < numGlyphsThis && sub.remaining() >= 2; q++) {
                    uint16_t gid = sub.u16();
                    uint32_t o1 = imageDataOffset + q * imageSize;
                    if (o1 + imageSize > cbdtData.size()) break;
                    bs.glyphLocations.emplace_back(gid, o1, imageSize);
                }
            }
            (void)imageFormat;
        }
        sizes.push_back(std::move(bs));
    }
}

std::vector<CbdtGlyph> Cbdt::glyphsOfStrike(size_t sizeIdx) const {
    std::vector<CbdtGlyph> out;
    if (sizeIdx >= sizes.size()) return out;
    for (auto& [gid, off, len] : sizes[sizeIdx].glyphLocations) {
        if (off + 8 > cbdtData.size()) continue;
        Reader r(cbdtData.data() + off, len);
        uint8_t format = r.u8();
        CbdtGlyph g;
        g.gid = gid;
        g.format = format;
        // small glyph metrics (formats 1,2,5,17,18,19)
        if (format == 17 || format == 18 || format == 19 || format == 1 || format == 2) {
            if (r.remaining() < 5) continue;
            g.height = r.u8();
            g.width = r.u8();
            g.bearingX = (int8_t)r.u8();
            g.bearingY = (int8_t)r.u8();
            g.advance = r.u8();
        } else if (format == 5 || format == 6 || format == 7) {
            if (r.remaining() < 8) continue;
            g.height = r.u8();
            g.width = r.u8();
            g.bearingX = (int8_t)r.u8();
            g.bearingY = (int8_t)r.u8();
            g.advance = r.u8();
            r.skip(3); // rest of big metrics
        }
        if (format == 18 && r.remaining() >= 4)
            r.u32(); // dataLen
        g.data = r.take(r.remaining());
        g.isPng = (format == 17 || format == 18 || format == 19);
        out.push_back(std::move(g));
    }
    return out;
}

// ---------------------------------------------------------------- SVG

std::vector<ColorTables::SvgDoc> ColorTables::svgDocuments() const {
    std::vector<SvgDoc> out;
    if (svgRaw.size() < 10) return out;
    Reader r(svgRaw);
    r.u16(); // version
    r.u32(); // docListOffset (should be 10)
    uint32_t numEntries = r.u32();
    struct Rec { uint16_t first, last; uint32_t off, len; };
    std::vector<Rec> recs;
    for (uint32_t i = 0; i < numEntries && r.remaining() >= 12; i++) {
        Rec e;
        e.first = r.u16();
        e.last = r.u16();
        e.off = r.u32();
        e.len = r.u32();
        recs.push_back(e);
    }
    for (auto& e : recs) {
        if (e.off == 0 || e.off + e.len > svgRaw.size()) continue;
        SvgDoc d;
        d.firstGlyph = e.first;
        d.lastGlyph = e.last;
        d.svg.assign(svgRaw.begin() + e.off, svgRaw.begin() + e.off + e.len);
        out.push_back(std::move(d));
    }
    return out;
}

void ColorTables::parse(Reader colrR, Reader cpalR, Reader sbixR, Reader cblcR, Reader cbdtR, Reader svgR,
                        uint16_t numGlyphs) {
    if (colrR) {
        hasColr = true;
        try { colr.parse(colrR); } catch (...) {}
    }
    if (cpalR) {
        hasCpal = true;
        try { cpal.parse(cpalR); } catch (...) {}
    }
    if (sbixR) {
        hasSbix = true;
        try { sbix.parse(sbixR, numGlyphs); } catch (...) {}
    }
    if (cblcR && cbdtR) {
        hasCbdt = true;
        try { cbdt.parse(cblcR, cbdtR, numGlyphs); } catch (...) {}
    }
    if (svgR) {
        hasSvg = true;
        svgRaw = std::vector<uint8_t>(svgR.data(), svgR.data() + svgR.size());
    }
}

} // namespace ftk
