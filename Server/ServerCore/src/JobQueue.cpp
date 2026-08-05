#include "core/JobQueue.h"

#include "core/CoreGlobal.h"

namespace nhn {

thread_local JobQueue* tCurrentJobQueue = nullptr;
thread_local uint64 tSliceEndTick = 0;

void JobQueue::Push(JobRef job, bool pushOnly) {
    const int32 previousCount = _jobCount.fetch_add(1);
    _jobs.Push(std::move(job));

    // Only the thread that took the count from 0 to 1 owns draining the queue;
    // everyone else just appends and returns.
    if (previousCount == 0) {
        if (tCurrentJobQueue == nullptr && !pushOnly) {
            Execute();
        } else {
            // Already busy with another queue. Handing this one to the global
            // queue keeps the two objects' jobs from nesting on one stack.
            GGlobalQueue->Push(shared_from_this());
        }
    }
}

void JobQueue::Execute() {
    tCurrentJobQueue = this;

    for (;;) {
        std::vector<JobRef> jobs;
        _jobs.PopAll(jobs);

        const int32 jobCount = static_cast<int32>(jobs.size());
        for (int32 i = 0; i < jobCount; ++i) {
            jobs[i]->Execute();
        }

        // Nothing arrived while we were running: we are done and release the
        // queue. Anyone pushing after this point re-claims it.
        if (_jobCount.fetch_sub(jobCount) == jobCount) {
            tCurrentJobQueue = nullptr;
            return;
        }

        // Work keeps arriving. Yield once the slice is spent so this worker can
        // go back to servicing socket completions; another worker picks the
        // queue up from the global queue.
        if (NowTick() >= tSliceEndTick) {
            tCurrentJobQueue = nullptr;
            GGlobalQueue->Push(shared_from_this());
            return;
        }
    }
}

void JobQueue::DoTimer(uint64 delayMs, CallbackType&& callback) {
    GJobTimer->Reserve(delayMs, shared_from_this(), std::make_shared<Job>(std::move(callback)));
}

void JobTimer::Reserve(uint64 delayMs, JobQueueRef owner, JobRef job) {
    TimerItem item;
    item.executeTick = NowTick() + delayMs;
    item.owner = std::move(owner);
    item.job = std::move(job);

    std::lock_guard<std::mutex> guard(_mutex);
    _items.push(std::move(item));
}

void JobTimer::Distribute(uint64 now) {
    // One distributor at a time; the others have completions to service and
    // should not queue up behind the heap lock.
    bool expected = false;
    if (!_distributing.compare_exchange_strong(expected, true)) {
        return;
    }

    std::vector<TimerItem> due;
    {
        std::lock_guard<std::mutex> guard(_mutex);
        while (!_items.empty()) {
            const TimerItem& earliest = _items.top();
            if (now < earliest.executeTick) {
                break;
            }
            due.push_back(earliest);
            _items.pop();
        }
    }

    _distributing.store(false);

    // Pushed outside the lock: Push can run the job inline on this thread.
    for (TimerItem& item : due) {
        item.owner->Push(item.job);
    }
}

void JobTimer::Clear() {
    std::lock_guard<std::mutex> guard(_mutex);
    std::priority_queue<TimerItem> empty;
    _items.swap(empty);
}

}  // namespace nhn
