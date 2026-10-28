#include "esp_timer.h"
#include "model_manager.hpp"
#include "object_tracker.hpp"
#include "obstacle_analyzer.hpp"
#include "runtime.hpp"
namespace vision {
namespace {
ModelManager detector;
bool initialized = false;
} // namespace
bool detector_init() {
    // app_main calls this synchronously before workers/WiFi. Only inference_task
    // uses the detector afterwards; no partially built model is published.
    initialized = detector.init();
    if (!initialized)
        detector.clear();
    health_model(initialized);
    return initialized;
}
void inference_task(void *) {
    if (!initialized) {
        for (;;)
            vTaskSuspend(nullptr);
    }
#if CONFIG_VISION_DL_DUAL_CORE
    bool workers_registered = false;
#endif
    for (;;) {
        FrameJob f;
        if (xQueueReceive(capture_queue, &f, pdMS_TO_TICKS(500)) != pdTRUE)
            continue;
        uint32_t epoch;
        auto s = settings_get(&epoch);
        if (!s.ai_enabled) {
            esp_camera_fb_return(f.fb);
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        if (esp_timer_get_time() - f.captured_us > 2000000) {
            esp_camera_fb_return(f.fb);
            perf_drop();
            continue;
        }
        AiPacket packet;
        packet.frame = f;
        packet.config_epoch = epoch;
        detector.run(f, s, packet);
#if CONFIG_VISION_DL_DUAL_CORE
        if (!workers_registered) {
            auto core0 = xTaskGetHandle("dl_mc0"), core1 = xTaskGetHandle("dl_mc1");
            if (core0 && core1) {
                perf_task_register("dl_mc0", core0);
                perf_task_register("dl_mc1", core1);
                workers_registered = true;
            }
        }
#endif
        perf_inference();
        if (xQueueSend(tracking_queue, &packet, 0) != pdTRUE) {
            AiPacket old;
            if (xQueueReceive(tracking_queue, &old, 0) == pdTRUE) {
                esp_camera_fb_return(old.frame.fb);
                perf_drop();
            }
            if (xQueueSend(tracking_queue, &packet, 0) != pdTRUE) {
                esp_camera_fb_return(f.fb);
                perf_drop();
            }
        }
        vTaskDelay(1);
    }
}
void tracking_task(void *) {
    ObjectTracker tracker;
    ObstacleAnalyzer analyzer;
    uint32_t previous_epoch = 0;
    for (;;) {
        AiPacket p;
        if (xQueueReceive(tracking_queue, &p, portMAX_DELAY) != pdTRUE)
            continue;
        uint32_t epoch;
        auto s = settings_get(&epoch);
        if (!s.ai_enabled || p.config_epoch != epoch) {
            esp_camera_fb_return(p.frame.fb);
            perf_drop();
            continue;
        }
        if (epoch != previous_epoch) {
            tracker.reset();
            previous_epoch = epoch;
        }
        FrameResult result;
        result.frame_id = p.frame.id;
        result.captured_us = p.frame.captured_us;
        result.inference_ms = p.inference_ms;
        int64_t start = esp_timer_get_time();
        tracker.update(p.detections.data(), p.count, p.frame.captured_us, s.confirm_frames, result);
        auto cls = analyzer.analyze(result, s);
        result.completed_us = esp_timer_get_time();
        result_publish(result, cls);
        perf_sample(Capture, p.frame.capture_ms);
        perf_sample(Preprocess, p.preprocess_ms);
        perf_sample(Inference, p.inference_ms);
        perf_sample(Postprocess, p.postprocess_ms);
        perf_sample(Tracking, (result.completed_us - start) / 1000.f);
        perf_sample(EndToEnd, (result.completed_us - result.captured_us) / 1000.f);
        preview_offer(p.frame, result, s);
        esp_camera_fb_return(p.frame.fb);
        vTaskDelay(1);
    }
}
} // namespace vision
