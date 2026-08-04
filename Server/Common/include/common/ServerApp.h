#pragma once

#include <string>

#include "core/Config.h"
#include "core/CoreGlobal.h"

namespace nhn {

/// Shared entry-point plumbing for all five executables.
///
/// Standard switches, honoured by every process:
///   --config=<path>   settings file (defaults to <name>.cfg beside the exe)
///   --log-level=      trace|debug|info|warn|error
///   --log-dir=        default "logs"
///   --workers=        IOCP worker threads, 0 = one per hardware thread
///   --no-wait         do not block for a developer on a fatal error
class ServerApp {
public:
    /// Reads configuration, boots the core and prints the startup banner.
    /// @return false if configuration was invalid, in which case the caller
    ///         should exit without starting services.
    static bool Bootstrap(int argc, char** argv, const std::string& processName);

    static Config& GetConfig();
    static const std::string& GetProcessName();

    /// Blocks until Ctrl+C, console close, or RequestShutdown.
    static void WaitForShutdown();

    static void RequestShutdown(const std::string& reason);
    static bool IsShuttingDown();

    /// Tears the core down. Safe to call once; further calls do nothing.
    static void Shutdown();
};

}  // namespace nhn
