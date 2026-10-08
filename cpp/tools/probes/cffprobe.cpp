// CFF diagnostic probe.
#include "ftk/Font.hpp"
#include "ftk/Cff.hpp"

#include <cstdio>
#include <string>

using namespace ftk;

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (argc < 2) {
        fprintf(stderr, "usage: cffprobe <font> [gid]\n");
        return 1;
    }
    std::string path = argv[1];
    uint16_t gid = argc > 2 ? (uint16_t)atoi(argv[2]) : 1;
    try {
        auto font = Font::openFile(path);
        printf("outlineFormat = %d\n", (int)font->outlineFormat());
        size_t len = 0;
        const uint8_t* raw = font->rawTable(tag_CFF, len);
        if (!raw) raw = font->rawTable(tag_CFF2, len);
        printf("CFF table size = %zu\n", len);
        if (!raw) return 0;

        CffFont cff;
        try {
            cff.parse(Reader(raw, len));
        } catch (const Error& e) {
            printf("CFF parse ERROR: %s\n", e.what());
            return 1;
        }
        printf("version=%d name='%s' isCID=%d numGlyphs=%u gsubrs=%zu fonts=%zu\n",
               (int)cff.majorVersion(), cff.fontName.c_str(), cff.isCID ? 1 : 0,
               cff.numGlyphs(), cff.globalSubrs.count(), cff.fonts.size());
        printf("charStringsOffset=%lld charsetOffset=%lld fdArray=%lld fdSelect=%lld\n",
               (long long)cff.charStringsOffset, (long long)cff.charsetOffset,
               (long long)cff.fdArrayOffset, (long long)cff.fdSelectOffset);
        if (gid < cff.numGlyphs()) {
            size_t clen = 0;
            const uint8_t* cs = cff.charstringBytes(gid, clen);
            printf("charstring[%u] len=%zu bytes:", gid, clen);
            for (size_t i = 0; i < clen && i < 32; i++) printf(" %02X", cs[i]);
            printf("\n");
            CffGlyphInfo info = cff.interpret(gid);
            printf("interpret: ok=%d segs=%zu hintCount=%zu hasWidth=%d width=%g\n",
                   info.ok ? 1 : 0, info.path.cmds.size(), info.hintCount,
                   info.hasWidth ? 1 : 0, info.width);
            uint16_t fd = cff.fdOfGlyph(gid);
            printf("fd=%u  fdPriv(subrs=%lld nom=%lld def=%lld)\n", fd,
                   (long long)(fd < cff.fonts.size() ? cff.fonts[fd].priv.subrs : -1),
                   (long long)(fd < cff.fonts.size() ? cff.fonts[fd].priv.nominalWidthX : -1),
                   (long long)(fd < cff.fonts.size() ? cff.fonts[fd].priv.defaultWidthX : -1));
            const CffIndex* ls = cff.localSubrs(fd);
            printf("localSubrs = %zu\n", ls ? ls->count() : 0);
        }
        GlyphData gd = font->glyph(gid);
        printf("Font::glyph -> %zu segs ok=%d advance=%g\n", gd.path.cmds.size(), gd.ok ? 1 : 0, gd.advance);
    } catch (const Error& e) {
        printf("ERROR: %s\n", e.what());
        return 1;
    }
    return 0;
}
