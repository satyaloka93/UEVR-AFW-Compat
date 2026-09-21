#include "../src/mods/vr/runtimes/OpenXRSubmissionPolicy.hpp"

using runtimes::submission::display_time;
// Native stale/missing/negative predictions, equal and future predictions.
static_assert(display_time(100, 160, 140, true, false) == 140);
static_assert(display_time(0, 100, 140, true, false) == 140);
static_assert(display_time(-1, 160, 140, true, false) == 140);
static_assert(display_time(140, 160, 140, true, false) == 140);
static_assert(display_time(160, 180, 140, true, false) == 160);
static_assert(display_time(0, 160, 140, true, false) == 160);
// No valid wait: no invented clock. AFW/AFR/Sequential paths unchanged.
static_assert(display_time(100, 160, 0, true, false) == 100);
static_assert(display_time(100, 160, 140, false, false) == 100);
static_assert(display_time(0, 160, 140, false, false) == 160);
// Preserve Avowed's existing authoritative-time policy in every mode.
static_assert(display_time(160, 180, 140, true, true) == 140);
static_assert(display_time(160, 180, 140, false, true) == 140);
static_assert(display_time(100, 160, 0, false, true) == 100);
// Regression sequence: mode changes must not retain a repair latch.
constexpr bool transitions() {
    for (int i = 0; i < 10000; ++i) {
        const int64_t wait = 1000000000LL + i * 11111111LL;
        const auto stale = wait - 44444444LL;
        if (display_time(stale, wait + 11111111, wait, true, false) != wait) return false;
        if (display_time(stale, wait + 11111111, wait, false, false) != stale) return false;
    }
    return true;
}
static_assert(transitions());
int main() { return transitions() ? 0 : 1; }
