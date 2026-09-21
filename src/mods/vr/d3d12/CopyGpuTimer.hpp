#pragma once
#include <cstdint>
#include <optional>
#include <d3d12.h>
#include "ComPtr.hpp"

namespace d3d12 {
// Owner must retire its command context before destroying/resetting this timer.
// No queue Signal, Wait, or CPU fence wait is introduced by measurement.
class CopyGpuTimer {
public:
    bool begin(ID3D12Device* device, ID3D12CommandQueue* queue, ID3D12GraphicsCommandList* list) {
        if (pending || disabled || !device || !queue || !list || (++calls % 8) != 0) return false;
        if (!heap) {
            D3D12_QUERY_HEAP_DESC q{};
            q.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP; q.Count = 2;
            D3D12_HEAP_PROPERTIES hp{};
            hp.Type = D3D12_HEAP_TYPE_READBACK; hp.CreationNodeMask = hp.VisibleNodeMask = 1;
            D3D12_RESOURCE_DESC rd{};
            rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = 16;
            rd.Height = 1; rd.DepthOrArraySize = rd.MipLevels = 1;
            rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            if (FAILED(queue->GetTimestampFrequency(&frequency)) || !frequency ||
                FAILED(device->CreateQueryHeap(&q, IID_PPV_ARGS(&heap))) ||
                FAILED(device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                    D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) {
                disabled = true; ++failures; return false;
            }
        }
        list->EndQuery(heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0);
        return true;
    }
    void end(ID3D12GraphicsCommandList* list) {
        list->EndQuery(heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 1);
        list->ResolveQueryData(heap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, readback.Get(), 0);
    }
    void submitted(ID3D12Fence* completion, UINT64 value) {
        fence = completion; fence_value = value; pending = true;
    }
    std::optional<double> collect() {
        if (!pending || !fence) return std::nullopt;
        const auto completed = fence->GetCompletedValue();
        if (completed == UINT64_MAX) { disabled = true; pending = false; ++failures; return std::nullopt; }
        if (completed < fence_value) return std::nullopt;
        pending = false;
        void* data{};
        const D3D12_RANGE read{0, 16}, written{0, 0};
        if (FAILED(readback->Map(0, &read, &data))) { ++failures; return std::nullopt; }
        const auto* ticks = static_cast<const UINT64*>(data);
        const auto start = ticks[0], finish = ticks[1];
        readback->Unmap(0, &written);
        if (finish < start || !frequency) { ++failures; return std::nullopt; }
        return static_cast<double>(finish - start) * 1000.0 / frequency;
    }
    uint64_t failures{};
private:
    ComPtr<ID3D12QueryHeap> heap;
    ComPtr<ID3D12Resource> readback;
    ComPtr<ID3D12Fence> fence;
    UINT64 frequency{}, fence_value{}, calls{};
    bool pending{}, disabled{};
};
}
