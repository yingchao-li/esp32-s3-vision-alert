#include "esp_log.h"
#include "model_spec.hpp"
#include "nvs.h"
#include "nvs_flash.h"
#include "runtime.hpp"
#include <cstring>

namespace vision {
QueueHandle_t capture_queue = nullptr, tracking_queue = nullptr;
namespace {
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
Settings config;
uint32_t epoch = 1;
Health health;
FrameResult result;
Alert alert;
bool nvs_ready = false;
} // namespace
void system_init() {
    esp_err_t e = nvs_flash_init(); // Never erase NVS on init errors.
    nvs_ready = e == ESP_OK;
    if (!nvs_ready)
        ESP_LOGW("NVS", "Init failed (%s); RAM configuration only, NO erase", esp_err_to_name(e));
    nvs_handle_t h;
    bool loaded = false;
    if (nvs_ready && nvs_open("vision", NVS_READONLY, &h) == ESP_OK) {
        Settings saved;
        size_t length = sizeof(saved);
        uint32_t version = 0;
        if (nvs_get_u32(h, "cfg_version", &version) == ESP_OK && version == 1 &&
            nvs_get_blob(h, "config", &saved, &length) == ESP_OK && length == sizeof(saved) &&
            valid_settings(saved)) {
            config = saved;
            loaded = true;
        }
        nvs_close(h);
    }
    if (!loaded) {
        // COCO YOLO11 score distributions differ from PicoDet; the PicoDet-tuned
        // 0.80 default would suppress most detections. Existing saved settings
        // are respected, so upgrade tests can still switch back in the UI.
        config.confidence = kYolo11CocoModel ? .35f : .80f;
    }
}
Settings settings_get(uint32_t *e) {
    portENTER_CRITICAL(&mux);
    auto s = config;
    if (e)
        *e = epoch;
    portEXIT_CRITICAL(&mux);
    return s;
}
bool settings_set(const Settings &s) {
    if (!valid_settings(s))
        return false;
    portENTER_CRITICAL(&mux);
    config = s;
    ++epoch;
    alert = {};
    portEXIT_CRITICAL(&mux);
    perf_reset_window();
    return true;
}
bool settings_save() {
    if (!nvs_ready)
        return false;
    auto s = settings_get();
    nvs_handle_t h;
    if (nvs_open("vision", NVS_READWRITE, &h) != ESP_OK)
        return false;
    bool ok = nvs_set_u32(h, "cfg_version", 1) == ESP_OK &&
              nvs_set_blob(h, "config", &s, sizeof(s)) == ESP_OK && nvs_commit(h) == ESP_OK;
    nvs_close(h);
    return ok;
}
Health health_get() {
    portENTER_CRITICAL(&mux);
    auto h = health;
    portEXIT_CRITICAL(&mux);
    return h;
}
void health_camera(bool ok, uint16_t pid) {
    portENTER_CRITICAL(&mux);
    health.camera = ok;
    if (pid)
        health.sensor_pid = pid;
    portEXIT_CRITICAL(&mux);
}
void health_model(bool ok) {
    portENTER_CRITICAL(&mux);
    health.model = ok;
    portEXIT_CRITICAL(&mux);
}
void health_hardware(uint32_t f, uint32_t p, int r) {
    portENTER_CRITICAL(&mux);
    health.flash_bytes = f;
    health.psram_bytes = p;
    health.reset_reason = r;
    portEXIT_CRITICAL(&mux);
}
void health_error(const char *reason) {
    portENTER_CRITICAL(&mux);
    ++health.errors;
    std::strncpy(health.last_error, reason, sizeof(health.last_error) - 1);
    portEXIT_CRITICAL(&mux);
    ESP_LOGE("HEALTH", "%s", reason);
}
void result_publish(const FrameResult &r, ClassId cls) {
    portENTER_CRITICAL(&mux);
    result = r;
    health.last_result_us = r.completed_us;
    alert = {cls, r.captured_us};
    portEXIT_CRITICAL(&mux);
}
FrameResult result_get() {
    portENTER_CRITICAL(&mux);
    auto r = result;
    portEXIT_CRITICAL(&mux);
    return r;
}
Alert alert_get() {
    portENTER_CRITICAL(&mux);
    auto a = alert;
    portEXIT_CRITICAL(&mux);
    return a;
}
} // namespace vision
