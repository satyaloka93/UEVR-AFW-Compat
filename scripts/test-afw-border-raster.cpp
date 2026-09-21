#include "../src/mods/vr/AfwBorderRaster.hpp"
#include <cassert>
#include <limits>
#include <vector>
int main() {
    struct Rect { int l,t,r,b; };
    std::vector<Rect> rects;
    auto emit = [&](int l,int t,int r,int b) { assert(l >= 0 && t >= 0 && r <= 100 && b <= 100 && l < r && t < b); rects.push_back({l,t,r,b}); };
    float bounds[]{.2f,.2f,.8f,.8f,1.f};
    vrmod::afw_border_rectangles(bounds,100,100,emit);
    assert(!rects.empty());
    for (auto r : rects) assert(!(r.l <= 50 && r.r > 50 && r.t <= 50 && r.b > 50));
    rects.clear(); bounds[0] = -.2f; bounds[2] = .4f;
    vrmod::afw_border_rectangles(bounds,100,100,emit); assert(!rects.empty());
    rects.clear(); bounds[0] = std::numeric_limits<float>::quiet_NaN();
    vrmod::afw_border_rectangles(bounds,100,100,emit); assert(rects.empty());
    bounds[0] = .5f; bounds[2] = .4f;
    vrmod::afw_border_rectangles(bounds,100,100,emit); assert(rects.empty());
}
