#include "../src/mods/AddonCommandListLifetime.hpp"
#include <dxgi1_4.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
using Microsoft::WRL::ComPtr;
void check(bool ok, const char* label) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); } }
void test_command_list_gpu();
int main() {
    test_command_list_gpu();
    ComPtr<IDXGIFactory4> factory;
    check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "factory");
    ComPtr<IDXGIAdapter> warp;
    check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))), "WARP");
    ComPtr<ID3D12Device> device, foreign;
    check(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))), "device");
    check(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&foreign))), "foreign device");
    ComPtr<ID3D12CommandAllocator> allocator, foreign_allocator;
    check(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))), "allocator");
    check(SUCCEEDED(foreign->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&foreign_allocator))), "foreign allocator");
    auto make_list = [](ID3D12Device* d, ID3D12CommandAllocator* a) {
        ComPtr<ID3D12GraphicsCommandList> list;
        check(SUCCEEDED(d->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, a, nullptr, IID_PPV_ARGS(&list))), "list");
        check(SUCCEEDED(list->Close()), "close");
        return list;
    };
    auto a = make_list(device.Get(), allocator.Get());
    auto b = make_list(device.Get(), allocator.Get());
    auto c = make_list(device.Get(), allocator.Get());
    auto other = make_list(foreign.Get(), foreign_allocator.Get());
    // Test-only GUID: never spoof RenoDX's live command-list interface pointer.
    const GUID tag{0x49f351e2,0x45c1,0x4acd,{0xb5,0x2e,0x36,0x22,0x75,0x08,0x13,0x94}};
    int initialized = 0, destroyed = 0;
    auto init = [&](addon_host::CommandListIdentity& item) {
        ++initialized;
        auto* ptr = &item;
        return SUCCEEDED(item.native()->SetPrivateData(tag, sizeof(ptr), &ptr));
    };
    auto destroy = [&](addon_host::CommandListIdentity& item) {
        ++destroyed;
        check(item.device() == device.Get(), "device alive during destruction");
        check(SUCCEEDED(item.native()->SetPrivateData(tag, 0, nullptr)), "remove tag before releasing list");
    };
    addon_host::CommandListLifetimes registry(device.Get(), 2, init, destroy);
    auto first = registry.acquire(a.Get());
    auto second = registry.acquire(b.Get());
    check(first && second && first != second, "distinct per-list identity");
    check(registry.acquire(a.Get()) == first && initialized == 2, "one init per identity");
    check(!registry.acquire(c.Get()) && !registry.acquire(nullptr), "bounded and null rejection");
    const uint8_t key[16]{1};
    first->set_private(key, 42);
    check(first->get_private(key) == 42 && second->get_private(key) == 0, "isolated private data");
    first->set_private(key, 0);
    check(first->get_private(key) == 0, "private data deletion");
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) threads.emplace_back([&] {
        for (int j = 0; j < 200; ++j) check(registry.acquire(a.Get()) == first, "concurrent identity");
    });
    for (auto& thread : threads) thread.join();
    check(initialized == 2, "concurrent init once");
    check(!registry.stop() && destroyed == 0, "active lease blocks destruction");
    check(!registry.acquire(a.Get()), "stop rejects new leases");
    auto* native = first->native();
    a.Reset(); // registry/lease must keep native alive
    UINT size = sizeof(void*); void* tagged{};
    check(SUCCEEDED(native->GetPrivateData(tag, &size, &tagged)) && tagged == first.get(), "native retained with stable tag");
    ComPtr<ID3D12GraphicsCommandList> retained(native);
    first.reset(); second.reset();
    check(registry.stop() && destroyed == 2 && registry.size() == 0, "paired destruction");
    size = sizeof(void*); tagged = nullptr;
    check(FAILED(retained->GetPrivateData(tag, &size, &tagged)), "tag removed");
    check(registry.stop() && destroyed == 2, "idempotent stop");
    addon_host::CommandListLifetimes foreign_check(device.Get(), 2, init, destroy);
    // D3D12 may return the same device for repeated creation on one adapter.
    if (foreign.Get() != device.Get())
        check(!foreign_check.acquire(other.Get()), "foreign device rejection");
    else
        std::puts("SKIP: foreign-device rejection (WARP device singleton)");
    check(foreign_check.stop(), "empty stop");
    int rolled_back = 0;
    addon_host::CommandListLifetimes failed(device.Get(), 1,
        [](auto&) { return false; }, [&](auto&) { ++rolled_back; });
    check(!failed.acquire(c.Get()) && rolled_back == 1 && failed.size() == 0, "failed init rolled back");
    check(failed.stop(), "failed registry stop");
    addon_host::CommandListLifetimes* recursive_ptr{};
    addon_host::CommandListLifetimes recursive(device.Get(), 1,
        [&](auto&) {
            check(!recursive_ptr->acquire(c.Get()), "recursive init rejects partial identity");
            check(!recursive_ptr->stop(), "recursive stop refuses initializing identity");
            return true;
        }, [](auto&) {});
    recursive_ptr = &recursive;
    { auto lease = recursive.acquire(c.Get()); check(bool(lease), "outer init succeeds"); }
    check(recursive.stop(), "recursive registry stop");
    std::puts("PASS: WARP per-list identity, isolation, concurrency, retention, capacity, rollback and teardown");
}
