#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace uobject_browser {
// Non-owning engine TArray header. Never copy an owning sdk::TArray here.
struct ArrayView {
    uintptr_t data;
    int32_t count;
    int32_t capacity;

    constexpr bool valid() const {
        return count >= 0 && capacity >= count && (count == 0 || data != 0);
    }

    constexpr uintptr_t element(int32_t index, size_t stride) const {
        if (!valid() || index < 0 || index >= count || stride == 0 ||
            static_cast<size_t>(count) > (std::numeric_limits<uintptr_t>::max() - data) / stride) {
            return 0;
        }
        return data + static_cast<size_t>(index) * stride;
    }
};

constexpr size_t object_stride(bool is_interface) {
    // FScriptInterface contains ObjectPointer followed by InterfacePointer.
    return sizeof(void*) * (is_interface ? 2 : 1);
}
}
