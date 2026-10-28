#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
namespace vision {
struct Distribution {
    uint32_t count = 0;
    float mean = 0, p50 = 0, p95 = 0, p99 = 0, max = 0;
};
template <size_t N = 128> class WindowStats {
  public:
    void add(float v) {
        if (!std::isfinite(v) || v < 0)
            return;
        v_[cursor_++ % N] = v;
        count_ = std::min<size_t>(N, count_ + 1);
    }
    Distribution summarize() const {
        Distribution d;
        d.count = count_;
        if (!count_)
            return d;
        auto copy = v_;
        std::sort(copy.begin(), copy.begin() + count_);
        for (size_t i = 0; i < count_; ++i)
            d.mean += copy[i];
        d.mean /= count_;
        auto p = [&](float q) { return copy[static_cast<size_t>(std::ceil(q * count_)) - 1]; };
        d.p50 = p(.50f);
        d.p95 = p(.95f);
        d.p99 = p(.99f);
        d.max = copy[count_ - 1];
        return d;
    }

  private:
    std::array<float, N> v_{};
    size_t cursor_ = 0, count_ = 0;
};
} // namespace vision
