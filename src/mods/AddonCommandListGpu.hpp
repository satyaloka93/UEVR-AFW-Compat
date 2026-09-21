#pragma once

#include <d3d12.h>
#include <cstdint>
#include <vector>

namespace addon_host {
// Restricted API18 usage translation. Unknown/special unsupported states fail
// explicitly; never silently substitute COMMON for an unknown usage.
inline bool translate_usage(uint32_t usage, D3D12_RESOURCE_STATES& result) {
    if (usage == 0x80000000u || usage == 0x80000804u) {
        result = D3D12_RESOURCE_STATE_COMMON;
        return true;
    }
    constexpr uint32_t supported = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 |
        0x40 | 0x80 | 0x100 | 0x200 | 0x400 | 0x800 | 0x1000 | 0x2000 | 0x8000;
    if (!usage || (usage & ~supported)) return false;
    uint32_t native = usage & ~0x8000u;
    if (usage & 0x8000u) native |= D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    // Match ReShade's combined depth usage: alone means write; combined with
    // other usage means read (D3D12 depth-write cannot coexist with reads).
    if ((usage & 0x30) == 0x30) {
        if (usage == 0x30) native = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        else native &= ~uint32_t(D3D12_RESOURCE_STATE_DEPTH_WRITE);
    }
    constexpr uint32_t writes = 0x4 | 0x8 | 0x10 | 0x100 | 0x400 | 0x1000;
    const auto write = native & writes;
    if (write && ((write & (write - 1)) || native != write)) return false;
    result = static_cast<D3D12_RESOURCE_STATES>(native);
    return true;
}

struct BarrierRequest {
    ID3D12Resource* resource;
    uint32_t before, after;
};

// These helpers require live native resources on the command list's device,
// a recording direct list, and accurate caller-supplied states. They do not
// infer state, convert arbitrary addon handles, or restore pipeline bindings.
inline bool record_barriers(ID3D12GraphicsCommandList* list,
                            uint32_t count, const BarrierRequest* requests) {
    if (!list || (count && !requests) || count > 4096) return false;
    std::vector<D3D12_RESOURCE_BARRIER> barriers;
    barriers.reserve(count);
    // Validate the complete batch before emitting any commands.
    for (uint32_t i = 0; i < count; ++i) {
        const auto& request = requests[i];
        D3D12_RESOURCE_STATES before{}, after{};
        if (!request.resource || !translate_usage(request.before, before) ||
            !translate_usage(request.after, after)) return false;
        D3D12_RESOURCE_BARRIER barrier{};
        if (before == D3D12_RESOURCE_STATE_UNORDERED_ACCESS && before == after) {
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.UAV.pResource = request.resource;
        } else {
            if (before == after) continue;
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = request.resource;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            barrier.Transition.StateBefore = before;
            barrier.Transition.StateAfter = after;
        }
        barriers.push_back(barrier);
    }
    if (!barriers.empty()) list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
    return true;
}

inline bool record_copy_resource(ID3D12GraphicsCommandList* list,
                                  ID3D12Resource* source, ID3D12Resource* destination) {
    if (!list || !source || !destination || source == destination) return false;
    const auto a = source->GetDesc(), b = destination->GetDesc();
    // Deliberately stricter than DXGI typeless-family compatibility for now.
    if (a.Dimension != b.Dimension || a.Width != b.Width || a.Height != b.Height ||
        a.DepthOrArraySize != b.DepthOrArraySize || a.MipLevels != b.MipLevels ||
        a.Format != b.Format || a.SampleDesc.Count != b.SampleDesc.Count ||
        a.SampleDesc.Quality != b.SampleDesc.Quality) return false;
    list->CopyResource(destination, source); // ReShade's argument order is source,dest.
    return true;
}
} // namespace addon_host
