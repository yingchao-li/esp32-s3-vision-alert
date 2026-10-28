#pragma once
#include <memory>
#include "dl_model_base.hpp"
#include "dl_image_preprocessor.hpp"
#include "dl_detect_pico_postprocessor.hpp"

class PersonDetector {
public:
    bool init();
    std::list<dl::detect::result_t> &run(const dl::image::img_t &image,
                                      double &pre_ms, double &model_ms, double &post_ms);
private:
    std::unique_ptr<dl::Model> model_;
    std::unique_ptr<dl::image::ImagePreprocessor> pre_;
    std::unique_ptr<dl::detect::PicoPostprocessor> post_;
};
