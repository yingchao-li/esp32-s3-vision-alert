#include "board_config.hpp"
#include "driver/gpio.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_system.h"
#include "runtime.hpp"
namespace {
bool launch(TaskFunction_t task, const char *name, uint32_t stack, UBaseType_t priority, int core) {
    TaskHandle_t h;
    if (xTaskCreatePinnedToCore(task, name, stack, nullptr, priority, &h, core) != pdPASS) {
        vision::health_error(name);
        return false;
    }
    vision::perf_task_register(name, h);
    return true;
}
} // namespace
extern "C" void app_main() {
    using namespace vision;
    gpio_set_direction(static_cast<gpio_num_t>(board::kBuzzer), GPIO_MODE_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(board::kBuzzer), 0);
    system_init();
    if (!perf_init()) {
        health_error("Performance mutex allocation failed");
        for (;;)
            vTaskDelay(pdMS_TO_TICKS(1000));
    }
    uint32_t flash = 0;
    esp_flash_get_size(nullptr, &flash);
    uint32_t psram = esp_psram_is_initialized() ? esp_psram_get_size() : 0;
    health_hardware(flash, psram, esp_reset_reason());
    ESP_LOGI("BOOT", "Freenove ESP32-S3 flash=%lu PSRAM=%lu reset=%d", (unsigned long)flash,
             (unsigned long)psram, esp_reset_reason());
    bool buzzer = buzzer_init();
    if (!buzzer)
        health_error("Buzzer initialization failed; output LOW");
    capture_queue = xQueueCreate(1, sizeof(FrameJob));
    tracking_queue = xQueueCreate(1, sizeof(AiPacket));
    bool resources = capture_queue && tracking_queue && flash >= 4 * 1024 * 1024 && psram >= 4 * 1024 * 1024;
    bool camera = resources && camera_init();
    // Reserve the real model before preview/worker stacks and network heaps.
    // A failed model leaves HTTP and the independent camera preview available.
    bool model = camera && detector_init();
    if (!preview_init())
        health_error("Preview disabled: allocation/task failure");
    if (camera) {
        bool workers = !model || (launch(tracking_task, "tracking", 8192, 6, 0) &&
                                  launch(inference_task, "inference", 16384, 5, 1));
        if (!workers)
            health_model(false);
        if (!launch(capture_task, "capture", 4096, 4, 0)) {
            health_camera(false);
            health_model(false);
        }
    } else
        health_error("Camera/PSRAM unavailable; automatic alarms disabled");
    if (!wifi_start())
        health_error("WiFi unavailable; local AI continues");
    if (!web_start())
        health_error("HTTP unavailable; local AI continues");
    launch(perf_task, "perf", 10240, 1, 0);
}
