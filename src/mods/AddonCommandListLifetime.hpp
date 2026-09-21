#pragma once

#include <d3d12.h>
#include <wrl/client.h>
#include <array>
#include <cstdint>
#include <cstring>
#include <functional>
#include <exception>
#include <map>
#include <memory>
#include <mutex>

namespace addon_host {
// Lifetime storage only: NOT a ReShade command_list ABI and never publishable
// to RenoDX directly. A complete command-list adapter must own one of these.
class CommandListIdentity {
public:
    explicit CommandListIdentity(ID3D12GraphicsCommandList* list) : native_(list) {
        if (list) list->GetDevice(IID_PPV_ARGS(&device_));
    }
    ID3D12GraphicsCommandList* native() const { return native_.Get(); }
    ID3D12Device* device() const { return device_.Get(); }
    uint64_t get_private(const uint8_t* guid) const {
        if (!guid) return 0;
        std::lock_guard lock(mutex_);
        Key key{}; std::memcpy(key.data(), guid, key.size());
        const auto it = private_.find(key);
        return it == private_.end() ? 0 : it->second;
    }
    void set_private(const uint8_t* guid, uint64_t value) {
        if (!guid) return;
        std::lock_guard lock(mutex_);
        Key key{}; std::memcpy(key.data(), guid, key.size());
        if (value) private_[key] = value;
        else private_.erase(key);
    }
private:
    using Key = std::array<uint8_t, 16>;
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> native_;
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    mutable std::mutex mutex_;
    std::map<Key, uint64_t> private_;
};

// Bounded retention prevents native pointer reuse until paired destruction.
// Leases cover the entire addon callback, not merely the registry lookup.
// stop() refuses teardown while any lease is active; the caller must retry
// BEFORE destroying the addon/device. It never waits while holding host locks.
class CommandListLifetimes {
public:
    using Lease = std::shared_ptr<CommandListIdentity>;
    using Init = std::function<bool(CommandListIdentity&)>;
    using Destroy = std::function<void(CommandListIdentity&)>;
    CommandListLifetimes(ID3D12Device* device, size_t capacity, Init init, Destroy destroy)
        : device_(device), capacity_(capacity), init_(std::move(init)), destroy_(std::move(destroy)) {}
    CommandListLifetimes(const CommandListLifetimes&) = delete;
    CommandListLifetimes& operator=(const CommandListLifetimes&) = delete;
    // Destroying the registry with outstanding leases is a programming error:
    // releasing tagged native objects without paired callbacks is unsafe.
    ~CommandListLifetimes() { if (!stop()) std::terminate(); }

    Lease acquire(ID3D12GraphicsCommandList* native) {
        std::lock_guard lock(mutex_);
        if (stopped_ || initializing_ || !native || !device_) return {};
        Microsoft::WRL::ComPtr<IUnknown> identity;
        if (FAILED(native->QueryInterface(IID_PPV_ARGS(&identity)))) return {};
        const auto found = lists_.find(identity.Get());
        if (found != lists_.end()) return found->second;
        if (lists_.size() >= capacity_) return {};
        auto item = std::make_shared<CommandListIdentity>(native);
        Microsoft::WRL::ComPtr<IUnknown> expected, actual;
        if (!item->device() || FAILED(device_.As(&expected)) ||
            FAILED(item->device()->QueryInterface(IID_PPV_ARGS(&actual))) || actual != expected)
            return {};
        // Callbacks must catch foreign SEH themselves. Destroy must not throw.
        // Reentrant acquire is rejected rather than publishing a partial object.
        initializing_ = true;
        bool ok = false;
        try { ok = init_ && init_(*item); }
        catch (...) {
            initializing_ = false;
            if (destroy_) destroy_(*item);
            throw;
        }
        initializing_ = false;
        if (!ok) {
            if (destroy_) destroy_(*item); // unwind a partially completed init
            return {};
        }
        try { lists_.emplace(identity.Get(), item); }
        catch (...) {
            if (destroy_) destroy_(*item);
            throw;
        }
        return item;
    }
    bool stop() {
        std::lock_guard lock(mutex_);
        if (initializing_) return false;
        stopped_ = true;
        for (const auto& entry : lists_)
            if (entry.second.use_count() != 1) return false;
        for (auto& entry : lists_)
            if (destroy_) destroy_(*entry.second);
        lists_.clear();
        return true;
    }
    size_t size() const { std::lock_guard lock(mutex_); return lists_.size(); }
private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    size_t capacity_;
    Init init_;
    Destroy destroy_;
    mutable std::recursive_mutex mutex_;
    bool stopped_{}, initializing_{};
    std::map<IUnknown*, Lease> lists_;
};
} // namespace addon_host
