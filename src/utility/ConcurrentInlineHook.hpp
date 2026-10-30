#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <safetyhook.hpp>

namespace utility {
// One installation per owner: no live rehook/reset. A dispatch lease owns the
// trampoline, without holding SafetyHook's mutex across the original function.
// As with the rest of the backend, destruction requires callers to stop entering
// the detour first; this does not make unloading executing backend code safe.
class ConcurrentInlineHook {
public:
    using Lease = std::shared_ptr<const safetyhook::InlineHook>;

    ~ConcurrentInlineHook() {
        if (auto hook = m_hook.load()) {
            // Disable before releasing the registry's ownership. Existing leases
            // keep the trampoline allocated until their original calls return.
            (void)hook->disable();
        }
    }

    std::optional<std::string> install(void* target, void* detour) {
        std::scoped_lock lock{m_install_mutex};
        if (m_hook.load()) {
            return "hook already published; live replacement is unsupported";
        }
        auto result = safetyhook::InlineHook::create(target, detour,
            safetyhook::InlineHook::StartDisabled);
        if (!result) {
            return "create failed: " + std::to_string(static_cast<int>(result.error().type));
        }
        auto hook = std::make_shared<safetyhook::InlineHook>(std::move(*result));
        // A thread can enter the detour as soon as enable patches the target.
        // Publish the complete owner before that can happen.
        m_hook.store(hook, std::memory_order_release);
        if (auto enabled = hook->enable(); !enabled) {
            return "enable failed: " + std::to_string(static_cast<int>(enabled.error().type));
        }
        return std::nullopt;
    }

    [[nodiscard]] Lease acquire() const {
        return m_hook.load(std::memory_order_acquire);
    }

private:
    std::mutex m_install_mutex;
    std::atomic<std::shared_ptr<safetyhook::InlineHook>> m_hook{};
};
}
