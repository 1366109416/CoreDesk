#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace coredesk::concurrency {

class ThreadPool {
public:
    using Task = std::function<void()>;

    explicit ThreadPool(std::size_t thread_count,
                        std::size_t max_queue_size = 4096);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    bool submit(Task task);
    void wait_idle();
    void shutdown();

    std::size_t thread_count() const noexcept;
    std::size_t uncaught_exception_count() const noexcept;

private:
    void worker_loop() noexcept;

    mutable std::mutex mutex_;
    std::condition_variable task_available_;
    std::condition_variable queue_space_available_;
    std::condition_variable idle_;
    std::deque<Task> tasks_;
    std::vector<std::thread> workers_;
    std::size_t max_queue_size_{4096};
    std::size_t active_count_{0};
    std::atomic_size_t uncaught_exception_count_{0};
    bool shutting_down_{false};
};

} // namespace coredesk::concurrency
