#include <windows.h>
#include <DbgHelp.h>
#include <ShlObj.h>
#include <filesystem>
#include <spdlog/spdlog.h>

#include <utility/Module.hpp>
#include <utility/Scan.hpp>
#include <utility/Patch.hpp>

#include "Framework.hpp"

#include "ExceptionHandler.hpp"

namespace {
// MiniDumpNormal plus thread and unloaded-module information, without full memory.
bool write_crash_dump(struct _EXCEPTION_POINTERS* ei, bool log) {
    const auto dbghelp = LoadLibrary("dbghelp.dll");

    if (!dbghelp) {
        if (log) spdlog::error("Exception occurred, but could not load dbghelp.dll");
        return false;
    }

    const auto final_path = Framework::get_persistent_dir("crash.dmp").string();

    if (log) spdlog::error("Attempting to write dump to {}", final_path);

    auto f = CreateFile(final_path.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);

    if (!f || f == INVALID_HANDLE_VALUE) {
        if (log) spdlog::error("Exception occurred, but could not create dump file");
        return false;
    }

    MINIDUMP_EXCEPTION_INFORMATION ei_info{GetCurrentThreadId(), ei, FALSE};
    auto minidump_write_dump = (decltype(MiniDumpWriteDump)*)GetProcAddress(dbghelp, "MiniDumpWriteDump");
    const auto type = (MINIDUMP_TYPE)(MiniDumpNormal | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);

    if (minidump_write_dump != nullptr) {
        minidump_write_dump(GetCurrentProcess(), GetCurrentProcessId(), f, type, &ei_info, nullptr, nullptr);
    }

    CloseHandle(f);
    return true;
}
}

LONG WINAPI framework::global_exception_handler(struct _EXCEPTION_POINTERS* ei) {
    if (g_logging_unsafe.load()) {
        // Shutdown: spdlog may be gone (its use here caused secondary crashes at exit).
        write_crash_dump(ei, false);
        return EXCEPTION_EXECUTE_HANDLER;
    }

    spdlog::flush_on(spdlog::level::err);

    spdlog::error("Exception occurred: {:x}", ei->ExceptionRecord->ExceptionCode);
    spdlog::error("RIP: {:x}", ei->ContextRecord->Rip);
    spdlog::error("RSP: {:x}", ei->ContextRecord->Rsp);
    spdlog::error("RCX: {:x}", ei->ContextRecord->Rcx);
    spdlog::error("RDX: {:x}", ei->ContextRecord->Rdx);
    spdlog::error("R8: {:x}", ei->ContextRecord->R8);
    spdlog::error("R9: {:x}", ei->ContextRecord->R9);
    spdlog::error("R10: {:x}", ei->ContextRecord->R10);
    spdlog::error("R11: {:x}", ei->ContextRecord->R11);
    spdlog::error("R12: {:x}", ei->ContextRecord->R12);
    spdlog::error("R13: {:x}", ei->ContextRecord->R13);
    spdlog::error("R14: {:x}", ei->ContextRecord->R14);
    spdlog::error("R15: {:x}", ei->ContextRecord->R15);
    spdlog::error("RAX: {:x}", ei->ContextRecord->Rax);
    spdlog::error("RBX: {:x}", ei->ContextRecord->Rbx);
    spdlog::error("RBP: {:x}", ei->ContextRecord->Rbp);
    spdlog::error("RSI: {:x}", ei->ContextRecord->Rsi);
    spdlog::error("RDI: {:x}", ei->ContextRecord->Rdi);
    spdlog::error("EFLAGS: {:x}", ei->ContextRecord->EFlags);
    spdlog::error("CS: {:x}", ei->ContextRecord->SegCs);
    spdlog::error("DS: {:x}", ei->ContextRecord->SegDs);
    spdlog::error("ES: {:x}", ei->ContextRecord->SegEs);
    spdlog::error("FS: {:x}", ei->ContextRecord->SegFs);
    spdlog::error("GS: {:x}", ei->ContextRecord->SegGs);
    spdlog::error("SS: {:x}", ei->ContextRecord->SegSs);

    const auto module_within = utility::get_module_within(ei->ContextRecord->Rip);

    if (module_within) {
        const auto module_path = utility::get_module_path(*module_within);

        if (module_path) {
            spdlog::error("Module: {:x} {}", (uintptr_t)*module_within, *module_path);
        } else {
            spdlog::error("Module: Unknown");
        }
    } else {
        spdlog::error("Module: Unknown");
    }

    if (!write_crash_dump(ei, true)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }

    return EXCEPTION_EXECUTE_HANDLER;
}

void framework::setup_exception_handler() {
    SetUnhandledExceptionFilter(global_exception_handler);
}
