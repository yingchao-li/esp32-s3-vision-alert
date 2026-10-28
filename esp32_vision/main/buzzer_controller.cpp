#include "alarm_policy.hpp"
#include "board_config.hpp"
#include "driver/gpio.h"
#include "driver/rmt_encoder.h"
#include "driver/rmt_tx.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "runtime.hpp"
namespace vision {
namespace {
rmt_channel_handle_t channel = nullptr;
rmt_encoder_handle_t encoder = nullptr;
QueueHandle_t test_queue = nullptr;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
BuzzerStatus status;
bool completed = false;
std::array<rmt_symbol_word_t, 64> symbols{};
bool IRAM_ATTR done(rmt_channel_handle_t, const rmt_tx_done_event_data_t *, void *) {
    portENTER_CRITICAL_ISR(&mux);
    completed = true;
    portEXIT_CRITICAL_ISR(&mux);
    return false;
}
void update_status(const BuzzerStatus &s) {
    portENTER_CRITICAL(&mux);
    status = s;
    portEXIT_CRITICAL(&mux);
}
void task(void *) {
    AlarmPolicy policy;
    BuzzerStatus state;
    bool enabled = true;
    int64_t deadline = 0;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        auto s = settings_get();
        auto h = health_get();
        auto a = alert_get();
        int64_t now = esp_timer_get_time();
        bool did_complete;
        portENTER_CRITICAL(&mux);
        did_complete = completed;
        completed = false;
        portEXIT_CRITICAL(&mux);
        if (policy.running() && did_complete) {
            policy.stopped();
            state.active = false;
            state.test = false;
            ++state.completed;
        }
        // Class changes/brief negative detections never interrupt an active
        // pattern. Only mute, disabled AI, health failure, stale pipeline or
        // a bounded TX deadline can cancel an automatic alarm.
        bool auto_fault = !state.test && (!s.ai_enabled || !h.camera || !h.model || !h.last_result_us ||
                                          now - h.last_result_us > 2000000);
        if (policy.running() && (!s.buzzer_enabled || auto_fault || now > deadline)) {
            rmt_disable(channel);
            enabled = false;
            gpio_set_level(static_cast<gpio_num_t>(board::kBuzzer), 0);
            policy.stopped();
            state.active = false;
            state.test = false;
            ++state.cancelled;
        }
        ClassId manual = ClassId::None;
        if (policy.ready(now))
            xQueueReceive(test_queue, &manual, 0);
        bool test = valid_class(manual) && s.buzzer_enabled;
        bool start = test || policy.may_start(a.cls, now, a.captured_us, h.camera && h.model, s);
        if (!policy.running() && start) {
            auto cls = test ? manual : a.cls;
            Pulse pulses[128];
            size_t count = waveform(cls, s.beep_ms, s.gap_ms, pulses, 128);
            if (!count) {
                health_error("Buzzer waveform capacity exceeded");
                vTaskDelay(pdMS_TO_TICKS(10));
                continue;
            }
            size_t n = (count + 1) / 2;
            for (size_t i = 0; i < n; ++i) {
                auto &v = symbols[i];
                v.val = 0;
                v.level0 = pulses[2 * i].level;
                v.duration0 = pulses[2 * i].duration;
                v.level1 = 2 * i + 1 < count ? pulses[2 * i + 1].level : 0;
                v.duration1 = 2 * i + 1 < count ? pulses[2 * i + 1].duration : 1;
            }
            if (!enabled) {
                if (rmt_enable(channel) != ESP_OK) {
                    health_error("Buzzer RMT enable failed");
                    for (;;)
                        vTaskSuspend(nullptr);
                }
                enabled = true;
            }
            portENTER_CRITICAL(&mux);
            completed = false;
            portEXIT_CRITICAL(&mux);
            rmt_transmit_config_t tx = {};
            tx.flags.eot_level = 0;
            if (rmt_transmit(channel, encoder, symbols.data(), n * sizeof(symbols[0]), &tx) != ESP_OK) {
                rmt_disable(channel);
                enabled = false;
                health_error("Buzzer RMT transmit failed");
                gpio_set_level(static_cast<gpio_num_t>(board::kBuzzer), 0);
                for (;;)
                    vTaskSuspend(nullptr);
            }
            now = esp_timer_get_time();
            policy.started(cls, now, s.alert_interval_ms);
            state.active = true;
            state.test = test;
            state.cls = cls;
            state.beeps = AlarmPolicy::count(cls);
            state.last_alert_us = now;
            deadline = now + int64_t(state.beeps * s.beep_ms + (state.beeps - 1) * s.gap_ms + 500) * 1000;
            ESP_LOGI(test ? "BUZZER_TEST" : "BUZZER", "%s count=%u (test=%d)", class_name(cls), state.beeps,
                     test);
        }
        state.pin_high = gpio_get_level(static_cast<gpio_num_t>(board::kBuzzer));
        update_status(state);
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
    }
}
} // namespace
BuzzerStatus buzzer_status() {
    portENTER_CRITICAL(&mux);
    auto s = status;
    portEXIT_CRITICAL(&mux);
    return s;
}
bool buzzer_test(ClassId cls) {
    auto b = buzzer_status();
    return valid_class(cls) && settings_get().buzzer_enabled && test_queue && !b.active &&
           xQueueSend(test_queue, &cls, 0) == pdTRUE;
}
bool buzzer_init() {
    gpio_set_level(static_cast<gpio_num_t>(board::kBuzzer), 0);
    gpio_config_t io = {};
    io.pin_bit_mask = 1ULL << board::kBuzzer;
    io.mode = GPIO_MODE_INPUT_OUTPUT;
    io.pull_down_en = GPIO_PULLDOWN_ENABLE;
    if (gpio_config(&io) != ESP_OK)
        return false;
    rmt_tx_channel_config_t c = {};
    c.gpio_num = static_cast<gpio_num_t>(board::kBuzzer);
    c.clk_src = RMT_CLK_SRC_DEFAULT;
    c.resolution_hz = 1000000;
    c.mem_block_symbols = 64;
    c.trans_queue_depth = 1;
    c.flags.io_loop_back = true;
    rmt_copy_encoder_config_t copy = {};
    rmt_tx_event_callbacks_t callbacks = {};
    callbacks.on_trans_done = done;
    if (rmt_new_tx_channel(&c, &channel) != ESP_OK || rmt_new_copy_encoder(&copy, &encoder) != ESP_OK ||
        rmt_tx_register_event_callbacks(channel, &callbacks, nullptr) != ESP_OK ||
        rmt_enable(channel) != ESP_OK)
        return false;
    test_queue = xQueueCreate(1, sizeof(ClassId));
    if (!test_queue)
        return false;
    TaskHandle_t h;
    if (xTaskCreatePinnedToCore(task, "buzzer", 4096, nullptr, 12, &h, 0) != pdPASS)
        return false;
    perf_task_register("buzzer", h);
    return true;
}
} // namespace vision
