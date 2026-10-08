// Subsetting implementation.
#include "ftk/Subset.hpp"
#include "ftk/Inflate.hpp"

#include <algorithm>
#include <cstring>
#include <map>

namespace ftk {

// ---------------------------------------------------------------------------
// CFF dict re-encoding with position-tracked int32 patch slots
// ---------------------------------------------------------------------------

static void encodeCffInt(Writer& w, int64_t v) {
    // Single-byte operands cover -107..107 (bytes 32..246); -139..-108 would
    // collide with operator byte values.
    if (v >= -107 && v <= 107) {
        w.u8((uint8_t)(v + 139));
    } else if (v >= 108 && v <= 1131) {
        int32_t b = (int32_t)v - 108;
        w.u8((uint8_t)((b >> 8) + 247));
        w.u8((uint8_t)(b & 0xFF));
    } else if (v >= -1131 && v <= -108) {
        int32_t b = -v - 108;
        w.u8((uint8_t)((b >> 8) + 251));
        w.u8((uint8_t)(b & 0xFF));
    } else if (v >= -32768 && v <= 32767) {
        w.u8(28);
        w.i16((int16_t)v);
    } else {
        w.u8(29);
        w.i32((int32_t)v);
    }
}

static void encodeCffReal(Writer& w, double d) {
    char buf[32];
    snprintf(buf, sizeof buf, "%.6g", d);
    std::string nibbles;
    for (char c : buf) {
        if (c >= '0' && c <= '9') nibbles += c;
        else if (c == '.') nibbles += 'a';
        else if (c == '-') nibbles += 'e';
        else if (c == 'E' || c == 'e') nibbles += 'b';
    }
    // exactly one 0xF terminator; pad with 0xF to a whole number of bytes
    nibbles += 'f';
    if (nibbles.size() % 2) nibbles += 'f';
    w.u8(30);
    for (size_t k = 0; k < nibbles.size(); k += 2) {
        auto nib = [](char c) -> uint8_t {
            if (c <= '9') return (uint8_t)(c - '0');
            return (uint8_t)(c - 'a' + 10);
        };
        w.u8((uint8_t)((nib(nibbles[k]) << 4) | nib(nibbles[k + 1])));
    }
}

// Emit a dict entry. markerOperand: index of operand to emit as a 29-encoded
// placeholder (recorded in markerPositions).
static void emitEntry(Writer& w, const DictEntry& e, int markerOperand,
                      std::vector<size_t>* markerPositions) {
    for (size_t i = 0; i < e.operands.size(); i++) {
        if ((int)i == markerOperand) {
            if (markerPositions) markerPositions->push_back(w.offset());
            w.u8(29);
            w.i32(0x7FFFFFF0); // patch marker
            continue;
        }
        const DictNumber& n = e.operands[i];
        if (n.isInt) encodeCffInt(w, n.i);
        else encodeCffReal(w, n.d);
    }
    if (e.op == 12) {
        w.u8(12);
        w.u8(e.op2); // escaped operator second byte
    } else {
        w.u8(e.op);
    }
}

static void patchMarker(std::vector<uint8_t>& buf, size_t pos, uint32_t value) {
    buf[pos + 1] = (uint8_t)(value >> 24);
    buf[pos + 2] = (uint8_t)(value >> 16);
    buf[pos + 3] = (uint8_t)(value >> 8);
    buf[pos + 4] = (uint8_t)value;
}

// Build a CFF INDEX from items.
static void buildIndex(Writer& w, const std::vector<std::vector<uint8_t>>& items) {
    if (items.empty()) {
        w.u16(0);
        return;
    }
    std::vector<uint32_t> offs;
    uint32_t cur = 1;
    offs.push_back(1);
    for (auto& it : items) {
        cur += (uint32_t)it.size();
        offs.push_back(cur);
    }
    w.u16((uint16_t)items.size());
    uint32_t last = offs.back();
    uint8_t offSize = last < 0x100 ? 1 : last < 0x10000 ? 2 : last < 0x1000000 ? 3 : 4;
    w.u8(offSize);
    for (uint32_t off : offs)
        for (int k = offSize - 1; k >= 0; k--)
            w.u8((uint8_t)((off >> (8 * k)) & 0xFF));
    for (auto& it : items)
        w.bytes(it);
}

static std::vector<uint8_t> indexFromItems(const CffIndex& idx) {
    Writer w;
    std::vector<std::vector<uint8_t>> items;
    for (size_t i = 0; i < idx.count(); i++) {
        size_t l;
        const uint8_t* p = idx.itemData(i, l);
        items.emplace_back(p, p + l);
    }
    buildIndex(w, items);
    return w.buf();
}

// ---------------------------------------------------------------------------
// subset
// ---------------------------------------------------------------------------

std::vector<uint8_t> subsetFont(const Font& font, const SubsetPlan& plan) {
    uint16_t numGlyphs = font.numGlyphs();
    if (numGlyphs == 0) fail("subset: font has no glyphs");

    // 1. keep set
    std::vector<bool> keep(numGlyphs, false);
    keep[0] = true; // .notdef
    for (uint16_t g : plan.gids)
        if (g < numGlyphs) keep[g] = true;
    for (auto& n : plan.names) {
        uint16_t g = font.glyphForName(n);
        if (g && g < numGlyphs) keep[g] = true;
    }
    if (!plan.unicodes.empty()) {
        auto gids = font.glyphIdsForUnicodes(plan.unicodes);
        for (uint16_t g : gids)
            if (g && g < numGlyphs) keep[g] = true;
    } else if (plan.keepEverything) {
        for (uint16_t g = 0; g < numGlyphs; g++) keep[g] = true;
    }
    if (font.glyf()) {
        std::vector<uint16_t> seeds;
        for (uint16_t g = 0; g < numGlyphs; g++)
            if (keep[g]) seeds.push_back(g);
        font.compositeClosure(seeds, keep);
    }

    std::vector<uint16_t> oldToNew(numGlyphs, 0xFFFF);
    uint16_t newCount = 0;
    for (uint16_t g = 0; g < numGlyphs; g++)
        if (keep[g]) oldToNew[g] = newCount++;
    if (newCount == 0) fail("subset: nothing to keep");

    struct NewTable { Tag tag; std::vector<uint8_t> data; };
    std::vector<NewTable> out;

    auto copyTable = [&](Tag t) -> bool {
        size_t len;
        const uint8_t* p = font.rawTable(t, len);
        if (!p) return false;
        out.push_back({t, std::vector<uint8_t>(p, p + len)});
        return true;
    };

    OutlineFormat of = font.outlineFormat();

    if (of == OutlineFormat::TrueType) {
        const GlyfTable* gt = font.glyf();
        if (!gt) fail("subset: glyf unavailable");
        Writer gw;
        std::vector<uint32_t> newLoca;
        for (uint16_t g = 0; g < numGlyphs; g++) {
            if (!keep[g]) continue;
            newLoca.push_back((uint32_t)gw.offset());
            Glyph gl = gt->glyph(g);
            if (gl.kind == Glyph::Kind::Empty) {
                // zero-length entry
            } else if (gl.kind == Glyph::Kind::Simple) {
                gw.i16((int16_t)gl.endPts.size());
                gw.i16(gl.bounds.xMin);
                gw.i16(gl.bounds.yMin);
                gw.i16(gl.bounds.xMax);
                gw.i16(gl.bounds.yMax);
                for (uint16_t e : gl.endPts) gw.u16(e);
                gw.u16(0); // instructionLength (drop hints)
                // flags + deltas
                std::vector<uint8_t> flags;
                std::vector<int32_t> dxs, dys;
                int32_t px = 0, py = 0;
                for (size_t i = 0; i < gl.points.size(); i++) {
                    int32_t dx = gl.points[i].x - px;
                    int32_t dy = gl.points[i].y - py;
                    px = gl.points[i].x;
                    py = gl.points[i].y;
                    dxs.push_back(dx);
                    dys.push_back(dy);
                    uint8_t f = gl.points[i].onCurve ? 0x01 : 0x00;
                    if (dx == 0) f |= 0x10;
                    else if (dx >= -255 && dx <= 255) { f |= 0x02; if (dx > 0) f |= 0x10; }
                    if (dy == 0) f |= 0x20;
                    else if (dy >= -255 && dy <= 255) { f |= 0x04; if (dy > 0) f |= 0x20; }
                    flags.push_back(f);
                }
                // repeat compression
                std::vector<uint8_t> finalFlags;
                for (size_t i = 0; i < flags.size();) {
                    uint8_t f = flags[i];
                    size_t j = i + 1;
                    while (j < flags.size() && flags[j] == f && j - i < 256) j++;
                    finalFlags.push_back(f);
                    if (j - i > 1) {
                        finalFlags.push_back((uint8_t)(j - i - 1));
                        finalFlags[finalFlags.size() - 2] |= 0x08;
                    }
                    i = j;
                }
                for (uint8_t f : finalFlags) gw.u8(f);
                for (size_t i = 0; i < gl.points.size(); i++) {
                    uint8_t f = flags[i];
                    if (f & 0x02) gw.u8((uint8_t)(f & 0x10 ? dxs[i] : -dxs[i]));
                    else if (!(f & 0x10)) gw.i16((int16_t)dxs[i]);
                }
                for (size_t i = 0; i < gl.points.size(); i++) {
                    uint8_t f = flags[i];
                    if (f & 0x04) gw.u8((uint8_t)(f & 0x20 ? dys[i] : -dys[i]));
                    else if (!(f & 0x20)) gw.i16((int16_t)dys[i]);
                }
            } else {
                // composite: remap component gids
                gw.i16(-1);
                gw.i16(gl.bounds.xMin);
                gw.i16(gl.bounds.yMin);
                gw.i16(gl.bounds.xMax);
                gw.i16(gl.bounds.yMax);
                for (size_t ci = 0; ci < gl.components.size(); ci++) {
                    const GlyfComponent& c = gl.components[ci];
                    uint16_t newGid = c.glyph < numGlyphs ? oldToNew[c.glyph] : 0xFFFF;
                    if (newGid == 0xFFFF)
                        fail("subset: internal composite closure error (gid " +
                             std::to_string(g) + " references " + std::to_string(c.glyph) +
                             ", keep=" + (c.glyph < numGlyphs && keep[c.glyph] ? "1" : "0") + ")");
                    uint16_t flags = c.flags;
                    flags |= 0x0001;  // ARG_1_AND_2_ARE_WORDS
                    flags &= (uint16_t)~0x0100; // drop WE_HAVE_INSTRUCTIONS
                    if (ci + 1 == gl.components.size()) flags &= (uint16_t)~0x0020;
                    else flags |= 0x0020;
                    gw.u16(flags);
                    gw.u16(newGid);
                    gw.i16((int16_t)c.arg1);
                    gw.i16((int16_t)c.arg2);
                    if (flags & 0x0008) gw.f2dot14(c.a);
                    else if (flags & 0x0040) { gw.f2dot14(c.a); gw.f2dot14(c.d); }
                    else if (flags & 0x0080) {
                        gw.f2dot14(c.a);
                        gw.f2dot14(c.b);
                        gw.f2dot14(c.c);
                        gw.f2dot14(c.d);
                    }
                }
            }
            while (gw.offset() % 4) gw.u8(0);
        }
        newLoca.push_back((uint32_t)gw.offset());

        bool longLoca = gw.offset() > 0x1FFFC;
        Writer lw;
        for (uint32_t v : newLoca) {
            if (longLoca) lw.u32(v);
            else lw.u16(v / 2);
        }
        out.push_back({tag_Glyf, gw.buf()});
        out.push_back({tag_Loca, lw.buf()});

        // head: patch indexToLocFormat + bounds
        std::vector<uint8_t> headData = font.rawTableCopy(tag_Head);
        if (headData.size() >= 52) {
            headData[50] = 0;
            headData[51] = (uint8_t)(longLoca ? 1 : 0);
            int16_t xMin = 0, yMin = 0, xMax = 0, yMax = 0;
            bool first = true;
            for (uint16_t g = 0; g < numGlyphs; g++) {
                if (!keep[g]) continue;
                Glyph gl = gt->glyph(g);
                if (gl.kind == Glyph::Kind::Empty) continue;
                if (first) {
                    xMin = gl.bounds.xMin; yMin = gl.bounds.yMin;
                    xMax = gl.bounds.xMax; yMax = gl.bounds.yMax;
                    first = false;
                } else {
                    xMin = std::min(xMin, gl.bounds.xMin);
                    yMin = std::min(yMin, gl.bounds.yMin);
                    xMax = std::max(xMax, gl.bounds.xMax);
                    yMax = std::max(yMax, gl.bounds.yMax);
                }
            }
            if (!first) {
                headData[36] = (uint8_t)(xMin >> 8); headData[37] = (uint8_t)xMin;
                headData[38] = (uint8_t)(yMin >> 8); headData[39] = (uint8_t)yMin;
                headData[40] = (uint8_t)(xMax >> 8); headData[41] = (uint8_t)xMax;
                headData[42] = (uint8_t)(yMax >> 8); headData[43] = (uint8_t)yMax;
            }
        }
        out.push_back({tag_Head, headData});

        const Hmtx* hm = font.hmtx();
        Writer hw;
        for (uint16_t g = 0; g < numGlyphs; g++) {
            if (!keep[g]) continue;
            hw.u16(hm ? hm->advance(g) : 0);
            hw.i16(hm ? hm->bearing(g) : 0);
        }
        out.push_back({tag_Hmtx, hw.buf()});

        std::vector<uint8_t> hheaData = font.rawTableCopy(tag_Hhea);
        if (hheaData.size() >= 36) {
            hheaData[34] = (uint8_t)(newCount >> 8);
            hheaData[35] = (uint8_t)newCount;
        }
        if (!hheaData.empty()) out.push_back({tag_Hhea, hheaData});

        std::vector<uint8_t> maxpData = font.rawTableCopy(tag_Maxp);
        if (maxpData.size() >= 6) {
            maxpData[4] = (uint8_t)(newCount >> 8);
            maxpData[5] = (uint8_t)newCount;
        }
        out.push_back({tag_Maxp, maxpData});
    } else if (of == OutlineFormat::CFF || of == OutlineFormat::CFF2) {
        const CffFont* cff = font.cff();
        if (!cff) fail("subset: CFF unavailable");
        bool cff2 = cff->isCFF2();
        Tag cffTag = cff2 ? tag_CFF2 : tag_CFF;

        // kept charstrings + sids
        std::vector<std::vector<uint8_t>> charstrings;
        std::vector<uint32_t> keptSids;
        for (uint16_t g = 0; g < numGlyphs; g++) {
            if (!keep[g]) continue;
            size_t len;
            const uint8_t* p = cff->charstringBytes(g, len);
            charstrings.emplace_back(p, p + len);
            keptSids.push_back(cff->glyphSID(g));
        }

        // charset (format 0)
        Writer charsetW;
        charsetW.u8(0);
        for (size_t i = 1; i < charstrings.size(); i++)
            charsetW.u16((uint16_t)keptSids[i]);

        // Only the font dicts (FDs) actually referenced by kept glyphs need to
        // be carried over; remap old -> new FD indices so the subset stays small.
        std::vector<int32_t> fdRemap(cff->fonts.size(), -1);
        std::vector<uint16_t> usedFDs;
        bool useFdSelect = cff->hasFDSelect;
        if (useFdSelect) {
            for (uint16_t g = 0; g < numGlyphs; g++) {
                if (!keep[g]) continue;
                uint16_t fd = cff->fdOfGlyph(g);
                if (fd >= fdRemap.size()) fd = 0;
                if (fdRemap[fd] < 0) {
                    fdRemap[fd] = (int32_t)usedFDs.size();
                    usedFDs.push_back(fd);
                }
            }
        } else {
            for (size_t i = 0; i < cff->fonts.size(); i++) {
                fdRemap[i] = (int32_t)i;
                usedFDs.push_back((uint16_t)i);
            }
        }
        if (usedFDs.empty()) usedFDs.push_back(0);

        // FDSelect format 0 (remapped FD indices)
        Writer fdselectW;
        if (useFdSelect) {
            fdselectW.u8(0);
            for (uint16_t g = 0; g < numGlyphs; g++) {
                if (!keep[g]) continue;
                uint16_t fd = cff->fdOfGlyph(g);
                int32_t nf = fd < fdRemap.size() ? fdRemap[fd] : 0;
                fdselectW.u8((uint8_t)(nf < 0 ? 0 : nf));
            }
        }

        // private blocks (private dict + trailing local subr INDEX, verbatim)
        std::vector<std::vector<uint8_t>> privBlocks(usedFDs.size());
        for (size_t k = 0; k < usedFDs.size(); k++) {
            size_t fi = usedFDs[k];
            const CffFontDict& fd = cff->fonts[fi];
            if (!(fd.privData && fd.privSize > 0)) continue;
            size_t len;
            const uint8_t* tbl = font.rawTable(cffTag, len);
            if (!tbl || (size_t)fd.privOffset + fd.privSize > len) continue;
            size_t total = (size_t)fd.privSize;
            if (fd.priv.subrs >= 0) {
                const CffIndex* ls = cff->localSubrs(fi);
                if (ls) {
                    uint32_t endPos = ls->endOffset();
                    if (endPos > (uint32_t)fd.privOffset && endPos <= len)
                        total = endPos - (uint32_t)fd.privOffset;
                }
            }
            privBlocks[k].assign(tbl + fd.privOffset, tbl + fd.privOffset + total);
        }

        // top dict with patch markers
        Writer td;
        std::vector<size_t> topMarkers;
        for (auto& e : cff->topDict.entries) {
            int marker = -1;
            if (e.op == 15 || e.op == 16 || e.op == 17 || e.op == 24 ||
                (e.op == 12 && (e.op2 == 36 || e.op2 == 37)))
                marker = 0;
            else if (e.op == 18)
                marker = 1; // [size, offset] — patch offset operand
            emitEntry(td, e, marker, &topMarkers);
        }
        std::vector<uint8_t> topDictData = td.buf();

        // FDArray items with private-offset markers (used FDs only)
        std::vector<std::vector<uint8_t>> fdItems;
        std::vector<std::vector<size_t>> fdMarkers;
        bool hasFdArray = cff->fdArrayIndex.count() > 0;
        if (hasFdArray) {
            for (size_t k = 0; k < usedFDs.size(); k++) {
                size_t fi = usedFDs[k];
                Writer fw2;
                std::vector<size_t> marks;
                for (auto& e : cff->fonts[fi].dict.entries) {
                    int marker = (e.op == 18) ? 1 : -1;
                    emitEntry(fw2, e, marker, &marks);
                }
                fdItems.push_back(fw2.buf());
                fdMarkers.push_back(marks);
            }
        }

        // assemble CFF
        Writer cw;
        std::vector<uint8_t> nameIdxData = cff2 ? std::vector<uint8_t>{}
                                                : indexFromItems(cff->nameIndex);
        std::vector<uint8_t> stringIdxData = cff2 ? std::vector<uint8_t>{}
                                                  : indexFromItems(cff->stringIndex);
        std::vector<uint8_t> gsubrIdxData = indexFromItems(cff->globalSubrs);
        size_t topDictBase = 0; // where topDictData lands inside cw

        if (cff2) {
            cw.u8(2);
            cw.u8(0);
            cw.u8(5); // hdrSize
            cw.u16((uint16_t)topDictData.size());
            topDictBase = cw.offset();
            cw.bytes(topDictData);
            cw.bytes(gsubrIdxData);
        } else {
            cw.u8(1);
            cw.u8(0);
            cw.u8(4); // hdrSize
            cw.u8(4); // offSize
            cw.bytes(nameIdxData);
            {
                Writer w2;
                std::vector<std::vector<uint8_t>> items{topDictData};
                buildIndex(w2, items);
                std::vector<uint8_t> idx = w2.buf();
                // items are written last, so the dict data starts here:
                topDictBase = cw.offset() + (idx.size() - topDictData.size());
                cw.bytes(idx);
            }
            cw.bytes(stringIdxData);
            cw.bytes(gsubrIdxData);
        }

        uint32_t offCharset = (uint32_t)cw.offset();
        cw.bytes(charsetW.buf());

        uint32_t offFdSelect = (uint32_t)cw.offset();
        if (useFdSelect) cw.bytes(fdselectW.buf());

        uint32_t offCharStrings = (uint32_t)cw.offset();
        {
            Writer w2;
            buildIndex(w2, charstrings);
            cw.bytes(w2.buf());
        }

        uint32_t offFdArray = 0;
        if (hasFdArray) {
            offFdArray = (uint32_t)cw.offset();
            Writer w2;
            buildIndex(w2, fdItems);
            cw.bytes(w2.buf());
        }

        std::vector<uint32_t> privOffsets(cff->fonts.size(), 0);
        for (size_t fi = 0; fi < privBlocks.size(); fi++) {
            if (privBlocks[fi].empty()) continue;
            privOffsets[fi] = (uint32_t)cw.offset();
            cw.bytes(privBlocks[fi]);
        }

        uint32_t offVarStore = 0;
        if (cff2 && cff->varStoreOffset >= 0) {
            size_t len;
            const uint8_t* tbl = font.rawTable(tag_CFF2, len);
            if (tbl && (size_t)cff->varStoreOffset < len) {
                offVarStore = (uint32_t)cw.offset();
                cw.bytes(tbl + cff->varStoreOffset, len - cff->varStoreOffset);
            }
        }

        // patch top dict markers (order matches entry walk; markers exist only
        // for offset-bearing operators)
        {
            size_t mi = 0;
            for (auto& e : cff->topDict.entries) {
                bool hasMarker = (e.op == 15 || e.op == 16 || e.op == 17 || e.op == 24 ||
                                  (e.op == 12 && (e.op2 == 36 || e.op2 == 37)) || e.op == 18);
                if (!hasMarker) continue;
                if (mi >= topMarkers.size()) break;
                uint32_t value = 0;
                if (e.op == 15) value = offCharset;
                else if (e.op == 16) value = 0; // standard encoding
                else if (e.op == 17) value = offCharStrings;
                else if (e.op == 24) value = offVarStore;
                else if (e.op == 12 && e.op2 == 36) value = offFdArray;
                else if (e.op == 12 && e.op2 == 37) value = useFdSelect ? offFdSelect : 0;
                else if (e.op == 18) value = privOffsets.empty() ? 0 : privOffsets[0];
                patchMarker(cw.buf(), topDictBase + topMarkers[mi], value);
                mi++;
            }
        }
        // patch FDArray markers: positions are relative to each item's start
        if (hasFdArray) {
            // locate each item's absolute start via the INDEX we wrote
            // (parse the INDEX header at offFdArray)
            const std::vector<uint8_t>& buf = cw.buf();
            size_t p = offFdArray;
            uint16_t count = (uint16_t)((buf[p] << 8) | buf[p + 1]);
            uint8_t offSize = buf[p + 2];
            auto readOff = [&](size_t q) -> uint32_t {
                uint32_t v = 0;
                for (int k = 0; k < offSize; k++) v = (v << 8) | buf[q + k];
                return v;
            };
            std::vector<uint32_t> offs;
            for (uint16_t i = 0; i <= count; i++)
                offs.push_back(readOff(p + 3 + (size_t)i * offSize));
            uint32_t dataStart = (uint32_t)(p + 3 + (size_t)(count + 1) * offSize);
            for (uint16_t i = 0; i < count && i < fdItems.size(); i++) {
                size_t itemStart = dataStart + (offs[i] - offs[0]);
                for (size_t m : fdMarkers[i]) {
                    uint32_t v = i < privOffsets.size() ? privOffsets[i] : 0;
                    patchMarker(cw.buf(), itemStart + m, v);
                }
            }
        }

        out.push_back({cffTag, cw.buf()});

        // hmtx/hhea/maxp/head
        const Hmtx* hm = font.hmtx();
        Writer hw;
        for (uint16_t g = 0; g < numGlyphs; g++)
            if (keep[g]) {
                hw.u16(hm ? hm->advance(g) : 0);
                hw.i16(hm ? hm->bearing(g) : 0);
            }
        out.push_back({tag_Hmtx, hw.buf()});

        std::vector<uint8_t> hheaData = font.rawTableCopy(tag_Hhea);
        if (hheaData.size() >= 36) {
            hheaData[34] = (uint8_t)(newCount >> 8);
            hheaData[35] = (uint8_t)newCount;
        }
        if (!hheaData.empty()) out.push_back({tag_Hhea, hheaData});

        std::vector<uint8_t> maxpData = font.rawTableCopy(tag_Maxp);
        if (maxpData.size() >= 6) {
            maxpData[4] = (uint8_t)(newCount >> 8);
            maxpData[5] = (uint8_t)newCount;
        }
        out.push_back({tag_Maxp, maxpData});
        copyTable(tag_Head);
    } else {
        fail("subset: unsupported outline format");
    }

    // post version 3.0 for all formats
    {
        Writer pw;
        if (const Post* p = font.post()) {
            pw.fixed16(3.0);
            pw.fixed16(p->italicAngle);
            pw.i16(p->underlinePosition);
            pw.i16(p->underlineThickness);
            pw.u32(p->isFixedPitch);
            pw.u32(p->minMemType42);
            pw.u32(p->maxMemType42);
            pw.u32(p->minMemType1);
            pw.u32(p->maxMemType1);
        } else {
            pw.fixed16(3.0);
            pw.fixed16(0);
            pw.i16(-100);
            pw.i16(50);
            pw.u32(0);
            pw.u32(0);
            pw.u32(0);
            pw.u32(0);
            pw.u32(0);
        }
        out.push_back({tag_Post, pw.buf()});
    }

    // cmap
    {
        std::vector<std::pair<uint32_t, uint16_t>> entries;
        if (const Cmap* c = font.cmap()) {
            if (const CmapSubtable* b = c->best()) {
                for (auto& e : b->map) {
                    uint16_t oldGid = e.second;
                    if (oldGid < numGlyphs && keep[oldGid] && oldToNew[oldGid] != 0xFFFF)
                        entries.emplace_back(e.first, oldToNew[oldGid]);
                }
            }
        }
        Writer cw2;
        cw2.u16(0);
        cw2.u16(1);
        bool needsFmt12 = false;
        for (auto& e : entries)
            if (e.first > 0xFFFF) { needsFmt12 = true; break; }
        if (needsFmt12) {
            cw2.u16(3);
            cw2.u16(10);
            cw2.u32(12);
            struct Grp { uint32_t start, end, val; };
            std::vector<Grp> grps;
            for (auto& e : entries) {
                if (!grps.empty() && e.first == grps.back().end + 1 &&
                    e.second == grps.back().val + (e.first - grps.back().start))
                    grps.back().end = e.first;
                else
                    grps.push_back({e.first, e.first, e.second});
            }
            cw2.u16(12);
            cw2.u16(0);
            cw2.u32(0);
            cw2.u32((uint32_t)grps.size());
            for (auto& g : grps) {
                cw2.u32(g.start);
                cw2.u32(g.end);
                cw2.u32(g.val);
            }
        } else {
            cw2.u16(3);
            cw2.u16(1);
            cw2.u32(12); // subtable follows the 4-byte header + one 8-byte record
            struct Seg { uint32_t start, end; int32_t delta; };
            std::vector<Seg> segs;
            for (auto& e : entries) {
                if (!segs.empty() && e.first == segs.back().end + 1) {
                    uint32_t len = segs.back().end - segs.back().start;
                    uint16_t expect = (uint16_t)(segs.back().start + len + 1 + segs.back().delta);
                    if (e.second == expect) {
                        segs.back().end = e.first;
                        continue;
                    }
                }
                Seg s;
                s.start = s.end = e.first;
                s.delta = ((int32_t)e.second - (int32_t)e.first) & 0xFFFF;
                // delta as int16
                if (s.delta > 0x7FFF) s.delta -= 0x10000;
                segs.push_back(s);
            }
            segs.push_back({0xFFFF, 0xFFFF, 1});
            uint16_t segCount = (uint16_t)segs.size();
            uint16_t searchRange = 2, entrySelector = 0;
            while ((uint32_t)searchRange * 2 <= (uint32_t)segCount * 2) {
                searchRange *= 2;
                entrySelector++;
            }
            uint16_t rangeShift = (uint16_t)(segCount * 2 - searchRange);
            cw2.u16(4);
            cw2.u16((uint16_t)(16 + segCount * 8));
            cw2.u16(0);
            cw2.u16(segCount * 2);
            cw2.u16(searchRange);
            cw2.u16(entrySelector);
            cw2.u16(rangeShift);
            for (auto& s : segs) cw2.u16((uint16_t)s.end);
            cw2.u16(0);
            for (auto& s : segs) cw2.u16((uint16_t)s.start);
            for (auto& s : segs) cw2.i16((int16_t)s.delta);
            for (size_t i = 0; i < segs.size(); i++) cw2.u16(0);
        }
        out.push_back({tag_Cmap, cw2.buf()});
    }

    // passthrough tables
    if (plan.keepNameTable) copyTable(tag_Name);
    if (plan.keepOs2) copyTable(tag_Os2);
    copyTable(tag_Gasp);
    copyTable(tag_Cvt_);
    copyTable(tag_Fpgm);
    copyTable(tag_Prep);
    if (plan.keepVertical) {
        std::vector<uint8_t> vheaData = font.rawTableCopy(tag_Vhea);
        if (vheaData.size() >= 36) {
            vheaData[34] = (uint8_t)(newCount >> 8);
            vheaData[35] = (uint8_t)newCount;
            out.push_back({tag_Vhea, vheaData});
            const Hmtx* vm = font.vmtx();
            if (vm) {
                Writer vw;
                vw.u16(vm->advance(0));
                vw.i16(vm->bearing(0));
                for (uint16_t g = 1; g < numGlyphs; g++)
                    if (keep[g]) vw.i16(vm->bearing(g));
                out.push_back({tag_Vmtx, vw.buf()});
            }
        }
        copyTable(tag_VORG);
    }
    if (plan.keepLayout) {
        copyTable(tag_GDEF);
        copyTable(tag_GSUB);
        copyTable(tag_GPOS);
        copyTable(tag_Kern);
        copyTable(tag_BASE);
    }
    for (Tag t : plan.dropTables) {
        (void)t; // drop = simply don't copy (already not copied)
    }

    // assemble sfnt
    std::sort(out.begin(), out.end(), [](const NewTable& a, const NewTable& b) { return a.tag < b.tag; });
    out.erase(std::unique(out.begin(), out.end(),
                          [](const NewTable& a, const NewTable& b) { return a.tag == b.tag; }),
              out.end());

    Writer fw;
    uint32_t sfntVer = font.sfnt().sfntVersion();
    fw.u32(sfntVer);
    uint16_t n = (uint16_t)out.size();
    fw.u16(n);
    uint16_t entrySelector = 0;
    uint16_t searchRange = 16;
    while ((uint32_t)searchRange * 2 <= (uint32_t)n * 16) {
        searchRange *= 2;
        entrySelector++;
    }
    fw.u16(searchRange);
    fw.u16(entrySelector);
    fw.u16((uint16_t)(n * 16 - searchRange));
    size_t dirPos = fw.offset();
    fw.zeros((size_t)n * 16);
    std::vector<uint32_t> tablePos(n);
    for (uint16_t i = 0; i < n; i++) {
        tablePos[i] = (uint32_t)fw.offset();
        fw.bytes(out[i].data);
        while (fw.offset() % 4) fw.u8(0);
        size_t dp = dirPos + (size_t)i * 16;
        fw.patch32(dp, out[i].tag);
        fw.patch32(dp + 4, checksumBE(out[i].data.data(), out[i].data.size()));
        fw.patch32(dp + 8, tablePos[i]);
        fw.patch32(dp + 12, (uint32_t)out[i].data.size());
    }

    // head.checkSumAdjustment
    auto& finalBuf = fw.buf();
    uint32_t total = 0;
    for (size_t i = 0; i + 4 <= finalBuf.size(); i += 4)
        total += (uint32_t(finalBuf[i]) << 24) | (uint32_t(finalBuf[i + 1]) << 16) |
                 (uint32_t(finalBuf[i + 2]) << 8) | finalBuf[i + 3];
    uint32_t adjustment = 0xB1B0AFBA - total;
    for (uint16_t i = 0; i < n; i++) {
        if (out[i].tag == tag_Head) {
            uint32_t off = tablePos[i];
            if (off + 12 <= finalBuf.size()) {
                finalBuf[off + 8] = (uint8_t)(adjustment >> 24);
                finalBuf[off + 9] = (uint8_t)(adjustment >> 16);
                finalBuf[off + 10] = (uint8_t)(adjustment >> 8);
                finalBuf[off + 11] = (uint8_t)adjustment;
                std::vector<uint8_t> headCopy(finalBuf.begin() + off,
                                              finalBuf.begin() + off +
                                                  std::min<size_t>(finalBuf.size() - off, 54));
                if (headCopy.size() >= 12) {
                    headCopy[8] = headCopy[9] = headCopy[10] = headCopy[11] = 0;
                    uint32_t sum = checksumBE(headCopy.data(), headCopy.size());
                    finalBuf[dirPos + (size_t)i * 16 + 4] = (uint8_t)(sum >> 24);
                    finalBuf[dirPos + (size_t)i * 16 + 5] = (uint8_t)(sum >> 16);
                    finalBuf[dirPos + (size_t)i * 16 + 6] = (uint8_t)(sum >> 8);
                    finalBuf[dirPos + (size_t)i * 16 + 7] = (uint8_t)sum;
                }
            }
            break;
        }
    }

    return finalBuf;
}

// ---------------------------------------------------------------------------
// WOFF conversion
// ---------------------------------------------------------------------------

std::vector<uint8_t> sfntToWoff(const std::vector<uint8_t>& sfntBytes) {
    if (sfntBytes.size() < 12) fail("woff: input too small");
    Reader r(sfntBytes);
    uint32_t flavor = r.u32();
    uint16_t numTables = r.u16();
    struct Rec { Tag tag; uint32_t off, len; };
    std::vector<Rec> recs(numTables);
    for (uint16_t i = 0; i < numTables; i++) {
        recs[i].tag = r.u32();
        r.u32(); // checksum
        recs[i].off = r.u32();
        recs[i].len = r.u32();
    }

    Writer w;
    w.u32(makeTag('w', 'O', 'F', 'F'));
    w.u32(flavor);
    w.u32(0); // length
    w.u16(numTables);
    w.u16(0);
    w.u32(0); // totalSfntSize
    w.u16(1);
    w.u16(0);
    w.u32(0); w.u32(0); w.u32(0); // meta
    w.u32(0); w.u32(0);           // private

    uint32_t totalSfnt = 12 + 16 * numTables;
    for (auto& e : recs) totalSfnt += (e.len + 3) & ~3u;

    std::vector<std::vector<uint8_t>> compressed;
    for (auto& e : recs)
        compressed.push_back(zlibCompressStored(sfntBytes.data() + e.off, e.len));

    size_t dirPos = w.offset();
    w.zeros((size_t)numTables * 20);
    uint32_t cur = (uint32_t)(dirPos + (size_t)numTables * 20);
    for (uint16_t i = 0; i < numTables; i++) {
        size_t p = dirPos + (size_t)i * 20;
        w.patch32(p, recs[i].tag);
        w.patch32(p + 4, cur);
        w.patch32(p + 8, (uint32_t)compressed[i].size());
        w.patch32(p + 12, recs[i].len);
        w.patch32(p + 16, checksumBE(sfntBytes.data() + recs[i].off, recs[i].len));
        cur += (uint32_t)((compressed[i].size() + 3) & ~3u);
    }
    for (auto& c : compressed) {
        w.bytes(c);
        while (w.offset() % 4) w.u8(0);
    }
    w.patch32(4, (uint32_t)w.offset());
    w.patch32(12, totalSfnt);
    return w.buf();
}

} // namespace ftk
