#include "../src/mods/AddonSwapchain.hpp"
#include <cstdio>
#include <cstdlib>
using Microsoft::WRL::ComPtr;
void check(bool ok, const char* label) { if (!ok) { std::fprintf(stderr, "FAIL: %s\n", label); std::exit(1); } }
// Exercise the exact virtual-member struct-return ABI used by the host slot.
struct DescInterface {
    virtual void a() = 0; virtual void b() = 0; virtual void c() = 0;
    virtual void d() = 0; virtual void e() = 0; virtual void f() = 0;
    virtual void g() = 0; virtual void h() = 0; virtual void i() = 0;
    virtual void j() = 0;
    virtual addon_host::ResourceDesc describe(uint64_t resource) const = 0;
};
addon_host::Swapchain snapshot;
addon_host::ResourceDesc* __fastcall desc_thunk(void*, addon_host::ResourceDesc* out, uint64_t handle) {
    *out = snapshot.describe(handle); return out;
}
int main() {
    ComPtr<IDXGIFactory4> factory;
    check(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "factory");
    ComPtr<IDXGIAdapter> warp;
    check(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))), "WARP");
    ComPtr<ID3D12Device> device;
    check(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))), "device");
    D3D12_COMMAND_QUEUE_DESC queue_desc{};
    ComPtr<ID3D12CommandQueue> queue;
    check(SUCCEEDED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))), "queue");
    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = 64; desc.Height = 64; desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1; desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2; desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    desc.Scaling = DXGI_SCALING_STRETCH; desc.AlphaMode = DXGI_ALPHA_MODE_IGNORE;
    ComPtr<IDXGISwapChain1> base;
    check(SUCCEEDED(factory->CreateSwapChainForComposition(queue.Get(), &desc, nullptr, &base)), "swapchain");
    ComPtr<IDXGISwapChain3> swap;
    check(SUCCEEDED(base.As(&swap)), "swapchain3");
    check(snapshot.bind(swap.Get()), "snapshot bind");
    check(snapshot.count() == 2 && snapshot.buffer(0) != 0, "real buffers");
    check(snapshot.buffer(2) == 0, "out of bounds");
    check(snapshot.describe(1).type == 0, "foreign resource rejected");
    void* vt[11]{};
    vt[10] = reinterpret_cast<void*>(&desc_thunk);
    void* object[] = {vt};
    auto* api = reinterpret_cast<DescInterface*>(object);
    const auto result = api->describe(snapshot.buffer(0));
    check(result.type == 3 && result.data.texture.width == 64 && result.heap == 1, "virtual description ABI");
    snapshot.reset();
    check(snapshot.count() == 0, "snapshot cleared");
    check(SUCCEEDED(swap->ResizeBuffers(3, 96, 64, DXGI_FORMAT_R8G8B8A8_UNORM, 0)), "resize after releasing snapshot");
    check(snapshot.bind(swap.Get()) && snapshot.count() == 3, "rebind after resize");
    check(snapshot.describe(snapshot.buffer(0)).data.texture.width == 96, "new buffer description");
    snapshot.reset();
    std::puts("PASS: WARP buffers, virtual resource-desc ABI, bounds, release/resize/rebind");
}
