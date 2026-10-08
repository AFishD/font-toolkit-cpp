// Isolation probe: teardown hang analysis.
#include "ftk/Platform.hpp"
#include "ftk/Threads.hpp"
#include "ftk/Font.hpp"

#include <atomic>
#include <cstdio>
#include <string>

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
    std::string dir = argc > 1 ? argv[1] : "Fonts";

    printf("[1] start\n");
    auto files = ftk::listFiles(dir, false);
    printf("[2] listFiles -> %zu\n", files.size());

    {
        ftk::ThreadPool pool(4);
        printf("[3] local pool(4) constructed\n");
        std::atomic<int> counter{0};
        pool.parallelFor(100, 1, [&](size_t lo, size_t hi) {
            for (size_t i = lo; i < hi; i++) counter.fetch_add(1);
        });
        printf("[4] parallelFor -> %d\n", counter.load());
        printf("[5] leaving scope (local pool destructor)\n");
    }
    printf("[6] local pool destroyed\n");

    ftk::ThreadPool& shared = ftk::ThreadPool::shared();
    printf("[7] shared pool size %u\n", shared.size());
    {
        std::atomic<int> c2{0};
        shared.parallelFor(50, 1, [&](size_t lo, size_t hi) {
            for (size_t i = lo; i < hi; i++) c2.fetch_add(1);
        });
        printf("[8] shared parallelFor -> %d\n", c2.load());
    }

    if (!files.empty()) {
        printf("[9] opening %s\n", files[0].c_str());
        auto font = ftk::Font::openFile(files[0]);
        auto info = font->info();
        printf("[10] opened: %s (%u glyphs)\n", info.family.c_str(), info.numGlyphs);
    }
    printf("[11] main returning\n");
    return 0;
}
