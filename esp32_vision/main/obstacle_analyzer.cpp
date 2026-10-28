#include "obstacle_analyzer.hpp"
namespace vision {
ClassId ObstacleAnalyzer::analyze(FrameResult &f, const Settings &s) {
    ClassId chosen = ClassId::None;
    std::array<ZoneState, kMaxObjects> next{};
    for (unsigned i = 0; i < f.count; ++i) {
        auto &t = f.tracks[i];
        int previous = -1;
        for (const auto &z : previous_)
            if (z.id == t.id)
                previous = z.zone;
        bool recent = f.captured_us >= t.seen_us && f.captured_us - t.seen_us <= 600000;
        bool inside = t.confirmed && recent && t.box.y2 >= s.roi_top && t.box.y2 <= s.roi_bottom;
        t.zone = inside ? (t.box.cx() < s.left_boundary ? 0 : t.box.cx() < s.right_boundary ? 1 : 2) : -1;
        t.event = t.zone == previous ? 0 : t.zone < 0 ? -1 : 1;
        t.risk = !inside ? 0 : t.box.y2 >= s.danger_y ? (t.zone == 1 ? 3 : 2) : 1;
        if (inside && t.area_rate > .15f)
            t.risk = std::min<unsigned>(3, t.risk + 1);
        next[i] = {t.id, t.zone};
        if (inside && valid_class(t.cls) &&
            (chosen == ClassId::None ||
             s.priority[static_cast<unsigned>(t.cls)] > s.priority[static_cast<unsigned>(chosen)]))
            chosen = t.cls;
    }
    previous_ = next;
    return chosen;
}
} // namespace vision
