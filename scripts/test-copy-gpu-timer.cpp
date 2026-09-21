#include "../src/mods/vr/d3d12/CopyGpuTimer.hpp"
#include <cstdio>

int main() {
    using d3d12::ComPtr;
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device)))) return 1;
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC q{};
    if (FAILED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue)))) return 2;
    ComPtr<ID3D12CommandAllocator> allocator;
    if (FAILED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator)))) return 3;
    ComPtr<ID3D12GraphicsCommandList> list;
    if (FAILED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list)))) return 4;
    ComPtr<ID3D12Fence> fence;
    if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)))) return 5;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; desc.Width = 16 * 1024 * 1024;
    desc.Height = 1; desc.DepthOrArraySize = desc.MipLevels = 1;
    desc.SampleDesc.Count = 1; desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> source, destination;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr, IID_PPV_ARGS(&source)))) return 15;
    if (FAILED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&destination)))) return 16;
    d3d12::CopyGpuTimer timer;
    if (timer.collect() || timer.begin(nullptr, nullptr, nullptr)) return 6;
    for (int i = 0; i < 7; ++i) if (timer.begin(device.Get(), queue.Get(), list.Get())) return 7;
    if (!timer.begin(device.Get(), queue.Get(), list.Get())) return 8;
    list->CopyResource(destination.Get(), source.Get());
    timer.end(list.Get());
    if (FAILED(list->Close())) return 9;
    ID3D12CommandList* lists[]{list.Get()};
    queue->ExecuteCommandLists(1, lists);
    if (FAILED(queue->Signal(fence.Get(), 1))) return 10;
    timer.submitted(fence.Get(), 1);
    // Pending query ownership prevents slot overwrite, even if already complete.
    if (timer.begin(device.Get(), queue.Get(), list.Get())) return 11;
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!event || FAILED(fence->SetEventOnCompletion(1, event))) return 12;
    // Only the TEST waits. The production timer never waits for GPU work.
    if (WaitForSingleObject(event, 10000) != WAIT_OBJECT_0) return 13;
    CloseHandle(event);
    const auto ms = timer.collect();
    if (!ms || *ms <= 0 || timer.collect() || timer.failures) return 14;
    std::printf("PASS: hardware timestamp resolve/readback, sampling, pending-slot protection, one-shot collection (%.6f ms)\n", *ms);
    return 0;
}
