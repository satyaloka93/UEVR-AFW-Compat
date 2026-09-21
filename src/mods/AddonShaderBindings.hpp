#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <atomic>
#include <climits>
#include <cstdint>
#include <memory>
#include <vector>

namespace addon_host {
// Metadata must come from the SAME serialized blob used to create signature.
// The eventual CreateRootSignature hook supplies that association. No guessing
// from setters observed after injection, when earlier bindings may be missing.
struct RootBindingLayout {
    struct Parameter {
        D3D12_ROOT_PARAMETER_TYPE type;
        UINT words{};
        D3D12_DESCRIPTOR_HEAP_TYPE heap{};
        UINT span{};
    };
    Microsoft::WRL::ComPtr<ID3D12RootSignature> signature;
    std::vector<Parameter> parameters;
    static std::shared_ptr<const RootBindingLayout> parse(ID3D12RootSignature* signature,
                                                         const void* blob, size_t size) {
        if (!signature || !blob || !size) return {};
        Microsoft::WRL::ComPtr<ID3D12VersionedRootSignatureDeserializer> parser;
        if (FAILED(D3D12CreateVersionedRootSignatureDeserializer(blob, size, IID_PPV_ARGS(&parser)))) return {};
        const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc{};
        if (FAILED(parser->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_0, &desc))) return {};
        // Direct heap indexing/local root signatures need separate contracts.
        if (desc->Desc_1_0.Flags & (D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE |
            D3D12_ROOT_SIGNATURE_FLAG_CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED |
            D3D12_ROOT_SIGNATURE_FLAG_SAMPLER_HEAP_DIRECTLY_INDEXED)) return {};
        if (desc->Desc_1_0.NumParameters > 64) return {};
        auto result = std::make_shared<RootBindingLayout>();
        result->signature = signature;
        for (UINT i = 0; i < desc->Desc_1_0.NumParameters; ++i) {
            const auto& source = desc->Desc_1_0.pParameters[i];
            Parameter parameter{source.ParameterType};
            switch (source.ParameterType) {
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                parameter.words = source.Constants.Num32BitValues;
                if (!parameter.words || parameter.words > 64) return {};
                break;
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                if (!source.DescriptorTable.NumDescriptorRanges) return {};
                parameter.heap = source.DescriptorTable.pDescriptorRanges[0].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER
                    ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                for (UINT r = 0, next = 0; r < source.DescriptorTable.NumDescriptorRanges; ++r) {
                    const auto& range = source.DescriptorTable.pDescriptorRanges[r];
                    const bool sampler = range.RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER;
                    if (sampler != (parameter.heap == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)) return {};
                    const auto offset = range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND
                        ? next : range.OffsetInDescriptorsFromTableStart;
                    // Unbounded tables require additional descriptor provenance.
                    if (!range.NumDescriptors || range.NumDescriptors == UINT_MAX || offset > UINT_MAX - range.NumDescriptors) return {};
                    next = offset + range.NumDescriptors;
                    if (next > parameter.span) parameter.span = next;
                }
                break;
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            case D3D12_ROOT_PARAMETER_TYPE_UAV: break;
            default: return {};
            }
            result->parameters.push_back(parameter);
        }
        return result;
    }
};

// Per-list observer and copyable snapshot for the shader-binding subset ONLY.
// Caller serializes access and reports every successful native setter/reset.
// Snapshots retain the list/PSO/signatures/heaps; resources referenced by GPU
// addresses or descriptors still require an external fence-aware lifetime.
// Reset/unknown bundle or ExecuteIndirect effects must invalidate the snapshot.
// No render targets, viewports, IA bindings, raytracing PSOs or resource states
// are restored here. Snapshot eligibility is NOT permission to run RenoDX.
class ShaderBindings {
    struct Recording { std::atomic<bool> valid{true}; };
    struct Argument {
        std::array<uint32_t, 64> words{};
        uint64_t written{}, address{};
        bool bound{};
    };
    struct Bank {
        std::shared_ptr<const RootBindingLayout> layout;
        std::array<Argument, 64> arguments{};
    };
public:
    void reset(ID3D12GraphicsCommandList* list, ID3D12PipelineState* initial = nullptr) {
        invalidate(); // invalidate every copy of the preceding recording
        *this = ShaderBindings{};
        recording_ = std::make_shared<Recording>();
        list_ = list;
        pipeline_ = initial;
    }
    void invalidate() { if (recording_) recording_->valid.store(false); }
    void pipeline(ID3D12PipelineState* value) { pipeline_ = value; }
    void signature(bool graphics, std::shared_ptr<const RootBindingLayout> layout) {
        auto& bank = banks_[graphics ? 1 : 0];
        if (bank.layout && layout && bank.layout->signature == layout->signature) return;
        bank = Bank{};
        bank.layout = std::move(layout);
    }
    // Explicitly call invalidate() for an unknown non-null root signature;
    // signature(..., nullptr) means a known null/unbound signature, not unknown.
    bool heaps(UINT count, ID3D12DescriptorHeap* const* values) {
        if (count > 2 || (count && !values)) { invalidate(); return false; }
        std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, 2> next;
        for (UINT i = 0; i < count; ++i) {
            if (!values[i]) { invalidate(); return false; }
            const auto desc = values[i]->GetDesc();
            if (desc.Type > D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER ||
                !(desc.Flags & D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE) || next[desc.Type]) {
                invalidate(); return false;
            }
            next[desc.Type] = values[i];
        }
        if (next != heaps_) {
            for (auto& bank : banks_) if (bank.layout)
                for (size_t i = 0; i < bank.layout->parameters.size(); ++i)
                    if (bank.layout->parameters[i].type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
                        bank.arguments[i].bound = false;
        }
        heaps_ = std::move(next);
        return true;
    }
    bool constants(bool graphics, UINT index, UINT offset, UINT count, const uint32_t* data) {
        auto* bank = parameter(graphics, index, D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS);
        if (!bank || (count && !data)) { invalidate(); return false; }
        const auto words = bank->layout->parameters[index].words;
        if (offset > words || count > words - offset) { invalidate(); return false; }
        auto& argument = bank->arguments[index];
        for (UINT i = 0; i < count; ++i) {
            argument.words[offset + i] = data[i];
            argument.written |= uint64_t{1} << (offset + i);
        }
        return true;
    }
    bool descriptor(bool graphics, UINT index, D3D12_ROOT_PARAMETER_TYPE type, uint64_t address) {
        if (type != D3D12_ROOT_PARAMETER_TYPE_CBV && type != D3D12_ROOT_PARAMETER_TYPE_SRV &&
            type != D3D12_ROOT_PARAMETER_TYPE_UAV && type != D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE) {
            invalidate(); return false;
        }
        auto* bank = parameter(graphics, index, type);
        if (!bank || !address) { invalidate(); return false; }
        if (type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE &&
            !table_in_heap(bank->layout->parameters[index].heap, address,
                bank->layout->parameters[index].span)) { invalidate(); return false; }
        bank->arguments[index].address = address;
        bank->arguments[index].bound = true;
        return true;
    }
    bool ready() const {
        if (!recording_ || !recording_->valid.load() || !list_ || !pipeline_) return false;
        for (const auto& bank : banks_) if (bank.layout) {
            for (size_t i = 0; i < bank.layout->parameters.size(); ++i) {
                const auto& p = bank.layout->parameters[i];
                const auto& a = bank.arguments[i];
                if (p.type == D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS) {
                    const auto mask = p.words == 64 ? ~uint64_t{} : (uint64_t{1} << p.words) - 1;
                    if (a.written != mask) return false;
                } else if (!a.bound) return false;
            }
        }
        return true;
    }
    bool restore(ID3D12GraphicsCommandList* list) const {
        if (!list || list != list_.Get() || !ready()) return false; // all checks before native emission
        ID3D12DescriptorHeap* heaps[2]{}; UINT count = 0;
        for (const auto& heap : heaps_) if (heap) heaps[count++] = heap.Get();
        list->SetDescriptorHeaps(count, heaps);
        list->SetPipelineState(pipeline_.Get());
        for (int graphics = 0; graphics < 2; ++graphics) {
            const auto& bank = banks_[graphics];
            auto* signature = bank.layout ? bank.layout->signature.Get() : nullptr;
            if (graphics) list->SetGraphicsRootSignature(signature);
            else list->SetComputeRootSignature(signature);
            if (!bank.layout) continue;
            for (UINT i = 0; i < bank.layout->parameters.size(); ++i) {
                const auto& p = bank.layout->parameters[i]; const auto& a = bank.arguments[i];
                switch (p.type) {
                case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
                    if (graphics) list->SetGraphicsRoot32BitConstants(i, p.words, a.words.data(), 0);
                    else list->SetComputeRoot32BitConstants(i, p.words, a.words.data(), 0);
                    break;
                case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
                    if (graphics) list->SetGraphicsRootDescriptorTable(i, {a.address});
                    else list->SetComputeRootDescriptorTable(i, {a.address});
                    break;
                case D3D12_ROOT_PARAMETER_TYPE_CBV:
                    if (graphics) list->SetGraphicsRootConstantBufferView(i, a.address);
                    else list->SetComputeRootConstantBufferView(i, a.address);
                    break;
                case D3D12_ROOT_PARAMETER_TYPE_SRV:
                    if (graphics) list->SetGraphicsRootShaderResourceView(i, a.address);
                    else list->SetComputeRootShaderResourceView(i, a.address);
                    break;
                case D3D12_ROOT_PARAMETER_TYPE_UAV:
                    if (graphics) list->SetGraphicsRootUnorderedAccessView(i, a.address);
                    else list->SetComputeRootUnorderedAccessView(i, a.address);
                    break;
                }
            }
        }
        return true;
    }
private:
    Bank* parameter(bool graphics, UINT index, D3D12_ROOT_PARAMETER_TYPE type) {
        auto& bank = banks_[graphics ? 1 : 0];
        if (!bank.layout || index >= bank.layout->parameters.size() || bank.layout->parameters[index].type != type) return nullptr;
        return &bank;
    }
    bool table_in_heap(D3D12_DESCRIPTOR_HEAP_TYPE type, uint64_t address, UINT span) const {
        const auto& heap = heaps_[type];
        if (!heap) return false;
        Microsoft::WRL::ComPtr<ID3D12Device> device;
        if (FAILED(heap->GetDevice(IID_PPV_ARGS(&device)))) return false;
        const auto stride = device->GetDescriptorHandleIncrementSize(type);
        const auto start = heap->GetGPUDescriptorHandleForHeapStart().ptr;
        if (!stride || address < start || (address - start) % stride) return false;
        const auto index = (address - start) / stride;
        const auto size = heap->GetDesc().NumDescriptors;
        return index < size && span <= size - index;
    }
    std::shared_ptr<Recording> recording_;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> list_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    std::array<Microsoft::WRL::ComPtr<ID3D12DescriptorHeap>, 2> heaps_;
    std::array<Bank, 2> banks_;
};
} // namespace addon_host
