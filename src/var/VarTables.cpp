// Variable font tables implementation.
#include "ftk/VarTables.hpp"

#include <algorithm>
#include <cmath>

namespace ftk {

// ---------------------------------------------------------------- fvar

void Fvar::parse(Reader r) {
    r = Reader(r.data(), r.size());
    majorVersion = r.u16();
    minorVersion = r.u16();
    axesArrayOffset = r.u16();
    reserved = r.u16();
    uint16_t axisCount = r.u16();
    uint16_t axisSize = r.u16();
    uint16_t instanceCount = r.u16();
    instanceSize = r.u16();

    if (axesArrayOffset > r.size()) fail("fvar: axes array out of range");
    Reader ar = r.subAt(axesArrayOffset, r.size() - axesArrayOffset);
    for (uint16_t i = 0; i < axisCount; i++) {
        if (ar.remaining() < 20) break;
        FvarAxis ax;
        ax.tag = ar.u32();
        ax.minValue = ar.fixed16();
        ax.defaultValue = ar.fixed16();
        ax.maxValue = ar.fixed16();
        ax.flags = ar.u16();
        ax.nameID = ar.u16();
        axes.push_back(ax);
        if (axisSize > 20) ar.skip(axisSize - 20);
    }

    // instances start right after the axes array
    size_t instOff = axesArrayOffset + (size_t)axisCount * axisSize;
    if (instOff > r.size()) return;
    Reader ir = r.subAt(instOff, r.size() - instOff);
    size_t coordBytes = (size_t)axisCount * 4; // Fixed (16.16) per axis
    size_t minInstSize = coordBytes + 4;
    for (uint16_t i = 0; i < instanceCount; i++) {
        if (ir.remaining() < minInstSize) break;
        FvarInstance inst;
        inst.nameID = ir.u16();
        inst.flags = ir.u16();
        for (uint16_t a = 0; a < axisCount; a++)
            inst.coords.push_back(ir.fixed16());
        size_t consumed = minInstSize;
        if (instanceSize >= minInstSize + 2 && ir.remaining() >= 2) {
            inst.postScriptNameID = ir.u16();
            consumed += 2;
        }
        if (instanceSize > consumed) {
            size_t extra = std::min<size_t>(instanceSize - consumed, ir.remaining());
            ir.skip(extra);
            consumed += extra;
        }
        instances.push_back(std::move(inst));
    }
}

const FvarAxis* Fvar::axis(Tag tag) const {
    for (auto& a : axes)
        if (a.tag == tag) return &a;
    return nullptr;
}
int Fvar::axisIndex(Tag tag) const {
    for (size_t i = 0; i < axes.size(); i++)
        if (axes[i].tag == tag) return (int)i;
    return -1;
}

std::vector<double> Fvar::normalize(const std::vector<double>& userCoords) const {
    std::vector<double> out(axes.size(), 0.0);
    for (size_t i = 0; i < axes.size(); i++) {
        double v = i < userCoords.size() ? userCoords[i] : axes[i].defaultValue;
        double def = axes[i].defaultValue;
        if (v == def) { out[i] = 0; continue; }
        if (v > def) {
            double maxv = axes[i].maxValue != def ? axes[i].maxValue : def + 1;
            out[i] = maxv > def ? std::min(1.0, (v - def) / (maxv - def)) : 0;
        } else {
            double minv = axes[i].minValue != def ? axes[i].minValue : def - 1;
            out[i] = minv < def ? std::max(-1.0, (v - def) / (def - minv)) : 0;
        }
    }
    return out;
}

// ---------------------------------------------------------------- avar

void Avar::parse(Reader r, const Fvar& fvar) {
    majorVersion = r.u16();
    minorVersion = r.u16();
    (void)minorVersion;
    r.u16(); // reserved
    uint16_t axisCount = r.u16();
    uint16_t axisIndex = 0;
    for (auto& ax : fvar.axes) {
        if (axisIndex >= axisCount) break;
        uint16_t pairCount = r.u16();
        AvarAxisMap map;
        for (uint16_t p = 0; p < pairCount && r.remaining() >= 4; p++) {
            AvarSegment s;
            s.from = r.f2dot14();
            s.to = r.f2dot14();
            map.segments.push_back(s);
        }
        axisMaps.emplace_back(ax.tag, std::move(map));
        axisIndex++;
    }
}

void Avar::apply(std::vector<double>& norm) const {
    size_t i = 0;
    for (auto& kv : axisMaps) {
        if (i >= norm.size()) break;
        double v = norm[i];
        const auto& segs = kv.second.segments;
        if (!segs.empty()) {
            double out = v;
            if (v <= segs.front().from) {
                out = segs.front().to;
            } else if (v >= segs.back().from) {
                out = segs.back().to;
            } else {
                for (size_t s = 0; s + 1 < segs.size(); s++) {
                    if (v >= segs[s].from && v <= segs[s + 1].from) {
                        double d = segs[s + 1].from - segs[s].from;
                        if (d == 0) { out = segs[s].to; break; }
                        double t = (v - segs[s].from) / d;
                        out = segs[s].to + t * (segs[s + 1].to - segs[s].to);
                        break;
                    }
                }
            }
            norm[i] = out;
        }
        i++;
    }
}

// ---------------------------------------------------------------- STAT

void Stat::parse(Reader r) {
    uint16_t major = r.u16();
    uint16_t minor = r.u16();
    (void)major; (void)minor;
    designAxisSize = r.u16();
    designAxisCount = r.u16();
    offsetToAxes = r.u32();
    axisValueCount = r.u16();
    uint16_t offsetToAxisValues = r.u16();
    elidedFallbackNameID = r.u16();

    if (offsetToAxes && offsetToAxes < r.size()) {
        Reader ar = r.subAt(offsetToAxes, r.size() - offsetToAxes);
        for (uint16_t i = 0; i < designAxisCount && ar.remaining() >= 8; i++) {
            StatAxis ax;
            ax.tag = ar.u32();
            ax.nameID = ar.u16();
            ax.ordering = ar.u16();
            axes.push_back(ax);
        }
    }
    if (offsetToAxisValues && offsetToAxisValues < r.size()) {
        Reader vr = r.subAt(offsetToAxisValues, r.size() - offsetToAxisValues);
        for (uint16_t i = 0; i < axisValueCount; i++) {
            uint16_t avail = (uint16_t)vr.remaining();
            if (avail < 8) break;
            uint16_t format = vr.u16();
            StatValue v;
            if (format == 1) {
                uint16_t axisIndex = vr.u16();
                v.flags = vr.u16();
                v.valueNameID = vr.u16();
                v.value = vr.fixed16();
                if (axisIndex < axes.size()) v.tag = axes[axisIndex].tag;
                v.axisIndex = axisIndex;
            } else if (format == 2) {
                uint16_t axisIndex = vr.u16();
                v.flags = vr.u16();
                v.valueNameID = vr.u16();
                double nominal = vr.fixed16();
                double rangeMin = vr.fixed16();
                double rangeMax = vr.fixed16();
                v.value = nominal;
                (void)rangeMin; (void)rangeMax;
                if (axisIndex < axes.size()) v.tag = axes[axisIndex].tag;
                v.axisIndex = axisIndex;
            } else if (format == 3) {
                uint16_t axisIndex = vr.u16();
                v.flags = vr.u16();
                v.valueNameID = vr.u16();
                v.value = vr.fixed16();
                v.linkedValue = vr.fixed16();
                v.hasLinked = true;
                if (axisIndex < axes.size()) v.tag = axes[axisIndex].tag;
                v.axisIndex = axisIndex;
            } else if (format == 4) {
                uint16_t axisCount = vr.u16();
                v.flags = vr.u16();
                v.valueNameID = vr.u16();
                for (uint16_t a = 0; a < axisCount && vr.remaining() >= 6; a++) {
                    Tag t = vr.u32();
                    double val = vr.fixed16();
                    if (a == 0) { v.tag = t; v.value = val; }
                }
            } else {
                break;
            }
            values.push_back(std::move(v));
        }
    }
}

// ---------------------------------------------------------------- IVS

float VariationRegion::scalar(const std::vector<double>& normalized) const {
    float s = 1.0f;
    for (size_t a = 0; a < coords.size(); a++) {
        float start = coords[a][0], peak = coords[a][1], end = coords[a][2];
        double c = a < normalized.size() ? normalized[a] : 0.0;
        if (peak == 0.0f) continue; // axis not varied
        if (c < start || c > end) return 0.0f;
        if (c == peak) { /* 1 */ }
        else if (c < peak) {
            if (peak == start) return 0.0f;
            s *= (float)((c - start) / (peak - start));
        } else {
            if (peak == end) return 0.0f;
            s *= (float)((end - c) / (end - peak));
        }
    }
    return s;
}

void ItemVariationStore::parse(Reader r) {
    format = r.u16();
    uint32_t regionListOffset = r.u32();
    uint16_t dataCount = r.u16();
    std::vector<uint32_t> dataOffsets(dataCount);
    for (auto& o : dataOffsets) o = r.u32();

    // region list
    if (regionListOffset < r.size()) {
        Reader rl = r.subAt(regionListOffset, r.size() - regionListOffset);
        uint16_t axisCount = rl.u16();
        uint16_t regionCount = rl.u16();
        regions.resize(regionCount);
        for (auto& reg : regions) {
            reg.coords.resize(axisCount);
            for (auto& c : reg.coords) {
                c[0] = rl.f2dot14();
                c[1] = rl.f2dot14();
                c[2] = rl.f2dot14();
            }
        }
    }

    dataItems.resize(dataCount);
    for (uint16_t d = 0; d < dataCount; d++) {
        if (dataOffsets[d] == 0 || dataOffsets[d] >= r.size()) continue;
        Reader dr = r.subAt(dataOffsets[d], r.size() - dataOffsets[d]);
        ItemVariationData& ivd = dataItems[d];
        ivd.itemCount = dr.u16();
        uint16_t wordDeltaCount = dr.u16();
        uint16_t regionIndexCount = dr.u16();
        for (uint16_t k = 0; k < regionIndexCount; k++)
            ivd.regionIndexes.push_back(dr.u16());
        // Bit 15 of wordDeltaCount selects 32-bit (vs 16-bit) "long" words for
        // the first wordDeltaCount values of each row; the rest are int8.
        bool longWords = (wordDeltaCount & 0x8000) != 0;
        uint16_t longCount = (uint16_t)(wordDeltaCount & 0x7FFF);
        for (uint16_t item = 0; item < ivd.itemCount; item++) {
            std::vector<float> row;
            row.reserve(regionIndexCount);
            bool bad = false;
            for (uint16_t k = 0; k < regionIndexCount; k++) {
                if (k < longCount) {
                    if (longWords) {
                        if (dr.remaining() < 4) { bad = true; break; }
                        row.push_back((float)dr.i32());
                    } else {
                        if (dr.remaining() < 2) { bad = true; break; }
                        row.push_back((float)dr.i16());
                    }
                } else {
                    if (dr.remaining() < 1) { bad = true; break; }
                    row.push_back((float)(int8_t)dr.u8());
                }
            }
            if (bad) break;
            ivd.deltas.push_back(std::move(row));
        }
    }
}

float ItemVariationStore::delta(uint16_t outer, uint16_t inner, const std::vector<double>& normalized) const {
    if (outer >= dataItems.size()) return 0.0f;
    const ItemVariationData& ivd = dataItems[outer];
    if (inner >= ivd.deltas.size()) return 0.0f;
    float total = 0;
    const auto& row = ivd.deltas[inner];
    for (size_t k = 0; k < row.size() && k < ivd.regionIndexes.size(); k++) {
        uint16_t ri = ivd.regionIndexes[k];
        if (ri >= regions.size()) continue;
        float s = regions[ri].scalar(normalized);
        total += row[k] * s;
    }
    return total;
}

void DeltaSetIndexMap::parse(Reader r) {
    // format(u8): 0 = uint16 mapCount, 1 = uint32 mapCount
    // entryFormat(u8): bits 0-3 = inner index bit count - 1,
    //                  bits 4-5 = entry size - 1
    uint8_t fmt = r.u8();
    format = fmt;
    uint8_t entryFormat = r.u8();
    uint32_t mapCount = fmt == 0 ? r.u16() : r.u32();
    uint8_t entrySize = (uint8_t)(((entryFormat >> 4) & 0x03) + 1);
    uint8_t innerBits = (uint8_t)((entryFormat & 0x0F) + 1);
    uint32_t innerMask = innerBits >= 16 ? 0xFFFFu : ((1u << innerBits) - 1u);

    entries.reserve(mapCount);
    for (uint32_t i = 0; i < mapCount; i++) {
        if (r.remaining() < entrySize) break;
        uint32_t v = 0;
        for (uint8_t k = 0; k < entrySize; k++) v = (v << 8) | r.u8();
        uint32_t outer = innerBits >= 32 ? 0 : (v >> innerBits);
        uint32_t inner = v & innerMask;
        entries.push_back((outer << 16) | (inner & 0xFFFF));
    }
}

uint32_t DeltaSetIndexMap::lookup(uint32_t gid) const {
    if (entries.empty()) return gid; // identity
    if (gid < entries.size()) return entries[gid];
    return entries.back();
}

// ---------------------------------------------------------------- HVAR

void Hvar::parse(Reader r) {
    // HVAR header: majorVersion(u16), minorVersion(u16), then four Offset32s
    uint16_t major = r.u16();
    r.u16(); // minorVersion
    varStoreFormat = major;
    uint32_t varStoreOffset = r.u32();
    uint32_t advMapOffset = r.u32();
    uint32_t lsbMapOffset = r.u32();
    uint32_t rsbMapOffset = r.u32();

    if (varStoreOffset && varStoreOffset < r.size())
        store.parse(r.subAt(varStoreOffset, r.size() - varStoreOffset));
    if (advMapOffset && advMapOffset < r.size()) {
        hasAdvanceMap = true;
        advanceMap.parse(r.subAt(advMapOffset, r.size() - advMapOffset));
    }
    if (lsbMapOffset && lsbMapOffset < r.size()) {
        hasLsbMap = true;
        lsbMap.parse(r.subAt(lsbMapOffset, r.size() - lsbMapOffset));
    }
    if (rsbMapOffset && rsbMapOffset < r.size()) {
        hasRsbMap = true;
        rsbMap.parse(r.subAt(rsbMapOffset, r.size() - rsbMapOffset));
    }
}

float Hvar::advanceDelta(uint16_t gid, const std::vector<double>& normalized) const {
    if (store.dataItems.empty()) return 0;
    uint32_t varIdx = advanceMap.lookup(gid);
    uint16_t outer = (uint16_t)(varIdx >> 16), inner = (uint16_t)(varIdx & 0xFFFF);
    return store.delta(outer, inner, normalized);
}

float Hvar::lsbDelta(uint16_t gid, const std::vector<double>& normalized) const {
    if (!hasLsbMap || store.dataItems.empty()) return 0;
    uint32_t varIdx = lsbMap.lookup(gid);
    uint16_t outer = (uint16_t)(varIdx >> 16), inner = (uint16_t)(varIdx & 0xFFFF);
    return store.delta(outer, inner, normalized);
}

// ---------------------------------------------------------------- gvar

void Gvar::parse(Reader r) {
    data = r.data();
    size = r.size();
    uint16_t version = r.u16();
    (void)version;
    r.u16(); // reserved
    axisCount = r.u16();
    sharedTupleCount = r.u16();
    sharedTuplesOffset = r.u32();
    glyphCount = r.u16();
    flags = r.u16();
    glyphVariationDataArrayOffset = r.u32();

    bool longOffsets = flags & 0x0001;
    glyphOffsets.clear();
    if (longOffsets) {
        for (uint16_t i = 0; i <= glyphCount && r.remaining() >= 4; i++)
            glyphOffsets.push_back(r.u32());
    } else {
        for (uint16_t i = 0; i <= glyphCount && r.remaining() >= 2; i++)
            glyphOffsets.push_back((uint32_t)r.u16() * 2);
    }

    if (sharedTuplesOffset && sharedTuplesOffset < r.size()) {
        Reader st = r.subAt(sharedTuplesOffset, r.size() - sharedTuplesOffset);
        for (uint16_t t = 0; t < sharedTupleCount && st.remaining() >= (size_t)axisCount * 2; t++) {
            std::vector<float> tuple;
            for (uint16_t a = 0; a < axisCount; a++)
                tuple.push_back(st.f2dot14());
            sharedTuples.push_back(tuple);
        }
    }
}

bool Gvar::hasGlyphVariation(uint16_t gid) const {
    if ((size_t)gid + 1 >= glyphOffsets.size()) return false;
    uint32_t a = glyphOffsets[gid], b = glyphOffsets[gid + 1];
    return a != b && b <= (uint32_t)size;
}

float Gvar::tupleScalar(const TupleHeader& h, const std::vector<double>& normalized) const {
    float s = 1.0f;
    for (size_t a = 0; a < h.peakTuple.size(); a++) {
        float peak = h.peakTuple[a];
        if (peak == 0.0f) continue;
        double c = a < normalized.size() ? normalized[a] : 0.0;
        float start, end;
        if (!h.intermediateStart.empty()) {
            start = h.intermediateStart[a];
            end = h.intermediateEnd[a];
        } else {
            start = peak > 0 ? 0.0f : peak;
            end = peak > 0 ? peak : 0.0f;
        }
        if (c < start || c > end) return 0.0f;
        if (c == peak) continue; // factor 1
        if (c < peak) {
            if (peak == start) return 0.0f;
            s *= (float)((c - start) / (peak - start));
        } else {
            if (peak == end) return 0.0f;
            s *= (float)((end - c) / (end - peak));
        }
    }
    return s;
}

// Decode a packed point-number list (private or shared).
// Layout: count byte (bit 7 = 16-bit count, bits 0-6 high bits), then runs of
// point *deltas* (bits 0-6 = run count-1, bit 7 = 16-bit values); the deltas
// are cumulative (relative to the previous point). Count 0 means all points.
static std::vector<uint16_t> decodePointList(Reader& dr, size_t numPoints) {
    std::vector<uint16_t> out;
    if (dr.remaining() < 1) return out;
    uint8_t b0 = dr.u8();
    uint32_t count = b0;
    if (b0 & 0x80) { // POINTS_ARE_WORDS (count is 16-bit)
        if (dr.remaining() < 1) return out;
        count = ((uint32_t)(b0 & 0x7F) << 8) | dr.u8();
    }
    if (count == 0) { // all points
        for (size_t i = 0; i < numPoints; i++) out.push_back((uint16_t)i);
        return out;
    }
    std::vector<int32_t> rel;
    rel.reserve(count);
    while (rel.size() < count) {
        if (dr.remaining() < 1) break;
        uint8_t run = dr.u8();
        size_t n = (size_t)(run & 0x7F) + 1;
        bool words = (run & 0x80) != 0;
        for (size_t k = 0; k < n && rel.size() < count; k++) {
            if (words) {
                if (dr.remaining() < 2) return out;
                rel.push_back((int32_t)dr.u16());
            } else {
                if (dr.remaining() < 1) return out;
                rel.push_back((int32_t)dr.u8());
            }
        }
    }
    int32_t cur = 0;
    for (int32_t d : rel) {
        cur += d;
        if (cur >= 0) out.push_back((uint16_t)cur);
    }
    return out;
}

bool Gvar::applyTuples(const uint8_t* p, size_t n, size_t numPoints,
                       std::vector<double>& outDeltas,
                       const std::vector<double>& normalized,
                       uint16_t gid) const {
    Reader r(p, n);
    uint16_t tupleVariationCount = r.u16();
    uint16_t offsetToData = r.u16();
    uint16_t count = tupleVariationCount & 0x0FFF;
    if (count == 0) return true;
    bool haveSharedPoints = (tupleVariationCount & 0x8000) != 0;

    // Parse tuple headers.
    std::vector<TupleHeader> headers;
    for (uint16_t t = 0; t < count && r.remaining() >= 4; t++) {
        TupleHeader h;
        h.variationDataSize = r.u16();
        h.tupleIndex = r.u16();
        if (h.tupleIndex & 0x8000) {
            for (uint16_t a = 0; a < axisCount && r.remaining() >= 2; a++)
                h.peakTuple.push_back(r.f2dot14());
        } else {
            uint16_t idx = h.tupleIndex & 0x0FFF;
            h.peakTuple = idx < sharedTuples.size()
                              ? sharedTuples[idx]
                              : std::vector<float>(axisCount, 0.0f);
        }
        if (h.tupleIndex & 0x4000) {
            for (uint16_t a = 0; a < axisCount && r.remaining() >= 2; a++)
                h.intermediateStart.push_back(r.f2dot14());
            for (uint16_t a = 0; a < axisCount && r.remaining() >= 2; a++)
                h.intermediateEnd.push_back(r.f2dot14());
        }
        headers.push_back(std::move(h));
    }

    // Shared point list (stored at offsetToData, before the per-tuple data).
    std::vector<uint16_t> sharedList;
    size_t off = r.offset();
    if (haveSharedPoints && offsetToData && offsetToData < n) {
        Reader sr(p + offsetToData, n - offsetToData);
        size_t start = sr.offset();
        sharedList = decodePointList(sr, numPoints);
        off = offsetToData + (sr.offset() - start);
    }

    auto readDeltas = [](Reader& dr, size_t countDeltas) -> std::vector<double> {
        // Packed deltas: run header bits 0-5 = run count-1, bit 6 = 16-bit
        // values, bit 7 = run of zeros.
        std::vector<double> out;
        while (out.size() < countDeltas) {
            if (dr.remaining() < 1) break;
            uint8_t ctrl = dr.u8();
            size_t runCount = (size_t)(ctrl & 0x3F) + 1;
            if (ctrl & 0x80) { // zeros
                for (size_t k = 0; k < runCount && out.size() < countDeltas; k++)
                    out.push_back(0.0);
                continue;
            }
            bool word = (ctrl & 0x40) != 0;
            for (size_t k = 0; k < runCount && out.size() < countDeltas; k++) {
                if (word) {
                    if (dr.remaining() < 2) return out;
                    out.push_back(dr.i16());
                } else {
                    if (dr.remaining() < 1) return out;
                    out.push_back((int8_t)dr.u8());
                }
            }
        }
        return out;
    };

    for (auto& h : headers) {
        size_t sz = h.variationDataSize;
        if (off + sz > n) sz = n - off;
        Reader dr(p + off, sz);
        float scalar = tupleScalar(h, normalized);
        if (scalar != 0.0f) {
            std::vector<uint16_t> points;
            if (h.tupleIndex & 0x2000) { // private point numbers
                points = decodePointList(dr, numPoints);
            } else if (haveSharedPoints && !sharedList.empty()) {
                points = sharedList;
            } else {
                for (uint16_t i = 0; i < numPoints; i++) points.push_back(i);
            }
            std::vector<double> dx = readDeltas(dr, points.size());
            std::vector<double> dy = readDeltas(dr, points.size());
            for (size_t k = 0; k < points.size(); k++) {
                double ddx = k < dx.size() ? dx[k] : 0;
                double ddy = k < dy.size() ? dy[k] : 0;
                if (points[k] < numPoints) {
                    outDeltas[2 * points[k]] += ddx * scalar;
                    outDeltas[2 * points[k] + 1] += ddy * scalar;
                }
            }
        }
        off += sz;
    }
    return true;
}

// Collect the union of explicitly-referenced points across applied tuples
// (for IUP). Returns an empty vector when every point is covered.
std::vector<uint16_t> Gvar::unionPointList_(uint16_t gid, size_t numPoints,
                                            const std::vector<double>& normalized) const {
    std::vector<uint16_t> unionPts;
    if ((size_t)gid + 1 >= glyphOffsets.size()) return unionPts;
    uint32_t a = glyphOffsets[gid], b = glyphOffsets[gid + 1];
    if (a == b || b > (uint32_t)size) return unionPts;
    const uint8_t* p = data + glyphVariationDataArrayOffset + a;
    size_t n = b - a;
    Reader r(p, n);
    uint16_t tvc = r.u16();
    uint16_t offsetToData = r.u16();
    uint16_t count = tvc & 0x0FFF;
    bool haveSharedPoints = (tvc & 0x8000) != 0;

    std::vector<TupleHeader> headers;
    for (uint16_t t = 0; t < count && r.remaining() >= 4; t++) {
        TupleHeader h;
        h.variationDataSize = r.u16();
        h.tupleIndex = r.u16();
        if (h.tupleIndex & 0x8000) {
            for (uint16_t k = 0; k < axisCount && r.remaining() >= 2; k++) h.peakTuple.push_back(r.f2dot14());
        } else {
            uint16_t idx = h.tupleIndex & 0x0FFF;
            h.peakTuple = idx < sharedTuples.size() ? sharedTuples[idx]
                                                    : std::vector<float>(axisCount, 0.0f);
        }
        if (h.tupleIndex & 0x4000) {
            for (uint16_t k = 0; k < axisCount && r.remaining() >= 2; k++) h.intermediateStart.push_back(r.f2dot14());
            for (uint16_t k = 0; k < axisCount && r.remaining() >= 2; k++) h.intermediateEnd.push_back(r.f2dot14());
        }
        headers.push_back(std::move(h));
    }

    std::vector<uint16_t> sharedList;
    size_t off = r.offset();
    if (haveSharedPoints && offsetToData && offsetToData < n) {
        Reader sr(p + offsetToData, n - offsetToData);
        size_t start = sr.offset();
        sharedList = decodePointList(sr, numPoints);
        off = offsetToData + (sr.offset() - start);
    }

    std::vector<bool> touched(numPoints, false);
    size_t touchedCount = 0;
    for (auto& h : headers) {
        size_t sz = h.variationDataSize;
        if (off + sz > n) sz = n - off;
        float scalar = tupleScalar(h, normalized);
        if (scalar != 0.0f) {
            std::vector<uint16_t> pts;
            if (h.tupleIndex & 0x2000) {
                Reader dr(p + off, sz);
                pts = decodePointList(dr, numPoints);
            } else if (haveSharedPoints && !sharedList.empty()) {
                pts = sharedList;
            } else {
                // all points covered — no IUP needed at all
                unionPts.clear();
                return unionPts;
            }
            for (uint16_t q : pts) {
                if (q < numPoints && !touched[q]) {
                    touched[q] = true;
                    touchedCount++;
                }
            }
        }
        off += sz;
    }
    if (touchedCount == 0) return unionPts;
    for (size_t i = 0; i < numPoints; i++)
        if (touched[i]) unionPts.push_back((uint16_t)i);
    return unionPts;
}

void Gvar::iup(std::vector<double>& deltas, const std::vector<GlyfPoint>& points,
               const std::vector<uint16_t>& endPts, const std::vector<uint16_t>& pointList) const {
    // Mark which points have explicit deltas
    std::vector<bool> touched(points.size(), false);
    for (uint16_t p : pointList)
        if (p < points.size()) touched[p] = true;

    size_t contourStart = 0;
    for (uint32_t ci = 0; ci < endPts.size(); ci++) {
        size_t end = endPts[ci] + 1;
        if (end > points.size()) break;
        size_t n = end - contourStart;
        if (n == 0) { contourStart = end; continue; }

        // find touched in this contour
        std::vector<size_t> touchedIdx;
        for (size_t i = 0; i < n; i++)
            if (touched[contourStart + i]) touchedIdx.push_back(i);
        if (touchedIdx.empty()) { contourStart = end; continue; }

        if (touchedIdx.size() == 1) {
            size_t ti = contourStart + touchedIdx[0];
            double dx = deltas[2 * ti], dy = deltas[2 * ti + 1];
            for (size_t i = 0; i < n; i++) {
                size_t gi = contourStart + i;
                if (!touched[gi]) {
                    deltas[2 * gi] = dx;
                    deltas[2 * gi + 1] = dy;
                }
            }
            contourStart = end;
            continue;
        }

        // for each pair of consecutive touched points (circular)
        for (size_t k = 0; k < touchedIdx.size(); k++) {
            size_t a = touchedIdx[k];
            size_t b = touchedIdx[(k + 1) % touchedIdx.size()];
            size_t ga = contourStart + a, gb = contourStart + b;
            double ax = points[ga].x, ay = points[ga].y;
            double bx = points[gb].x, by = points[gb].y;
            double dax = deltas[2 * ga], day = deltas[2 * ga + 1];
            double dbx = deltas[2 * gb], dby = deltas[2 * gb + 1];
            // points strictly between a and b (non-wrapping walk)
            size_t i = a;
            for (;;) {
                i = (i + 1) % n;
                if (i == b) break;
                size_t gi = contourStart + i;
                if (touched[gi]) continue;
                double px = points[gi].x, py = points[gi].y;
                double dx, dy;
                if (dax == dbx && day == dby) {
                    dx = dax; dy = day;
                } else {
                    // x
                    double xmin = std::min(ax, bx), xmax = std::max(ax, bx);
                    if (ax == bx) dx = dax;
                    else if (px <= xmin) dx = ax < bx ? dax : dbx;
                    else if (px >= xmax) dx = ax < bx ? dbx : dax;
                    else dx = dax + (dbx - dax) * (px - ax) / (bx - ax);
                    // y
                    double ymin = std::min(ay, by), ymax = std::max(ay, by);
                    if (ay == by) dy = day;
                    else if (py <= ymin) dy = ay < by ? day : dby;
                    else if (py >= ymax) dy = ay < by ? dby : day;
                    else dy = day + (dby - day) * (py - ay) / (by - ay);
                }
                deltas[2 * gi] = dx;
                deltas[2 * gi + 1] = dy;
            }
        }
        contourStart = end;
    }
}

bool Gvar::applyToPoints(uint16_t gid, std::vector<GlyfPoint>& points,
                         std::vector<GlyfPoint>& phantoms,
                         const std::vector<uint16_t>& endPts,
                         const std::vector<double>& normalized) const {
    if ((size_t)gid + 1 >= glyphOffsets.size()) return false;
    uint32_t a = glyphOffsets[gid], b = glyphOffsets[gid + 1];
    if (a == b || b > (uint32_t)size) return false;

    size_t numPoints = points.size() + 4; // + phantom
    std::vector<double> deltas(2 * numPoints, 0.0);
    if (!applyTuples(data + glyphVariationDataArrayOffset + a, b - a,
                     numPoints, deltas, normalized, gid))
        return false;

    std::vector<uint16_t> unionPts = unionPointList_(gid, numPoints, normalized);
    if (!unionPts.empty() && unionPts.size() < numPoints)
        iup(deltas, points, endPts, unionPts);

    for (size_t i = 0; i < points.size(); i++) {
        points[i].x = (int32_t)std::lround(points[i].x + deltas[2 * i]);
        points[i].y = (int32_t)std::lround(points[i].y + deltas[2 * i + 1]);
    }
    for (size_t k = 0; k < 4 && k < phantoms.size(); k++) {
        phantoms[k].x = (int32_t)std::lround(phantoms[k].x + deltas[2 * (points.size() + k)]);
        phantoms[k].y = (int32_t)std::lround(phantoms[k].y + deltas[2 * (points.size() + k) + 1]);
    }
    return true;
}

bool Gvar::applyToComposite(uint16_t gid, std::vector<std::pair<double, double>>& offsets,
                            std::vector<GlyfPoint>& phantoms,
                            const std::vector<double>& normalized) const {
    if ((size_t)gid + 1 >= glyphOffsets.size()) return false;
    uint32_t a = glyphOffsets[gid], b = glyphOffsets[gid + 1];
    if (a == b || b > (uint32_t)size) return false;
    size_t numPoints = offsets.size() + 4;
    std::vector<double> deltas(2 * numPoints, 0.0);
    if (!applyTuples(data + glyphVariationDataArrayOffset + a, b - a, numPoints, deltas, normalized, gid))
        return false;
    for (size_t i = 0; i < offsets.size(); i++) {
        offsets[i].first += deltas[2 * i];
        offsets[i].second += deltas[2 * i + 1];
    }
    for (size_t k = 0; k < 4 && k < phantoms.size(); k++) {
        phantoms[k].x = (int32_t)std::lround(phantoms[k].x + deltas[2 * (offsets.size() + k)]);
        phantoms[k].y = (int32_t)std::lround(phantoms[k].y + deltas[2 * (offsets.size() + k) + 1]);
    }
    return true;
}

// ---------------------------------------------------------------- StoreRegions

StoreRegions::StoreRegions(const ItemVariationStore* store, const std::vector<double>* normalized)
    : store_(store), normalized_(normalized) {}

const std::vector<float>& StoreRegions::scalars(uint16_t vsindex) {
    auto it = cache_.find(vsindex);
    if (it != cache_.end()) return it->second;
    std::vector<float> out;
    if (store_ && normalized_) {
        // Blend deltas are ordered by the region indices referenced by the
        // IVS data item selected with vsindex (not the whole region list).
        if (vsindex < store_->dataItems.size()) {
            const auto& idxs = store_->dataItems[vsindex].regionIndexes;
            out.reserve(idxs.size());
            for (uint16_t ri : idxs) {
                float s = ri < store_->regions.size()
                              ? store_->regions[ri].scalar(*normalized_)
                              : 0.0f;
                out.push_back(s);
            }
        }
    }
    return cache_.emplace(vsindex, std::move(out)).first->second;
}

} // namespace ftk
