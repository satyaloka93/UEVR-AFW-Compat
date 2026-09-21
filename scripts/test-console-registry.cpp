#include "../dependencies/submodules/UESDK/src/sdk/ConsoleRegistryValidation.hpp"
#include <cstdio>
#include <cstdlib>
#include <limits>

using namespace sdk::console_validation;
static void fake_function() {}
static void check(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
}
int main() {
    uintptr_t table[] = {reinterpret_cast<uintptr_t>(&fake_function)};
    uintptr_t variable = reinterpret_cast<uintptr_t>(table);
    std::array<Entry, 32> entries{};
    const wchar_t* names[] = {L"r.Shadow.Virtual.Enable", L"t.MaxFPS", L"r.ScreenPercentage"};
    for (int i = 0; i < 3; ++i) {
        const auto length = static_cast<int>(wcslen(names[i]) + 1);
        entries[i] = {reinterpret_cast<uintptr_t>(names[i]), length, length,
            reinterpret_cast<uintptr_t>(&variable), 0, 0};
    }
    struct Manager { uintptr_t table; Array array; } manager{reinterpret_cast<uintptr_t>(table),
        {reinterpret_cast<uintptr_t>(entries.data()), 32, 32}};
    check(snapshot(&manager).size() == 3, "valid registry");
    check(snapshot(nullptr).empty(), "null manager");
    check(snapshot(reinterpret_cast<void*>(1)).empty(), "invalid manager");
    manager.array.count = std::numeric_limits<int32_t>::max();
    check(snapshot(&manager).empty(), "bounded count");
    manager.array.count = -1;
    check(snapshot(&manager).empty(), "negative count");
    manager.array.count = 32; manager.array.capacity = 31;
    check(snapshot(&manager).empty(), "capacity mismatch");
    manager.array.capacity = 32;
    entries[0].length = 257;
    check(snapshot(&manager).empty(), "oversize string");
    entries[0].length = static_cast<int>(wcslen(names[0]) + 1);
    entries[0].value = 1;
    check(snapshot(&manager).empty(), "invalid variable");
    entries[0].value = reinterpret_cast<uintptr_t>(&variable);
    auto* guard = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    check(guard != nullptr, "guard allocation");
    entries[0].key = reinterpret_cast<uintptr_t>(guard);
    check(snapshot(&manager).empty(), "unreadable string");
    manager.array.elements = reinterpret_cast<uintptr_t>(guard);
    check(snapshot(&manager).empty(), "unreadable array");
    VirtualFree(guard, 0, MEM_RELEASE);
    std::puts("PASS console registry validation: valid snapshot and malformed/unreadable rejection");
}
