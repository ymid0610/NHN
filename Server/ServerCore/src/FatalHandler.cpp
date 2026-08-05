#include "core/FatalHandler.h"

#include <DbgHelp.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <exception>
#include <filesystem>
#include <mutex>

#include "core/Log.h"

#pragma comment(lib, "dbghelp.lib")

namespace nhn {
namespace {

FatalOptions gOptions;
std::atomic<bool> gFailing{false};
std::atomic<bool> gSymbolsReady{false};
std::mutex gDbgHelpMutex;  // DbgHelp is single-threaded by contract.

std::string Narrow(const wchar_t* text) {
    if (text == nullptr) {
        return {};
    }
    const int length = ::WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
        return {};
    }
    std::string result(static_cast<size_t>(length - 1), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), length, nullptr, nullptr);
    return result;
}

void EnsureSymbols() {
    if (gSymbolsReady.load(std::memory_order_acquire)) {
        return;
    }
    ::SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_LOAD_LINES);
    if (::SymInitialize(::GetCurrentProcess(), nullptr, TRUE) != FALSE) {
        gSymbolsReady.store(true, std::memory_order_release);
    }
}

/// Resolves one instruction address to "module!function file:line".
std::string DescribeFrame(DWORD64 address) {
    const HANDLE process = ::GetCurrentProcess();

    alignas(SYMBOL_INFO) char symbolStorage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME] = {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;

    std::string name = "<unknown>";
    DWORD64 displacement = 0;
    if (::SymFromAddr(process, address, &displacement, symbol) != FALSE) {
        name = symbol->Name;
    }

    std::string location;
    IMAGEHLP_LINE64 lineInfo{};
    lineInfo.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD lineDisplacement = 0;
    if (::SymGetLineFromAddr64(process, address, &lineDisplacement, &lineInfo) != FALSE) {
        const char* fileName = lineInfo.FileName;
        for (const char* cursor = lineInfo.FileName; cursor != nullptr && *cursor != '\0';
             ++cursor) {
            if (*cursor == '\\' || *cursor == '/') {
                fileName = cursor + 1;
            }
        }
        location = std::format("  {}:{}", fileName, lineInfo.LineNumber);
    }

    return std::format("0x{:016X}  {}+0x{:X}{}", address, name, displacement, location);
}

/// Walks either the supplied exception context or the caller's own stack.
std::string CaptureStack(const CONTEXT* sourceContext) {
    std::lock_guard<std::mutex> guard(gDbgHelpMutex);
    EnsureSymbols();

    CONTEXT context{};
    if (sourceContext != nullptr) {
        context = *sourceContext;
    } else {
        ::RtlCaptureContext(&context);
    }

    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    const HANDLE process = ::GetCurrentProcess();
    const HANDLE thread = ::GetCurrentThread();

    std::string result;
    for (int depth = 0; depth < 62; ++depth) {
        if (::StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context, nullptr,
                          ::SymFunctionTableAccess64, ::SymGetModuleBase64, nullptr) == FALSE) {
            break;
        }
        if (frame.AddrPC.Offset == 0) {
            break;
        }
        result += std::format("  #{:<2} {}\n", depth, DescribeFrame(frame.AddrPC.Offset));
    }

    if (result.empty()) {
        result = "  <stack unavailable>\n";
    }
    return result;
}

std::string WriteMiniDump(EXCEPTION_POINTERS* exceptionPointers) {
    std::error_code ec;
    std::filesystem::create_directories(gOptions.dumpDirectory, ec);

    SYSTEMTIME now{};
    ::GetLocalTime(&now);
    const std::string path =
        std::format("{}/{}_{:04}{:02}{:02}_{:02}{:02}{:02}_{}.dmp", gOptions.dumpDirectory,
                    gOptions.processName, now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute,
                    now.wSecond, ::GetCurrentProcessId());

    const HANDLE file = ::CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                      FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }

    MINIDUMP_EXCEPTION_INFORMATION info{};
    info.ThreadId = ::GetCurrentThreadId();
    info.ExceptionPointers = exceptionPointers;
    info.ClientPointers = FALSE;

    const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory |
                                                 MiniDumpWithDataSegs | MiniDumpWithHandleData |
                                                 MiniDumpWithThreadInfo);

    const BOOL written =
        ::MiniDumpWriteDump(::GetCurrentProcess(), ::GetCurrentProcessId(), file, type,
                            exceptionPointers != nullptr ? &info : nullptr, nullptr, nullptr);
    ::CloseHandle(file);

    return written != FALSE ? path : std::string{};
}

bool StdinIsConsole() {
    const HANDLE handle = ::GetStdHandle(STD_INPUT_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD mode = 0;
    // Only a real console has a console mode; pipes and files do not.
    return ::GetConsoleMode(handle, &mode) != FALSE;
}

void WaitForDeveloper() {
    if (!gOptions.waitForDeveloper) {
        std::fputs("\n[fatal] --no-wait set; exiting without acknowledgement.\n", stderr);
        return;
    }
    if (!StdinIsConsole()) {
        std::fputs("\n[fatal] stdin is not a console; exiting without acknowledgement.\n", stderr);
        return;
    }

    std::fputs("\n=== Press ENTER to shut down. ===\n", stderr);
    std::fflush(stderr);

    const HANDLE handle = ::GetStdHandle(STD_INPUT_HANDLE);
    ::FlushConsoleInputBuffer(handle);
    for (;;) {
        INPUT_RECORD record{};
        DWORD read = 0;
        if (::ReadConsoleInputA(handle, &record, 1, &read) == FALSE || read == 0) {
            return;  // Console went away; do not spin.
        }
        if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown != FALSE &&
            record.Event.KeyEvent.wVirtualKeyCode == VK_RETURN) {
            return;
        }
    }
}

/// The single exit path shared by every trap below.
[[noreturn]] void Report(std::string_view reason, std::string_view origin, const char* file,
                         int32 line, EXCEPTION_POINTERS* exceptionPointers) {
    // Only the first thread to fail produces a report. Any other thread that
    // trips afterwards parks here rather than interleaving output or racing
    // DbgHelp.
    if (gFailing.exchange(true, std::memory_order_acq_rel)) {
        ::Sleep(INFINITE);
        ::TerminateProcess(::GetCurrentProcess(), 3);
    }

    const CONTEXT* context =
        exceptionPointers != nullptr ? exceptionPointers->ContextRecord : nullptr;
    const std::string stack = CaptureStack(context);
    const std::string dumpPath = WriteMiniDump(exceptionPointers);

    std::string report = "\n";
    report += "================ FATAL ================\n";
    report += std::format("process : {} (pid {})\n", gOptions.processName, ::GetCurrentProcessId());
    report += std::format("thread  : {}\n", ::GetCurrentThreadId());
    report += std::format("origin  : {}\n", origin);
    report += std::format("reason  : {}\n", reason);
    if (file != nullptr) {
        report += std::format("at      : {}:{}\n", file, line);
    }
    report += std::format("dump    : {}\n", dumpPath.empty() ? "<not written>" : dumpPath);
    report += "stack   :\n";
    report += stack;
    report += "=======================================\n";

    // Written straight to the handles as well as the log, because the reason a
    // process died is the one line that must never be lost to buffering.
    std::fwrite(report.data(), 1, report.size(), stderr);
    std::fflush(stderr);
    Log::Write(LogLevel::Fatal, file != nullptr ? file : "FatalHandler.cpp", line, report);
    Log::Flush();

    WaitForDeveloper();

    Log::Shutdown();
    // TerminateProcess, not exit(): static destructors and atexit handlers run
    // against state we already know is broken.
    ::TerminateProcess(::GetCurrentProcess(), 1);
    ::Sleep(INFINITE);  // Unreachable; satisfies [[noreturn]].
}

const char* ExceptionName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION: return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT: return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO: return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_ILLEGAL_INSTRUCTION: return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO: return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_PRIV_INSTRUCTION: return "PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW: return "STACK_OVERFLOW";
        case EXCEPTION_IN_PAGE_ERROR: return "IN_PAGE_ERROR";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
        default: return "UNKNOWN";
    }
}

LONG WINAPI OnUnhandledException(EXCEPTION_POINTERS* pointers) {
    const DWORD code = pointers->ExceptionRecord->ExceptionCode;
    std::string reason = std::format("{} (0x{:08X}) at 0x{:016X}", ExceptionName(code), code,
                                     reinterpret_cast<DWORD64>(pointers->ExceptionRecord->ExceptionAddress));

    // Access violations carry the faulting address and direction, which is
    // usually enough to identify the bug without opening the dump.
    if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) &&
        pointers->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR operation = pointers->ExceptionRecord->ExceptionInformation[0];
        const ULONG_PTR address = pointers->ExceptionRecord->ExceptionInformation[1];
        const char* verb = operation == 0 ? "reading" : (operation == 1 ? "writing" : "executing");
        reason += std::format(" while {} 0x{:016X}", verb, address);
    }

    Report(reason, "SetUnhandledExceptionFilter", nullptr, 0, pointers);
}

void OnTerminate() {
    std::string reason = "std::terminate called";
    if (auto current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& e) {
            reason = std::format("unhandled std::exception: {}", e.what());
        } catch (...) {
            reason = "unhandled exception of unknown type";
        }
    }
    Report(reason, "std::set_terminate", nullptr, 0, nullptr);
}

void OnPureCall() {
    Report("pure virtual function called", "_set_purecall_handler", nullptr, 0, nullptr);
}

void OnInvalidParameter(const wchar_t* expression, const wchar_t* function, const wchar_t* file,
                        unsigned int line, uintptr_t /*reserved*/) {
    const std::string reason =
        std::format("invalid CRT parameter: expression='{}' function='{}'",
                    Narrow(expression), Narrow(function));
    const std::string sourceFile = Narrow(file);
    Report(reason, "_set_invalid_parameter_handler",
           sourceFile.empty() ? nullptr : sourceFile.c_str(), static_cast<int32>(line), nullptr);
}

void OnAbortSignal(int /*signal*/) {
    Report("abort() called", "SIGABRT", nullptr, 0, nullptr);
}

int OnCrtReport(int reportType, char* message, int* returnValue) {
    if (reportType == _CRT_ASSERT || reportType == _CRT_ERROR) {
        Report(message != nullptr ? message : "CRT assertion", "_CrtSetReportHook", nullptr, 0,
               nullptr);
    }
    if (returnValue != nullptr) {
        *returnValue = 0;
    }
    return TRUE;
}

}  // namespace

void FatalHandler::Install(const FatalOptions& options) {
    gOptions = options;

    // Suppress the Windows Error Reporting dialog; this handler owns the
    // reporting, and a modal dialog on a headless box hangs the process.
    ::SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);

    ::SetUnhandledExceptionFilter(OnUnhandledException);
    std::set_terminate(OnTerminate);
    ::_set_purecall_handler(OnPureCall);
    ::_set_invalid_parameter_handler(OnInvalidParameter);
    ::signal(SIGABRT, OnAbortSignal);
    ::_set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#ifdef NHN_DEBUG
    ::_CrtSetReportHook(OnCrtReport);
#else
    (void)&OnCrtReport;
#endif
}

void FatalHandler::SetWaitForDeveloper(bool wait) {
    gOptions.waitForDeveloper = wait;
}

bool FatalHandler::IsFailing() {
    return gFailing.load(std::memory_order_acquire);
}

void FatalHandler::Fail(std::string_view reason, const char* file, int32 line) {
    Report(reason, "explicit", file, line, nullptr);
}

}  // namespace nhn
