#include "detector.hpp"
#include "alarm_policy.hpp"
#include <cstring>
#include <new>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"

extern "C" const uint8_t person_model_start[];
extern "C" const uint8_t person_model_end[];

bool PersonDetector::init() {
    constexpr uint8_t expected[32] = {
        0x3e,0xb5,0x82,0x7e,0x12,0x32,0x27,0x28,0xfe,0x99,0xea,0x9c,0x86,0xd7,0x1e,0xa3,
        0x8a,0xa5,0xa5,0xb7,0x00,0x64,0xab,0xf7,0xd7,0x5b,0xc9,0x01,0x40,0xb7,0x40,0x15};
    const size_t length = person_model_end - person_model_start;
    uint8_t digest[32];
    if (length != 435328 || reinterpret_cast<uintptr_t>(person_model_start) % 16 != 0 ||
        mbedtls_sha256(person_model_start, length, digest, 0) != 0 ||
        std::memcmp(digest, expected, sizeof(expected)) != 0) {
        ESP_LOGE("MODEL", "Model missing, misaligned or SHA256 mismatch; alarm disabled");
        return false;
    }
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < 2 * 1024 * 1024 ||
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 120 * 1024) {
        ESP_LOGE("MODEL", "Insufficient contiguous PSRAM/internal RAM; alarm disabled");
        return false;
    }
    model_.reset(new (std::nothrow) dl::Model(reinterpret_cast<const char *>(person_model_start),
                 fbs::MODEL_LOCATION_IN_FLASH_RODATA, 64 * 1024));
    if (!model_ || model_->get_inputs().size() != 1 || model_->get_outputs().size() != 6) {
        ESP_LOGE("MODEL", "Graph load/build failed; alarm disabled");
        return false;
    }
    const auto *input = model_->get_inputs().begin()->second;
    if (!input || !input->data || input->shape != std::vector<int>({1,224,224,3}) ||
        input->dtype != dl::DATA_TYPE_INT8) {
        ESP_LOGE("MODEL", "Unexpected input tensor; expected NHWC 1x224x224x3 INT8");
        return false;
    }
    for (int i = 0; i < 3; ++i) {
        for (const char *prefix : {"score", "bbox"}) {
            auto *out = model_->get_output(std::string(prefix) + std::to_string(i));
            const int edge = 28 >> i;
            if (!out || !out->data || out->shape != std::vector<int>({1,edge,edge,
                    std::strcmp(prefix, "score") == 0 ? 1 : 32})) {
                ESP_LOGE("MODEL", "Unexpected PicoDet output tensor");
                return false;
            }
        }
    }
    // Same normalization, DFL anchors and NMS as Espressif pedestrian_detect 0.3.2.
    model_->minimize();
    pre_.reset(new (std::nothrow) dl::image::ImagePreprocessor(model_.get(), {0,0,0}, {1,1,1}));
    if (!pre_) return false;
    post_.reset(new (std::nothrow) dl::detect::PicoPostprocessor(model_.get(), pre_.get(),
        person_beep::kConfidence, 0.5f, 10, {{8,8,4,4}, {16,16,8,8}, {32,32,16,16}}));
    if (!post_) return false;
    model_->profile_memory();
    ESP_LOGI("MODEL", "Person detector loaded: PicoDet S8 v1, INT8 224x224, threshold=%.2f",
             static_cast<double>(person_beep::kConfidence));
    return true;
}

std::list<dl::detect::result_t> &PersonDetector::run(const dl::image::img_t &image,
                                                  double &pre_ms, double &model_ms, double &post_ms) {
    int64_t start = esp_timer_get_time();
    pre_->preprocess(image);
    int64_t after_pre = esp_timer_get_time();
    model_->run(dl::RUNTIME_MODE_SINGLE_CORE);
    int64_t after_model = esp_timer_get_time();
    post_->clear_result();
    post_->postprocess();
    auto &results = post_->get_result(image.width, image.height);
    int64_t end = esp_timer_get_time();
    pre_ms = (after_pre - start) / 1000.0;
    model_ms = (after_model - after_pre) / 1000.0;
    post_ms = (end - after_model) / 1000.0;
    return results;
}
