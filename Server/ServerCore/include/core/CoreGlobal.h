#pragma once

#include <string>

#include "core/Log.h"
#include "core/Types.h"

namespace nhn {

class IocpCore;
class GlobalQueue;
class JobTimer;

/// The completion port shared by every service in the process.
extern Ref<IocpCore> GIocpCore;
extern GlobalQueue* GGlobalQueue;
extern JobTimer* GJobTimer;

/// Single bootstrap for all five executables: logging, the fatal handler,
/// winsock, the completion port, the scheduler globals and the worker pool.
class Core {
public:
    struct Options {
        std::string processName = "server";
        std::string logDirectory = "logs";
        LogLevel logLevel = LogLevel::Info;
        /// Mirrors the --no-wait switch; see FatalOptions::waitForDeveloper.
        bool waitForDeveloperOnFatal = true;
        /// 0 selects one worker per hardware thread.
        int32 workerThreadCount = 0;
    };

    static void Init(const Options& options);

    /// Stops the worker pool, waits for it, then releases winsock and the
    /// buffer pools. Safe to call once; a second call is a no-op.
    static void Shutdown();
};

}  // namespace nhn
