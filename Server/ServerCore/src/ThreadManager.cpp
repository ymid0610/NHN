#include "core/ThreadManager.h"

#include <atomic>
#include <thread>
#include <vector>

#include "core/CoreGlobal.h"
#include "core/IocpCore.h"
#include "core/JobQueue.h"
#include "core/Log.h"

namespace nhn {
namespace {

/// How long a worker may spend on job queues before returning to the
/// completion port. Short enough that a busy room cannot delay socket reads.
constexpr uint64 kSliceMs = 16;

/// Blocking wait on the completion port. Bounded so timers still fire on an
/// otherwise idle server.
constexpr uint32 kDispatchTimeoutMs = 10;

std::vector<std::thread> gWorkers;
std::atomic<bool> gStopRequested{false};
std::atomic<int32> gWorkerCount{0};

void WorkerLoop() {
    ThreadManager::InitThread();
    while (!gStopRequested.load(std::memory_order_relaxed)) {
        ThreadManager::PumpOnce(kDispatchTimeoutMs);
    }
}

}  // namespace

void ThreadManager::InitThread() {
    tSliceEndTick = NowTick() + kSliceMs;
}

void ThreadManager::PumpOnce(uint32 dispatchTimeoutMs) {
    // One slice covers the completion plus the job work that follows it.
    tSliceEndTick = NowTick() + kSliceMs;

    GIocpCore->Dispatch(dispatchTimeoutMs);

    GJobTimer->Distribute(NowTick());

    for (;;) {
        JobQueueRef jobQueue = GGlobalQueue->Pop();
        if (jobQueue == nullptr) {
            break;
        }
        jobQueue->Execute();

        // Execute() honours the slice internally; re-check here so a long chain
        // of parked queues cannot hold the worker off the socket indefinitely.
        if (NowTick() >= tSliceEndTick) {
            break;
        }
    }
}

void ThreadManager::Start(int32 workerCount) {
    if (workerCount <= 0) {
        workerCount = static_cast<int32>(std::thread::hardware_concurrency());
        if (workerCount <= 0) {
            workerCount = 4;
        }
    }

    gStopRequested.store(false, std::memory_order_relaxed);
    gWorkerCount.store(workerCount, std::memory_order_relaxed);

    gWorkers.reserve(static_cast<size_t>(workerCount));
    for (int32 i = 0; i < workerCount; ++i) {
        gWorkers.emplace_back(WorkerLoop);
    }

    LOG_INFO("worker pool started with {} threads", workerCount);
}

void ThreadManager::RequestStop() {
    gStopRequested.store(true, std::memory_order_relaxed);

    // Workers block in GetQueuedCompletionStatus with a short timeout, so they
    // notice the flag on their own; posting once per worker just makes exit
    // immediate instead of waiting out the timeout.
    const int32 count = gWorkerCount.load(std::memory_order_relaxed);
    for (int32 i = 0; i < count; ++i) {
        ::PostQueuedCompletionStatus(GIocpCore->GetHandle(), 0, 0, nullptr);
    }
}

void ThreadManager::Join() {
    for (std::thread& worker : gWorkers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    gWorkers.clear();
    gWorkerCount.store(0, std::memory_order_relaxed);
}

bool ThreadManager::StopRequested() {
    return gStopRequested.load(std::memory_order_relaxed);
}

int32 ThreadManager::WorkerCount() {
    return gWorkerCount.load(std::memory_order_relaxed);
}

}  // namespace nhn
