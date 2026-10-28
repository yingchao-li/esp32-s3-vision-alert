#include "object_tracker.hpp"

namespace vision {
void ObjectTracker::reset() { tracks_ = {}; } // Do not reuse IDs after settings/AI reset.
void ObjectTracker::update(const Detection *d, size_t n, int64_t now, uint16_t confirm, FrameResult &out) {
    n = std::min(n, kMaxObjects);
    std::array<bool, kMaxObjects> used{};
    for (auto &t : tracks_) {
        if (t.id && (now < t.seen_us || now - t.seen_us > 2000000))
            t = {};
        t.matched = false;
        t.event = 0;
    }
    // Global greedy matching, class gated, to constant-velocity predictions.
    for (size_t step = 0; step < kMaxObjects; ++step) {
        float best = .20f;
        int ti = -1, di = -1;
        for (size_t i = 0; i < tracks_.size(); ++i) {
            auto &t = tracks_[i];
            if (!t.id || t.matched)
                continue;
            float dt = std::min(.5f, std::max(0.f, (now - t.seen_us) / 1000000.f));
            Box predicted = t.box;
            predicted.x1 += t.vx * dt;
            predicted.x2 += t.vx * dt;
            predicted.y1 += t.vy * dt;
            predicted.y2 += t.vy * dt;
            for (size_t j = 0; j < n; ++j) {
                if (used[j] || d[j].cls != t.cls || !d[j].box.valid())
                    continue;
                float score = iou(predicted, d[j].box);
                float dx = predicted.cx() - d[j].box.cx(), dy = predicted.cy() - d[j].box.cy();
                float ratio = d[j].box.area() / std::max(t.box.area(), .00001f);
                if (score < .2f && dx * dx + dy * dy < .01f && ratio > .5f && ratio < 2.f)
                    score = .21f;
                if (score > best) {
                    best = score;
                    ti = i;
                    di = j;
                }
            }
        }
        if (ti < 0)
            break;
        auto &t = tracks_[ti];
        const auto &o = d[di];
        float dt = std::max(.001f, (now - t.seen_us) / 1000000.f);
        t.vx = .6f * t.vx + .4f * (o.box.cx() - t.box.cx()) / dt;
        t.vy = .6f * t.vy + .4f * (o.box.cy() - t.box.cy()) / dt;
        t.area_rate = .6f * t.area_rate + .4f * (o.box.area() - t.box.area()) / dt;
        auto blend = [](float a, float b) { return .25f * a + .75f * b; };
        t.box = {blend(t.box.x1, o.box.x1), blend(t.box.y1, o.box.y1), blend(t.box.x2, o.box.x2),
                 blend(t.box.y2, o.box.y2)};
        t.score = o.score;
        t.seen_us = now;
        t.hits = std::min<unsigned>(65535, t.hits + 1);
        t.matched = true;
        t.confirmed = t.hits >= confirm;
        used[di] = true;
    }
    for (size_t j = 0; j < n; ++j) {
        if (used[j] || !valid_class(d[j].cls) || !d[j].box.valid() || !std::isfinite(d[j].score))
            continue;
        for (auto &t : tracks_)
            if (!t.id) {
                t = {};
                t.id = next_id_++;
                if (!next_id_)
                    next_id_ = 1;
                t.cls = d[j].cls;
                t.score = d[j].score;
                t.box = d[j].box;
                t.seen_us = now;
                t.hits = 1;
                t.matched = true;
                t.confirmed = confirm <= 1;
                break;
            }
    }
    out.count = 0;
    for (auto &t : tracks_)
        if (t.id) {
            // A miss resets tentative consecutive-frame confirmation; confirmed
            // tracks retain ID through short occlusion, for up to two seconds.
            if (!t.matched && !t.confirmed)
                t.hits = 0;
            out.tracks[out.count++] = t;
        }
}
} // namespace vision
