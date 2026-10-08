// Font facade implementation.
#include "ftk/Font.hpp"
#include "ftk/Layout.hpp"
#include "ftk/Color.hpp"
#include "ftk/Platform.hpp"

#include <algorithm>
#include <cstring>

namespace ftk {

// ---------------------------------------------------------------------------
// open
// ---------------------------------------------------------------------------

std::unique_ptr<Font> Font::open(std::vector<uint8_t> data, uint32_t ttcIndex) {
    auto f = std::unique_ptr<Font>(new Font());
    f->sfnt_ = SfntFont::parse(std::move(data), ttcIndex);
    return f;
}

std::unique_ptr<Font> Font::openFile(const std::string& utf8Path, uint32_t ttcIndex) {
    auto data = readFile(utf8Path);
    auto f = open(std::move(data), ttcIndex);
    f->path_ = utf8Path;
    return f;
}

int Font::numFacesInFile(const std::string& utf8Path) {
    try {
        auto data = readFile(utf8Path);
        if (data.size() < 12) return 1;
        if (data[0] == 't' && data[1] == 't' && data[2] == 'c' && data[3] == 'f')
            return (int)((uint32_t(data[8]) << 24) | (uint32_t(data[9]) << 16) |
                         (uint32_t(data[10]) << 8) | data[11]);
        return 1;
    } catch (...) {
        return -1;
    }
}

const std::vector<uint8_t>& Font::fileBytes() const {
    return sfnt_->originalBytes();
}

// ---------------------------------------------------------------------------
// lazy table caches (recursive mutex: caches may cascade into other caches)
// ---------------------------------------------------------------------------

#define FTK_LAZY_GETTER(Field, TagExpr, Type, ParseCall)                    \
    const Type* Font::Field() const {                                       \
        std::lock_guard<std::recursive_mutex> lk(mu_);                      \
        if (Field##_) return Field##_.get();                                \
        size_t len;                                                         \
        const uint8_t* p = sfnt_->tableData(TagExpr, len);                  \
        if (!p) return nullptr;                                             \
        auto t = std::make_unique<Type>();                                  \
        try {                                                               \
            Type* tp = t.get();                                             \
            (void)tp;                                                       \
            t->ParseCall;                                                   \
        } catch (const Error&) {                                            \
            return nullptr;                                                 \
        }                                                                   \
        Field##_ = std::move(t);                                            \
        return Field##_.get();                                              \
    }

FTK_LAZY_GETTER(head, tag_Head, Head, parse(Reader(p, len)))
FTK_LAZY_GETTER(hhea, tag_Hhea, Hhea, parse(Reader(p, len)))
FTK_LAZY_GETTER(vhea, tag_Vhea, Hhea, parse(Reader(p, len)))
FTK_LAZY_GETTER(maxp, tag_Maxp, Maxp, parse(Reader(p, len)))
FTK_LAZY_GETTER(os2, tag_Os2, Os2, parse(Reader(p, len)))
FTK_LAZY_GETTER(post, tag_Post, Post, parse(Reader(p, len)))
FTK_LAZY_GETTER(cmap, tag_Cmap, Cmap, parse(Reader(p, len)))
FTK_LAZY_GETTER(gasp, tag_Gasp, Gasp, parse(Reader(p, len)))
FTK_LAZY_GETTER(kern, tag_Kern, Kern, parse(Reader(p, len)))
FTK_LAZY_GETTER(vorg, tag_VORG, VORG, parse(Reader(p, len)))
FTK_LAZY_GETTER(fvar, tag_Fvar, Fvar, parse(Reader(p, len)))
FTK_LAZY_GETTER(gvar, tag_Gvar, Gvar, parse(Reader(p, len)))
FTK_LAZY_GETTER(hvar, tag_HVAR, Hvar, parse(Reader(p, len)))
FTK_LAZY_GETTER(vvar, tag_VVAR, Hvar, parse(Reader(p, len)))

const NameTable* Font::nameTable() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (name_) return name_.get();
    size_t len;
    const uint8_t* p = sfnt_->tableData(tag_Name, len);
    if (!p) return nullptr;
    auto t = std::make_unique<NameTable>();
    try {
        t->parse(Reader(p, len));
    } catch (const Error&) {
        return nullptr;
    }
    name_ = std::move(t);
    return name_.get();
}

const Avar* Font::avar() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (avar_) return avar_.get();
    size_t len;
    const uint8_t* p = sfnt_->tableData(tag_Avar, len);
    if (!p) return nullptr;
    auto t = std::make_unique<Avar>();
    try {
        const Fvar* fv = fvar();
        t->parse(Reader(p, len), fv ? *fv : Fvar());
    } catch (const Error&) {
        return nullptr;
    }
    avar_ = std::move(t);
    return avar_.get();
}

const Stat* Font::stat() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (stat_) return stat_.get();
    size_t len;
    const uint8_t* p = sfnt_->tableData(tag_STAT, len);
    if (!p) return nullptr;
    auto t = std::make_unique<Stat>();
    try {
        t->parse(Reader(p, len));
        // resolve names
        if (const NameTable* nm = nameTable()) {
            for (auto& ax : t->axes) ax.name = nm->get(ax.nameID);
            for (auto& v : t->values) v.name = nm->get(v.valueNameID);
        }
    } catch (const Error&) {
        return nullptr;
    }
    stat_ = std::move(t);
    return stat_.get();
}

const Hmtx* Font::hmtx() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (hmtx_) return hmtx_.get();
    size_t len;
    const uint8_t* p = sfnt_->tableData(tag_Hmtx, len);
    if (!p) return nullptr;
    auto t = std::make_unique<Hmtx>();
    uint16_t nMetrics = 0, nGlyphs = 0;
    if (const Hhea* h = hhea()) nMetrics = h->numberOfHMetrics;
    if (const Maxp* m = maxp()) nGlyphs = m->numGlyphs;
    try {
        t->parse(Reader(p, len), nMetrics, nGlyphs);
    } catch (const Error&) {
        return nullptr;
    }
    hmtx_ = std::move(t);
    return hmtx_.get();
}

const Hmtx* Font::vmtx() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (vmtx_) return vmtx_.get();
    size_t len;
    const uint8_t* p = sfnt_->tableData(tag_Vmtx, len);
    if (!p) return nullptr;
    auto t = std::make_unique<Hmtx>();
    t->vertical = true;
    uint16_t nMetrics = 0, nGlyphs = 0;
    if (const Hhea* h = vhea()) nMetrics = h->numberOfHMetrics;
    if (const Maxp* m = maxp()) nGlyphs = m->numGlyphs;
    try {
        t->parse(Reader(p, len), nMetrics, nGlyphs);
    } catch (const Error&) {
        return nullptr;
    }
    vmtx_ = std::move(t);
    return vmtx_.get();
}

const GlyfTable* Font::glyf() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (glyf_) return glyf_.get();
    size_t len, llen;
    const uint8_t* p = sfnt_->tableData(tag_Glyf, len);
    const uint8_t* lp = sfnt_->tableData(tag_Loca, llen);
    if (!p || !lp) return nullptr;
    auto t = std::make_unique<GlyfTable>();
    int16_t locFormat = 0;
    uint16_t nGlyphs = 0;
    if (const Head* h = head()) locFormat = h->indexToLocFormat;
    if (const Maxp* m = maxp()) nGlyphs = m->numGlyphs;
    try {
        t->parse(Reader(p, len), Reader(lp, llen), locFormat, nGlyphs);
    } catch (const Error&) {
        return nullptr;
    }
    glyf_ = std::move(t);
    return glyf_.get();
}

const CffFont* Font::cff() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (cff_) return cff_.get();
    Tag tag = sfnt_->has(tag_CFF2) ? tag_CFF2 : tag_CFF;
    size_t len;
    const uint8_t* p = sfnt_->tableData(tag, len);
    if (!p) return nullptr;
    auto t = std::make_unique<CffFont>();
    try {
        t->parse(Reader(p, len));
    } catch (const Error&) {
        return nullptr;
    }
    cff_ = std::move(t);
    return cff_.get();
}

const LayoutTables* Font::layout() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (layout_) return layout_.get();
    auto t = std::make_unique<LayoutTables>();
    size_t len;
    const uint8_t* gdef = sfnt_->tableData(tag_GDEF, len);
    const uint8_t* gsub = sfnt_->tableData(tag_GSUB, len);
    const uint8_t* gpos = sfnt_->tableData(tag_GPOS, len);
    try {
        t->parse(gdef ? Reader(gdef, len) : Reader(),
                 gsub ? Reader(gsub, len) : Reader(),
                 gpos ? Reader(gpos, len) : Reader());
    } catch (const Error&) {
    }
    layout_ = std::move(t);
    return layout_.get();
}

const ColorTables* Font::color() const {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (color_) return color_.get();
    auto t = std::make_unique<ColorTables>();
    size_t len;
    uint16_t nGlyphs = 0;
    if (const Maxp* m = maxp()) nGlyphs = m->numGlyphs;
    const uint8_t* colr = sfnt_->tableData(tag_COLR, len);
    const uint8_t* cpal = sfnt_->tableData(tag_CPAL, len);
    const uint8_t* sbix = sfnt_->tableData(tag_Sbix, len);
    const uint8_t* cblc = sfnt_->tableData(tag_CBLC, len);
    const uint8_t* cbdt = sfnt_->tableData(tag_CBDT, len);
    const uint8_t* svg = sfnt_->tableData(tag_SVG_, len);
    try {
        t->parse(colr ? Reader(colr, len) : Reader(),
                 cpal ? Reader(cpal, len) : Reader(),
                 sbix ? Reader(sbix, len) : Reader(),
                 cblc ? Reader(cblc, len) : Reader(),
                 cbdt ? Reader(cbdt, len) : Reader(),
                 svg ? Reader(svg, len) : Reader(),
                 nGlyphs);
    } catch (const Error&) {
    }
    color_ = std::move(t);
    return color_.get();
}

// ---------------------------------------------------------------------------
// raw
// ---------------------------------------------------------------------------

std::vector<Tag> Font::tableTags() const { return sfnt_->tableTags(); }
bool Font::hasTable(Tag t) const { return sfnt_->has(t); }

const uint8_t* Font::rawTable(Tag t, size_t& len) const {
    return sfnt_->tableData(t, len);
}
std::vector<uint8_t> Font::rawTableCopy(Tag t) const {
    size_t len;
    const uint8_t* p = rawTable(t, len);
    if (!p) return {};
    return std::vector<uint8_t>(p, p + len);
}

bool Font::verifyChecksums(std::string* err) const {
    return sfnt_->verifyChecksums(err);
}

// ---------------------------------------------------------------------------
// info
// ---------------------------------------------------------------------------

static std::string containerName(ContainerType c) {
    switch (c) {
    case ContainerType::Sfnt: return "sfnt";
    case ContainerType::Ttc: return "ttc";
    case ContainerType::Woff: return "woff";
    case ContainerType::Woff2: return "woff2";
    default: return "unknown";
    }
}

FontInfo Font::info(bool computeChecksums) const {
    FontInfo fi;
    fi.path = path_;
    const NameTable* nm = nameTable();
    auto get = [&](uint16_t id) -> std::string { return nm ? nm->get(id) : std::string(); };
    fi.family = get(NAME_FAMILY);
    fi.subfamily = get(NAME_SUBFAMILY);
    fi.fullName = get(NAME_FULLNAME);
    fi.postScriptName = get(NAME_POSTSCRIPT);
    fi.version = get(NAME_VERSION);
    fi.uniqueID = get(NAME_UNIQUEID);
    fi.copyright = get(NAME_COPYRIGHT);
    fi.trademark = get(NAME_TRADEMARK);
    fi.designer = get(NAME_DESIGNER);
    fi.description = get(NAME_DESCRIPTION);
    fi.manufacturer = get(NAME_MANUFACTURER);
    fi.license = get(NAME_LICENSE);
    fi.licenseUrl = get(NAME_LICENSE_URL);
    fi.typoFamily = get(NAME_TYPO_FAMILY);
    fi.typoSubfamily = get(NAME_TYPO_SUBFAMILY);

    if (const Head* h = head()) {
        fi.unitsPerEm = h->unitsPerEm;
        fi.bounds = {h->xMin, h->yMin, h->xMax, h->yMax, true};
    }
    if (const Maxp* m = maxp()) fi.numGlyphs = m->numGlyphs;
    fi.numFaces = sfnt_->numFaces();
    fi.faceIndex = sfnt_->faceIndex();
    fi.outline = outlineFormat();
    fi.container = sfnt_->container();
    fi.flavor = containerName(sfnt_->container());
    switch (fi.outline) {
    case OutlineFormat::TrueType: fi.flavor += "/TrueType"; break;
    case OutlineFormat::CFF: fi.flavor += "/CFF"; break;
    case OutlineFormat::CFF2: fi.flavor += "/CFF2"; break;
    default: break;
    }

    if (const Hhea* h = hhea()) {
        fi.ascender = h->ascender;
        fi.descender = h->descender;
        fi.lineGap = h->lineGap;
        fi.advanceWidthMax = h->advanceWidthMax;
    }
    if (const Os2* o = os2()) {
        fi.typoAscender = o->sTypoAscender;
        fi.typoDescender = o->sTypoDescender;
        fi.typoLineGap = o->sTypoLineGap;
        fi.winAscent = o->usWinAscent;
        fi.winDescent = o->usWinDescent;
        fi.weightClass = o->usWeightClass;
        fi.widthClass = o->usWidthClass;
        fi.fsSelection = o->fsSelection;
        fi.fsType = o->fsType;
        fi.isBold = (o->fsSelection & 0x20) != 0;
        fi.isItalic = (o->fsSelection & 0x01) != 0;
        fi.isOblique = (o->fsSelection & 0x200) != 0;
        fi.isRegular = (o->fsSelection & 0x40) != 0;
        fi.vendor = o->achVendID;
        memcpy(fi.panose, o->panose, 10);
    }
    if (const Post* p = post()) {
        fi.fixedPitch = p->isFixedPitch != 0;
        fi.italicAngle = p->italicAngle;
    }
    if (const Cmap* c = cmap()) {
        auto entries = c->bestEntries();
        fi.cmapMappings = entries.size();
        if (!entries.empty()) {
            fi.cmapMin = entries.front().first;
            fi.cmapMax = entries.back().first;
        }
        if (const CmapSubtable* b = c->best()) fi.cmapBestFormat = b->format;
    }
    fi.hasVertical = hasTable(tag_Vhea) && hasTable(tag_Vmtx);
    if (const Fvar* f = fvar()) {
        fi.isVariable = !f->axes.empty();
        fi.numAxes = f->axes.size();
        fi.numInstances = f->instances.size();
    }
    fi.tableTags = tableTags();
    fi.fileSize = sfnt_->originalBytes().size();
    if (computeChecksums) {
        fi.checksumsOk = sfnt_->verifyChecksums(&fi.checksumError);
    } else {
        fi.checksumsOk = true;
        fi.checksumError.clear();
        fi.checksumsVerified = false;
    }
    return fi;
}

// ---------------------------------------------------------------------------
// glyphs
// ---------------------------------------------------------------------------

uint16_t Font::numGlyphs() const {
    if (const Maxp* m = maxp()) return m->numGlyphs;
    if (const CffFont* c = cff()) return c->numGlyphs();
    return 0;
}

OutlineFormat Font::outlineFormat() const {
    if (hasTable(tag_CFF2)) return OutlineFormat::CFF2;
    if (hasTable(tag_CFF)) return OutlineFormat::CFF;
    if (hasTable(tag_Glyf)) return OutlineFormat::TrueType;
    return OutlineFormat::None;
}

uint16_t Font::glyphForUnicode(uint32_t cp) const {
    if (const Cmap* c = cmap()) return c->glyph(cp);
    return 0;
}

uint32_t Font::unicodeForGlyph(uint16_t gid) const {
    if (const Cmap* c = cmap()) {
        if (const CmapSubtable* b = c->best()) {
            for (auto& e : b->map)
                if (e.second == gid) return e.first;
        }
    }
    return 0;
}

std::string Font::glyphName(uint16_t gid) const {
    switch (outlineFormat()) {
    case OutlineFormat::CFF:
    case OutlineFormat::CFF2:
        if (const CffFont* c = cff()) return c->glyphName(gid);
        break;
    default:
        if (const Post* p = post()) {
            std::string n = p->glyphName(gid);
            if (!n.empty()) return n;
        }
        break;
    }
    return "gid" + std::to_string(gid);
}

uint16_t Font::glyphForName(const std::string& name) const {
    if (const CffFont* c = cff()) {
        if (!c->isCFF2()) {
            for (uint16_t g = 0; g < c->numGlyphs(); g++)
                if (c->glyphName(g) == name) return g;
        }
    }
    if (const Post* p = post()) {
        for (uint16_t g = 0; g < numGlyphs(); g++)
            if (p->glyphName(g) == name) return g;
    }
    // unicode name like "uniXXXX" / "uXXXXX"
    uint32_t cp = 0;
    bool parsed = false;
    if (name.size() > 3 && name.compare(0, 3, "uni") == 0 && name.size() <= 7) {
        parsed = true;
        for (size_t i = 3; i < name.size() && parsed; i++) {
            char c = name[i];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= c - '0';
            else if (c >= 'a' && c <= 'f') cp |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') cp |= c - 'A' + 10;
            else parsed = false;
        }
    } else if (name.size() > 1 && name[0] == 'u' && name.size() <= 6) {
        parsed = true;
        for (size_t i = 1; i < name.size() && parsed; i++) {
            char c = name[i];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= c - '0';
            else if (c >= 'a' && c <= 'f') cp |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') cp |= c - 'A' + 10;
            else parsed = false;
        }
    }
    if (parsed) return glyphForUnicode(cp);
    return 0;
}

std::vector<uint16_t> Font::glyphIdsForUnicodes(const std::vector<uint32_t>& cps) const {
    std::vector<uint16_t> out(cps.size(), 0);
    const Cmap* c = cmap();
    if (!c) return out;
    for (size_t i = 0; i < cps.size(); i++)
        out[i] = c->glyph(cps[i]);
    return out;
}

void Font::compositeClosure(const std::vector<uint16_t>& gids, std::vector<bool>& keep) const {
    if (const GlyfTable* g = glyf()) {
        std::vector<bool> visited(g->locaCount(), false);
        for (uint16_t gid : gids)
            g->componentClosure(gid, keep, visited);
    }
}

// ---------------------------------------------------------------------------
// variable support
// ---------------------------------------------------------------------------

bool Font::isVariable() const {
    if (const Fvar* f = fvar()) return !f->axes.empty();
    return false;
}

std::vector<double> Font::defaultCoords() const {
    std::vector<double> out;
    if (const Fvar* f = fvar())
        for (auto& a : f->axes) out.push_back(a.defaultValue);
    return out;
}

std::vector<double> Font::normalizeCoords(const std::vector<double>& userCoords) const {
    std::vector<double> norm;
    const Fvar* f = fvar();
    if (!f) return norm;
    norm = f->normalize(userCoords);
    if (const Avar* av = avar()) av->apply(norm);
    return norm;
}

// ---------------------------------------------------------------------------
// glyph rendering (glyf + gvar / CFF + CFF2 blend)
// ---------------------------------------------------------------------------

CffGlyphInfo Font::cffGlyph(uint16_t gid, const std::vector<double>* norm) const {
    const CffFont* c = cff();
    if (!c) return CffGlyphInfo();

    // CFF2 blends need the variation store even at the default location: the
    // number of region deltas per value comes from the store, not the stack.
    std::vector<double> zeroNorm;
    const std::vector<double>* useNorm = norm;
    if (c->isCFF2() && c->varStoreOffset >= 0 && (!useNorm || useNorm->empty())) {
        const Fvar* fv = fvar();
        zeroNorm.assign(fv ? fv->axes.size() : 1, 0.0);
        useNorm = &zeroNorm;
    }

    std::unique_ptr<ItemVariationStore> store;
    std::unique_ptr<StoreRegions> regions;
    if (useNorm && !useNorm->empty() && c->varStoreOffset >= 0) {
        Tag tag = c->isCFF2() ? tag_CFF2 : tag_CFF;
        size_t len;
        const uint8_t* p = sfnt_->tableData(tag, len);
        // In CFF2 the vstore operand points at a length-prefixed
        // ItemVariationStore (u16 length, then the store).
        size_t vsOff = (size_t)c->varStoreOffset + (c->isCFF2() ? 2 : 0);
        if (p && vsOff < len) {
            store = std::make_unique<ItemVariationStore>();
            try {
                store->parse(Reader(p + vsOff, len - vsOff));
            } catch (const Error&) {
                store.reset();
            }
            if (store)
                regions = std::make_unique<StoreRegions>(store.get(), useNorm);
        }
    }
    return c->interpret(gid, regions.get());
}

// CFF path composition with seac support and affine transform.
void Font::composeCff(uint16_t gid, const double m[6], const std::vector<double>* norm,
                      Path& out, int depth) const {
    if (depth > 16) return;
    CffGlyphInfo info = cffGlyph(gid, norm);
    if (!info.ok) return;
    Path& local = info.path;
    auto xf = [&m](float& x, float& y) {
        double dx = x, dy = y;
        double nx = m[0] * dx + m[2] * dy + m[4];
        double ny = m[1] * dx + m[3] * dy + m[5];
        x = (float)nx;
        y = (float)ny;
    };
    for (auto& c : local.cmds) {
        switch (c.op) {
        case PathOp::MoveTo:
        case PathOp::LineTo: xf(c.x0, c.y0); break;
        case PathOp::QuadTo: xf(c.x0, c.y0); xf(c.x1, c.y1); break;
        case PathOp::CubicTo: xf(c.x0, c.y0); xf(c.x1, c.y1); xf(c.x2, c.y2); break;
        case PathOp::Close: break;
        }
        if (c.op == PathOp::MoveTo) out.moves++;
        out.cmds.push_back(c);
    }
    // seac (deprecated but used): base + accent composition
    for (auto& sc : info.seac) {
        uint16_t sgid = glyphForName(sc.glyphName);
        if (sgid == 0 || sgid == gid) continue;
        double sm[6] = {m[0], m[1], m[2], m[3],
                        m[0] * sc.dx + m[2] * sc.dy + m[4],
                        m[1] * sc.dx + m[3] * sc.dy + m[5]};
        composeCff(sgid, sm, norm, out, depth + 1);
    }
}

void Font::compose(uint16_t gid, const double m[6], const std::vector<double>* norm,
                   Path& out, std::vector<std::pair<double, double>>* accPoints, int depth) const {
    if (depth > 16) return;
    const GlyfTable* glyfT = glyf();
    if (!glyfT) return;
    Glyph g = glyfT->glyph(gid);

    double phAdvance = 0;
    if (const Hmtx* hm = hmtx()) phAdvance = hm->advance(gid);

    if (g.kind == Glyph::Kind::Simple) {
        std::vector<GlyfPoint> points = g.points;
        std::vector<GlyfPoint> phantoms = {
            {0, 0, true, false}, {(int32_t)phAdvance, 0, true, false},
            {0, 0, true, false}, {0, 0, true, false}};
        if (norm && norm->size() && gvar()) {
            const Gvar* gv = gvar();
            if (gv->hasGlyphVariation(gid))
                gv->applyToPoints(gid, points, phantoms, g.endPts, *norm);
        }
        Path local;
        contourPointsToPath(points, g.endPts, local);
        auto xf = [&m](float& x, float& y) {
            double dx = x, dy = y;
            double nx = m[0] * dx + m[2] * dy + m[4];
            double ny = m[1] * dx + m[3] * dy + m[5];
            x = (float)nx;
            y = (float)ny;
        };
        for (auto& c : local.cmds) {
            switch (c.op) {
            case PathOp::MoveTo:
            case PathOp::LineTo: xf(c.x0, c.y0); break;
            case PathOp::QuadTo: xf(c.x0, c.y0); xf(c.x1, c.y1); break;
            case PathOp::CubicTo: xf(c.x0, c.y0); xf(c.x1, c.y1); xf(c.x2, c.y2); break;
            case PathOp::Close: break;
            }
            if (c.op == PathOp::MoveTo) out.moves++;
            out.cmds.push_back(c);
        }
        if (accPoints) {
            for (auto& p : points) {
                double x = p.x, y = p.y;
                xformPt(m, x, y);
                accPoints->emplace_back(x, y);
            }
        }
    } else if (g.kind == Glyph::Kind::Composite) {
        std::vector<std::pair<double, double>> offsets;
        for (auto& comp : g.components)
            offsets.emplace_back(comp.e, comp.f);
        std::vector<GlyfPoint> phantoms = {
            {0, 0, true, false}, {(int32_t)phAdvance, 0, true, false},
            {0, 0, true, false}, {0, 0, true, false}};
        if (norm && norm->size() && gvar()) {
            const Gvar* gv = gvar();
            if (gv->hasGlyphVariation(gid))
                gv->applyToComposite(gid, offsets, phantoms, *norm);
        }

        for (size_t i = 0; i < g.components.size(); i++) {
            const GlyfComponent& comp = g.components[i];
            double e = offsets[i].first, f2 = offsets[i].second;
            std::vector<std::pair<double, double>> childPts;
            if (!comp.argsAreXY() && accPoints) {
                size_t i1 = (size_t)comp.arg1, i2 = (size_t)comp.arg2;
                std::vector<std::pair<double, double>> pts;
                double cm0[6] = {m[0] * comp.a + m[2] * comp.b,
                                 m[1] * comp.a + m[3] * comp.b,
                                 m[0] * comp.c + m[2] * comp.d,
                                 m[1] * comp.c + m[3] * comp.d,
                                 m[4], m[5]};
                composePointPass(comp.glyph, cm0, pts, depth + 1);
                if (i1 < accPoints->size() && i2 < pts.size()) {
                    e = (*accPoints)[i1].first - pts[i2].first;
                    f2 = (*accPoints)[i1].second - pts[i2].second;
                }
            }
            double cm[6] = {
                m[0] * comp.a + m[2] * comp.b,
                m[1] * comp.a + m[3] * comp.b,
                m[0] * comp.c + m[2] * comp.d,
                m[1] * comp.c + m[3] * comp.d,
                m[0] * e + m[2] * f2 + m[4],
                m[1] * e + m[3] * f2 + m[5]};
            compose(comp.glyph, cm, norm, out, accPoints ? &childPts : nullptr, depth + 1);
            if (accPoints)
                accPoints->insert(accPoints->end(), childPts.begin(), childPts.end());
        }
    }
}

void Font::composePointPass(uint16_t gid, const double m[6],
                            std::vector<std::pair<double, double>>& pts, int depth) const {
    if (depth > 16) return;
    const GlyfTable* glyfT = glyf();
    if (!glyfT) return;
    Glyph g = glyfT->glyph(gid);
    if (g.kind == Glyph::Kind::Simple) {
        for (auto& p : g.points) {
            double x = p.x, y = p.y;
            xformPt(m, x, y);
            pts.emplace_back(x, y);
        }
    } else if (g.kind == Glyph::Kind::Composite) {
        for (auto& comp : g.components) {
            double cm[6] = {
                m[0] * comp.a + m[2] * comp.b,
                m[1] * comp.a + m[3] * comp.b,
                m[0] * comp.c + m[2] * comp.d,
                m[1] * comp.c + m[3] * comp.d,
                m[0] * comp.e + m[2] * comp.f + m[4],
                m[1] * comp.e + m[3] * comp.f + m[5]};
            composePointPass(comp.glyph, cm, pts, depth + 1);
        }
    }
}

GlyphData Font::glyph(uint16_t gid, const std::vector<double>* userCoords) const {
    GlyphData gd;
    gd.name = glyphName(gid);
    std::vector<double> norm;
    const std::vector<double>* normPtr = nullptr;
    if (userCoords && userCoords->size()) {
        norm = normalizeCoords(*userCoords);
        normPtr = &norm;
    }

    switch (outlineFormat()) {
    case OutlineFormat::TrueType: {
        double ident[6] = {1, 0, 0, 1, 0, 0};
        std::vector<std::pair<double, double>> acc;
        compose(gid, ident, normPtr, gd.path, &acc, 0);
        break;
    }
    case OutlineFormat::CFF:
    case OutlineFormat::CFF2: {
        double ident[6] = {1, 0, 0, 1, 0, 0};
        composeCff(gid, ident, normPtr, gd.path, 0);
        CffGlyphInfo info = cffGlyph(gid, normPtr);
        gd.width = info.width;
        gd.hasWidth = info.hasWidth;
        break;
    }
    default:
        break;
    }

    FBounds fb = gd.path.bounds();
    gd.bounds = {int16_t(fb.valid ? fb.xMin : 0), int16_t(fb.valid ? fb.yMin : 0),
                 int16_t(fb.valid ? fb.xMax : 0), int16_t(fb.valid ? fb.yMax : 0), fb.valid};
    gd.advance = advance(gid, userCoords);
    if (const Hmtx* hm = hmtx()) gd.lsb = hm->bearing(gid);
    gd.ok = !gd.path.empty() || gd.advance > 0 || gid == 0;
    return gd;
}

Path Font::glyphPath(uint16_t gid, const std::vector<double>* userCoords) const {
    return glyph(gid, userCoords).path;
}

void Font::glyphPathInto(uint16_t gid, Path& out,
                         const std::vector<double>* userCoords) const {
    out.cmds.clear();
    out.moves = 0;
    if (out.cmds.capacity() < 64) out.cmds.reserve(64);

    std::vector<double> norm;
    const std::vector<double>* normPtr = nullptr;
    if (userCoords && userCoords->size()) {
        norm = normalizeCoords(*userCoords);
        normPtr = &norm;
    }
    double ident[6] = {1, 0, 0, 1, 0, 0};
    switch (outlineFormat()) {
    case OutlineFormat::TrueType: {
        std::vector<std::pair<double, double>> acc;
        compose(gid, ident, normPtr, out, &acc, 0);
        break;
    }
    case OutlineFormat::CFF:
    case OutlineFormat::CFF2:
        composeCff(gid, ident, normPtr, out, 0);
        break;
    default:
        break;
    }
}

FBounds Font::glyphBoundsF(uint16_t gid, const std::vector<double>* userCoords) const {
    return glyph(gid, userCoords).path.bounds();
}

Bounds Font::glyphBounds(uint16_t gid, const std::vector<double>* userCoords) const {
    FBounds fb = glyphBoundsF(gid, userCoords);
    Bounds b{int16_t(fb.valid ? fb.xMin : 0), int16_t(fb.valid ? fb.yMin : 0),
             int16_t(fb.valid ? fb.xMax : 0), int16_t(fb.valid ? fb.yMax : 0), fb.valid};
    return b;
}

double Font::advance(uint16_t gid, const std::vector<double>* userCoords) const {
    double base = 0;
    if (const Hmtx* hm = hmtx()) {
        base = hm->advance(gid);
    } else if (outlineFormat() == OutlineFormat::CFF || outlineFormat() == OutlineFormat::CFF2) {
        base = cffGlyph(gid, nullptr).width;
    }
    if (userCoords && userCoords->size()) {
        std::vector<double> norm = normalizeCoords(*userCoords);
        if (const Hvar* hv = hvar()) {
            base += hv->advanceDelta(gid, norm);
        } else if (outlineFormat() == OutlineFormat::TrueType && gvar() &&
                   gvar()->hasGlyphVariation(gid)) {
            const GlyfTable* gt = glyf();
            const Gvar* gv = gvar();
            if (gt && gv) {
                Glyph g = gt->glyph(gid);
                std::vector<GlyfPoint> phantoms = {
                    {0, 0, true, false}, {(int32_t)base, 0, true, false},
                    {0, 0, true, false}, {0, 0, true, false}};
                if (g.kind == Glyph::Kind::Simple) {
                    std::vector<GlyfPoint> pts = g.points;
                    if (gv->applyToPoints(gid, pts, phantoms, g.endPts, norm))
                        base = phantoms[1].x - phantoms[0].x;
                } else if (g.kind == Glyph::Kind::Composite) {
                    std::vector<std::pair<double, double>> offs(g.components.size(), {0, 0});
                    if (gv->applyToComposite(gid, offs, phantoms, norm))
                        base = phantoms[1].x - phantoms[0].x;
                }
            }
        }
    }
    return base;
}

double Font::verticalAdvance(uint16_t gid, const std::vector<double>* userCoords) const {
    double base = 0;
    if (const Hmtx* vm = vmtx())
        base = vm->advance(gid);
    if (base == 0 && hasTable(tag_VORG)) {
        if (const Head* h = head()) base = h->unitsPerEm;
    }
    (void)userCoords;
    return base;
}

double Font::kerning(uint16_t left, uint16_t right) const {
    if (const LayoutTables* lt = layout()) {
        int16_t k = lt->kerning(left, right);
        if (k) return k;
    }
    if (const Kern* k = kern())
        return k->kern(left, right);
    return 0;
}

} // namespace ftk
