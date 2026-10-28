#include "board_config.hpp"
#include "buzzer.hpp"
#include "detector.hpp"
#include <cinttypes>
#include "esp_camera.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
[[noreturn]] void stop_safely(const char *reason) {
    buzzer_publish({});
    ESP_LOGE("SYSTEM", "%s; alarm disabled. Fix cause and press RESET.", reason);
    // No ESP_ERROR_CHECK/abort/restart loop on expected startup failures.
    for (;;) {
        ESP_LOGE("SYSTEM", "SAFE IDLE: %s; GPIO14 LOW", reason);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

void print_memory() {
    ESP_LOGI("MEMORY", "internal free=%u, min=%u, largest=%u; PSRAM free=%u, largest=%u bytes",
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
        static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
        static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)));
}

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
    c.xclk_freq_hz = 10000000;  // Freenove official default.
    c.ledc_timer = LEDC_TIMER_0;
    c.ledc_channel = LEDC_CHANNEL_0;
    c.pixel_format = PIXFORMAT_RGB565;
    c.frame_size = FRAMESIZE_QVGA;  // 320x240; ESP-DL resizes to 224x224.
    c.jpeg_quality = 12;
    c.fb_count = 2;
    c.fb_location = CAMERA_FB_IN_PSRAM;
    c.grab_mode = CAMERA_GRAB_LATEST;
    const esp_err_t error = esp_camera_init(&c);
    if (error != ESP_OK) {
        ESP_LOGE("CAMERA", "Initialization failed: %s (0x%x)", esp_err_to_name(error), error);
        return false;
    }
    sensor_t *s = esp_camera_sensor_get();
    if (!s) {
        esp_camera_deinit();
        return false;
    }
    const char *name = s->id.PID == OV3660_PID ? "OV3660" :
                       s->id.PID == OV2640_PID ? "OV2640" :
                       s->id.PID == OV5640_PID ? "OV5640" :
                       s->id.PID == GC0308_PID ? "GC0308" : "driver-supported sensor";
    ESP_LOGI("CAMERA", "Sensor=%s, PID=0x%04x (read from hardware)", name, s->id.PID);
    if (s->set_vflip(s, 1) != 0 || s->set_hmirror(s, 0) != 0) {
        ESP_LOGE("CAMERA", "Cannot configure sensor orientation");
        esp_camera_deinit();
        return false;
    }
    if (s->id.PID == OV3660_PID &&
        (s->set_brightness(s, 1) != 0 || s->set_saturation(s, -2) != 0)) {
        ESP_LOGE("CAMERA", "Cannot configure OV3660 exposure/color");
        esp_camera_deinit();
        return false;
    }
    ESP_LOGI("CAMERA", "Initialized: Freenove pins, RGB565BE 320x240, two PSRAM buffers");
    // Let automatic exposure settle. No inference/alarm on these frames.
    for (int i = 0; i < 5; ++i) {
        auto *fb = esp_camera_fb_get();
        if (!fb) {
            esp_camera_deinit();
            return false;
        }
        esp_camera_fb_return(fb);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return true;
}
}

extern "C" void app_main() {
    if (!buzzer_init()) stop_safely("Buzzer initialization failed");
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_bytes = 0;
    if (esp_flash_get_size(nullptr, &flash_bytes) != ESP_OK) stop_safely("Cannot read flash size");
    const size_t psram_bytes = esp_psram_is_initialized() ? esp_psram_get_size() : 0;
    ESP_LOGI("HARDWARE", "Freenove ESP32-S3, cores=%d, revision=%d, reset_reason=%d",
             chip.cores, chip.revision, esp_reset_reason());
    ESP_LOGI("HARDWARE", "Flash=%" PRIu32 " bytes, PSRAM=%u bytes (measured)",
             flash_bytes, static_cast<unsigned>(psram_bytes));
    print_memory();
    if (flash_bytes < 4 * 1024 * 1024 || psram_bytes < 4 * 1024 * 1024) {
        stop_safely("Requires >=4MB Flash and >=4MB working PSRAM; see README for flash header");
    }
    if (!camera_init()) stop_safely("Camera unavailable");
    print_memory();
    PersonDetector detector;
    if (!detector.init()) stop_safely("Person model unavailable");
    print_memory();
    unsigned frame_number = 0;
    unsigned failures = 0;
    for (;;) {
        const int64_t start = esp_timer_get_time();
        camera_fb_t *fb = esp_camera_fb_get();
        if (!fb) {
            buzzer_publish({});
            ESP_LOGE("CAMERA", "Capture failed (%u/5); alarm disabled", ++failures);
            if (failures >= 5) stop_safely("Repeated camera capture failures");
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (!fb->buf || fb->format != PIXFORMAT_RGB565 || fb->width != 320 ||
            fb->height != 240 || fb->len != fb->width * fb->height * 2) {
            esp_camera_fb_return(fb);
            stop_safely("Invalid camera frame format/length");
        }
        const int64_t captured = static_cast<int64_t>(fb->timestamp.tv_sec) * 1000000 +
                                 fb->timestamp.tv_usec;
        // Reject a driver returning old buffers: old frames must not retrigger alarms.
        if (captured <= 0 || captured > esp_timer_get_time() ||
            esp_timer_get_time() - captured > person_beep::kResultMaxAgeUs) {
            esp_camera_fb_return(fb);
            buzzer_publish({});
            ESP_LOGW("CAMERA", "Stale frame rejected (%u/5)", ++failures);
            if (failures >= 5) stop_safely("Camera frames are stale");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        failures = 0;
        dl::image::img_t image = {};
        image.data = fb->buf;
        image.width = fb->width;
        image.height = fb->height;
        image.pix_type = dl::image::DL_IMAGE_PIX_TYPE_RGB565BE;
        const int64_t inference_start = esp_timer_get_time();
        double pre_ms, model_ms, post_ms;
        auto &results = detector.run(image, pre_ms, model_ms, post_ms);
        const int64_t done = esp_timer_get_time();
        bool person = false;
        for (const auto &r : results) {
            const bool valid_box = r.box.size() == 4 && r.box[0] >= 0 && r.box[1] >= 0 &&
                r.box[2] > r.box[0] && r.box[3] > r.box[1] &&
                r.box[2] < image.width && r.box[3] < image.height;
            if (person_beep::accepted_person(r.category, r.score, valid_box)) person = true;
        }
        // Only a completed, valid, fresh neural-network result can request sound.
        buzzer_publish({true, person, captured});
        esp_camera_fb_return(fb);
        ESP_LOGI("FRAME", "#%u Inference: %.2f ms (pre=%.2f, model=%.2f, post=%.2f), capture=%.2f ms, age=%.2f ms",
            ++frame_number, (done - inference_start) / 1000.0, pre_ms, model_ms, post_ms,
            (inference_start - start) / 1000.0, (done - captured) / 1000.0);
        for (const auto &r : results) {
            if (person_beep::accepted_person(r.category, r.score, r.box.size() == 4 &&
                  r.box[2] > r.box[0] && r.box[3] > r.box[1])) {
                ESP_LOGI("DETECTION", "person, confidence=%.4f, box=[%d,%d,%d,%d] in 320x240",
                    static_cast<double>(r.score), r.box[0], r.box[1], r.box[2], r.box[3]);
            }
        }
        if (!person) ESP_LOGI("DETECTION", "No person above threshold %.2f", static_cast<double>(person_beep::kConfidence));
        if (frame_number % 100 == 0) print_memory();
        // Yield to idle/watchdog; camera continues capturing into its other buffer.
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
