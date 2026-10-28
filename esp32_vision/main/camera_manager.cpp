#include "board_config.hpp"
#include "esp_log.h"
#include "esp_timer.h"
#include "runtime.hpp"
namespace vision {
bool camera_init() {
    camera_config_t c = {};
    c.pin_pwdn = board::kPwdn;
    c.pin_reset = board::kReset;
    c.pin_xclk = board::kXclk;
    c.pin_sccb_sda = board::kSda;
    c.pin_sccb_scl = board::kScl;
    c.pin_d0 = board::kD0;
    c.pin_d1 = board::kD1;
    c.pin_d2 = board::kD2;
    c.pin_d3 = board::kD3;
    c.pin_d4 = board::kD4;
    c.pin_d5 = board::kD5;
    c.pin_d6 = board::kD6;
    c.pin_d7 = board::kD7;
    c.pin_vsync = board::kVsync;
    c.pin_href = board::kHref;
    c.pin_pclk = board::kPclk;
    c.xclk_freq_hz = 10000000;
    c.ledc_timer = LEDC_TIMER_0;
    c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_RGB565;
    c.frame_size = FRAMESIZE_QVGA;
    c.fb_count = 2;
    c.jpeg_quality = 12;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;
    auto e = esp_camera_init(&c);
    if (e != ESP_OK) {
        health_error(esp_err_to_name(e));
        return false;
    }
    auto *s = esp_camera_sensor_get();
    if (!s || s->set_vflip(s, 1) || s->set_hmirror(s, 0)) {
        esp_camera_deinit();
        health_error("Sensor configuration failed");
        return false;
    }
    if (s->id.PID == OV3660_PID && (s->set_brightness(s, 1) || s->set_saturation(s, -2))) {
        esp_camera_deinit();
        health_error("OV3660 configuration failed");
        return false;
    }
    health_camera(true, s->id.PID);
    const char *name = s->id.PID == GC0308_PID ? "GC0308" : s->id.PID == OV3660_PID ? "OV3660" : "other";
    ESP_LOGI("CAMERA",
             "Initialized %s PID=0x%04x; RGB565 320x240, 153600 bytes/FB, "
             "2 FB in PSRAM, GRAB_LATEST, XCLK=10MHz; unchanged Freenove GPIOs",
             name, s->id.PID);
    ESP_LOGI("CAMERA", "Preview uses ESP32 software RGB565->JPEG; GC0308 has no native JPEG. "
                       "Initialization is not validation of image content.");
    return true;
}
void capture_task(void *) {
    uint32_t id = 0;
    unsigned failures = 0;
    for (;;) {
        int64_t start = esp_timer_get_time();
        auto *fb = esp_camera_fb_get();
        int64_t now = esp_timer_get_time();
        if (!fb) {
            health_camera(false);
            health_error("Camera capture failed");
            if (++failures >= 5) {
                health_error("Camera latched off; reset after inspection");
                for (;;)
                    vTaskSuspend(nullptr);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        int64_t timestamp = int64_t(fb->timestamp.tv_sec) * 1000000 + fb->timestamp.tv_usec;
        if (!fb->buf || fb->format != PIXFORMAT_RGB565 || fb->width != 320 || fb->height != 240 ||
            fb->len != 320 * 240 * 2 || timestamp <= 0 || timestamp > now || now - timestamp > 2000000) {
            esp_camera_fb_return(fb);
            health_camera(false);
            health_error("Invalid/stale frame rejected");
            if (++failures >= 5) {
                for (;;)
                    vTaskSuspend(nullptr);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        failures = 0;
        health_camera(true);
        perf_capture();
        FrameJob job{fb, ++id, timestamp, (now - start) / 1000.f};
        auto settings = settings_get();
        if (!settings.ai_enabled || !health_get().model) {
            FrameResult empty;
            empty.frame_id = job.id;
            empty.captured_us = timestamp;
            empty.completed_us = now;
            result_publish(empty, ClassId::None);
            preview_offer(job, empty, settings);
            esp_camera_fb_return(fb);
            vTaskDelay(1);
            continue;
        }
        if (xQueueSend(capture_queue, &job, 0) != pdTRUE) {
            FrameJob old;
            if (xQueueReceive(capture_queue, &old, 0) == pdTRUE) {
                esp_camera_fb_return(old.fb);
                perf_drop();
            }
            if (xQueueSend(capture_queue, &job, 0) != pdTRUE) {
                esp_camera_fb_return(fb);
                perf_drop();
            }
        }
        vTaskDelay(1);
    }
}
} // namespace vision
