#include <sdk/CVar.hpp>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Execute only small test-owned getter/setter stubs, never game code.
// The Release/null-guard and extra-setter positions model the installed TOW2
// int-variable interface: Release 16, Set 17, extra setter 18, Bool 19,
// Int 20, Float 21. Slot 18 traps if accidentally called as a getter.
struct Probe : sdk::IConsoleVariable {
    bool layout(uint32_t setter, uint32_t integer, uint32_t floating) {
        const auto info = locate_vtable_indices(true);
        return info && info->set_vtable_index == setter && info->get_int_vtable_index == integer && info->get_float_vtable_index == floating;
    }
};
static void check(bool value, const char* label) {
    if (!value) { std::fprintf(stderr, "FAIL %s\n", label); std::exit(1); }
}
int main() {
    auto* code = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    check(code != nullptr, "allocate fixture");
    std::memset(code, 0xCC, 4096);
    const unsigned char noop[]{0xC3};
    const unsigned char null_return[]{0x33,0xC0,0xC3};
    const unsigned char release[]{0x48,0x83,0xEC,0x28,0x48,0x85,0xC9,0x74,0x0A,0x48,0x8B,0x01,0xBA,0x01,0,0,0,0xFF,0x10,0x48,0x83,0xC4,0x28,0xC3};
    const unsigned char setter[]{0xC7,0x41,0x50,0x7B,0,0,0,0xC3};
    const unsigned char wrong_getter[]{0x0F,0x0B,0xC3};
    const unsigned char boolean[]{0x83,0x79,0x50,0,0x0F,0x95,0xC0,0xC3};
    const unsigned char integer[]{0x8B,0x41,0x50,0xC3};
    const unsigned char floating[]{0xF3,0x0F,0x10,0x41,0x50,0xC3};
    std::array<uintptr_t,32> slots{};
    std::memcpy(code,noop,sizeof(noop)); slots.fill(reinterpret_cast<uintptr_t>(code));
    auto put = [&](size_t slot, const auto& bytes) {
        std::memcpy(code + slot*64, bytes, sizeof(bytes)); slots[slot] = reinterpret_cast<uintptr_t>(code + slot*64);
    };
    put(15,null_return); put(16,release); put(17,setter); put(18,wrong_getter);
    put(19,boolean); put(20,integer); put(21,floating);
    DWORD previous{};
    check(VirtualProtect(code,4096,PAGE_EXECUTE_READ,&previous), "protect fixture");
    FlushInstructionCache(GetCurrentProcess(),code,4096);
    alignas(16) std::array<unsigned char,128> object{};
    const auto table = slots.data(); std::memcpy(object.data(),&table,sizeof(table));
    auto* variable = reinterpret_cast<Probe*>(object.data());
    check(variable->layout(17,20,21), "extra setter does not become GetInt");
    variable->Set(L"123",0x0E000000);
    check(variable->GetInt()==123, "correct setter and integer getter");
    float value=1.5f; std::memcpy(object.data()+0x50,&value,sizeof(value));
    check(variable->GetFloat()==1.5f, "correct float getter");
    // Older UE layout: no extra setter. Do not hardcode TOW2's getter indices
    // when enabling the same validated path for another game.
    std::array<uintptr_t,32> older = slots;
    older[18]=slots[19]; older[19]=slots[20]; older[20]=slots[21];
    const auto older_table=older.data();
    std::memcpy(object.data(),&older_table,sizeof(older_table));
    check(variable->layout(17,19,20), "single-setter layout supported");
    variable->Set(L"123",0x0E000000);
    check(variable->GetInt()==123, "single-setter integer getter");
    std::memcpy(object.data()+0x50,&value,sizeof(value));
    check(variable->GetFloat()==1.5f, "single-setter float getter");
    std::array<uintptr_t,32> invalid = slots;
    invalid[16]=slots[0]; const auto invalid_table=invalid.data();
    std::memcpy(object.data(),&invalid_table,sizeof(invalid_table));
    check(!variable->validate_access(), "missing Release rejected");
    VirtualFree(code,0,MEM_RELEASE);
    std::puts("PASS verified CVar interface: extra-setter regression, read/write stubs, malformed rejection");
}
