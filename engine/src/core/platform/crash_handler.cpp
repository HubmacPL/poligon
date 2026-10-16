#include "poligon/core/crash_handler.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <dbghelp.h>

#include <crtdbg.h>
#include <csignal>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "crash_report.h"

namespace poligon {

namespace {

constexpr int kCrashExitCode = 4;
constexpr int kMaxFrames = 48;

// The report is built in a fixed buffer: after a crash the heap may be unusable.
char g_report[16384];
size_t g_report_length = 0;
char g_report_path[MAX_PATH] = "";
bool g_installed = false;
volatile LONG g_reporting = 0;

void Append(const char* format, ...) {
    if (g_report_length >= sizeof(g_report) - 1) {
        return;
    }
    va_list arguments;
    va_start(arguments, format);
    const int written =
        std::vsnprintf(g_report + g_report_length, sizeof(g_report) - g_report_length, format, arguments);
    va_end(arguments);
    if (written > 0) {
        g_report_length += static_cast<size_t>(written);
        if (g_report_length > sizeof(g_report) - 1) {
            g_report_length = sizeof(g_report) - 1;
        }
    }
}

void AppendProgramName() {
    char path[MAX_PATH];
    const DWORD length = GetModuleFileNameA(nullptr, path, sizeof(path));
    if (length > 0 && length < sizeof(path)) {
        const char* name = std::strrchr(path, '\\');
        Append("Program: %s\n", name != nullptr ? name + 1 : path);
    }
}

bool InitializeSymbols() {
    static bool initialized = false;
    static bool available = false;
    if (!initialized) {
        initialized = true;
        SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
        available = SymInitialize(GetCurrentProcess(), nullptr, TRUE) != FALSE;
    }
    return available;
}

void AppendFrame(int index, DWORD64 address) {
    const HANDLE process = GetCurrentProcess();

    char symbol_storage[sizeof(SYMBOL_INFO) + 512];
    SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbol_storage);
    std::memset(symbol_storage, 0, sizeof(symbol_storage));
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 511;

    DWORD64 symbol_offset = 0;
    if (SymFromAddr(process, address, &symbol_offset, symbol)) {
        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(line);
        DWORD line_offset = 0;
        if (SymGetLineFromAddr64(process, address, &line_offset, &line)) {
            Append("  #%d %s (%s:%lu)\n", index, symbol->Name, line.FileName, line.LineNumber);
        } else {
            Append("  #%d %s\n", index, symbol->Name);
        }
        return;
    }

    // No symbol: at least say which module the address is in.
    IMAGEHLP_MODULE64 module{};
    module.SizeOfStruct = sizeof(module);
    if (SymGetModuleInfo64(process, address, &module)) {
        Append("  #%d %s+0x%llx\n", index, module.ModuleName,
               static_cast<unsigned long long>(address - module.BaseOfImage));
    } else {
        Append("  #%d 0x%016llx\n", index, static_cast<unsigned long long>(address));
    }
}

// Stack of the code that is running right now.
void AppendCurrentStack(int frames_to_skip) {
    Append("Stack:\n");
    if (!InitializeSymbols()) {
        Append("  (symbols unavailable)\n");
    }
    void* frames[kMaxFrames];
    const USHORT count = CaptureStackBackTrace(static_cast<DWORD>(frames_to_skip + 1), kMaxFrames, frames, nullptr);
    for (USHORT i = 0; i < count; ++i) {
        // A return address points after the call; step back one byte to land inside the calling line.
        AppendFrame(i, reinterpret_cast<DWORD64>(frames[i]) - 1);
    }
}

// Stack at the moment a CPU exception happened, reconstructed from the saved registers.
void AppendExceptionStack(const CONTEXT& exception_context) {
    Append("Stack:\n");
    if (!InitializeSymbols()) {
        Append("  (symbols unavailable)\n");
    }
    CONTEXT context = exception_context;
    STACKFRAME64 frame{};
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;

    for (int index = 0; index < kMaxFrames; ++index) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, GetCurrentProcess(), GetCurrentThread(), &frame, &context, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
            frame.AddrPC.Offset == 0) {
            break;
        }
        // Frame 0 is the faulting instruction itself; the others are return addresses.
        AppendFrame(index, index == 0 ? frame.AddrPC.Offset : frame.AddrPC.Offset - 1);
    }
}

[[noreturn]] void WriteReportAndExit(int exit_code) {
    if (g_installed && g_report_path[0] != '\0') {
        const HANDLE file = CreateFileA(g_report_path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                                        nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(file, g_report, static_cast<DWORD>(g_report_length), &written, nullptr);
            CloseHandle(file);
            Append("Report saved to: %s\n", g_report_path);
        } else {
            Append("Report could not be saved to: %s\n", g_report_path);
        }
    }

    std::fflush(stdout);
    std::fputs(g_report, stderr);
    std::fflush(stderr);
    TerminateProcess(GetCurrentProcess(), static_cast<UINT>(exit_code));
    for (;;) {
    }
}

// Only the first failure is reported; a failure while reporting ends the process at once.
void BeginReport() {
    if (InterlockedExchange(&g_reporting, 1) != 0) {
        TerminateProcess(GetCurrentProcess(), static_cast<UINT>(kCrashExitCode));
    }
    g_report_length = 0;
    g_report[0] = '\0';
}

const char* DescribeException(DWORD code) {
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION:
        return "access violation";
    case EXCEPTION_STACK_OVERFLOW:
        return "stack overflow";
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        return "integer division by zero";
    case EXCEPTION_ILLEGAL_INSTRUCTION:
        return "illegal instruction";
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        return "array bounds exceeded";
    case EXCEPTION_IN_PAGE_ERROR:
        return "page could not be loaded";
    case EXCEPTION_BREAKPOINT:
        return "breakpoint";
    default:
        return "CPU exception";
    }
}

// Windows exception code the Microsoft C++ runtime uses for `throw`.
constexpr DWORD kCppExceptionCode = 0xE06D7363;

// Finds the std::exception inside a thrown C++ object and copies its what() text.
// This reads the description of the thrown type that the Microsoft compiler attaches to
// every `throw` (the layout is stable and documented by its use in debuggers, but it is
// compiler-specific). Returns false for anything not derived from std::exception.
bool ReadCppExceptionMessage(const EXCEPTION_RECORD& record, char* message, size_t message_size) {
    if (record.NumberParameters < 4) {
        return false;
    }
    struct CatchableType {
        DWORD properties;
        DWORD type_descriptor_offset;
        int member_displacement;
        int vbtable_displacement;
        int displacement_in_vbtable;
        DWORD size;
        DWORD copy_function_offset;
    };
    __try {
        const char* thrown_object = reinterpret_cast<const char*>(record.ExceptionInformation[1]);
        const char* throw_info = reinterpret_cast<const char*>(record.ExceptionInformation[2]);
        const char* image_base = reinterpret_cast<const char*>(record.ExceptionInformation[3]);
        if (thrown_object == nullptr || throw_info == nullptr || image_base == nullptr) {
            return false;
        }
        // ThrowInfo: attributes, destructor, forward-compat handler, catchable type array (all 32-bit).
        const DWORD array_offset = *reinterpret_cast<const DWORD*>(throw_info + 12);
        const char* type_array = image_base + array_offset;
        const int type_count = *reinterpret_cast<const int*>(type_array);
        for (int i = 0; i < type_count && i < 32; ++i) {
            const DWORD type_offset = *reinterpret_cast<const DWORD*>(type_array + 4 + i * 4);
            const CatchableType* type = reinterpret_cast<const CatchableType*>(image_base + type_offset);
            // TypeDescriptor: two pointers, then the decorated type name.
            const char* type_name = image_base + type->type_descriptor_offset + 2 * sizeof(void*);
            if (std::strcmp(type_name, ".?AVexception@std@@") == 0 && type->vbtable_displacement == -1) {
                const std::exception* exception =
                    reinterpret_cast<const std::exception*>(thrown_object + type->member_displacement);
                strncpy_s(message, message_size, exception->what(), _TRUNCATE);
                return true;
            }
        }
    } __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION ? EXCEPTION_EXECUTE_HANDLER
                                                                 : EXCEPTION_CONTINUE_SEARCH) {
        // The description was not laid out as expected; report the exception without its message.
        message[0] = '\0';
    }
    return false;
}

LONG WINAPI HandleException(EXCEPTION_POINTERS* info) {
    BeginReport();
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    char exception_message[400];
    if (record.ExceptionCode == kCppExceptionCode) {
        // The stack below starts at the `throw` statement.
        if (ReadCppExceptionMessage(record, exception_message, sizeof(exception_message))) {
            Append("CRASH: uncaught C++ exception: %s\n", exception_message);
        } else {
            Append("CRASH: uncaught C++ exception (not derived from std::exception)\n");
        }
    } else if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
        const char* operation = record.ExceptionInformation[0] == 0   ? "reading"
                                : record.ExceptionInformation[0] == 1 ? "writing"
                                                                      : "executing";
        Append("CRASH: access violation %s address 0x%016llx\n", operation,
               static_cast<unsigned long long>(record.ExceptionInformation[1]));
    } else {
        Append("CRASH: %s (code 0x%08lx)\n", DescribeException(record.ExceptionCode), record.ExceptionCode);
    }
    AppendProgramName();
    AppendExceptionStack(*info->ContextRecord);
    WriteReportAndExit(kCrashExitCode);
}

void HandleAbort(int) {
    ReportAndExit("CRASH: abort() was called", kCrashExitCode, 1);
}

void HandleTerminate() {
    // std::terminate is most often reached through an exception nobody caught; say which one.
    char headline[512] = "CRASH: std::terminate was called";
    if (const std::exception_ptr current = std::current_exception()) {
        try {
            std::rethrow_exception(current);
        } catch (const std::exception& exception) {
            std::snprintf(headline, sizeof(headline), "CRASH: uncaught C++ exception: %s", exception.what());
        } catch (...) {
            std::snprintf(headline, sizeof(headline), "CRASH: uncaught C++ exception of an unknown type");
        }
    }
    ReportAndExit(headline, kCrashExitCode, 1);
}

void HandlePureCall() {
    ReportAndExit("CRASH: pure virtual function call", kCrashExitCode, 1);
}

void HandleInvalidParameter(const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) {
    ReportAndExit("CRASH: invalid argument passed to a C runtime function", kCrashExitCode, 1);
}

#ifdef _DEBUG
// The debug C and C++ runtime report failed checks (for example a vector index out of range)
// through this hook, then end the process in a way no other handler sees. Report here instead.
int HandleDebugRuntimeReport(int report_type, char* message, int* /*return_value*/) {
    if (report_type != _CRT_ASSERT && report_type != _CRT_ERROR) {
        return FALSE;  // let ordinary debug output through
    }
    char headline[700];
    std::snprintf(headline, sizeof(headline), "CRASH: debug runtime check failed: %s", message != nullptr ? message : "");
    // Messages end with a line break; the report adds its own.
    const size_t length = std::strlen(headline);
    if (length > 0 && headline[length - 1] == '\n') {
        headline[length - 1] = '\0';
    }
    ReportAndExit(headline, kCrashExitCode, 1);
}
#endif

} // namespace

void ReportAndExit(const char* headline, int exit_code, int frames_to_skip) {
    BeginReport();
    Append("%s\n", headline);
    AppendProgramName();
    AppendCurrentStack(frames_to_skip + 1);
    WriteReportAndExit(exit_code);
}

void InstallCrashHandler(const CrashHandlerConfig& config) {
    if (config.report_path != nullptr) {
        strncpy_s(g_report_path, sizeof(g_report_path), config.report_path, _TRUNCATE);
    } else {
        // "<program>.exe" -> "<program>_crash.txt", next to the executable.
        const DWORD length = GetModuleFileNameA(nullptr, g_report_path, sizeof(g_report_path));
        char* extension = length > 0 && length < sizeof(g_report_path) ? std::strrchr(g_report_path, '.') : nullptr;
        if (extension != nullptr && static_cast<size_t>(extension - g_report_path) + 11 < sizeof(g_report_path)) {
            strcpy_s(extension, sizeof(g_report_path) - static_cast<size_t>(extension - g_report_path), "_crash.txt");
        } else {
            g_report_path[0] = '\0';
        }
    }

    // No Windows error dialogs.
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    // No "abort() has been called" dialog; abort goes to HandleAbort.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    // Debug C runtime: write assertion and error messages to stderr instead of showing a dialog.
    // These calls compile to nothing in release builds.
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
#ifdef _DEBUG
    _CrtSetReportHook2(_CRT_RPTHOOK_INSTALL, HandleDebugRuntimeReport);
#endif

    // Keep some stack in reserve so that a stack overflow can still be reported.
    ULONG reserved_stack_bytes = 256 * 1024;
    SetThreadStackGuarantee(&reserved_stack_bytes);

    SetUnhandledExceptionFilter(HandleException);
    std::signal(SIGABRT, HandleAbort);
    std::set_terminate(HandleTerminate);
    _set_purecall_handler(HandlePureCall);
    _set_invalid_parameter_handler(HandleInvalidParameter);

    g_installed = true;
}

} // namespace poligon
