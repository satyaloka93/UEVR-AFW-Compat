#pragma once

namespace vrmod {
// Exact formats are conservative: compatible typeless families may also be
// copied, but AFW owns its targets and can allocate the source format directly.
template <typename Desc>
constexpr bool afw_copy_layout_matches(const Desc& a, const Desc& b) {
    return a.Dimension == b.Dimension && a.Width == b.Width &&
        a.Height == b.Height && a.DepthOrArraySize == b.DepthOrArraySize &&
        a.MipLevels == b.MipLevels && a.Format == b.Format &&
        a.SampleDesc.Count == b.SampleDesc.Count &&
        a.SampleDesc.Quality == b.SampleDesc.Quality;
}
}
