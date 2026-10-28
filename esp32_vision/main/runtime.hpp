#pragma once
#include "esp_camera.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "stats.hpp"
#include "vision_types.hpp"
#include <type_traits>

namespace vision {
enum Metric : uint8_t {
    Capture,
    Preprocess,
    Inference,
    Postprocess,
    Tracking,
    EndToEnd,
    Preview,
    MetricCount
};
constexpr const char *metric_names[] = {"capture_ms",  "preprocess_ms", "inference_ms", "postprocess_ms",
                                        "tracking_ms", "end_to_end_ms", "preview_ms"};
struct FrameJob {
    camera_fb_t *fb = nullptr;
    uint32_t id = 0;
    int64_t captured_us = 0;
    float capture_ms = 0;
};
struct AiPacket {
    FrameJob frame;
    std::array<Detection, kMaxObjects> detections{};
    uint8_t count = 0;
    float preprocess_ms = 0, inference_ms = 0, postprocess_ms = 0;
    uint32_t config_epoch = 0;
};
static_assert(std::is_trivially_copyable_v<FrameJob> && std::is_trivially_copyable_v<AiPacket>,
              "FreeRTOS queue payloads must remain trivially copyable");
struct Health {
    bool camera = false, model = false;
    uint32_t errors = 0;
    int64_t last_result_us = 0;
    char last_error[96]{};
    uint32_t flash_bytes = 0, psram_bytes = 0;
    uint16_t sensor_pid = 0;
    int reset_reason = 0;
};
struct BuzzerStatus {
    bool active = false, test = false, pin_high = false;
    ClassId cls = ClassId::None;
    unsigned beeps = 0;
    int64_t last_alert_us = 0;
    uint32_t completed = 0, cancelled = 0;
};
struct PerfSnapshot {
    std::array<Distribution, MetricCount> latency{};
    float fps = 0;
    uint32_t captures = 0, inferences = 0, dropped = 0, preview_dropped = 0;
    uint32_t internal_free = 0, internal_min = 0, psram_free = 0, psram_largest = 0;
    struct Stack {
        char name[16]{};
        uint32_t free_bytes = 0;
    };
    std::array<Stack, 10> stacks{};
};
struct Alert {
    ClassId cls = ClassId::None;
    int64_t captured_us = 0;
};
extern QueueHandle_t capture_queue, tracking_queue;
Settings settings_get(uint32_t *epoch = nullptr);
bool settings_set(const Settings &s);
Health health_get();
void health_camera(bool ok, uint16_t pid = 0);
void health_model(bool ok);
void health_error(const char *reason);
void health_hardware(uint32_t flash, uint32_t psram, int reason);
void result_publish(const FrameResult &r, ClassId alert);
FrameResult result_get();
Alert alert_get();
void system_init();
bool camera_init();
bool detector_init();
void capture_task(void *);
void inference_task(void *);
void tracking_task(void *);
bool buzzer_init();
bool buzzer_test(ClassId cls);
BuzzerStatus buzzer_status();
bool perf_init();
void perf_reset_window();
void perf_sample(Metric metric, float ms);
void perf_capture();
void perf_inference();
void perf_drop(bool preview = false);
void perf_task_register(const char *name, TaskHandle_t handle);
void perf_task(void *);
PerfSnapshot perf_get();
bool preview_init();
void preview_offer(const FrameJob &frame, const FrameResult &result, const Settings &settings);
struct PreviewLease {
    const uint8_t *jpeg = nullptr;
    size_t length = 0;
    FrameResult result;
    int slot = -1;
};
bool preview_acquire(PreviewLease &lease);
void preview_release(PreviewLease &lease);
bool web_start();
bool wifi_start();
void wifi_poll();
void wifi_ip(char *out, size_t len);
bool wifi_configure(const char *ssid, const char *password, bool persist);
bool settings_save();
} // namespace vision
