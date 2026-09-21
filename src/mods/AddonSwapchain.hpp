#pragma once
#include <d3d12.h>
#include <dxgi1_4.h>
#include <wrl/client.h>
#include <vector>
#include <cstddef>

namespace addon_host {
// ReShade resource_desc ABI: aligned union at +8, heap at +32, 48 bytes.
struct ResourceDesc {
    uint32_t type{};
    uint32_t padding{};
    union {
        uint64_t aligned_storage[3];
        struct {
            uint32_t width, height;
            uint16_t layers, levels;
            uint32_t format;
            uint16_t samples;
        } texture;
    } data{};
    uint32_t heap{}, usage{}, flags{};
};
static_assert(sizeof(ResourceDesc) == 48);
static_assert(offsetof(ResourceDesc, data) == 8);
static_assert(offsetof(ResourceDesc, heap) == 32);

inline ResourceDesc describe_texture(const D3D12_RESOURCE_DESC& native) {
    ResourceDesc out{};
    if (native.Dimension != D3D12_RESOURCE_DIMENSION_TEXTURE2D || native.Width > UINT32_MAX)
        return out;
    out.type = 3; // resource_type::texture_2d
    out.data.texture = {static_cast<uint32_t>(native.Width), native.Height,
        native.DepthOrArraySize, native.MipLevels, static_cast<uint32_t>(native.Format),
        static_cast<uint16_t>(native.SampleDesc.Count)};
    out.heap = 1; // memory_heap::default_
    out.usage = 0xc00; // copy source and destination
    if (native.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET) out.usage |= 4;
    if (native.Flags & D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS) out.usage |= 8;
    if (!(native.Flags & D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)) out.usage |= 0xc0;
    return out;
}
// Own a snapshot between initialization and pre-ResizeBuffers teardown.
// Callers serialize access. Never keep these references across ResizeBuffers.
class Swapchain {
public:
    bool bind(IDXGISwapChain3* swap) {
        reset();
        if (!swap) return false;
        DXGI_SWAP_CHAIN_DESC desc{};
        if (FAILED(swap->GetDesc(&desc)) || desc.BufferCount == 0 || desc.BufferCount > 16)
            return false;
        std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> buffers(desc.BufferCount);
        for (UINT i = 0; i < desc.BufferCount; ++i)
            if (FAILED(swap->GetBuffer(i, IID_PPV_ARGS(&buffers[i])))) return false;
        m_swap = swap;
        m_buffers = std::move(buffers);
        m_hwnd = desc.OutputWindow;
        return true;
    }
    void reset() { m_buffers.clear(); m_swap.Reset(); m_hwnd = nullptr; }
    void* hwnd() const { return m_hwnd; }
    uint32_t count() const { return static_cast<uint32_t>(m_buffers.size()); }
    uint64_t buffer(uint32_t index) const {
        return index < m_buffers.size() ? reinterpret_cast<uint64_t>(m_buffers[index].Get()) : 0;
    }
    uint32_t index() const {
        if (!m_swap) return 0;
        const auto i = m_swap->GetCurrentBackBufferIndex();
        return i < m_buffers.size() ? i : 0;
    }
    ResourceDesc describe(uint64_t handle) const {
        for (const auto& resource : m_buffers)
            if (reinterpret_cast<uint64_t>(resource.Get()) == handle)
                return describe_texture(resource->GetDesc());
        return {}; // never dereference unregistered/foreign handles
    }
    bool supports_color(uint32_t color) const {
        DXGI_COLOR_SPACE_TYPE native;
        switch (color) {
        case 1: native = DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709; break;
        case 2: native = DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709; break;
        case 3: native = DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020; break;
        default: return false;
        }
        UINT flags = 0;
        return m_swap && SUCCEEDED(m_swap->CheckColorSpaceSupport(native, &flags)) &&
               (flags & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT) != 0;
    }
private:
    Microsoft::WRL::ComPtr<IDXGISwapChain3> m_swap;
    std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> m_buffers;
    HWND m_hwnd{};
};
} // namespace addon_host
