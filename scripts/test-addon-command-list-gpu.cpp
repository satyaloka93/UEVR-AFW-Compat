#include "../src/mods/AddonCommandListGpu.hpp"
#include "../src/mods/AddonShaderBindings.hpp"
#include <d3dcompiler.h>
#include <dxgi1_4.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
using Microsoft::WRL::ComPtr;
namespace {
void require(bool ok, const char* label) {
    if (!ok) { std::fprintf(stderr, "FAIL GPU: %s\n", label); std::exit(1); }
}
}
void test_command_list_gpu() {
    ComPtr<ID3D12Debug> debug;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug)))) debug->EnableDebugLayer();
    else std::puts("SKIP: D3D12 debug layer unavailable");
    ComPtr<IDXGIFactory4> factory;
    require(SUCCEEDED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))), "factory");
    ComPtr<IDXGIAdapter> warp;
    require(SUCCEEDED(factory->EnumWarpAdapter(IID_PPV_ARGS(&warp))), "WARP");
    ComPtr<ID3D12Device> device;
    require(SUCCEEDED(D3D12CreateDevice(warp.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&device))), "device");
    ComPtr<ID3D12InfoQueue> info;
    device.As(&info);
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC q{};
    require(SUCCEEDED(device->CreateCommandQueue(&q, IID_PPV_ARGS(&queue))), "queue");
    ComPtr<ID3D12CommandAllocator> allocator;
    require(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&allocator))), "allocator");
    ComPtr<ID3D12GraphicsCommandList> list;
    require(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, allocator.Get(), nullptr, IID_PPV_ARGS(&list))), "list");
    auto buffer = [&](D3D12_HEAP_TYPE heap, D3D12_RESOURCE_STATES state, UINT64 bytes,
                      D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE) {
        D3D12_HEAP_PROPERTIES h{}; h.Type = heap;
        D3D12_RESOURCE_DESC d{}; d.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        d.Width = bytes; d.Height = 1; d.DepthOrArraySize = 1; d.MipLevels = 1;
        d.SampleDesc.Count = 1; d.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        d.Flags = flags;
        ComPtr<ID3D12Resource> resource;
        require(SUCCEEDED(device->CreateCommittedResource(&h, D3D12_HEAP_FLAG_NONE, &d, state, nullptr, IID_PPV_ARGS(&resource))), "buffer");
        return resource;
    };
    auto upload = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, 1024);
    auto target = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, 1024);
    auto readback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, 1024);
    auto wrong_size = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST, 512);
    uint32_t expected[256];
    for (unsigned i = 0; i < 256; ++i) expected[i] = 0x12345678u ^ (i * 0x10203u);
    void* mapped{}; D3D12_RANGE empty{0, 0};
    require(SUCCEEDED(upload->Map(0, &empty, &mapped)), "upload map");
    std::memcpy(mapped, expected, sizeof(expected)); upload->Unmap(0, nullptr);
    D3D12_RESOURCE_STATES state{};
    require(addon_host::translate_usage(0x8000, state) && state == D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER, "constant buffer mapping");
    require(addon_host::translate_usage(0x80000804u, state) && state == D3D12_RESOURCE_STATE_PRESENT, "present mapping");
    require(!addon_host::translate_usage(0, state) && !addon_host::translate_usage(0x40000000, state), "unknown usages rejected");
    require(!addon_host::translate_usage(0x408, state), "multiple writes rejected");
    require(addon_host::translate_usage(0x30, state) && state == D3D12_RESOURCE_STATE_DEPTH_WRITE, "depth write mapping");
    require(addon_host::translate_usage(0x70, state) && state == (D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE), "depth read mapping");
    require(addon_host::record_copy_resource(list.Get(), upload.Get(), target.Get()), "source/destination copy");
    // If the first entry of this rejected batch were emitted, the next valid
    // transition would have the wrong before-state. Debug-layer checks catch it.
    addon_host::BarrierRequest bad[]{{target.Get(), 0x400, 0x800}, {target.Get(), 0, 0x800}};
    require(!addon_host::record_barriers(list.Get(), 2, bad), "invalid batch rejected atomically");
    addon_host::BarrierRequest good{target.Get(), 0x400, 0x800};
    require(addon_host::record_barriers(list.Get(), 1, &good), "copy transition");
    require(!addon_host::record_copy_resource(list.Get(), target.Get(), wrong_size.Get()), "size mismatch rejected");
    require(!addon_host::record_copy_resource(list.Get(), target.Get(), target.Get()), "self copy rejected");
    require(addon_host::record_copy_resource(list.Get(), target.Get(), readback.Get()), "readback copy");
    D3D12_RESOURCE_DESC texture_desc{};
    texture_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    texture_desc.Width = 8; texture_desc.Height = 8;
    texture_desc.DepthOrArraySize = 1; texture_desc.MipLevels = 1;
    texture_desc.SampleDesc.Count = 1; texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_HEAP_PROPERTIES default_heap{}; default_heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    ComPtr<ID3D12Resource> texture_source, texture_dest;
    require(SUCCEEDED(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
        &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture_source))), "source texture");
    require(SUCCEEDED(device->CreateCommittedResource(&default_heap, D3D12_HEAP_FLAG_NONE,
        &texture_desc, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&texture_dest))), "destination texture");
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{}; UINT64 texture_bytes{};
    device->GetCopyableFootprints(&texture_desc, 0, 1, 0, &footprint, nullptr, nullptr, &texture_bytes);
    auto texture_upload = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, texture_bytes);
    auto texture_readback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, texture_bytes);
    require(SUCCEEDED(texture_upload->Map(0, &empty, &mapped)), "texture upload map");
    for (unsigned y = 0; y < 8; ++y)
        std::memcpy(static_cast<uint8_t*>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch,
            expected + y * 8, 8 * sizeof(uint32_t));
    texture_upload->Unmap(0, nullptr);
    D3D12_TEXTURE_COPY_LOCATION upload_location{};
    upload_location.pResource = texture_upload.Get();
    upload_location.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    upload_location.PlacedFootprint = footprint;
    D3D12_TEXTURE_COPY_LOCATION source_location{};
    source_location.pResource = texture_source.Get();
    source_location.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    list->CopyTextureRegion(&source_location, 0, 0, 0, &upload_location, nullptr);
    addon_host::BarrierRequest texture_barrier{texture_source.Get(), 0x400, 0x800};
    require(addon_host::record_barriers(list.Get(), 1, &texture_barrier), "source texture transition");
    require(addon_host::record_copy_resource(list.Get(), texture_source.Get(), texture_dest.Get()), "whole texture copy");
    texture_barrier.resource = texture_dest.Get();
    require(addon_host::record_barriers(list.Get(), 1, &texture_barrier), "destination texture transition");
    source_location.pResource = texture_dest.Get();
    upload_location.pResource = texture_readback.Get();
    list->CopyTextureRegion(&upload_location, 0, 0, 0, &source_location, nullptr);

    // Baseline -> overwrite every supported compute binding -> restore ->
    // dispatch without any manual rebinding. GPU results must match baseline.
    const char shader[] = R"(
cbuffer RootValues : register(b0) { uint factor; uint index; };
cbuffer BufferValues : register(b1) { uint bias; };
ByteAddressBuffer input : register(t0);
RWByteAddressBuffer output : register(u0);
RWByteAddressBuffer tableOutput : register(u1);
[numthreads(1,1,1)] void main() {
    uint value = factor + bias + input.Load(0);
    output.Store(index * 4, value);
    tableOutput.Store(index * 4, value + 10);
})";
    const char other_shader[] = R"(
RWByteAddressBuffer output : register(u0);
[numthreads(1,1,1)] void main() { output.Store(0, 999); }
)";
    ComPtr<ID3DBlob> code, other_code, errors, root_blob;
    require(SUCCEEDED(D3DCompile(shader, sizeof(shader), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &code, &errors)), "compile baseline shader");
    require(SUCCEEDED(D3DCompile(other_shader, sizeof(other_shader), nullptr, nullptr, nullptr, "main", "cs_5_0", 0, 0, &other_code, &errors)), "compile different shader");
    D3D12_DESCRIPTOR_RANGE range_desc{D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0, 0};
    D3D12_ROOT_PARAMETER parameters[5]{};
    parameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    parameters[0].Constants = {0, 0, 2};
    parameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV; parameters[1].Descriptor = {1, 0};
    parameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_SRV; parameters[2].Descriptor = {0, 0};
    parameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_UAV; parameters[3].Descriptor = {0, 0};
    parameters[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    parameters[4].DescriptorTable = {1, &range_desc};
    D3D12_ROOT_SIGNATURE_DESC root_desc{5, parameters, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_NONE};
    require(SUCCEEDED(D3D12SerializeRootSignature(&root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &root_blob, &errors)), "serialize root");
    ComPtr<ID3D12RootSignature> root, other_root;
    require(SUCCEEDED(device->CreateRootSignature(0, root_blob->GetBufferPointer(), root_blob->GetBufferSize(), IID_PPV_ARGS(&root))), "root");
    D3D12_ROOT_SIGNATURE_DESC empty_root_desc{};
    ComPtr<ID3DBlob> empty_root_blob;
    require(SUCCEEDED(D3D12SerializeRootSignature(&empty_root_desc, D3D_ROOT_SIGNATURE_VERSION_1, &empty_root_blob, &errors)), "empty root blob");
    require(SUCCEEDED(device->CreateRootSignature(0, empty_root_blob->GetBufferPointer(), empty_root_blob->GetBufferSize(), IID_PPV_ARGS(&other_root))), "different root");
    D3D12_COMPUTE_PIPELINE_STATE_DESC pipeline_desc{};
    pipeline_desc.pRootSignature = root.Get();
    pipeline_desc.CS = {code->GetBufferPointer(), code->GetBufferSize()};
    ComPtr<ID3D12PipelineState> pipeline, other_pipeline;
    require(SUCCEEDED(device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&pipeline))), "baseline pipeline");
    pipeline_desc.CS = {other_code->GetBufferPointer(), other_code->GetBufferSize()};
    require(SUCCEEDED(device->CreateComputePipelineState(&pipeline_desc, IID_PPV_ARGS(&other_pipeline))), "different pipeline");
    auto bias_buffer = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, 256);
    auto input_buffer = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, 256);
    auto wrong_input = buffer(D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ, 256);
    auto fill_value = [&](ID3D12Resource* resource, uint32_t value) {
        require(SUCCEEDED(resource->Map(0, &empty, &mapped)), "shader input map");
        std::memcpy(mapped, &value, sizeof(value)); resource->Unmap(0, nullptr);
    };
    fill_value(bias_buffer.Get(), 5); fill_value(input_buffer.Get(), 8); fill_value(wrong_input.Get(), 100);
    auto result_buffer = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto table_buffer = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto scratch_buffer = buffer(D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, 256, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
    auto result_readback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, 256);
    auto table_readback = buffer(D3D12_HEAP_TYPE_READBACK, D3D12_RESOURCE_STATE_COPY_DEST, 256);
    D3D12_DESCRIPTOR_HEAP_DESC heap_desc{D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE, 0};
    ComPtr<ID3D12DescriptorHeap> heap, other_heap;
    require(SUCCEEDED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap))), "baseline heap");
    require(SUCCEEDED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&other_heap))), "different heap");
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.Format = DXGI_FORMAT_R32_TYPELESS; uav.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    uav.Buffer.NumElements = 64; uav.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW;
    device->CreateUnorderedAccessView(table_buffer.Get(), nullptr, &uav, heap->GetCPUDescriptorHandleForHeapStart());
    device->CreateUnorderedAccessView(scratch_buffer.Get(), nullptr, &uav, other_heap->GetCPUDescriptorHandleForHeapStart());
    auto layout = addon_host::RootBindingLayout::parse(root.Get(), root_blob->GetBufferPointer(), root_blob->GetBufferSize());
    auto other_layout = addon_host::RootBindingLayout::parse(other_root.Get(), empty_root_blob->GetBufferPointer(), empty_root_blob->GetBufferSize());
    require(layout && other_layout, "parse layouts");
    addon_host::ShaderBindings saved;
    require(!saved.ready() && !saved.restore(list.Get()), "unknown capture refused");
    saved.reset(list.Get(), pipeline.Get()); saved.signature(false, layout);
    ID3D12DescriptorHeap* native_heaps[]{heap.Get()};
    require(saved.heaps(1, native_heaps), "observe heap");
    uint32_t constants[]{7, 0};
    require(saved.constants(false, 0, 0, 1, constants), "partial constants");
    require(!saved.ready(), "partial state refused");
    require(saved.constants(false, 0, 1, 1, constants + 1), "complete constants");
    require(saved.descriptor(false, 1, D3D12_ROOT_PARAMETER_TYPE_CBV, bias_buffer->GetGPUVirtualAddress()), "observe CBV");
    require(saved.descriptor(false, 2, D3D12_ROOT_PARAMETER_TYPE_SRV, input_buffer->GetGPUVirtualAddress()), "observe SRV");
    require(saved.descriptor(false, 3, D3D12_ROOT_PARAMETER_TYPE_UAV, result_buffer->GetGPUVirtualAddress()), "observe UAV");
    require(saved.descriptor(false, 4, D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE, heap->GetGPUDescriptorHandleForHeapStart().ptr), "observe table");
    require(saved.ready(), "complete capture");
    list->SetPipelineState(pipeline.Get()); list->SetDescriptorHeaps(1, native_heaps);
    list->SetComputeRootSignature(root.Get()); list->SetComputeRoot32BitConstants(0, 2, constants, 0);
    list->SetComputeRootConstantBufferView(1, bias_buffer->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(2, input_buffer->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(3, result_buffer->GetGPUVirtualAddress());
    list->SetComputeRootDescriptorTable(4, heap->GetGPUDescriptorHandleForHeapStart());
    list->Dispatch(1, 1, 1);
    addon_host::BarrierRequest uav_order[]{{result_buffer.Get(), 8, 8}, {table_buffer.Get(), 8, 8}};
    require(addon_host::record_barriers(list.Get(), 2, uav_order), "UAV ordering before second dispatch");
    uint32_t second_index = 1;
    list->SetComputeRoot32BitConstant(0, second_index, 1);
    require(saved.constants(false, 0, 1, 1, &second_index), "observe partial overwrite");
    auto invalid = saved;
    invalid.signature(false, layout); require(invalid.ready(), "same signature preserves arguments");
    invalid.signature(false, other_layout); invalid.signature(false, layout);
    require(!invalid.ready(), "changed signature invalidates arguments");
    invalid = saved; native_heaps[0] = other_heap.Get();
    require(invalid.heaps(1, native_heaps) && !invalid.ready(), "changed heap invalidates tables");
    // Clobber with another PSO, constants, CBV/SRV/UAV, table and signature.
    list->SetPipelineState(other_pipeline.Get()); list->SetDescriptorHeaps(1, native_heaps);
    uint32_t wrong_constants[]{99, 9}; list->SetComputeRoot32BitConstants(0, 2, wrong_constants, 0);
    list->SetComputeRootConstantBufferView(1, wrong_input->GetGPUVirtualAddress());
    list->SetComputeRootShaderResourceView(2, wrong_input->GetGPUVirtualAddress());
    list->SetComputeRootUnorderedAccessView(3, scratch_buffer->GetGPUVirtualAddress());
    list->SetComputeRootDescriptorTable(4, other_heap->GetGPUDescriptorHandleForHeapStart());
    list->SetComputeRootSignature(other_root.Get());
    require(saved.restore(list.Get()), "restore shader bindings");
    list->Dispatch(1, 1, 1); // absolutely no manual rebind after restore
    ComPtr<ID3D12GraphicsCommandList> different_list;
    ComPtr<ID3D12CommandAllocator> different_allocator;
    require(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&different_allocator))), "different allocator");
    require(SUCCEEDED(device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, different_allocator.Get(), nullptr, IID_PPV_ARGS(&different_list))), "different list");
    require(!saved.restore(different_list.Get()), "cross-list restoration rejected");
    require(SUCCEEDED(different_list->Close()), "close different list");
    invalid = saved; invalid.invalidate();
    require(!invalid.ready() && !saved.ready(), "unknown execution invalidates all recording snapshots");
    saved.reset(list.Get(), pipeline.Get());
    auto stale_snapshot = saved;
    saved.reset(list.Get(), pipeline.Get());
    require(!stale_snapshot.ready(), "reset invalidates old recording snapshot");
    addon_host::BarrierRequest shader_to_copy[]{{result_buffer.Get(), 8, 0x800}, {table_buffer.Get(), 8, 0x800}};
    require(addon_host::record_barriers(list.Get(), 2, shader_to_copy), "shader outputs to copy source");
    require(addon_host::record_copy_resource(list.Get(), result_buffer.Get(), result_readback.Get()), "root UAV readback copy");
    require(addon_host::record_copy_resource(list.Get(), table_buffer.Get(), table_readback.Get()), "table UAV readback copy");
    require(SUCCEEDED(list->Close()), "close recording");
    ID3D12CommandList* lists[]{list.Get()}; queue->ExecuteCommandLists(1, lists);
    ComPtr<ID3D12Fence> fence;
    require(SUCCEEDED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))), "fence");
    require(SUCCEEDED(queue->Signal(fence.Get(), 1)), "signal");
    HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    require(event != nullptr, "event");
    require(SUCCEEDED(fence->SetEventOnCompletion(1, event)), "fence event");
    require(WaitForSingleObject(event, 10000) == WAIT_OBJECT_0, "GPU completed before timeout");
    CloseHandle(event);
    D3D12_RANGE range{0, sizeof(expected)};
    require(SUCCEEDED(readback->Map(0, &range, &mapped)), "readback map");
    require(std::memcmp(mapped, expected, sizeof(expected)) == 0, "all GPU-copied bytes match");
    readback->Unmap(0, &empty);
    D3D12_RANGE texture_range{0, static_cast<SIZE_T>(texture_bytes)};
    require(SUCCEEDED(texture_readback->Map(0, &texture_range, &mapped)), "texture readback map");
    for (unsigned y = 0; y < 8; ++y)
        require(std::memcmp(static_cast<uint8_t*>(mapped) + footprint.Offset + y * footprint.Footprint.RowPitch,
            expected + y * 8, 8 * sizeof(uint32_t)) == 0, "all texture pixels match");
    texture_readback->Unmap(0, &empty);
    D3D12_RANGE shader_range{0, 8};
    require(SUCCEEDED(result_readback->Map(0, &shader_range, &mapped)), "root UAV readback map");
    require(static_cast<uint32_t*>(mapped)[0] == 20 && static_cast<uint32_t*>(mapped)[1] == 20, "restored shader/root bindings match baseline");
    result_readback->Unmap(0, &empty);
    require(SUCCEEDED(table_readback->Map(0, &shader_range, &mapped)), "table readback map");
    require(static_cast<uint32_t*>(mapped)[0] == 30 && static_cast<uint32_t*>(mapped)[1] == 30, "restored heap/table bindings match baseline");
    table_readback->Unmap(0, &empty);
    if (info) {
        for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
            SIZE_T bytes{}; info->GetMessage(i, nullptr, &bytes);
            std::vector<uint8_t> storage(bytes);
            auto* message = reinterpret_cast<D3D12_MESSAGE*>(storage.data());
            require(SUCCEEDED(info->GetMessage(i, message, &bytes)), "debug message read");
            if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
                std::fprintf(stderr, "%s\n", message->pDescription);
                require(false, "D3D12 validation error");
            }
        }
        std::puts("PASS: D3D12 debug layer reports no errors");
    }
    std::puts("PASS: WARP GPU buffer/texture copy/readback, state conversion, transition and invalid-batch rejection");
    std::puts("PASS: WARP shader bindings restored after PSO/root/constants/CBV/SRV/UAV/heap/table clobber");
}
