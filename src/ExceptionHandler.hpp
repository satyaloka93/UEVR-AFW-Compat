#include <windows.h>
#include <atomic>

namespace framework {
// Set when UEVR starts tearing down; logging (spdlog) may already be destroyed, so the
// crash handler must only write the dump from then on.
inline std::atomic<bool> g_logging_unsafe{false};

LONG WINAPI global_exception_handler(struct _EXCEPTION_POINTERS* ei);
void setup_exception_handler();
}
