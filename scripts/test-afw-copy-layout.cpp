#include "../src/mods/vr/AfwCopyLayout.hpp"

struct Desc {
    int Dimension = 3;
    unsigned long long Width = 2676;
    unsigned Height = 2728;
    unsigned short DepthOrArraySize = 1, MipLevels = 1;
    int Format = 34;
    struct { unsigned Count = 1, Quality = 0; } SampleDesc;
};

constexpr bool test() {
    const Desc source{};
    if (!vrmod::afw_copy_layout_matches(source, source)) return false;
    for (int field = 0; field < 8; ++field) {
        auto destination = source;
        switch (field) {
        case 0: ++destination.Dimension; break;
        case 1: ++destination.Width; break;
        case 2: ++destination.Height; break;
        case 3: ++destination.DepthOrArraySize; break;
        case 4: ++destination.MipLevels; break;
        case 5: destination.Format = 16; break; // R32G32_FLOAT vs R16G16_FLOAT
        case 6: ++destination.SampleDesc.Count; break;
        case 7: ++destination.SampleDesc.Quality; break;
        }
        if (vrmod::afw_copy_layout_matches(source, destination)) return false;
    }
    return true;
}
static_assert(test());
int main() { return test() ? 0 : 1; }
