// ftk unit + corpus tests.
#include "ftk/Bytes.hpp"
#include "ftk/Inflate.hpp"
#include "ftk/Font.hpp"
#include "ftk/Dump.hpp"
#include "ftk/Subset.hpp"
#include "ftk/Platform.hpp"
#include "ftk/Threads.hpp"
#include "ftk/Layout.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace ftk;

static int g_fail = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);\
            g_fail++;                                                      \
        }                                                                  \
    } while (0)

static void testBytes() {
    uint8_t buf[] = {0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC};
    Reader r(buf, sizeof buf);
    CHECK(r.u16() == 0x1234);
    CHECK(r.u24() == 0x56789A);
    CHECK(r.u8() == 0xBC);
    Writer w;
    w.u32(0xDEADBEEF);
    w.patch32(0, 0x01020304);
    CHECK(w.buf().size() == 4);
    CHECK(w.buf()[0] == 1 && w.buf()[3] == 4);
    CHECK(tagToStr(makeTag('h','e','a','d')) == "head");
    CHECK(strToTag("OS/2") == makeTag('O','S','/','2'));
    uint32_t sum = checksumBE(buf, 6);
    CHECK(sum == 0x12345678 + 0x9ABC0000);
}

static void testInflate() {
    // zlib stream of "hello hello hello" made with stored blocks
    const char* msg = "hello hello hello";
    auto z1 = zlibCompressStored((const uint8_t*)msg, strlen(msg));
    auto back = zlibDecompress(z1.data(), z1.size());
    CHECK(back.size() == strlen(msg));
    CHECK(memcmp(back.data(), msg, strlen(msg)) == 0);
}

static void testUnicode() {
    std::string s = utf16BEToUtf8((const uint8_t*)"\x4F\x60\x59\x7D", 4);
    CHECK(s == "\xE4\xBD\xA0\xE5\xA5\xBD"); // 你好
    auto be = utf8ToUtf16BE(s);
    CHECK(be.size() == 4 && be[0] == 0x4F && be[1] == 0x60);
}

static void testTtc(const std::string& fontsDir) {
    // find the TTC in the corpus
    auto files = listFiles(fontsDir, false);
    std::string ttc;
    for (auto& f : files) {
        std::string lower = toLowerAscii(f);
        if (lower.size() > 4 && lower.compare(lower.size() - 4, 4, ".ttc") == 0) {
            ttc = f;
            break;
        }
    }
    if (ttc.empty()) {
        fprintf(stderr, "note: no TTC in corpus, skipping\n");
        return;
    }
    int faces = Font::numFacesInFile(ttc);
    CHECK(faces >= 1);
    auto font = Font::openFile(ttc, 0);
    FontInfo fi = font->info();
    CHECK(fi.numFaces == faces);
    if (faces > 1) {
        auto f2 = Font::openFile(ttc, 1);
        CHECK(f2->info().faceIndex == 1);
    }
}

static void testThreadPool() {
    std::atomic<int> counter{0};
    parallelFor(1000, 1, [&](size_t lo, size_t hi) {
        for (size_t i = lo; i < hi; i++) counter.fetch_add(1);
    });
    CHECK(counter.load() == 1000);
    fprintf(stderr, "pool size: %u\n", ThreadPool::shared().size());
}

int main(int argc, char** argv) {
    // Corpus tests need a font directory: argv[1], else $FTK_FONTS, else ./Fonts.
    std::string fontsDir = "Fonts";
    if (const char* env = getenv("FTK_FONTS")) fontsDir = env;
    if (argc > 1) fontsDir = argv[1];

    testBytes();
    testInflate();
    testUnicode();
    testThreadPool();
    testTtc(fontsDir);

    if (g_fail) {
        fprintf(stderr, "%d test(s) FAILED\n", g_fail);
        return 1;
    }
    printf("all tests passed\n");
    return 0;
}
