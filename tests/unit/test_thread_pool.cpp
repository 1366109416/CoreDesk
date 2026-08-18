#include "coredesk/common/Cancellation.h"
#include "coredesk/concurrency/ThreadPool.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace coredesk {
namespace {

using concurrency::ThreadPool;

TEST(CancellationTest, SourceCancelsToken)
{
    CancellationSource source;
    const auto token = source.token();

    EXPECT_FALSE(token.is_cancelled());
    source.cancel();
    EXPECT_TRUE(token.is_cancelled());
}

TEST(ThreadPoolTest, ExecutesSubmittedTasks)
{
    ThreadPool pool(4);
    std::atomic<int> counter{0};

    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(pool.submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    pool.wait_idle();
    EXPECT_EQ(counter.load(std::memory_order_relaxed), 1000);
}

TEST(ThreadPoolTest, WaitIdleWaitsForActiveTask)
{
    ThreadPool pool(1);
    std::mutex mutex;
    std::condition_variable started;
    std::condition_variable release;
    bool task_started = false;
    bool may_finish = false;
    bool task_finished = false;

    ASSERT_TRUE(pool.submit([&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            task_started = true;
        }
        started.notify_one();

        {
            std::unique_lock<std::mutex> lock(mutex);
            release.wait(lock, [&] { return may_finish; });
            task_finished = true;
        }
    }));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(started.wait_for(lock, std::chrono::seconds(2), [&] { return task_started; }));
    }

    std::atomic<bool> waiter_done{false};
    std::thread waiter([&] {
        pool.wait_idle();
        waiter_done.store(true, std::memory_order_relaxed);
    });

    {
        std::lock_guard<std::mutex> lock(mutex);
        EXPECT_FALSE(task_finished);
        EXPECT_FALSE(waiter_done.load(std::memory_order_relaxed));
        may_finish = true;
    }
    release.notify_one();

    waiter.join();
    EXPECT_TRUE(waiter_done.load(std::memory_order_relaxed));
}

TEST(ThreadPoolTest, BoundedQueueDoesNotDropTasks)
{
    ThreadPool pool(1, 2);
    std::atomic<int> counter{0};

    for (int i = 0; i < 16; ++i) {
        ASSERT_TRUE(pool.submit([&counter] {
            counter.fetch_add(1, std::memory_order_relaxed);
        }));
    }

    pool.wait_idle();
    EXPECT_EQ(counter.load(std::memory_order_relaxed), 16);
}

TEST(ThreadPoolTest, SubmitWaitsWhenBoundedQueueIsFull)
{
    ThreadPool pool(1, 1);
    std::mutex mutex;
    std::condition_variable condition;
    bool blocker_started = false;
    bool release_blocker = false;
    bool queued_task_started = false;

    ASSERT_TRUE(pool.submit([&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            blocker_started = true;
        }
        condition.notify_all();

        std::unique_lock<std::mutex> lock(mutex);
        condition.wait(lock, [&] { return release_blocker; });
    }));

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return blocker_started; }));
    }

    ASSERT_TRUE(pool.submit([&] {
        std::lock_guard<std::mutex> lock(mutex);
        queued_task_started = true;
        condition.notify_all();
    }));

    auto third_submit = std::async(std::launch::async, [&] {
        return pool.submit([] {});
    });

    EXPECT_EQ(third_submit.wait_for(std::chrono::milliseconds(50)), std::future_status::timeout);

    {
        std::lock_guard<std::mutex> lock(mutex);
        release_blocker = true;
    }
    condition.notify_all();

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return queued_task_started; }));
    }

    EXPECT_TRUE(third_submit.get());
    pool.wait_idle();
}

TEST(ThreadPoolTest, ShutdownIsIdempotent)
{
    ThreadPool pool(2);
    ASSERT_TRUE(pool.submit([] {}));

    pool.shutdown();
    pool.shutdown();
}

TEST(ThreadPoolTest, SubmitReturnsFalseAfterShutdown)
{
    ThreadPool pool(1);
    pool.shutdown();

    EXPECT_FALSE(pool.submit([] {}));
}

TEST(ThreadPoolTest, TaskExceptionIsObservableAndWorkerContinues)
{
    ThreadPool pool(1);
    std::atomic<int> completed_after_exception{0};

    ASSERT_TRUE(pool.submit([] {
        throw std::runtime_error("expected test exception");
    }));
    ASSERT_TRUE(pool.submit([&] {
        completed_after_exception.fetch_add(1, std::memory_order_relaxed);
    }));

    pool.wait_idle();

    EXPECT_EQ(pool.uncaught_exception_count(), 1U);
    EXPECT_EQ(completed_after_exception.load(std::memory_order_relaxed), 1);
}

TEST(ThreadPoolTest, DestructorShutdownsAndJoinsWorkers)
{
    std::mutex mutex;
    std::condition_variable condition;
    bool task_started = false;
    bool release_task = false;
    bool task_finished = false;

    {
        ThreadPool pool(1);
        ASSERT_TRUE(pool.submit([&] {
            {
                std::lock_guard<std::mutex> lock(mutex);
                task_started = true;
            }
            condition.notify_all();

            std::unique_lock<std::mutex> lock(mutex);
            condition.wait(lock, [&] { return release_task; });
            task_finished = true;
        }));

        {
            std::unique_lock<std::mutex> lock(mutex);
            ASSERT_TRUE(condition.wait_for(lock, std::chrono::seconds(2), [&] { return task_started; }));
            release_task = true;
        }
        condition.notify_all();
    }

    std::lock_guard<std::mutex> lock(mutex);
    EXPECT_TRUE(task_finished);
}

} // namespace
} // namespace coredesk
