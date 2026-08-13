#include "coredesk/concurrency/ThreadPool.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace coredesk::concurrency {

ThreadPool::ThreadPool(std::size_t thread_count, std::size_t max_queue_size)
    : max_queue_size_(std::max<std::size_t>(1, max_queue_size))
{
    if (thread_count == 0) {
        throw std::invalid_argument("ThreadPool thread_count must be greater than zero");
    }

    workers_.reserve(thread_count);
    for (std::size_t i = 0; i < thread_count; ++i) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool()
{
    shutdown();
}

bool ThreadPool::submit(Task task)
{
    if (!task) {
        return false;
    }

    std::unique_lock<std::mutex> lock(mutex_);
    queue_space_available_.wait(lock, [this] {
        return shutting_down_ || tasks_.size() < max_queue_size_;
    });

    if (shutting_down_) {
        return false;
    }

    tasks_.push_back(std::move(task));
    task_available_.notify_one();
    return true;
}

void ThreadPool::wait_idle()
{
    std::unique_lock<std::mutex> lock(mutex_);
    idle_.wait(lock, [this] {
        return tasks_.empty() && active_count_ == 0;
    });
}

void ThreadPool::shutdown()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutting_down_) {
            return;
        }
        shutting_down_ = true;
    }

    task_available_.notify_all();
    queue_space_available_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        idle_.notify_all();
    }
}

std::size_t ThreadPool::thread_count() const noexcept
{
    return workers_.size();
}

std::size_t ThreadPool::uncaught_exception_count() const noexcept
{
    return uncaught_exception_count_.load(std::memory_order_relaxed);
}

void ThreadPool::worker_loop() noexcept
{
    while (true) {
        Task task;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            task_available_.wait(lock, [this] {
                return shutting_down_ || !tasks_.empty();
            });

            if (shutting_down_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop_front();
            ++active_count_;
            queue_space_available_.notify_one();
        }

        try {
            task();
        } catch (...) {
            uncaught_exception_count_.fetch_add(1, std::memory_order_relaxed);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            --active_count_;
            if (tasks_.empty() && active_count_ == 0) {
                idle_.notify_all();
            }
        }
    }
}

} // namespace coredesk::concurrency
