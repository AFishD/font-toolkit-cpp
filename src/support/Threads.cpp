#include "ftk/Threads.hpp"

namespace ftk {

ThreadPool& ThreadPool::shared() {
    // Intentionally leaked: on MinGW-w64, running a thread-owning static
    // destructor from inside a DLL during process teardown deadlocks against
    // the loader lock (reproducible with a minimal DLL singleton). The worker
    // threads are reclaimed by the OS at process exit.
    static ThreadPool* pool = new ThreadPool();
    return *pool;
}

ThreadPool::ThreadPool(unsigned threads) {
    if (threads == 0) {
        unsigned hc = std::thread::hardware_concurrency();
        threads = hc ? hc : 4;
    }
    for (unsigned i = 0; i < threads; i++)
        workers_.emplace_back([this] { workerLoop(); });
}

ThreadPool::~ThreadPool() {
    if (getenv("FTK_TRACE")) { fprintf(stderr, "[trace] ~ThreadPool begin (workers=%zu)\n", workers_.size()); fflush(stderr); }
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    cvWork_.notify_all();
    for (auto& w : workers_)
        if (w.joinable()) w.join();
    if (getenv("FTK_TRACE")) { fprintf(stderr, "[trace] ~ThreadPool end\n"); fflush(stderr); }
}

void ThreadPool::run(std::function<void()> fn) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        queue_.push_back(std::move(fn));
    }
    cvWork_.notify_one();
}

void ThreadPool::waitIdle() {
    std::unique_lock<std::mutex> lk(mu_);
    cvDone_.wait(lk, [this] { return queue_.empty() && active_ == 0; });
}

void ThreadPool::workerLoop() {
    for (;;) {
        std::function<void()> job;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cvWork_.wait(lk, [this] { return stop_ || !queue_.empty(); });
            if (stop_ && queue_.empty()) return;
            job = std::move(queue_.front());
            queue_.erase(queue_.begin());
            active_++;
        }
        job();
        {
            std::lock_guard<std::mutex> lk(mu_);
            active_--;
            if (queue_.empty() && active_ == 0) cvDone_.notify_all();
        }
    }
}

void ThreadPool::parallelFor(size_t n, size_t grain, const std::function<void(size_t, size_t)>& fn) {
    if (n == 0) return;
    if (grain == 0) grain = 1;
    if (workers_.empty() || n <= grain) {
        fn(0, n);
        return;
    }

    static thread_local bool inParallel = false;
    if (inParallel) { // avoid deadlock from nested use
        fn(0, n);
        return;
    }

    size_t ntasks = (n + grain - 1) / grain;
    size_t nthreads = workers_.size();
    if (ntasks > nthreads * 4) ntasks = nthreads * 4;
    if (ntasks < 1) ntasks = 1;

    std::atomic<size_t> next(0);
    std::atomic<int> done(0);

    for (size_t t = 1; t < ntasks; t++) {
        run([&] {
            inParallel = true;
            for (;;) {
                size_t id = next.fetch_add(1);
                if (id * grain >= n) break;
                size_t lo = id * grain;
                size_t hi = std::min(n, lo + grain);
                fn(lo, hi);
            }
            inParallel = false;
            done.fetch_add(1);
            // Wake the orchestrating waitIdle path.
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (queue_.empty() && active_ == 0) cvDone_.notify_all();
            }
        });
    }

    inParallel = true;
    for (;;) {
        size_t id = next.fetch_add(1);
        if (id * grain >= n) break;
        size_t lo = id * grain;
        size_t hi = std::min(n, lo + grain);
        fn(lo, hi);
    }
    inParallel = false;

    // Wait for spawned tasks to finish.
    waitIdle();
}

void parallelFor(size_t n, size_t grain, const std::function<void(size_t, size_t)>& fn) {
    ThreadPool::shared().parallelFor(n, grain, fn);
}

} // namespace ftk
