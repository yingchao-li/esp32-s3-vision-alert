#pragma once
#include "vision_types.hpp"
namespace vision {
inline unsigned mode_limit(Mode m) { return m == Mode::Accuracy ? 24 : m == Mode::Balanced ? 16 : 8; }
inline float mode_nms(Mode m) { return m == Mode::Accuracy ? .60f : m == Mode::Balanced ? .50f : .40f; }
struct CandidatePool {
    std::array<Detection, 128> values{};
    std::array<unsigned, 4> counts{};
    void clear() { counts = {}; }
    void add(const Detection &d) {
        if (!valid_class(d.cls) || !d.box.valid() || !std::isfinite(d.score))
            return;
        unsigned c = static_cast<unsigned>(d.cls), base = 32 * c;
        if (counts[c] < 32)
            values[base + counts[c]++] = d;
        else {
            unsigned lowest = base;
            for (unsigned j = 1; j < 32; ++j)
                if (values[base + j].score < values[lowest].score)
                    lowest = base + j;
            if (d.score > values[lowest].score)
                values[lowest] = d;
        }
    }
    size_t compact() {
        size_t n = 0;
        for (unsigned c = 0; c < 4; ++c)
            for (unsigned i = 0; i < counts[c]; ++i)
                values[n++] = values[c * 32 + i];
        return n;
    }
};
inline size_t class_aware_nms(Detection *candidates, size_t count, Detection *out, size_t limit,
                              float threshold) {
    std::sort(candidates, candidates + count,
              [](const Detection &a, const Detection &b) { return a.score > b.score; });
    size_t n = 0;
    for (size_t i = 0; i < count && n < limit; ++i) {
        auto &d = candidates[i];
        bool keep = d.box.valid() && valid_class(d.cls) && std::isfinite(d.score);
        for (size_t j = 0; j < n && keep; ++j)
            if (d.cls == out[j].cls && iou(d.box, out[j].box) > threshold)
                keep = false;
        if (keep)
            out[n++] = d;
    }
    return n;
}
} // namespace vision
