// ftk: threading utilities — shared thread pool + parallel_for.
// Part of the Font_Toolkit C++ rewrite.
#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <memory>
#include <algorithm>

namespace ftk {

class ThreadPool {
public:
    static ThreadPool& shared();

    explicit ThreadPool(unsigned threads = 0);
    ~ThreadPool();

    unsigned size() const { return (unsigned)workers_.size(); }

    // Enqueue one task.
    void run(std::function<void()> fn);

    // Wait until queue is drained and all workers idle.
    void waitIdle();

    // Run fn(i) for i in [0, n) across pool threads; blocks until done.
    // grain: minimum items per task chunk. Nested calls run serially.
    void parallelFor(size_t n, size_t grain, const std::function<void(size_t, size_t)>& fn);

private:
    void workerLoop();

    std::vector<std::thread> workers_;
    std::mutex mu_;
    std::condition_variable cvWork_;
    std::condition_variable cvDone_;
    std::vector<std::function<void()>> queue_;
    size_t active_ = 0;
    bool stop_ = false;
};

// Convenience: use the shared process-wide pool.
void parallelFor(size_t n, size_t grain, const std::function<void(size_t, size_t)>& fn);

} // namespace ftk
