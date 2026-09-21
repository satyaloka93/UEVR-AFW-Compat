#pragma once
#include <algorithm>
#include <cmath>

namespace vrmod {
// Rasterize the same lerp(max(abs(p)), length(p), roundness) contour as
// Cheeky's composite shader. Clipping must not squeeze an edge ellipse.
template<class Emit>
void afw_border_rectangles(const float* b, int width, int height, Emit emit) {
    if (!b || width <= 0 || height <= 0 || width > 16384 || height > 16384) return;
    for (int i = 0; i < 5; ++i) if (!std::isfinite(b[i])) return;
    if (b[0] < -2 || b[1] < -2 || b[2] > 3 || b[3] > 3 || b[2] <= b[0] || b[3] <= b[1]) return;
    const float cx = (b[0] + b[2]) * width * .5f, cy = (b[1] + b[3]) * height * .5f;
    const float rx = (b[2] - b[0]) * width * .5f, ry = (b[3] - b[1]) * height * .5f;
    if (rx < 1 || ry < 1) return;
    const float round = std::clamp(b[4], 0.f, 1.f);
    const float thickness = 1.5f / (std::min)(rx, ry);
    const auto span = [&](float y, float radius) {
        if (y > radius || radius <= 0) return -1.f;
        if (round == 1.f) return std::sqrt((std::max)(0.f, radius*radius - y*y)) * rx;
        if (round == 0.f) return radius * rx;
        float lo = 0, hi = radius;
        for (int i = 0; i < 16; ++i) {
            const float x = (lo + hi) * .5f;
            const float d = (1 - round) * (std::max)(x, y) + round * std::sqrt(x*x + y*y);
            if (d <= radius) lo = x; else hi = x;
        }
        return lo * rx;
    };
    const auto rect = [&](float left, float right, int y) {
        const int l = std::clamp(static_cast<int>(std::floor(left)), 0, width);
        const int r = std::clamp(static_cast<int>(std::ceil(right)), 0, width);
        if (l < r) emit(l, y, r, y + 1);
    };
    const int top = std::clamp(static_cast<int>(std::floor(cy - ry * (1 + thickness))), 0, height);
    const int bottom = std::clamp(static_cast<int>(std::ceil(cy + ry * (1 + thickness))), 0, height);
    for (int y = top; y < bottom; ++y) {
        const float py = std::abs(y + .5f - cy) / ry;
        const float outer = span(py, 1 + thickness), inner = span(py, 1 - thickness);
        if (outer < 0) continue;
        if (inner < 0) rect(cx - outer, cx + outer, y);
        else { rect(cx - outer, cx - inner, y); rect(cx + inner, cx + outer, y); }
    }
}
}
