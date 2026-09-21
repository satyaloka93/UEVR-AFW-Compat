#pragma once

#include <cstdint>

namespace runtimes::submission {
// A pose describes the image actually rendered: never replace it with a newly
// located pose just to repair the presentation timestamp. Future pipelined
// predictions remain valid; only expired Native predictions are clamped.
constexpr int64_t display_time(int64_t pipeline, int64_t speculative,
                              int64_t waited, bool native, bool avowed) {
    const auto candidate = pipeline != 0 ? pipeline : speculative;
    if (waited > 0 && (avowed || (native && candidate < waited))) {
        return waited;
    }
    return candidate;
}
}
