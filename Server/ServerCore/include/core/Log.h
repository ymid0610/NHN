#pragma once

#include <format>
#include <string>
#include <string_view>

#include "core/Types.h"

namespace nhn {

enum class LogLevel : uint8 {
    Trace = 0,
    Debug,
    Info,
    Warn,
    Error,
    Fatal,
};

/// Process-wide logger writing to the console and, once a directory is given,
/// to a rolling-per-run file. Safe to call from any thread; writes are
/// serialised by a mutex, which is fine because logging is not on the packet
/// hot path.
class Log {
public:
    /// @param name       short process name used in the filename and prefix
    /// @param directory  log directory, created if missing; empty disables file output
    static void Init(std::string_view name, std::string_view directory, LogLevel level);
    static void Shutdown();

    static void SetLevel(LogLevel level);
    static LogLevel GetLevel();
    static bool ShouldLog(LogLevel level);

    /// Writes one already-formatted line. Prefer the LOG_* macros.
    static void Write(LogLevel level, const char* file, int32 line, std::string_view message);

    /// Flushes the file sink. Called on the fatal path, where the process is
    /// about to die and buffered lines would otherwise be lost.
    static void Flush();

    static const char* LevelName(LogLevel level);
};

}  // namespace nhn

// std::format is evaluated at the call site so the format string stays a
// compile-time constant and argument errors surface as compile errors.
#define NHN_LOG(level, ...)                                                     \
    do {                                                                        \
        if (::nhn::Log::ShouldLog(level)) {                                     \
            ::nhn::Log::Write((level), __FILE__, __LINE__,                      \
                              ::std::format(__VA_ARGS__));                      \
        }                                                                       \
    } while (false)

#define LOG_TRACE(...) NHN_LOG(::nhn::LogLevel::Trace, __VA_ARGS__)
#define LOG_DEBUG(...) NHN_LOG(::nhn::LogLevel::Debug, __VA_ARGS__)
#define LOG_INFO(...) NHN_LOG(::nhn::LogLevel::Info, __VA_ARGS__)
#define LOG_WARN(...) NHN_LOG(::nhn::LogLevel::Warn, __VA_ARGS__)
#define LOG_ERROR(...) NHN_LOG(::nhn::LogLevel::Error, __VA_ARGS__)
