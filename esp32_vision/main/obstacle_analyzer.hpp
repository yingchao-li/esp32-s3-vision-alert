#pragma once
#include "vision_types.hpp"
namespace vision {
class ObstacleAnalyzer {
  public:
    ClassId analyze(FrameResult &frame, const Settings &settings);

  private:
    struct ZoneState {
        uint32_t id = 0;
        int8_t zone = -1;
    };
    std::array<ZoneState, kMaxObjects> previous_{};
};
} // namespace vision
