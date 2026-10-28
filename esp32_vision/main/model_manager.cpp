#include "model_manager.hpp"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "model_spec.hpp"
#include <cstring>
#include <new>
extern "C" const uint8_t vision_model_start[], vision_model_end[];
namespace vision {
namespace {
void log_memory(const char *stage) {
    constexpr uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    constexpr uint32_t simd = internal | MALLOC_CAP_SIMD;
    constexpr uint32_t psram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT | MALLOC_CAP_SIMD;
    ESP_LOGI("MODEL",
             "%s: internal free=%u largest=%u; SIMD internal free=%u largest=%u; "
             "SIMD PSRAM free=%u largest=%u",
             stage, unsigned(heap_caps_get_free_size(internal)),
             unsigned(heap_caps_get_largest_free_block(internal)), unsigned(heap_caps_get_free_size(simd)),
             unsigned(heap_caps_get_largest_free_block(simd)), unsigned(heap_caps_get_free_size(psram)),
             unsigned(heap_caps_get_largest_free_block(psram)));
}
bool fail(const char *stage, esp_err_t error) {
    char reason[96];
    std::snprintf(reason, sizeof(reason), "Model init/%s: %s (0x%x)", stage, esp_err_to_name(error),
                  unsigned(error));
    health_error(reason);
    return false;
}
} // namespace
bool ModelManager::four_classes() { return kFourClassModel; }
bool ModelManager::three_classes() { return kYolo11CocoModel; }
const char *ModelManager::family() { return kModelFamily; }
const char *ModelManager::name() { return kModelName; }
bool ModelManager::init() {
    uint8_t digest[32];
    char hex[65];
    const size_t bytes = uintptr_t(vision_model_end) - uintptr_t(vision_model_start);
    ESP_LOGI("MODEL", "init/verify: rodata=%p, bytes=%u expected=%u; ESP-DL=3.3.14; family=%s; %s",
             vision_model_start, unsigned(bytes), unsigned(kModelBytes), kModelFamily, name());
    if (bytes != kModelBytes || uintptr_t(vision_model_start) % 16)
        return fail("rodata size/alignment", ESP_ERR_INVALID_SIZE);
    const int sha_result = mbedtls_sha256(vision_model_start, bytes, digest, 0);
    if (sha_result) {
        ESP_LOGE("MODEL", "mbedtls_sha256 returned %d", sha_result);
        return fail("SHA256 calculation", ESP_FAIL);
    }
    for (unsigned i = 0; i < 32; ++i)
        std::snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    if (std::strcmp(hex, kModelSha256)) {
        return fail("SHA256 mismatch", ESP_ERR_INVALID_CRC);
    }
    ESP_LOGI("MODEL", "init/verify: SHA256=%s PASS", hex);
    log_memory("before constructor");
    if (heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) < 2 * 1024 * 1024 ||
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < 100 * 1024) {
        return fail("memory preflight", ESP_ERR_NO_MEM);
    }
    // IDF 5.4.2's reserved DMA pool lacks MALLOC_CAP_SIMD. A generic internal
    // free-block check cannot justify a 32KB ESP-DL SIMD arena. Use the supported
    // PSRAM activation arena; retain internal memory for camera DMA, WiFi/stacks.
    constexpr int internal_arena_bytes = 0;
    // The 2.86MB YOLO11 COCO parameters stay in flash (param_copy=false) because
    // the 8MB PSRAM must also hold the ~6MB activation arena plus camera buffers.
    // The smaller baseline/four-class models keep the previous copy behaviour.
    const bool param_copy = !kYolo11CocoModel;
    ESP_LOGI("MODEL", "init/construct: PSRAM activation arena; internal budget=%d, param_copy=%s",
             internal_arena_bytes, param_copy ? "true" : "false");
    model_.reset(new (std::nothrow) dl::Model(reinterpret_cast<const char *>(vision_model_start),
                                              fbs::MODEL_LOCATION_IN_FLASH_RODATA, internal_arena_bytes,
                                              dl::MEMORY_MANAGER_GREEDY, nullptr, param_copy));
    log_memory("after constructor");
    if (!model_)
        return fail("model object allocation", ESP_ERR_NO_MEM);
    ESP_LOGI("MODEL", "init/graph: inputs=%u outputs=%u", unsigned(model_->get_inputs().size()),
             unsigned(model_->get_outputs().size()));
    if (model_->get_inputs().size() != 1 || model_->get_outputs().size() != 6)
        return fail("load/planner; see native ESP-DL log", ESP_FAIL);
    auto *in = model_->get_inputs().begin()->second;
    if (!in || !in->data || in->shape != std::vector<int>({1, kModelHeight, kModelWidth, 3}) ||
        in->dtype != dl::DATA_TYPE_INT8)
        return fail("input tensor contract/storage", ESP_ERR_INVALID_STATE);
    ESP_LOGI("MODEL", "init/input: INT8 [1,%d,%d,3] data=%p", kModelHeight, kModelWidth, in->data);
    // ESP-DL output naming: PicoDet uses bbox0..2 (32 channels, DFL), ESPDet and
    // YOLO11 use box0..2. YOLO11 also has 64 box channels (4 sides * reg_max 16)
    // and one score channel per COCO category.
    const int score_channels = kFourClassModel ? 4 : (kYolo11CocoModel ? kModelClassCount : 1);
    const int box_channels = kFourClassModel ? 4 : (kYolo11CocoModel ? 64 : 32);
    const bool box_named_box = kFourClassModel || kYolo11CocoModel;
    for (int i = 0; i < 3; ++i) {
        auto *score = model_->get_output("score" + std::to_string(i));
        auto *box = model_->get_output(std::string(box_named_box ? "box" : "bbox") + std::to_string(i));
        int h = kModelHeight / (8 << i), w = kModelWidth / (8 << i);
        if (!score || !box || !score->data || !box->data || score->dtype != dl::DATA_TYPE_INT8 ||
            box->dtype != dl::DATA_TYPE_INT8 ||
            score->shape != std::vector<int>({1, h, w, score_channels}) ||
            box->shape != std::vector<int>({1, h, w, box_channels}))
            return fail("output tensor contract/storage", ESP_ERR_INVALID_STATE);
        ESP_LOGI("MODEL", "init/output %d: score [1,%d,%d,%d] data=%p; box [1,%d,%d,%d] data=%p", i, h, w,
                 score_channels, score->data, h, w, box_channels, box->data);
    }
    model_->minimize();
    const std::array<float, 3> mean{0, 0, 0};
    const std::array<float, 3> stddev = (kFourClassModel || kYolo11CocoModel)
                                            ? std::array<float, 3>{255, 255, 255}
                                            : std::array<float, 3>{1, 1, 1};
    pre_.reset(new (std::nothrow) dl::image::ImagePreprocessor(model_.get(), mean, stddev));
    if (!pre_)
        return fail("preprocessor allocation", ESP_ERR_NO_MEM);
    if (kFourClassModel) {
        pre_->enable_letterbox({114, 114, 114});
        candidates_.reset(new (std::nothrow) CandidatePool());
        if (!candidates_)
            return fail("candidate pool allocation", ESP_ERR_NO_MEM);
    } else if (kYolo11CocoModel) {
        // Matches the official ESP-DL coco_detect example: RGB /255, letterbox
        // padding 114, three strides, class-agnostic stock NMS with top_k cap.
        pre_->enable_letterbox({114, 114, 114});
        post_.reset(new (std::nothrow) dl::detect::yolo11PostProcessor(
            model_.get(), pre_.get(), .25f, .7f, kMaxObjects,
            {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}}));
        if (!post_)
            return fail("YOLO11 postprocessor allocation", ESP_ERR_NO_MEM);
    } else {
        post_.reset(new (std::nothrow)
                        dl::detect::PicoPostprocessor(model_.get(), pre_.get(), .8f, .5f, kMaxObjects,
                                                      {{8, 8, 4, 4}, {16, 16, 8, 8}, {32, 32, 16, 16}}));
        if (!post_)
            return fail("postprocessor allocation", ESP_ERR_NO_MEM);
    }
    log_memory("ready");
    ESP_LOGI("MODEL",
             "Detector loaded; family=%s; model_classes=%d; four-class=%s; three-class(no cone)=%s; %s",
             kModelFamily, kModelClassCount, four_classes() ? "true" : "false",
             three_classes() ? "true" : "false", name());
    return true;
}
void ModelManager::run(const FrameJob &f, const Settings &s, AiPacket &o) {
    dl::image::img_t image{f.fb->buf, uint16_t(f.fb->width), uint16_t(f.fb->height),
                           dl::image::DL_IMAGE_PIX_TYPE_RGB565BE};
    if (post_) {
        post_->set_score_thr(s.confidence);
        // COCO YOLO11 keeps the upstream NMS threshold (0.7); the PicoDet/ESPDet
        // backends keep the mode-specific thresholds.
        post_->set_nms_thr(kYolo11CocoModel ? .7f : mode_nms(s.mode));
    }
    int64_t a = esp_timer_get_time();
    pre_->preprocess(image);
    int64_t b = esp_timer_get_time();
#if CONFIG_VISION_DL_DUAL_CORE
    model_->run(dl::RUNTIME_MODE_MULTI_CORE);
#else
    model_->run(dl::RUNTIME_MODE_SINGLE_CORE);
#endif
    int64_t c = esp_timer_get_time();
    o.count = 0;
    if (kFourClassModel) {
        candidates_->clear();
        float threshold = std::log(s.confidence / (1.f - s.confidence));
        float sx = pre_->get_resize_scale_x(true), sy = pre_->get_resize_scale_y(true);
        int left = pre_->get_border_left(), top = pre_->get_border_top();
        for (int stage = 0; stage < 3; ++stage) {
            auto *score = model_->get_output("score" + std::to_string(stage));
            auto *box = model_->get_output("box" + std::to_string(stage));
            const auto *sp = static_cast<const int8_t *>(score->data),
                       *bp = static_cast<const int8_t *>(box->data);
            float se = std::ldexp(1.f, score->exponent), be = std::ldexp(1.f, box->exponent);
            int stride = 8 << stage;
            for (int y = 0; y < score->shape[1]; ++y)
                for (int x = 0; x < score->shape[2]; ++x) {
                    for (unsigned cls = 0; cls < 4; ++cls) {
                        float logit = sp[cls] * se;
                        if (logit < threshold)
                            continue;
                        float cx = x * stride + stride / 2.f, cy = y * stride + stride / 2.f;
                        auto clip = [](float v) { return std::min(1.f, std::max(0.f, v)); };
                        Box b{clip(((cx - bp[0] * be * stride) - left) * sx / 320.f),
                              clip(((cy - bp[1] * be * stride) - top) * sy / 240.f),
                              clip(((cx + bp[2] * be * stride) - left) * sx / 320.f),
                              clip(((cy + bp[3] * be * stride) - top) * sy / 240.f)};
                        float probability = 1.f / (1.f + std::exp(-logit));
                        if (probability >= s.confidence)
                            candidates_->add({static_cast<ClassId>(cls), probability, b});
                    }
                    sp += 4;
                    bp += 4;
                }
        }
        size_t n = candidates_->compact();
        o.count = class_aware_nms(candidates_->values.data(), n, o.detections.data(), mode_limit(s.mode),
                                  mode_nms(s.mode));
    } else if (kYolo11CocoModel) {
        // The stock postprocessor decodes and NMS-filters all 80 COCO categories;
        // only person/bicycle/car are mapped into the alarm vocabulary. The other
        // categories are dropped here, never renamed to a supported class.
        post_->clear_result();
        post_->postprocess();
        auto &res = post_->get_result(image.width, image.height);
        for (const auto &r : res) {
            if (o.count >= mode_limit(s.mode))
                break;
            ClassId cls = coco_to_class(r.category);
            if (!valid_class(cls) || !std::isfinite(r.score) || r.score < s.confidence || r.score > 1 ||
                r.box.size() != 4)
                continue;
            Box box{r.box[0] / float(image.width), r.box[1] / float(image.height),
                    r.box[2] / float(image.width), r.box[3] / float(image.height)};
            if (box.valid())
                o.detections[o.count++] = {cls, r.score, box};
        }
    } else {
        post_->clear_result();
        post_->postprocess();
        auto &res = post_->get_result(image.width, image.height);
        for (const auto &r : res) {
            if (o.count >= mode_limit(s.mode))
                break;
            if (r.category < 0 || r.category >= (kFourClassModel ? 4 : 1) || !std::isfinite(r.score) ||
                r.score < s.confidence || r.score > 1 || r.box.size() != 4)
                continue;
            Box box{r.box[0] / 320.f, r.box[1] / 240.f, r.box[2] / 320.f, r.box[3] / 240.f};
            if (box.valid())
                o.detections[o.count++] = {static_cast<ClassId>(r.category), r.score, box};
        }
    }
    int64_t d = esp_timer_get_time();
    o.preprocess_ms = (b - a) / 1000.f;
    o.inference_ms = (c - b) / 1000.f;
    o.postprocess_ms = (d - c) / 1000.f;
}
} // namespace vision
