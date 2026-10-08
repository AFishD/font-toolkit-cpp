// WOFF2 decoding: header + table directory, Brotli payload, and
// reconstruction of the transformed glyf/loca and hmtx tables.
#include "ftk/Sfnt.hpp"
#include "ftk/Brotli.hpp"

#include <algorithm>
#include <cstring>

namespace ftk {

namespace {

// Known table tags (WOFF2 spec, index 0..62).
const Tag kWoff2KnownTags[63] = {
    FTK_TAG('c','m','a','p'), FTK_TAG('h','e','a','d'), FTK_TAG('h','h','e','a'),
    FTK_TAG('h','m','t','x'), FTK_TAG('m','a','x','p'), FTK_TAG('n','a','m','e'),
    FTK_TAG('O','S','/','2'), FTK_TAG('p','o','s','t'), FTK_TAG('c','v','t',' '),
    FTK_TAG('f','p','g','m'), FTK_TAG('g','l','y','f'), FTK_TAG('l','o','c','a'),
    FTK_TAG('p','r','e','p'), FTK_TAG('C','F','F',' '), FTK_TAG('V','O','R','G'),
    FTK_TAG('E','B','D','T'), FTK_TAG('E','B','L','C'), FTK_TAG('g','a','s','p'),
    FTK_TAG('h','d','m','x'), FTK_TAG('k','e','r','n'), FTK_TAG('L','T','S','H'),
    FTK_TAG('P','C','L','T'), FTK_TAG('V','D','M','X'), FTK_TAG('v','h','e','a'),
    FTK_TAG('v','m','t','x'), FTK_TAG('B','A','S','E'), FTK_TAG('G','D','E','F'),
    FTK_TAG('G','P','O','S'), FTK_TAG('G','S','U','B'), FTK_TAG('E','B','S','C'),
    FTK_TAG('J','S','T','F'), FTK_TAG('M','A','T','H'), FTK_TAG('C','B','D','T'),
    FTK_TAG('C','B','L','C'), FTK_TAG('C','O','L','R'), FTK_TAG('C','P','A','L'),
    FTK_TAG('S','V','G',' '), FTK_TAG('s','b','i','x'), FTK_TAG('a','c','n','t'),
    FTK_TAG('a','v','a','r'), FTK_TAG('b','d','a','t'), FTK_TAG('b','l','o','c'),
    FTK_TAG('b','s','l','n'), FTK_TAG('c','v','a','r'), FTK_TAG('f','d','s','c'),
    FTK_TAG('f','e','a','t'), FTK_TAG('f','m','t','x'), FTK_TAG('f','v','a','r'),
    FTK_TAG('g','v','a','r'), FTK_TAG('h','s','t','y'), FTK_TAG('j','u','s','t'),
    FTK_TAG('l','c','a','r'), FTK_TAG('m','o','r','t'), FTK_TAG('m','o','r','x'),
    FTK_TAG('o','p','b','d'), FTK_TAG('p','r','o','p'), FTK_TAG('t','r','a','k'),
    FTK_TAG('Z','a','p','f'), FTK_TAG('S','i','l','f'), FTK_TAG('G','l','a','t'),
    FTK_TAG('G','l','o','c'), FTK_TAG('F','e','a','t'), FTK_TAG('S','i','l','l')
};

// UIntBase128 (variable length, big-endian 7-bit groups)
uint32_t readBase128(Reader& r) {
    uint32_t value = 0;
    for (int i = 0; i < 5; i++) {
        if (r.remaining() < 1) fail("woff2: truncated UIntBase128");
        uint8_t b = r.u8();
        if (i == 0 && b == 0x80) fail("woff2: UIntBase128 has leading zero");
        if (value & 0xFE000000) fail("woff2: UIntBase128 overflow");
        value = (value << 7) | (b & 0x7F);
        if (!(b & 0x80)) return value;
    }
    fail("woff2: UIntBase128 too long");
    return 0;
}

// 255UInt16: 253 => next u16; 254 => next byte + 506; 255 => next byte + 253.
uint16_t read255UInt16(Reader& r) {
    uint8_t code = r.u8();
    if (code == 253) return r.u16();
    if (code == 254) return (uint16_t)(r.u8() + 506);
    if (code == 255) return (uint16_t)(r.u8() + 253);
    return code;
}

} // namespace

// ---------------------------------------------------------------------------
// WOFF2 glyf/loca reconstruction
// ---------------------------------------------------------------------------

namespace {

struct Woff2Glyf {
    // transformed header
    uint16_t optionFlags = 0;
    uint16_t numGlyphs = 0;
    uint16_t indexFormat = 0;
    std::vector<uint8_t> nContourStream, nPointsStream, flagStream, glyphStream;
    std::vector<uint8_t> compositeStream, bboxStream, instructionStream;
    std::vector<uint8_t> bboxBitmap, overlapSimpleBitmap;

    std::vector<uint8_t> glyf;   // reconstructed table
    std::vector<uint8_t> loca;   // reconstructed loca table

    void parse(const uint8_t* p, size_t n);
    void reconstruct();
};

void Woff2Glyf::parse(const uint8_t* p, size_t n) {
    Reader r(p, n);
    r.u16(); // reserved
    optionFlags = r.u16();
    numGlyphs = r.u16();
    indexFormat = r.u16();
    uint32_t nContourSize = r.u32();
    uint32_t nPointsSize = r.u32();
    uint32_t flagSize = r.u32();
    uint32_t glyphSize = r.u32();
    uint32_t compositeSize = r.u32();
    uint32_t bboxSize = r.u32();
    uint32_t instructionSize = r.u32();
    if (r.remaining() < (size_t)nContourSize + nPointsSize + flagSize + glyphSize +
                             compositeSize + bboxSize + instructionSize)
        fail("woff2: glyf transform streams exceed table size");

    nContourStream = r.take(nContourSize);
    nPointsStream = r.take(nPointsSize);
    flagStream = r.take(flagSize);
    glyphStream = r.take(glyphSize);
    compositeStream = r.take(compositeSize);
    bboxStream = r.take(bboxSize);
    instructionStream = r.take(instructionSize);
    if (optionFlags & 0x0001)
        overlapSimpleBitmap = r.take((numGlyphs + 7) / 8);
    (void)n;
}

void Woff2Glyf::reconstruct() {
    if (nContourStream.size() < (size_t)numGlyphs * 2) fail("woff2: short nContourStream");
    if (bboxStream.size() < (size_t)((numGlyphs + 31) / 32) * 4) fail("woff2: short bboxStream");
    const uint8_t* bboxBitmap = bboxStream.data();
    size_t bboxBitmapSize = (size_t)((numGlyphs + 31) / 32) * 4;
    Reader bboxR(bboxStream.data() + bboxBitmapSize, bboxStream.size() - bboxBitmapSize);
    Reader glyphR(glyphStream);
    Reader compositeR(compositeStream);
    Reader instructionR(instructionStream);
    Reader nPointsR(nPointsStream);
    size_t flagPos = 0; // flagStream cursor (one flag byte per point)

    Writer glyfW;
    std::vector<uint32_t> locaOffsets;
    locaOffsets.push_back(0);

    for (uint16_t gid = 0; gid < numGlyphs; gid++) {
        int16_t numberOfContours = (int16_t)((nContourStream[2 * gid] << 8) | nContourStream[2 * gid + 1]);
        if (numberOfContours == 0) {
            while (glyfW.offset() % 4) glyfW.u8(0);
            locaOffsets.push_back((uint32_t)glyfW.offset());
            continue;
        }

        // bounding box
        bool haveBBox = (bboxBitmap[gid >> 3] & (0x80 >> (gid & 7))) != 0;
        int16_t xMin = 0, yMin = 0, xMax = 0, yMax = 0;
        bool bboxFromStream = false;
        if (haveBBox) {
            if (bboxR.remaining() < 8) fail("woff2: bbox stream underrun");
            xMin = bboxR.i16();
            yMin = bboxR.i16();
            xMax = bboxR.i16();
            yMax = bboxR.i16();
            bboxFromStream = true;
        } else if (numberOfContours < 0) {
            fail("woff2: composite glyph without explicit bbox");
        }

        std::vector<uint16_t> endPts;
        std::vector<uint8_t> pointFlags;
        std::vector<std::pair<int32_t, int32_t>> points;
        std::vector<uint8_t> instructions;
        Writer compW;

        if (numberOfContours > 0) {
            // simple glyph: per-contour point counts
            uint32_t totalPoints = 0;
            for (int c = 0; c < numberOfContours; c++) {
                if (nPointsR.remaining() < 1) fail("woff2: nPoints stream underrun");
                uint16_t nPts = read255UInt16(nPointsR);
                totalPoints += nPts;
                endPts.push_back((uint16_t)(totalPoints - 1));
            }
            if (flagStream.size() < flagPos + totalPoints) fail("woff2: flag stream underrun");

            // triplet decoding
            points.reserve(totalPoints);
            pointFlags.reserve(totalPoints);
            int32_t x = 0, y = 0;
            for (uint32_t i = 0; i < totalPoints; i++) {
                uint8_t flag = flagStream[flagPos++];
                bool onCurve = !(flag >> 7);
                flag &= 0x7F;
                int nBytes = flag < 84 ? 1 : flag < 120 ? 2 : flag < 124 ? 3 : 4;
                if (glyphR.remaining() < (size_t)nBytes) fail("woff2: glyph stream underrun");
                const uint8_t* t = glyphR.bytes(nBytes);
                int32_t dx = 0, dy = 0;
                auto withSign = [](uint8_t f, int32_t v) { return (f & 1) ? v : -v; };
                if (flag < 10) {
                    dx = 0;
                    dy = withSign(flag, ((flag & 14) << 7) + t[0]);
                } else if (flag < 20) {
                    dx = withSign(flag, (((flag - 10) & 14) << 7) + t[0]);
                    dy = 0;
                } else if (flag < 84) {
                    int b0 = flag - 20;
                    int b1 = t[0];
                    dx = withSign(flag, 1 + (b0 & 0x30) + (b1 >> 4));
                    dy = withSign((uint8_t)(flag >> 1), 1 + ((b0 & 0x0C) << 2) + (b1 & 0x0F));
                } else if (flag < 120) {
                    int b0 = flag - 84;
                    dx = withSign(flag, 1 + ((b0 / 12) << 8) + t[0]);
                    dy = withSign((uint8_t)(flag >> 1), 1 + (((b0 % 12) >> 2) << 8) + t[1]);
                } else if (flag < 124) {
                    int b2 = t[1];
                    dx = withSign(flag, (t[0] << 4) + (b2 >> 4));
                    dy = withSign((uint8_t)(flag >> 1), ((b2 & 0x0F) << 8) + t[2]);
                } else {
                    dx = withSign(flag, (t[0] << 8) + t[1]);
                    dy = withSign((uint8_t)(flag >> 1), (t[2] << 8) + t[3]);
                }
                x += dx;
                y += dy;
                points.emplace_back(x, y);
                uint8_t f = onCurve ? 0x01 : 0x00;
                if (i == 0 && !overlapSimpleBitmap.empty() &&
                    (overlapSimpleBitmap[gid >> 3] & (0x80 >> (gid & 7))))
                    f |= 0x40; // OVERLAP_SIMPLE
                pointFlags.push_back(f);
            }

            // instructions
            if (glyphR.remaining() < 1)
                fail("woff2: missing instruction length (gid " + std::to_string(gid) +
                     ", points " + std::to_string(totalPoints) + ")");
            uint16_t instrLen = read255UInt16(glyphR);
            if (instructionR.remaining() < instrLen)
                fail("woff2: instruction stream underrun (gid " + std::to_string(gid) +
                     ", len " + std::to_string(instrLen) + ", remaining " +
                     std::to_string(instructionR.remaining()) + ", contours " +
                     std::to_string(numberOfContours) + ", points " +
                     std::to_string(totalPoints) + ", glyphRemaining " +
                     std::to_string(glyphR.remaining()) + ")");
            instructions = instructionR.take(instrLen);

            // computed bbox when absent
            if (!bboxFromStream) {
                int32_t mnx = points.empty() ? 0 : points[0].first;
                int32_t mny = points.empty() ? 0 : points[0].second;
                int32_t mxx = mnx, mxy = mny;
                for (auto& pt : points) {
                    mnx = std::min(mnx, pt.first);
                    mny = std::min(mny, pt.second);
                    mxx = std::max(mxx, pt.first);
                    mxy = std::max(mxy, pt.second);
                }
                xMin = (int16_t)mnx; yMin = (int16_t)mny;
                xMax = (int16_t)mxx; yMax = (int16_t)mxy;
            }
        } else {
            // composite: raw component records, last one clears MORE_COMPONENTS
            bool more = true;
            bool haveInstructions = false;
            while (more) {
                if (compositeR.remaining() < 4) fail("woff2: composite stream underrun");
                size_t start = compositeR.offset();
                uint16_t flags = compositeR.u16();
                compositeR.u16(); // glyph index
                if (flags & 0x0001) {
                    if (compositeR.remaining() < 4) fail("woff2: composite args underrun");
                    compositeR.skip(4);
                } else {
                    if (compositeR.remaining() < 2) fail("woff2: composite args underrun");
                    compositeR.skip(2);
                }
                if (flags & 0x0008) compositeR.skip(2);
                else if (flags & 0x0040) compositeR.skip(4);
                else if (flags & 0x0080) compositeR.skip(8);
                more = (flags & 0x0020) != 0;
                haveInstructions = (flags & 0x0100) != 0;
                size_t len = compositeR.offset() - start;
                compW.bytes(compositeStream.data() + start, len);
            }
            if (haveInstructions) {
                if (glyphR.remaining() < 1) fail("woff2: missing composite instruction length");
                uint16_t instrLen = read255UInt16(glyphR);
                if (instructionR.remaining() < instrLen) fail("woff2: instruction stream underrun");
                instructions = instructionR.take(instrLen);
            }
        }

        // write glyph record
        glyfW.i16(numberOfContours);
        glyfW.i16(xMin);
        glyfW.i16(yMin);
        glyfW.i16(xMax);
        glyfW.i16(yMax);
        if (numberOfContours > 0) {
            for (uint16_t e : endPts) glyfW.u16(e);
            glyfW.u16((uint16_t)instructions.size());
            glyfW.bytes(instructions);
            // delta-encode points with the standard TrueType flag bytes
            std::vector<uint8_t> outFlags(pointFlags);
            std::vector<int32_t> dxs, dys;
            int32_t px = 0, py = 0;
            for (size_t i = 0; i < points.size(); i++) {
                int32_t dx = points[i].first - px;
                int32_t dy = points[i].second - py;
                px = points[i].first;
                py = points[i].second;
                dxs.push_back(dx);
                dys.push_back(dy);
                if (dx == 0) outFlags[i] |= 0x10;
                else if (dx >= -255 && dx <= 255) { outFlags[i] |= 0x02; if (dx > 0) outFlags[i] |= 0x10; }
                if (dy == 0) outFlags[i] |= 0x20;
                else if (dy >= -255 && dy <= 255) { outFlags[i] |= 0x04; if (dy > 0) outFlags[i] |= 0x20; }
            }
            // repeat compression
            std::vector<uint8_t> finalFlags;
            for (size_t i = 0; i < outFlags.size();) {
                uint8_t f = outFlags[i];
                size_t j = i + 1;
                while (j < outFlags.size() && outFlags[j] == f && j - i < 256) j++;
                finalFlags.push_back(f);
                if (j - i > 1) {
                    finalFlags.push_back((uint8_t)(j - i - 1));
                    finalFlags[finalFlags.size() - 2] |= 0x08;
                }
                i = j;
            }
            for (uint8_t f : finalFlags) glyfW.u8(f);
            for (size_t i = 0; i < points.size(); i++) {
                uint8_t f = outFlags[i];
                if (f & 0x02) glyfW.u8((uint8_t)((f & 0x10) ? dxs[i] : -dxs[i]));
                else if (!(f & 0x10)) glyfW.i16((int16_t)dxs[i]);
            }
            for (size_t i = 0; i < points.size(); i++) {
                uint8_t f = outFlags[i];
                if (f & 0x04) glyfW.u8((uint8_t)((f & 0x20) ? dys[i] : -dys[i]));
                else if (!(f & 0x20)) glyfW.i16((int16_t)dys[i]);
            }
        } else {
            glyfW.bytes(compW.buf());
            if (!instructions.empty()) glyfW.bytes(instructions);
        }
        while (glyfW.offset() % 4) glyfW.u8(0);
        locaOffsets.push_back((uint32_t)glyfW.offset());
    }

    glyf = glyfW.buf();
    Writer locaW;
    bool longLoca = indexFormat != 0;
    for (uint32_t off : locaOffsets) {
        if (longLoca) locaW.u32(off);
        else locaW.u16((uint16_t)(off / 2));
    }
    loca = locaW.buf();
}

} // namespace

// ---------------------------------------------------------------------------
// SfntFont::loadWoff2
// ---------------------------------------------------------------------------

void SfntFont::loadWoff2(Reader r) {
    r.seek(0);
    r.u32(); // signature
    uint32_t flavor = r.u32();
    r.u32(); // length
    uint16_t numTables = r.u16();
    uint16_t reserved = r.u16();
    if (reserved != 0) fail("woff2: reserved field is not zero");
    r.u32(); // totalSfntSize
    uint32_t totalCompressedSize = r.u32();
    r.u16(); r.u16(); // version
    r.u32(); r.u32(); r.u32(); // meta offset/length/origLength
    r.u32(); r.u32(); // priv offset/length
    if (flavor == makeTag('t', 't', 'c', 'f'))
        fail("woff2: font collections are not supported");

    struct Entry {
        Tag tag = 0;
        uint32_t origLength = 0;
        uint32_t transformLength = 0;
        bool transformed = false;
        bool hasTransformLength = false;
    };
    std::vector<Entry> entries(numTables);
    for (uint16_t i = 0; i < numTables; i++) {
        uint8_t flags = r.u8();
        Entry& e = entries[i];
        uint8_t tagIdx = flags & 0x3F;
        uint8_t transformVersion = (uint8_t)((flags >> 6) & 0x03);
        e.tag = tagIdx == 63 ? r.u32() : kWoff2KnownTags[tagIdx];
        e.origLength = readBase128(r);
        if (e.tag == tag_Glyf || e.tag == tag_Loca) {
            e.hasTransformLength = transformVersion != 3;
            e.transformed = transformVersion == 0;
        } else if (e.tag == tag_Hmtx) {
            e.hasTransformLength = transformVersion != 0;
            e.transformed = transformVersion == 1;
        } else {
            if (transformVersion != 0)
                fail("woff2: unexpected transform version for table '" + tagToStr(e.tag) + "'");
            e.hasTransformLength = false;
            e.transformed = false;
        }
        if (e.hasTransformLength) e.transformLength = readBase128(r);
    }

    size_t decompressedSize = 0;
    for (auto& e : entries)
        decompressedSize += e.hasTransformLength ? e.transformLength : e.origLength;

    size_t dirEnd = r.offset();
    if (dirEnd + totalCompressedSize > r.size()) fail("woff2: compressed data out of range");
    std::vector<uint8_t> raw = brotliDecompress(r.data() + dirEnd, totalCompressedSize,
                                                decompressedSize);

    // split the decompressed stream per table
    struct TableData {
        Tag tag;
        std::vector<uint8_t> data;
        bool transformed = false;
    };
    std::vector<TableData> tables;
    size_t pos = 0;
    uint16_t numGlyphs = 0;
    for (auto& e : entries) {
        size_t len = e.hasTransformLength ? e.transformLength : e.origLength;
        if (pos + len > raw.size()) fail("woff2: table data out of range");
        TableData td;
        td.tag = e.tag;
        td.transformed = e.transformed;
        td.data.assign(raw.begin() + pos, raw.begin() + pos + len);
        pos += len;
        tables.push_back(std::move(td));
    }

    // ensure glyf precedes loca handling: reconstruct both
    std::vector<uint8_t> glyfOut, locaOut;
    bool haveGlyfTransform = false;
    uint16_t locaFormat = 1;
    for (auto& td : tables) {
        if (td.tag == tag_Glyf && td.transformed) {
            Woff2Glyf g;
            g.parse(td.data.data(), td.data.size());
            numGlyphs = g.numGlyphs;
            locaFormat = g.indexFormat;
            g.reconstruct();
            glyfOut = std::move(g.glyf);
            locaOut = std::move(g.loca);
            haveGlyfTransform = true;
            break;
        }
    }
    // helper: xMin of a glyph from a reconstructed glyf/loca pair
    auto glyfXMin = [&](uint16_t gid, int16_t& xMin) -> bool {
        uint32_t a = 0, b = 0;
        if (locaFormat == 0) {
            if ((size_t)gid * 2 + 4 > locaOut.size()) return false;
            a = (uint32_t)((locaOut[gid * 2] << 8) | locaOut[gid * 2 + 1]) * 2;
            b = (uint32_t)((locaOut[gid * 2 + 2] << 8) | locaOut[gid * 2 + 3]) * 2;
        } else {
            if ((size_t)gid * 4 + 8 > locaOut.size()) return false;
            auto rd = [&](size_t o) {
                return (uint32_t)((locaOut[o] << 24) | (locaOut[o + 1] << 16) |
                                  (locaOut[o + 2] << 8) | locaOut[o + 3]);
            };
            a = rd((size_t)gid * 4);
            b = rd((size_t)gid * 4 + 4);
        }
        if (a == b) { xMin = 0; return true; }               // empty glyph
        if (a + 4 > glyfOut.size() || b > glyfOut.size()) return false;
        xMin = (int16_t)((glyfOut[a + 2] << 8) | glyfOut[a + 3]);
        return true;
    };
    if (haveGlyfTransform) {
        for (auto& td : tables) {
            if (td.tag == tag_Glyf) {
                td.data = glyfOut;
                td.transformed = false;
            } else if (td.tag == tag_Loca) {
                td.data = locaOut;
                td.transformed = false;
            }
        }
    }

    // hmtx transform needs glyf (for xMin) and hhea (numberOfHMetrics)
    bool haveHmtxTransform = false;
    for (auto& td : tables)
        if (td.tag == tag_Hmtx && td.transformed) haveHmtxTransform = true;
    if (haveHmtxTransform) {
        // find hhea numberOfHMetrics and numGlyphs
        uint16_t numberOfHMetrics = 0;
        for (auto& td : tables) {
            if (td.tag == tag_Hhea && td.data.size() >= 36) {
                numberOfHMetrics = (uint16_t)((td.data[34] << 8) | td.data[35]);
            }
            if (td.tag == tag_Maxp && td.data.size() >= 6 && numGlyphs == 0)
                numGlyphs = (uint16_t)((td.data[4] << 8) | td.data[5]);
        }
        if (numGlyphs == 0) fail("woff2: cannot resolve numGlyphs for transformed hmtx");
        if (numberOfHMetrics > numGlyphs) numberOfHMetrics = numGlyphs;

        for (auto& td : tables) {
            if (td.tag != tag_Hmtx || !td.transformed) continue;
            Reader hr(td.data);
            uint8_t flags = hr.u8();
            if (flags & 0xFC) fail("woff2: reserved bits set in transformed hmtx flags");
            bool hasLsbArray = (flags & 1) == 0;
            bool hasLeftSideBearingArray = (flags & 2) == 0;
            if (!hasLsbArray && !hasLeftSideBearingArray)
                fail("woff2: transformed hmtx has no lsb arrays");
            std::vector<uint16_t> advances(numberOfHMetrics);
            for (auto& a : advances) a = hr.u16();
            std::vector<int16_t> lsbs(numberOfHMetrics, 0);
            if (hasLsbArray) {
                for (auto& l : lsbs) l = hr.i16();
            } else {
                // derive from each glyph's xMin (spec: absent lsb array)
                for (uint16_t g = 0; g < numberOfHMetrics; g++) {
                    int16_t xMin = 0;
                    if (haveGlyfTransform && glyfXMin(g, xMin)) lsbs[g] = xMin;
                }
            }
            size_t nSide = numGlyphs - numberOfHMetrics;
            std::vector<int16_t> leftSideBearings(nSide, 0);
            if (hasLeftSideBearingArray) {
                for (auto& l : leftSideBearings) l = hr.i16();
            } else if (haveGlyfTransform) {
                for (size_t g = 0; g < nSide; g++) {
                    int16_t xMin = 0;
                    if (glyfXMin((uint16_t)(numberOfHMetrics + g), xMin))
                        leftSideBearings[g] = xMin;
                }
            }
            Writer hw;
            for (uint16_t g = 0; g < numberOfHMetrics; g++) {
                hw.u16(advances[g]);
                hw.i16(lsbs[g]);
            }
            for (size_t g = 0; g < nSide; g++)
                hw.i16(leftSideBearings[g]);
            td.data = hw.buf();
            td.transformed = false;
        }
    }

    // rebuild a plain sfnt
    Writer w;
    w.u32(flavor);
    w.u16(numTables);
    uint16_t entrySelector = 0;
    uint16_t searchRange = 16;
    while ((uint32_t)searchRange * 2 <= (uint32_t)numTables * 16) {
        searchRange *= 2;
        entrySelector++;
    }
    w.u16(searchRange);
    w.u16(entrySelector);
    w.u16((uint16_t)(numTables * 16 - searchRange));
    size_t dirPos = w.offset();
    w.zeros((size_t)numTables * 16);

    // sort by tag for the sfnt directory
    std::vector<size_t> order(numTables);
    for (uint16_t i = 0; i < numTables; i++) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return tables[a].tag < tables[b].tag;
    });

    std::vector<uint32_t> offsets(numTables);
    for (uint16_t i = 0; i < numTables; i++) {
        TableData& td = tables[order[i]];
        offsets[i] = (uint32_t)w.offset();
        w.bytes(td.data);
        while (w.offset() % 4) w.u8(0);
    }
    for (uint16_t i = 0; i < numTables; i++) {
        TableData& td = tables[order[i]];
        size_t p = dirPos + (size_t)i * 16;
        uint32_t sum;
        if (td.tag == tag_Head && td.data.size() >= 12) {
            // head's directory checksum is computed with checkSumAdjustment zeroed
            std::vector<uint8_t> tmp(td.data);
            tmp[8] = tmp[9] = tmp[10] = tmp[11] = 0;
            sum = checksumBE(tmp.data(), tmp.size());
        } else {
            sum = checksumBE(td.data.data(), td.data.size());
        }
        w.patch32(p, td.tag);
        w.patch32(p + 4, sum);
        w.patch32(p + 8, offsets[i]);
        w.patch32(p + 12, (uint32_t)td.data.size());
    }

    // Recompute head.checkSumAdjustment so the reconstructed font is valid.
    {
        std::vector<uint8_t>& buf = w.buf();
        uint32_t total = 0;
        for (size_t i = 0; i + 4 <= buf.size(); i += 4)
            total += (uint32_t(buf[i]) << 24) | (uint32_t(buf[i + 1]) << 16) |
                     (uint32_t(buf[i + 2]) << 8) | buf[i + 3];
        uint32_t adjustment = 0xB1B0AFBA - total;
        for (uint16_t i = 0; i < numTables; i++) {
            if (tables[order[i]].tag != tag_Head) continue;
            uint32_t off = offsets[i];
            if (off + 12 > buf.size()) break;
            buf[off + 8] = (uint8_t)(adjustment >> 24);
            buf[off + 9] = (uint8_t)(adjustment >> 16);
            buf[off + 10] = (uint8_t)(adjustment >> 8);
            buf[off + 11] = (uint8_t)adjustment;
            // directory checksum stays as computed (adjustment zeroed)
            break;
        }
    }

    recon_ = w.buf();
    buf_ = recon_.data();
    bufSize_ = recon_.size();
    container_ = ContainerType::Woff2;
    parseSfntDirectory(Reader(recon_), 0);
}

} // namespace ftk
