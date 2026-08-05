#pragma once

#include <atomic>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>

#include "core/Types.h"

namespace nhn {

using CallbackType = std::function<void()>;

class Job {
public:
    explicit Job(CallbackType&& callback) : _callback(std::move(callback)) {}
    void Execute() { _callback(); }

private:
    CallbackType _callback;
};

using JobRef = Ref<Job>;

template <class T>
class LockQueue {
public:
    void Push(T item) {
        std::lock_guard<std::mutex> guard(_mutex);
        _items.push(std::move(item));
    }

    bool Pop(T& out) {
        std::lock_guard<std::mutex> guard(_mutex);
        if (_items.empty()) {
            return false;
        }
        out = std::move(_items.front());
        _items.pop();
        return true;
    }

    void PopAll(std::vector<T>& out) {
        std::lock_guard<std::mutex> guard(_mutex);
        out.reserve(out.size() + _items.size());
        while (!_items.empty()) {
            out.push_back(std::move(_items.front()));
            _items.pop();
        }
    }

    void Clear() {
        std::lock_guard<std::mutex> guard(_mutex);
        std::queue<T> empty;
        _items.swap(empty);
    }

    size_t Size() const {
        std::lock_guard<std::mutex> guard(_mutex);
        return _items.size();
    }

private:
    mutable std::mutex _mutex;
    std::queue<T> _items;
};

class JobQueue;
using JobQueueRef = Ref<JobQueue>;

/// Serialises all work touching one logical object.
///
/// Rooms, chat channels and instances each own a queue. Every mutation of their
/// state is pushed as a job, and the queue guarantees exactly one thread runs
/// its jobs at a time. Inside a handler the object can therefore be treated as
/// single-threaded, which removes essentially every lock the room logic would
/// otherwise need — and with it the deadlocks that come from taking two room
/// locks at once (kick, host migration, handoff).
///
/// Derive from this and push work with DoAsync. Capture a strong reference to
/// the owner in the lambda; the queue does not keep the object alive by itself.
class JobQueue : public std::enable_shared_from_this<JobQueue> {
public:
    virtual ~JobQueue() = default;

    void DoAsync(CallbackType&& callback) {
        Push(std::make_shared<Job>(std::move(callback)));
    }

    /// Runs @p callback on this queue after @p delayMs. Ordering relative to
    /// other timers on the same queue is by due time, not by call order.
    void DoTimer(uint64 delayMs, CallbackType&& callback);

    void ClearJobs() { _jobs.Clear(); }

    void Push(JobRef job, bool pushOnly = false);

    /// Drains queued jobs. Called by whichever worker claimed the queue; not
    /// meant to be invoked directly.
    void Execute();

private:
    LockQueue<JobRef> _jobs;
    std::atomic<int32> _jobCount{0};
};

/// Holds queues that still have work but whose claiming thread had to let go —
/// either because it was already draining a different queue, or because it hit
/// its time slice. Workers pull from here between completions.
class GlobalQueue {
public:
    void Push(JobQueueRef jobQueue) { _jobQueues.Push(std::move(jobQueue)); }

    JobQueueRef Pop() {
        JobQueueRef result;
        return _jobQueues.Pop(result) ? result : nullptr;
    }

    void Clear() { _jobQueues.Clear(); }

private:
    LockQueue<JobQueueRef> _jobQueues;
};

/// Min-heap of jobs waiting for their due time. A single worker distributes at
/// a time; the rest skip the attempt rather than blocking.
class JobTimer {
public:
    void Reserve(uint64 delayMs, JobQueueRef owner, JobRef job);
    void Distribute(uint64 now);
    void Clear();

private:
    struct TimerItem {
        // Inverted so std::priority_queue, a max-heap, yields the earliest due.
        bool operator<(const TimerItem& other) const { return executeTick > other.executeTick; }

        uint64 executeTick = 0;
        JobQueueRef owner;
        JobRef job;
    };

    std::mutex _mutex;
    std::priority_queue<TimerItem> _items;
    std::atomic<bool> _distributing{false};
};

/// The queue this thread is currently draining, or null. Used to avoid a
/// worker recursing from one queue's handler into another queue's drain, which
/// would let two objects' jobs interleave on one stack.
extern thread_local JobQueue* tCurrentJobQueue;

/// Deadline for the current time slice. A worker that overruns it hands its
/// queue back so it can return to servicing completions.
extern thread_local uint64 tSliceEndTick;

}  // namespace nhn
