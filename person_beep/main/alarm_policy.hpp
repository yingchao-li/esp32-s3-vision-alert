#pragma once
#include <cstdint>
#include <cmath>

namespace person_beep {
constexpr float kConfidence = 0.80f;
constexpr int64_t kResultMaxAgeUs = 1500000;
constexpr int64_t kCooldownUs = 2000000;

inline bool accepted_person(int category, float score, bool valid_box) {
    return category == 0 && std::isfinite(score) && score >= kConfidence &&
           score <= 1.0f && valid_box;
}

struct Observation {
    bool healthy = false;
    bool person = false;
    int64_t frame_started_us = 0;
};

inline bool actionable(const Observation &o, int64_t now) {
    return o.healthy && o.person && o.frame_started_us > 0 &&
           now >= o.frame_started_us && now - o.frame_started_us <= kResultMaxAgeUs;
}

// Hardware RMT executes HIGH 200ms / LOW 200ms / HIGH 200ms / LOW.
// This state machine schedules one sequence and starts cooldown only AFTER
// hardware completion (or forced LOW). Requests are never queued.
class AlarmPolicy {
public:
    bool can_start(const Observation &o, int64_t now) const {
        return !running_ && now >= next_allowed_us_ && actionable(o, now);
    }
    void started() { running_ = true; }
    void stopped(int64_t now) {
        running_ = false;
        next_allowed_us_ = now + kCooldownUs;
    }
    bool running() const { return running_; }
private:
    bool running_ = false;
    int64_t next_allowed_us_ = 0;
};
}
