#pragma once

#include "core/Types.h"

namespace nhn {

/// Owns the IOCP worker pool.
///
/// Every worker runs the same body: service one completion, release any timers
/// that came due, then drain queues parked on the global queue. All three are
/// bounded by a time slice so no single kind of work can starve the others.
class ThreadManager {
public:
    static void Start(int32 workerCount);

    /// Signals workers to exit and wakes any blocked in the completion port.
    static void RequestStop();
    static void Join();

    static bool StopRequested();
    static int32 WorkerCount();

    /// One iteration of the worker body. Exposed so a main thread that wants to
    /// participate (the test client) can pump without spawning a worker.
    static void PumpOnce(uint32 dispatchTimeoutMs);

    /// Per-thread setup for threads created outside Start() that will touch
    /// job queues or send buffers.
    static void InitThread();
};

}  // namespace nhn
