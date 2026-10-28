#pragma once
#include "detection_filter.hpp"
#include "dl_detect_espdet_postprocessor.hpp"
#include "dl_detect_pico_postprocessor.hpp"
#include "dl_detect_yolo11_postprocessor.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_model_base.hpp"
#include "runtime.hpp"
#include <memory>
namespace vision {
class ModelManager {
  public:
    bool init();
    void clear() {
        post_.reset();
        pre_.reset();
        model_.reset();
        candidates_.reset();
    }
    void run(const FrameJob &frame, const Settings &settings, AiPacket &output);
    static bool four_classes();
    static bool three_classes();
    static const char *family();
    static const char *name();

  private:
    std::unique_ptr<dl::Model> model_;
    std::unique_ptr<dl::image::ImagePreprocessor> pre_;
    std::unique_ptr<dl::detect::DetectPostprocessor> post_;
    std::unique_ptr<CandidatePool> candidates_;
};
} // namespace vision
