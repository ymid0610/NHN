#include "common/ServerApp.h"

#include <condition_variable>
#include <format>
#include <mutex>

#include "core/FatalHandler.h"
#include "core/Log.h"

namespace nhn {
namespace {

Config gConfig;
std::string gProcessName = "server";

std::mutex gShutdownMutex;
std::condition_variable gShutdownSignal;
bool gShuttingDown = false;
std::string gShutdownReason;

LogLevel ParseLogLevel(const std::string& text, LogLevel fallback) {
    if (text == "trace") return LogLevel::Trace;
    if (text == "debug") return LogLevel::Debug;
    if (text == "info") return LogLevel::Info;
    if (text == "warn") return LogLevel::Warn;
    if (text == "error") return LogLevel::Error;
    return fallback;
}

BOOL WINAPI OnConsoleControl(DWORD controlType) {
    switch (controlType) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            ServerApp::RequestShutdown("console interrupt");
            // Returning TRUE claims the event; the main thread wakes and runs
            // the orderly shutdown instead of the process being killed here.
            return TRUE;
        default:
            return FALSE;
    }
}

}  // namespace

bool ServerApp::Bootstrap(int argc, char** argv, const std::string& processName) {
    gProcessName = processName;

    // Command line first, so --config can point somewhere else; then the file;
    // then the command line again so explicit switches win over the file.
    gConfig.ApplyCommandLine(argc, argv);
    const std::string configPath = gConfig.GetString("config", processName + ".cfg");
    if (!gConfig.LoadFile(configPath)) {
        return false;
    }
    gConfig.ApplyCommandLine(argc, argv);

    Core::Options options;
    options.processName = processName;
    options.logDirectory = gConfig.GetString("log-dir", "logs");
    options.logLevel = ParseLogLevel(gConfig.GetString("log-level", "info"), LogLevel::Info);
    options.waitForDeveloperOnFatal = !gConfig.GetBool("no-wait", false);
    options.workerThreadCount = gConfig.GetInt("workers", 0);

    Core::Init(options);

    ::SetConsoleCtrlHandler(OnConsoleControl, TRUE);

    LOG_INFO("=== {} starting ===", processName);
    const std::string dump = gConfig.Dump();
    if (!dump.empty()) {
        LOG_INFO("configuration:\n{}", dump);
    }
    if (!options.waitForDeveloperOnFatal) {
        LOG_WARN("--no-wait is set: a fatal error will exit without acknowledgement");
    }

    return true;
}

Config& ServerApp::GetConfig() {
    return gConfig;
}

const std::string& ServerApp::GetProcessName() {
    return gProcessName;
}

void ServerApp::RequestShutdown(const std::string& reason) {
    {
        std::lock_guard<std::mutex> guard(gShutdownMutex);
        if (gShuttingDown) {
            return;
        }
        gShuttingDown = true;
        gShutdownReason = reason;
    }
    gShutdownSignal.notify_all();
}

bool ServerApp::IsShuttingDown() {
    std::lock_guard<std::mutex> guard(gShutdownMutex);
    return gShuttingDown;
}

void ServerApp::WaitForShutdown() {
    LOG_INFO("{} is running; press Ctrl+C to stop", gProcessName);

    std::unique_lock<std::mutex> lock(gShutdownMutex);
    gShutdownSignal.wait(lock, [] { return gShuttingDown; });
    const std::string reason = gShutdownReason;
    lock.unlock();

    LOG_INFO("shutting down: {}", reason);
}

void ServerApp::Shutdown() {
    Core::Shutdown();
}

}  // namespace nhn
