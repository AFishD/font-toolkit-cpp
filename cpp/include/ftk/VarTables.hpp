// ftk: variable font tables — fvar, avar, STAT, ItemVariationStore,
// gvar (tuple application incl. IUP), HVAR/VVAR.
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Bytes.hpp"
#include "ftk/Tables.hpp"
#include "ftk/Glyphs.hpp"
#include "ftk/Cff.hpp"

#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace ftk {

class Font; // fwd

// ---------------------------------------------------------------- fvar
struct FvarAxis {
    Tag tag = 0;
    uint16_t nameID = 0;
    uint16_t flags = 0;
    double minValue = 0, defaultValue = 0, maxValue = 0;
    std::string name; // resolved via name table
};

struct FvarInstance {
    std::string name;
    uint16_t nameID = 0;
    uint16_t flags = 0;
    uint16_t postScriptNameID = 0xFFFF;
    std::vector<double> coords; // user-space coordinates, size == axes
    std::vector<double> normalized; // normalized (post avar), computed lazily
};

struct Fvar {
    uint16_t majorVersion = 1, minorVersion = 0;
    uint16_t axesArrayOffset = 0, reserved = 0;
    uint16_t instanceSize = 0;
    std::vector<FvarAxis> axes;
    std::vector<FvarInstance> instances;

    void parse(Reader r);
    const FvarAxis* axis(Tag tag) const;
    int axisIndex(Tag tag) const;
    // user coords -> normalized [-1,1]
    std::vector<double> normalize(const std::vector<double>& userCoords) const;
};

// ---------------------------------------------------------------- avar
struct AvarSegment { double from = 0, to = 0; };
struct AvarAxisMap { std::vector<AvarSegment> segments; };
struct Avar {
    uint16_t majorVersion = 1, minorVersion = 0;
    std::vector<std::pair<Tag, AvarAxisMap>> axisMaps;
    void parse(Reader r, const Fvar& fvar);
    void apply(std::vector<double>& normalized) const;
};

// ---------------------------------------------------------------- STAT
struct StatAxis {
    Tag tag = 0;
    uint16_t nameID = 0;
    uint16_t ordering = 0;
    std::string name;
};
struct StatValue {
    int axisIndex = -1; // index into StatAxis list (format 1) or -1
    Tag tag = 0;
    uint16_t valueNameID = 0;
    uint16_t flags = 0;
    double value = 0;
    double linkedValue = 0;
    bool hasLinked = false;
    std::string name;
};
struct Stat {
    uint16_t designAxisSize = 0, designAxisCount = 0;
    uint16_t axisValueCount = 0;
    uint32_t offsetToAxes = 0;
    uint16_t elidedFallbackNameID = 0;
    std::vector<StatAxis> axes;
    std::vector<StatValue> values;
    void parse(Reader r);
};

// ---------------------------------------------------------------- ItemVariationStore
struct VariationRegion {
    // per axis: start, peak, end
    std::vector<std::array<float, 3>> coords;
    float scalar(const std::vector<double>& normalized) const;
};

struct ItemVariationData {
    uint16_t itemCount = 0;
    std::vector<uint16_t> regionIndexes;
    // deltas[item][regionIndexInList] — full rows
    std::vector<std::vector<float>> deltas;
};

struct ItemVariationStore {
    uint16_t format = 1;
    std::vector<VariationRegion> regions;
    std::vector<ItemVariationData> dataItems;

    void parse(Reader r);
    // returns delta for varIdx=(outer,inner); scalars for normalized coords
    float delta(uint16_t outer, uint16_t inner, const std::vector<double>& normalized) const;
};

struct DeltaSetIndexMap {
    uint8_t format = 0;
    std::vector<uint32_t> entries; // gid -> varIdx (outer<<16|inner)

    void parse(Reader r);
    uint32_t lookup(uint32_t gid) const; // falls back to gid
};

// ---------------------------------------------------------------- HVAR / VVAR
struct Hvar {
    uint16_t varStoreFormat = 1;
    ItemVariationStore store;
    bool hasAdvanceMap = false, hasLsbMap = false, hasRsbMap = false;
    DeltaSetIndexMap advanceMap, lsbMap, rsbMap;

    void parse(Reader r);
    float advanceDelta(uint16_t gid, const std::vector<double>& normalized) const;
    float lsbDelta(uint16_t gid, const std::vector<double>& normalized) const;
};

// ---------------------------------------------------------------- gvar
struct Gvar {
    uint16_t axisCount = 0;
    uint16_t sharedTupleCount = 0;
    uint32_t sharedTuplesOffset = 0;
    uint16_t glyphCount = 0;
    uint16_t flags = 0;
    uint32_t glyphVariationDataArrayOffset = 0;
    std::vector<uint32_t> glyphOffsets;
    // shared tuples: [tuple][axis] f2dot14
    std::vector<std::vector<float>> sharedTuples;

    const uint8_t* data = nullptr;
    size_t size = 0;

    void parse(Reader r);

    // Apply deltas to a simple glyph's points (with phantom points appended).
    // points: in/out; phantoms: 4 pairs (in/out). Returns false on parse error.
    bool applyToPoints(uint16_t gid, std::vector<GlyfPoint>& points,
                       std::vector<GlyfPoint>& phantoms,
                       const std::vector<uint16_t>& endPts,
                       const std::vector<double>& normalized) const;

    // Apply deltas to a composite glyph's components (translation pairs).
    bool applyToComposite(uint16_t gid, std::vector<std::pair<double, double>>& offsets,
                          std::vector<GlyfPoint>& phantoms,
                          const std::vector<double>& normalized) const;

    bool hasGlyphVariation(uint16_t gid) const;

private:
    struct TupleHeader {
        uint16_t variationDataSize = 0;
        uint16_t tupleIndex = 0;
        std::vector<float> peakTuple;    // normalized
        std::vector<float> intermediateStart, intermediateEnd;
    };

    bool applyTuples(const uint8_t* p, size_t n, size_t numPoints,
                     std::vector<double>& outDeltas /*2*numPoints*/,
                     const std::vector<double>& normalized, uint16_t gid = 0) const;
    float tupleScalar(const TupleHeader& h, const std::vector<double>& normalized) const;
    void iup(std::vector<double>& deltas, const std::vector<GlyfPoint>& points,
             const std::vector<uint16_t>& endPts, const std::vector<uint16_t>& pointList) const;
    std::vector<uint16_t> unionPointList_(uint16_t gid, size_t numPoints,
                                          const std::vector<double>& normalized) const;
};

// region scalars provider for CFF2 blending
class StoreRegions : public BlendRegions {
public:
    StoreRegions(const ItemVariationStore* store, const std::vector<double>* normalized);
    const std::vector<float>& scalars(uint16_t vsindex) override;

private:
    const ItemVariationStore* store_ = nullptr;
    const std::vector<double>* normalized_ = nullptr;
    const ItemVariationStore* vstores_[16] = {nullptr};
    std::map<uint16_t, std::vector<float>> cache_;
};

} // namespace ftk
