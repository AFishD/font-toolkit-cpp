// glyf/loca parsing and TrueType outline extraction.
#include "ftk/Glyphs.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <sstream>

namespace ftk {

// ---------------------------------------------------------------------------
// Path
// ---------------------------------------------------------------------------

size_t Path::totalPoints() const {
    size_t n = 0;
    for (auto& c : cmds) {
        switch (c.op) {
        case PathOp::MoveTo:
        case PathOp::LineTo: n += 1; break;
        case PathOp::QuadTo: n += 2; break;
        case PathOp::CubicTo: n += 3; break;
        case PathOp::Close: break;
        }
    }
    return n;
}

size_t Path::totalCurves() const {
    size_t n = 0;
    for (auto& c : cmds)
        if (c.op == PathOp::QuadTo || c.op == PathOp::CubicTo) n++;
    return n;
}

FBounds Path::bounds() const {
    FBounds b;
    for (auto& c : cmds) {
        switch (c.op) {
        case PathOp::MoveTo:
        case PathOp::LineTo: b.add(c.x0, c.y0); break;
        case PathOp::QuadTo:
            b.add(c.x0, c.y0);
            b.add(c.x1, c.y1);
            break;
        case PathOp::CubicTo:
            b.add(c.x0, c.y0);
            b.add(c.x1, c.y1);
            b.add(c.x2, c.y2);
            break;
        case PathOp::Close: break;
        }
    }
    return b;
}

std::string Path::toSvg(double scale, double dx, double dy, bool flipY) const {
    std::ostringstream o;
    o.precision(2);
    auto Y = [&](double y) { return flipY ? -y : y; };
    for (auto& c : cmds) {
        switch (c.op) {
        case PathOp::MoveTo:
            o << "M" << (c.x0 * scale + dx) << " " << (Y(c.y0) * scale + dy) << " ";
            break;
        case PathOp::LineTo:
            o << "L" << (c.x0 * scale + dx) << " " << (Y(c.y0) * scale + dy) << " ";
            break;
        case PathOp::QuadTo:
            o << "Q" << (c.x0 * scale + dx) << " " << (Y(c.y0) * scale + dy) << " "
              << (c.x1 * scale + dx) << " " << (Y(c.y1) * scale + dy) << " ";
            break;
        case PathOp::CubicTo:
            o << "C" << (c.x0 * scale + dx) << " " << (Y(c.y0) * scale + dy) << " "
              << (c.x1 * scale + dx) << " " << (Y(c.y1) * scale + dy) << " "
              << (c.x2 * scale + dx) << " " << (Y(c.y2) * scale + dy) << " ";
            break;
        case PathOp::Close:
            o << "Z";
            break;
        }
    }
    return o.str();
}

// ---------------------------------------------------------------------------
// glyf / loca
// ---------------------------------------------------------------------------

void GlyfTable::parse(Reader glyf, Reader loca, int16_t indexToLocFormat, uint16_t numGlyphs) {
    glyf_ = glyf.data();
    glyfSize_ = glyf.size();
    longFormat_ = indexToLocFormat != 0;

    size_t n = numGlyphs + 1;
    if (longFormat_) {
        size_t avail = loca.remaining() / 4;
        if (n > avail) n = avail;
        loca_.reserve(n);
        for (size_t i = 0; i < n; i++) loca_.push_back(loca.u32());
    } else {
        size_t avail = loca.remaining() / 2;
        if (n > avail) n = avail;
        loca_.reserve(n);
        for (size_t i = 0; i < n; i++) loca_.push_back((uint32_t)loca.u16() * 2);
    }
}

bool GlyfTable::glyphRange(uint16_t gid, uint32_t& start, uint32_t& end) const {
    if ((size_t)gid + 1 >= loca_.size()) return false;
    start = loca_[gid];
    end = loca_[gid + 1];
    if (start > end || end > glyfSize_) return false;
    return start != end;
}

Glyph GlyfTable::glyph(uint16_t gid) const {
    Glyph g;
    uint32_t start, end;
    if (!glyphRange(gid, start, end)) return g;
    Reader r(glyf_ + start, end - start);
    int16_t numberOfContours = r.i16();
    g.bounds.xMin = r.i16();
    g.bounds.yMin = r.i16();
    g.bounds.xMax = r.i16();
    g.bounds.yMax = r.i16();
    g.bounds.valid = true;

    if (numberOfContours == 0) {
        g.kind = Glyph::Kind::Empty;
        return g;
    }

    if (numberOfContours > 0) {
        g.kind = Glyph::Kind::Simple;
        uint16_t nc = (uint16_t)numberOfContours;
        if (nc > (r.size() / 2)) { g.kind = Glyph::Kind::Empty; return g; }
        g.endPts.reserve(nc);
        for (uint16_t i = 0; i < nc; i++) g.endPts.push_back(r.u16());
        if (g.endPts.empty()) { g.kind = Glyph::Kind::Empty; return g; }
        uint16_t numPts = (uint16_t)(g.endPts.back() + 1);

        uint16_t insLen = r.u16();
        if (r.remaining() >= insLen)
            g.instructions = r.take(insLen);
        else
            r.skip(r.remaining());

        // flags
        std::vector<uint8_t> flags;
        flags.reserve(numPts);
        while (flags.size() < numPts && r.remaining() >= 1) {
            uint8_t f = r.u8();
            flags.push_back(f);
            if (f & 0x08) { // REPEAT_FLAG
                if (r.remaining() < 1) break;
                uint8_t rep = r.u8();
                for (uint8_t k = 0; k < rep && flags.size() < numPts; k++)
                    flags.push_back(f);
            }
        }
        if (flags.size() < numPts)
            flags.resize(numPts, 0);

        // x deltas
        int32_t x = 0;
        g.points.resize(numPts);
        for (uint16_t i = 0; i < numPts; i++) {
            uint8_t f = flags[i];
            if (f & 0x02) { // short: byte is an unsigned magnitude
                int d = r.u8();
                x += (f & 0x10) ? d : -d;
            } else if (!(f & 0x10)) {
                x += r.i16();
            }
            g.points[i].x = x;
        }
        int32_t y = 0;
        for (uint16_t i = 0; i < numPts; i++) {
            uint8_t f = flags[i];
            if (f & 0x04) { // short: unsigned magnitude
                int d = r.u8();
                y += (f & 0x20) ? d : -d;
            } else if (!(f & 0x20)) {
                y += r.i16();
            }
            g.points[i].y = y;
            g.points[i].onCurve = (f & 0x01) != 0;
        }
    } else {
        // composite
        g.kind = Glyph::Kind::Composite;
        bool more = true;
        while (more) {
            if (r.remaining() < 6) break; // flags + glyphIndex + minimum args
            GlyfComponent comp;
            comp.flags = r.u16();
            comp.glyph = r.u16();
            if (comp.flags & 0x0001) { // ARG_1_AND_2_ARE_WORDS
                if (r.remaining() < 4) break;
                comp.arg1 = r.i16();
                comp.arg2 = r.i16();
            } else {
                if (r.remaining() < 2) break;
                comp.arg1 = (int8_t)r.u8();
                comp.arg2 = (int8_t)r.u8();
            }
            if (comp.flags & 0x0008) { // SCALE
                if (r.remaining() < 2) break;
                comp.a = comp.d = r.f2dot14();
            } else if (comp.flags & 0x0040) { // X_AND_Y_SCALE
                if (r.remaining() < 4) break;
                comp.a = r.f2dot14();
                comp.d = r.f2dot14();
            } else if (comp.flags & 0x0080) { // TWO_BY_TWO
                if (r.remaining() < 8) break;
                comp.a = r.f2dot14();
                comp.b = r.f2dot14();
                comp.c = r.f2dot14();
                comp.d = r.f2dot14();
            }
            if (comp.flags & 0x0002) {
                comp.e = comp.arg1;
                comp.f = comp.arg2;
                if (comp.flags & 0x0004) { // ROUND_XY_TO_GRID
                    comp.e = std::round(comp.e);
                    comp.f = std::round(comp.f);
                }
            }
            if (comp.flags & 0x0400) g.overlapSimple = 1;
            more = (comp.flags & 0x0020) != 0;
            g.components.push_back(comp);
        }
    }
    return g;
}

void GlyfTable::componentClosure(uint16_t gid, std::vector<bool>& seen) const {
    std::vector<bool> visited(loca_.size(), false);
    componentClosure(gid, seen, visited);
}

void GlyfTable::componentClosure(uint16_t gid, std::vector<bool>& seen,
                                 std::vector<bool>& visited) const {
    // `seen` is the caller's keep-set (seeds are already true), so traversal
    // state must live in `visited`.
    if (gid >= loca_.size() - 1 || visited[gid]) return;
    visited[gid] = true;
    seen[gid] = true;
    Glyph g = glyph(gid);
    if (g.kind != Glyph::Kind::Composite) return;
    for (auto& c : g.components)
        componentClosure(c.glyph, seen, visited);
}

// ---------------------------------------------------------------------------
// contour → path conversion (TrueType quadratic semantics)
// ---------------------------------------------------------------------------

void contourPointsToPath(const std::vector<GlyfPoint>& pts, const std::vector<uint16_t>& endPts, Path& out) {
    size_t contourStart = 0;
    for (uint32_t ci = 0; ci < endPts.size(); ci++) {
        size_t end = endPts[ci] + 1;
        if (end > pts.size()) break;
        if (end <= contourStart) { contourStart = end; continue; }
        size_t n = end - contourStart;
        if (n == 1) {
            // Degenerate single-point contour: fontTools emits moveTo+close
            // for an on-curve point and nothing for a lone off-curve point.
            if (pts[contourStart].onCurve) {
                out.moveTo((float)pts[contourStart].x, (float)pts[contourStart].y);
                out.close();
            }
            contourStart = end;
            continue;
        }
        const GlyfPoint* c = &pts[contourStart];

        // Find first on-curve point
        int first = -1;
        for (size_t i = 0; i < n; i++)
            if (c[i].onCurve) { first = (int)i; break; }

        double startX, startY;
        if (first < 0) {
            // all off-curve: start at implied midpoint between last and first
            double mx = (c[n - 1].x + c[0].x) / 2.0;
            double my = (c[n - 1].y + c[0].y) / 2.0;
            startX = mx; startY = my;
            out.moveTo(startX, startY);
            for (size_t k = 0; k < n; k++) {
                const GlyfPoint& p = c[k];
                const GlyfPoint& pn = c[(k + 1) % n];
                double mx2 = (p.x + pn.x) / 2.0, my2 = (p.y + pn.y) / 2.0;
                out.quadTo(p.x, p.y, mx2, my2);
            }
        } else {
            // rotate so first on-curve is start
            std::vector<const GlyfPoint*> order;
            order.reserve(n);
            for (size_t k = 0; k < n; k++)
                order.push_back(&c[(first + k) % n]);
            startX = order[0]->x;
            startY = order[0]->y;
            out.moveTo(startX, startY);
            const GlyfPoint* pending = nullptr;
            for (size_t k = 1; k < n; k++) {
                const GlyfPoint* p = order[k];
                if (p->onCurve) {
                    if (pending) {
                        out.quadTo(pending->x, pending->y, p->x, p->y);
                        pending = nullptr;
                    } else {
                        out.lineTo(p->x, p->y);
                    }
                } else {
                    if (pending) {
                        // implied on-curve midpoint
                        double mx = (pending->x + p->x) / 2.0, my = (pending->y + p->y) / 2.0;
                        out.quadTo(pending->x, pending->y, mx, my);
                    }
                    pending = p;
                }
            }
            // close back to start
            if (pending) {
                out.quadTo(pending->x, pending->y, startX, startY);
            } else {
                // nothing pending: close (start point == first; the path returns)
            }
        }
        out.close();
        contourStart = end;
    }
}

void GlyfTable::simpleToPath(const Glyph& g, Path& out) {
    contourPointsToPath(g.points, g.endPts, out);
}

} // namespace ftk
