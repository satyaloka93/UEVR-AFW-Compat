#include "../src/mods/uobjecthook/ArrayView.hpp"
#include <cstring>
#include <initializer_list>

using uobject_browser::ArrayView;
using uobject_browser::object_stride;
static_assert(ArrayView{0, 0, 0}.valid());
static_assert(!ArrayView{0, 1, 1}.valid());
static_assert(!ArrayView{16, -1, 1}.valid());
static_assert(!ArrayView{16, 2, 1}.valid());
static_assert(!ArrayView{16, 0, -1}.valid());
static_assert(ArrayView{16, 2, 2}.element(1, 8) == 24);
static_assert(ArrayView{16, 2, 2}.element(2, 8) == 0);
static_assert(ArrayView{16, 2, 2}.element(-1, 8) == 0);
static_assert(ArrayView{16, 2, 2}.element(0, 0) == 0);
static_assert(ArrayView{UINTPTR_MAX - 8, 2, 2}.element(0, 8) == 0);

int main() {
    // Native interface pointers MUST NOT become additional UObject entries.
    const uintptr_t interfaces[]{0x1000, 0x1010, 0, 0, 0x2000, 0x2010};
    const uintptr_t objects[]{0x1000, 0, 0x2000};
    for (bool is_interface : {false, true}) {
        const ArrayView view{reinterpret_cast<uintptr_t>(is_interface ? interfaces : objects), 3, 3};
        for (int i = 0; i < 3; ++i) {
            uintptr_t object{};
            std::memcpy(&object, reinterpret_cast<void*>(view.element(i, object_stride(is_interface))), sizeof(object));
            if (object != objects[i]) return 1;
        }
    }
    return 0;
}
