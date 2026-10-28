#pragma once
#include "vision_types.hpp"

namespace vision {
class ObjectTracker {
  public:
    void reset();
    void update(const Detection *detections, size_t n, int64_t now, uint16_t confirmation, FrameResult &out);

  private:
    std::array<Track, kMaxObjects> tracks_{};
    uint32_t next_id_ = 1;
};
} // namespace vision
