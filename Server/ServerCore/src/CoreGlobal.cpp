#include "core/CoreGlobal.h"

#include <atomic>

#include "core/Buffer.h"
#include "core/FatalHandler.h"
#include "core/IocpCore.h"
#include "core/JobQueue.h"
#include "core/Net.h"
#include "core/ThreadManager.h"

namespace nhn {

Ref<IocpCore> GIocpCore;
GlobalQueue* GGlobalQueue = nullptr;
JobTimer* GJobTimer = nullptr;

namespace {
std::atomic<bool> gInitialised{false};
}

void Core::Init(const Options& options) {
    if (gInitialised.exchange(true)) {
        return;
    }

    Log::Init(options.processName, options.logDirectory, options.logLevel);

    FatalOptions fatalOptions;
    fatalOptions.processName = options.processName;
    fatalOptions.dumpDirectory = options.logDirectory;
    fatalOptions.waitForDeveloper = options.waitForDeveloperOnFatal;
    FatalHandler::Install(fatalOptions);

    NHN_CHECK(SocketUtils::Startup(), "winsock initialisation failed");

    GIocpCore = std::make_shared<IocpCore>();
    GGlobalQueue = new GlobalQueue();
    GJobTimer = new JobTimer();

    ThreadManager::Start(options.workerThreadCount);
}

void Core::Shutdown() {
    if (!gInitialised.exchange(false)) {
        return;
    }

    // Order matters: stop the workers before tearing down anything they touch.
    ThreadManager::RequestStop();
    ThreadManager::Join();

    if (GJobTimer != nullptr) {
        GJobTimer->Clear();
        delete GJobTimer;
        GJobTimer = nullptr;
    }
    if (GGlobalQueue != nullptr) {
        GGlobalQueue->Clear();
        delete GGlobalQueue;
        GGlobalQueue = nullptr;
    }

    GIocpCore.reset();

    SendBufferManager::Shutdown();
    SocketUtils::Cleanup();

    Log::Flush();
    Log::Shutdown();
}

}  // namespace nhn
