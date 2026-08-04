#include "core/Log.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <thread>

namespace nhn {
namespace {

struct LogState {
    std::mutex mutex;
    std::FILE* file = nullptr;
    std::atomic<LogLevel> level{LogLevel::Info};
    std::string name;
    bool colorEnabled = false;
};

LogState& State() {
    static LogState state;
    return state;
}

const char* LevelColor(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "\x1b[90m";
        case LogLevel::Debug: return "\x1b[36m";
        case LogLevel::Info: return "\x1b[32m";
        case LogLevel::Warn: return "\x1b[33m";
        case LogLevel::Error: return "\x1b[31m";
        case LogLevel::Fatal: return "\x1b[97m\x1b[41m";
    }
    return "";
}

/// Turns on virtual terminal processing so the ANSI colour codes above render
/// instead of being printed literally. Returns false on consoles that refuse.
bool EnableConsoleColor() {
    const HANDLE handle = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (handle == INVALID_HANDLE_VALUE || handle == nullptr) {
        return false;
    }
    DWORD mode = 0;
    if (::GetConsoleMode(handle, &mode) == FALSE) {
        return false;
    }
    return ::SetConsoleMode(handle, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING) != FALSE;
}

/// Trims the leading directories so log lines carry "Room.cpp" rather than the
/// full absolute path baked in by __FILE__.
const char* BaseName(const char* path) {
    const char* result = path;
    for (const char* cursor = path; *cursor != '\0'; ++cursor) {
        if (*cursor == '\\' || *cursor == '/') {
            result = cursor + 1;
        }
    }
    return result;
}

std::string TimeStamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t seconds = system_clock::to_time_t(now);
    std::tm parts{};
    ::localtime_s(&parts, &seconds);
    return std::format("{:02}:{:02}:{:02}.{:03}", parts.tm_hour, parts.tm_min, parts.tm_sec,
                       static_cast<int>(ms.count()));
}

}  // namespace

void Log::Init(std::string_view name, std::string_view directory, LogLevel level) {
    LogState& state = State();
    std::lock_guard<std::mutex> guard(state.mutex);

    state.name = std::string(name);
    state.level.store(level, std::memory_order_relaxed);
    state.colorEnabled = EnableConsoleColor();

    if (directory.empty()) {
        return;
    }

    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        std::fprintf(stderr, "[log] cannot create directory '%.*s': %s\n",
                     static_cast<int>(directory.size()), directory.data(), ec.message().c_str());
        return;
    }

    const auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
    ::localtime_s(&parts, &now);
    const std::string path =
        std::format("{}/{}_{:04}{:02}{:02}_{:02}{:02}{:02}.log", directory, state.name,
                    parts.tm_year + 1900, parts.tm_mon + 1, parts.tm_mday, parts.tm_hour,
                    parts.tm_min, parts.tm_sec);

    if (::fopen_s(&state.file, path.c_str(), "wb") != 0) {
        state.file = nullptr;
        std::fprintf(stderr, "[log] cannot open '%s'\n", path.c_str());
    }
}

void Log::Shutdown() {
    LogState& state = State();
    std::lock_guard<std::mutex> guard(state.mutex);
    if (state.file != nullptr) {
        std::fclose(state.file);
        state.file = nullptr;
    }
}

void Log::SetLevel(LogLevel level) {
    State().level.store(level, std::memory_order_relaxed);
}

LogLevel Log::GetLevel() {
    return State().level.load(std::memory_order_relaxed);
}

bool Log::ShouldLog(LogLevel level) {
    return level >= State().level.load(std::memory_order_relaxed);
}

const char* Log::LevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Trace: return "TRC";
        case LogLevel::Debug: return "DBG";
        case LogLevel::Info: return "INF";
        case LogLevel::Warn: return "WRN";
        case LogLevel::Error: return "ERR";
        case LogLevel::Fatal: return "FTL";
    }
    return "???";
}

void Log::Write(LogLevel level, const char* file, int32 line, std::string_view message) {
    LogState& state = State();

    const std::string plain =
        std::format("{} [{}] [{:5}] {} ({}:{})\n", TimeStamp(), LevelName(level),
                    ::GetCurrentThreadId(), message, BaseName(file), line);

    std::lock_guard<std::mutex> guard(state.mutex);

    if (state.colorEnabled) {
        std::fputs(LevelColor(level), stdout);
        std::fwrite(plain.data(), 1, plain.size(), stdout);
        std::fputs("\x1b[0m", stdout);
    } else {
        std::fwrite(plain.data(), 1, plain.size(), stdout);
    }

    if (state.file != nullptr) {
        std::fwrite(plain.data(), 1, plain.size(), state.file);
    }

    // Info and above are flushed immediately. A server's stdout is usually
    // redirected to a file, which makes it fully buffered — without this the
    // log of a running process stays empty until it exits or fills 4 KB, and
    // watching a live server is most of what these logs are for. Debug and
    // trace stay buffered because they can be high volume.
    if (level >= LogLevel::Info) {
        std::fflush(stdout);
        if (state.file != nullptr) {
            std::fflush(state.file);
        }
    }
}

void Log::Flush() {
    LogState& state = State();
    std::lock_guard<std::mutex> guard(state.mutex);
    std::fflush(stdout);
    if (state.file != nullptr) {
        std::fflush(state.file);
    }
}

}  // namespace nhn
