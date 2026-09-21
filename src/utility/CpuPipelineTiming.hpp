#pragma once
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <spdlog/spdlog.h>

namespace uevr::diagnostics {
// Caller-owned/thread-local counters. CPU wall time includes waits; it is not
// GPU busy time. No fences, flushes, extra jobs, or timing changes.
class CpuPipelineTiming {
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;
    void record(const char* scope, Time begin, Time stage1, Time stage2, Time end) {
        if (m_start == Time{}) m_start = begin;
        const auto ms = [](auto duration) { return std::chrono::duration<double, std::milli>(duration).count(); };
        const std::array<double, 3> values{ms(stage1 - begin), ms(stage2 - stage1), ms(end - stage2)};
        for (size_t i = 0; i < values.size(); ++i) { m_sum[i] += values[i]; m_max[i] = (std::max)(m_max[i], values[i]); }
        if (m_previous != Time{}) { m_intervals += ms(begin - m_previous); ++m_interval_count; }
        m_previous = begin;
        ++m_count;
        if (end - m_start < std::chrono::seconds(5)) return;
        spdlog::info("[UEVR CPU pipeline] {} tid={} samples={} stage_ms={:.3f}/{:.3f}/{:.3f} max_ms={:.3f}/{:.3f}/{:.3f} interval_ms={:.3f}",
            scope, GetCurrentThreadId(), m_count, m_sum[0]/m_count, m_sum[1]/m_count, m_sum[2]/m_count,
            m_max[0], m_max[1], m_max[2], m_interval_count ? m_intervals/m_interval_count : 0.0);
        *this = {};
    }
private:
    Time m_start{}, m_previous{};
    std::array<double, 3> m_sum{}, m_max{};
    uint64_t m_count{}, m_interval_count{};
    double m_intervals{};
};
}
