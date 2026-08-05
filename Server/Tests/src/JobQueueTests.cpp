#include <gtest/gtest.h>

#include <atomic>
#include <thread>
#include <vector>

#include "core/Core.h"

using namespace nhn;

namespace {

/// Boots the core once for the whole binary: the job queue depends on the
/// global queue, the timer and the worker pool.
class CoreEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        Core::Options options;
        options.processName = "Tests";
        options.logDirectory = "";  // console only
        options.logLevel = LogLevel::Warn;
        // A test that trips an invariant must fail the run, not block waiting
        // for someone to press a key.
        options.waitForDeveloperOnFatal = false;
        options.workerThreadCount = 4;
        Core::Init(options);
    }

    void TearDown() override { Core::Shutdown(); }
};

const auto* kCoreEnvironment =
    ::testing::AddGlobalTestEnvironment(new CoreEnvironment());

bool WaitUntil(const std::function<bool()>& predicate, uint32 timeoutMs) {
    const TickCount deadline = NowTick() + timeoutMs;
    while (NowTick() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return predicate();
}

}  // namespace

TEST(JobQueue, RunsEveryPushedJobExactlyOnce) {
    auto queue = std::make_shared<JobQueue>();
    std::atomic<int32> executed{0};

    constexpr int32 kJobCount = 5000;
    for (int32 i = 0; i < kJobCount; ++i) {
        queue->DoAsync([&executed]() { executed.fetch_add(1); });
    }

    ASSERT_TRUE(WaitUntil([&] { return executed.load() == kJobCount; }, 10'000))
        << "only " << executed.load() << " of " << kJobCount << " jobs ran";
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_EQ(executed.load(), kJobCount) << "a job ran more than once";
}

TEST(JobQueue, NeverRunsTwoJobsFromOneQueueConcurrently) {
    auto queue = std::make_shared<JobQueue>();
    std::atomic<int32> inside{0};
    std::atomic<int32> overlaps{0};
    std::atomic<int32> completed{0};

    constexpr int32 kJobCount = 2000;

    // Pushed from several threads at once, which is exactly the contention the
    // room logic relies on being handled.
    std::vector<std::thread> pushers;
    for (int32 t = 0; t < 4; ++t) {
        pushers.emplace_back([&, t]() {
            for (int32 i = 0; i < kJobCount / 4; ++i) {
                queue->DoAsync([&]() {
                    if (inside.fetch_add(1) != 0) {
                        overlaps.fetch_add(1);
                    }
                    // Long enough that a genuine overlap would be observed.
                    std::this_thread::yield();
                    inside.fetch_sub(1);
                    completed.fetch_add(1);
                });
            }
            (void)t;
        });
    }
    for (std::thread& pusher : pushers) {
        pusher.join();
    }

    ASSERT_TRUE(WaitUntil([&] { return completed.load() == kJobCount; }, 20'000))
        << "only " << completed.load() << " of " << kJobCount << " jobs ran";
    EXPECT_EQ(overlaps.load(), 0) << "two jobs from one queue ran at the same time";
}

TEST(JobQueue, KeepsPerQueueOrdering) {
    auto queue = std::make_shared<JobQueue>();
    std::vector<int32> order;
    std::mutex orderLock;
    std::atomic<int32> completed{0};

    constexpr int32 kJobCount = 500;
    for (int32 i = 0; i < kJobCount; ++i) {
        queue->DoAsync([&, i]() {
            {
                std::lock_guard<std::mutex> guard(orderLock);
                order.push_back(i);
            }
            completed.fetch_add(1);
        });
    }

    ASSERT_TRUE(WaitUntil([&] { return completed.load() == kJobCount; }, 10'000));

    std::lock_guard<std::mutex> guard(orderLock);
    ASSERT_EQ(order.size(), static_cast<size_t>(kJobCount));
    for (int32 i = 0; i < kJobCount; ++i) {
        ASSERT_EQ(order[static_cast<size_t>(i)], i) << "jobs were reordered at index " << i;
    }
}

TEST(JobQueue, IndependentQueuesRunInParallel) {
    // Two queues must not serialise against each other, otherwise one busy room
    // would stall every other room on the server.
    auto first = std::make_shared<JobQueue>();
    auto second = std::make_shared<JobQueue>();

    std::atomic<bool> firstEntered{false};
    std::atomic<bool> secondSawFirst{false};
    std::atomic<bool> done{false};

    first->DoAsync([&]() {
        firstEntered.store(true);
        // Hold this queue while the other one runs.
        const TickCount deadline = NowTick() + 500;
        while (NowTick() < deadline && !secondSawFirst.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    second->DoAsync([&]() {
        if (WaitUntil([&] { return firstEntered.load(); }, 1000)) {
            secondSawFirst.store(true);
        }
        done.store(true);
    });

    ASSERT_TRUE(WaitUntil([&] { return done.load(); }, 5000));
    EXPECT_TRUE(secondSawFirst.load()) << "the second queue was blocked by the first";
}

TEST(JobTimer, RunsJobsAfterTheRequestedDelay) {
    auto queue = std::make_shared<JobQueue>();
    std::atomic<TickCount> firedAt{0};

    const TickCount scheduledAt = NowTick();
    queue->DoTimer(120, [&]() { firedAt.store(NowTick()); });

    ASSERT_TRUE(WaitUntil([&] { return firedAt.load() != 0; }, 5000));

    const uint64 elapsed = firedAt.load() - scheduledAt;
    EXPECT_GE(elapsed, 100u) << "timer fired early";
    EXPECT_LT(elapsed, 2000u) << "timer fired far too late";
}

TEST(JobTimer, RunsTimersInDueOrder) {
    auto queue = std::make_shared<JobQueue>();
    std::vector<int32> order;
    std::mutex orderLock;

    // Scheduled out of order on purpose.
    const std::vector<std::pair<uint32, int32>> schedule = {
        {200, 3}, {50, 1}, {300, 4}, {120, 2}};

    for (const auto& [delay, tag] : schedule) {
        queue->DoTimer(delay, [&, tag]() {
            std::lock_guard<std::mutex> guard(orderLock);
            order.push_back(tag);
        });
    }

    ASSERT_TRUE(WaitUntil(
        [&] {
            std::lock_guard<std::mutex> guard(orderLock);
            return order.size() == schedule.size();
        },
        5000));

    std::lock_guard<std::mutex> guard(orderLock);
    EXPECT_EQ(order, (std::vector<int32>{1, 2, 3, 4}));
}
