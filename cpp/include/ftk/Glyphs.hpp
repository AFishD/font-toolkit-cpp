// ftk: glyph outlines — path model, glyf/loca parsing, composite resolution.
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include "ftk/Bytes.hpp"
#include "ftk/Tables.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace ftk {

// ---------------------------------------------------------------------------
// Path model (filled outlines; quadratic and cubic segments supported).
// ---------------------------------------------------------------------------

enum class PathOp : uint8_t { MoveTo, LineTo, QuadTo, CubicTo, Close };

struct FBounds {
    float xMin = 0, yMin = 0, xMax = 0, yMax = 0;
    bool valid = false;
    void add(float x, float y) {
        if (!valid) { xMin = xMax = x; yMin = yMax = y; valid = true; return; }
        if (x < xMin) xMin = x;
        if (x > xMax) xMax = x;
        if (y < yMin) yMin = y;
        if (y > yMax) yMax = y;
    }
};

struct PathCmd {
    PathOp op;
    float x0 = 0, y0 = 0, x1 = 0, y1 = 0, x2 = 0, y2 = 0;
};

struct Path {
    std::vector<PathCmd> cmds;
    int moves = 0;

    void moveTo(float x, float y) {
        cmds.push_back({PathOp::MoveTo, x, y});
        moves++;
    }
    void lineTo(float x, float y) { cmds.push_back({PathOp::LineTo, x, y}); }
    void quadTo(float cx, float cy, float x, float y) { cmds.push_back({PathOp::QuadTo, cx, cy, x, y}); }
    void cubicTo(float c1x, float c1y, float c2x, float c2y, float x, float y) {
        cmds.push_back({PathOp::CubicTo, c1x, c1y, c2x, c2y, x, y});
    }
    void close() { cmds.push_back({PathOp::Close}); }

    bool empty() const { return cmds.empty(); }
    size_t totalPoints() const;     // on+off curve points
    size_t totalCurves() const;     // quad+cubic segments
    FBounds bounds() const;         // includes control points (fontTools semantics)

    // SVG path 'd' string; flip Y for typical screen coords when flipY.
    std::string toSvg(double scale = 1.0, double dx = 0, double dy = 0, bool flipY = false) const;
};

// ---------------------------------------------------------------------------
// glyf / loca
// ---------------------------------------------------------------------------

struct GlyfPoint {
    int32_t x = 0, y = 0;
    bool onCurve = true;
    bool isInferred = false; // implied on-curve midpoint
};

struct GlyfComponent {
    uint16_t glyph = 0;
    uint16_t flags = 0;
    int32_t arg1 = 0, arg2 = 0;    // offsets (XY values) or point indices
    double a = 1, b = 0, c = 0, d = 1; // 2x2 transform
    double e = 0, f = 0;                 // translation

    bool argsAreXY() const { return flags & 0x0002; }
    bool useMyMetrics() const { return flags & 0x0200; }
    bool roundXYToGrid() const { return flags & 0x0004; }
};

struct Glyph {
    enum class Kind { Empty, Simple, Composite };
    Kind kind = Kind::Empty;
    Bounds bounds;
    // simple
    std::vector<uint16_t> endPts;      // end point indices per contour
    std::vector<GlyfPoint> points;
    std::vector<uint8_t> instructions;
    // composite
    std::vector<GlyfComponent> components;
    uint16_t overlapSimple = 0; // OVERLAP_COMPOUND flag mirror

    bool isSimple() const { return kind == Kind::Simple; }
    bool isComposite() const { return kind == Kind::Composite; }
};

class GlyfTable {
public:
    void parse(Reader glyf, Reader loca, int16_t indexToLocFormat, uint16_t numGlyphs);

    // Parse a glyph (no variation applied). Thread-safe (const).
    Glyph glyph(uint16_t gid) const;
    bool glyphRange(uint16_t gid, uint32_t& start, uint32_t& end) const;
    uint16_t numGlyphs() const { return (uint16_t)loca_.size(); }
    bool empty() const { return glyfSize_ == 0; }

    // Convert a parsed simple glyph to a quadratic path.
    static void simpleToPath(const Glyph& g, Path& out);

    const uint8_t* glyfData() const { return glyf_; }
    size_t glyfSize() const { return glyfSize_; }
    const std::vector<uint32_t>& loca() const { return loca_; }
    bool longFormat() const { return longFormat_; }

    // Collect glyph ids referenced by composites (closure, incl. self).
    // `seen` doubles as the keep-set: seeds may already be true.
    void componentClosure(uint16_t gid, std::vector<bool>& seen) const;
    void componentClosure(uint16_t gid, std::vector<bool>& seen,
                          std::vector<bool>& visited) const;
    size_t locaCount() const { return loca_.size(); }

private:
    const uint8_t* glyf_ = nullptr;
    size_t glyfSize_ = 0;
    std::vector<uint32_t> loca_;
    bool longFormat_ = false;
};

// Convert quadratic TrueType contours (list of points + endPts) to path cmds.
void contourPointsToPath(const std::vector<GlyfPoint>& pts, const std::vector<uint16_t>& endPts, Path& out);

} // namespace ftk
