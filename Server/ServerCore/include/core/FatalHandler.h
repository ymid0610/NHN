#pragma once

#include <format>
#include <string>
#include <string_view>

#include "core/Types.h"

namespace nhn {

struct FatalOptions {
    /// Short process name, used to name the minidump file.
    std::string processName = "process";
    /// Where minidumps are written. Created if missing.
    std::string dumpDirectory = "logs";
    /// When true the process prints the reason and blocks until a developer
    /// acknowledges before exiting, as required for interactive debugging.
    /// Automatically ignored when stdin is not a console, so redirected or
    /// service-hosted runs cannot wedge forever.
    bool waitForDeveloper = true;
};

/// Converts every abnormal-termination path Windows and the CRT offer into a
/// single routine: report the reason, symbolise the stack, write a minidump,
/// wait for the developer, then exit.
///
/// Install() once at the top of main, before any threads are spawned.
class FatalHandler {
public:
    static void Install(const FatalOptions& options);

    /// Overrides the wait behaviour after installation. The test client toggles
    /// this when running non-interactive scripts.
    static void SetWaitForDeveloper(bool wait);

    /// Reports @p reason and terminates. Never returns.
    [[noreturn]] static void Fail(std::string_view reason, const char* file, int32 line);

    /// True once a fatal report is in flight. Worker loops check this so they
    /// stop producing noise while the first failing thread writes its dump.
    static bool IsFailing();
};

}  // namespace nhn

/// Unconditional abort with a formatted reason.
#define NHN_FATAL(...) \
    ::nhn::FatalHandler::Fail(::std::format(__VA_ARGS__), __FILE__, __LINE__)

/// Invariant check retained in every configuration. Used for conditions whose
/// violation means server state is already corrupt, where continuing would do
/// more damage than stopping.
#define NHN_CHECK(condition, ...)                                              \
    do {                                                                       \
        if (!(condition)) [[unlikely]] {                                       \
            ::nhn::FatalHandler::Fail(                                         \
                ::std::format("CHECK failed: {}  |  {}", #condition,           \
                              ::std::format(__VA_ARGS__)),                     \
                __FILE__, __LINE__);                                           \
        }                                                                      \
    } while (false)

#ifdef NHN_DEBUG
#define NHN_ASSERT(condition, ...) NHN_CHECK(condition, __VA_ARGS__)
#else
#define NHN_ASSERT(condition, ...) ((void)0)
#endif
