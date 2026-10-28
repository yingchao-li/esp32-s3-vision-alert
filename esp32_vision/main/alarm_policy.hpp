#pragma once
#include "vision_types.hpp"
namespace vision {
class AlarmPolicy {
  public:
    bool may_start(ClassId cls, int64_t now, int64_t fresh, bool healthy, const Settings &s) const {
        return !running_ && s.buzzer_enabled && s.ai_enabled && healthy && valid_class(cls) && fresh > 0 &&
               now >= fresh && now - fresh <= 2000000 && now >= next_start_;
    }
    void started(ClassId cls, int64_t now, uint32_t interval) {
        running_ = true;
        active_ = cls;
        next_start_ = now + int64_t(interval) * 1000;
    }
    void stopped() {
        running_ = false;
        active_ = ClassId::None;
    }
    bool running() const { return running_; }
    bool ready(int64_t now) const { return !running_ && now >= next_start_; }
    ClassId active() const { return active_; }
    static unsigned count(ClassId c) { return valid_class(c) ? static_cast<unsigned>(c) + 1 : 0; }

  private:
    bool running_ = false;
    ClassId active_ = ClassId::None;
    int64_t next_start_ = 0;
};
struct Pulse {
    uint8_t level;
    uint16_t duration;
};
// RMT at 1MHz; split phases into <=30ms halves to fit 15-bit duration fields.
inline size_t waveform(ClassId c, uint32_t beep_ms, uint32_t gap_ms, Pulse *out, size_t capacity) {
    size_t n = 0;
    unsigned beeps = AlarmPolicy::count(c);
    if (!beeps || beep_ms < 30 || beep_ms > 500 || gap_ms < 30 || gap_ms > 500)
        return 0;
    auto phase = [&](unsigned level, uint32_t ms) {
        uint32_t us = ms * 1000;
        while (us) {
            if (n >= capacity)
                return false;
            auto part = std::min<uint32_t>(30000, us);
            out[n++] = {static_cast<uint8_t>(level), static_cast<uint16_t>(part)};
            us -= part;
        }
        return true;
    };
    for (unsigned i = 0; i < beeps; ++i) {
        if (!phase(1, beep_ms))
            return 0;
        if (i + 1 < beeps && !phase(0, gap_ms))
            return 0;
    }
    if (n >= capacity)
        return 0;
    out[n++] = {0, 1};
    return n;
}
} // namespace vision
