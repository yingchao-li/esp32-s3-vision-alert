#include "buzzer.hpp"
#include "board_config.hpp"
#include "driver/gpio.h"
#include "driver/rmt_tx.h"
#include "driver/rmt_encoder.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
person_beep::Observation latest;
rmt_channel_handle_t channel = nullptr;
rmt_encoder_handle_t encoder = nullptr;
bool tx_done = false;  // Access only under mux, including from the ISR.
// APB 80MHz / 80 = 1MHz (within the RMT 8-bit clock-divider range).
// Each 200ms phase uses 4 symbols * 2 halves * 25000us; adjacent halves
// retain the same level. The final symbol explicitly returns to LOW.
rmt_symbol_word_t pattern[13];

bool IRAM_ATTR on_done(rmt_channel_handle_t, const rmt_tx_done_event_data_t *, void *) {
    portENTER_CRITICAL_ISR(&mux);
    tx_done = true;
    portEXIT_CRITICAL_ISR(&mux);
    return false;
}

void force_low() {
    if (channel) {
        rmt_disable(channel);  // Stop an in-flight sequence as well.
    }
    gpio_set_level(static_cast<gpio_num_t>(board::kBuzzer), 0);
}

void buzzer_task(void *) {
    person_beep::AlarmPolicy policy;
    int64_t started_us = 0;
    bool enabled = true;
    int previous_pad_level = -1;
    TickType_t wake = xTaskGetTickCount();
    for (;;) {
        const int64_t now = esp_timer_get_time();
        const int pad_level = gpio_get_level(static_cast<gpio_num_t>(board::kBuzzer));
        if (pad_level != previous_pad_level) {
            ESP_LOGI("BUZZER_PIN", "GPIO14 actual pad=%d, t=%lld us", pad_level, now);
            previous_pad_level = pad_level;
        }
        person_beep::Observation observation;
        portENTER_CRITICAL(&mux);
        observation = latest;
        const bool completed = tx_done;
        tx_done = false;
        portEXIT_CRITICAL(&mux);
        if (policy.running() && completed) {
            policy.stopped(now);  // At least 2000ms LOW after actual completion.
            ESP_LOGI("BUZZER", "LOW, cooldown >= 2000 ms, t=%lld us", now);
        }
        if (policy.running() && (!person_beep::actionable(observation, now) ||
                                 now - started_us > 1000000)) {
            force_low();
            enabled = false;
            // rmt_disable() may take time to stop the peripheral. Start the
            // full LOW interval only after it has returned, never before it.
            policy.stopped(esp_timer_get_time());
            ESP_LOGW("BUZZER", "Stopped: stale/negative result or TX timeout; GPIO14 LOW");
        }
        if (policy.can_start(observation, now)) {
            if (!enabled) {
                if (rmt_enable(channel) != ESP_OK) {
                    force_low();
                    ESP_LOGE("BUZZER", "Cannot enable RMT; alarm disabled");
                    vTaskDelete(nullptr);
                }
                enabled = true;
            }
            portENTER_CRITICAL(&mux);
            tx_done = false;
            portEXIT_CRITICAL(&mux);
            rmt_transmit_config_t tx = {};
            tx.loop_count = 0;
            tx.flags.eot_level = 0;
            if (rmt_transmit(channel, encoder, pattern, sizeof(pattern), &tx) != ESP_OK) {
                force_low();
                ESP_LOGE("BUZZER", "Cannot start RMT; alarm disabled");
                vTaskDelete(nullptr);
            }
            started_us = now;
            policy.started();
            ESP_LOGI("BUZZER", "Beep Beep! GPIO14, 200/200/200 ms, t=%lld us", now);
        }
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(5));
    }
}
}

void buzzer_publish(const person_beep::Observation &observation) {
    portENTER_CRITICAL(&mux);
    latest = observation;
    portEXIT_CRITICAL(&mux);
}

bool buzzer_init() {
    gpio_set_level(static_cast<gpio_num_t>(board::kBuzzer), 0);
    gpio_config_t gpio = {};
    gpio.pin_bit_mask = 1ULL << board::kBuzzer;
    gpio.mode = GPIO_MODE_INPUT_OUTPUT;
    gpio.pull_down_en = GPIO_PULLDOWN_ENABLE;
    if (gpio_config(&gpio) != ESP_OK) return false;
    gpio_set_level(static_cast<gpio_num_t>(board::kBuzzer), 0);
    for (int i = 0; i < 12; ++i) {
        pattern[i].level0 = (i < 4 || i >= 8) ? 1 : 0;
        pattern[i].level1 = pattern[i].level0;
        pattern[i].duration0 = 25000;
        pattern[i].duration1 = 25000;
    }
    pattern[12].level0 = 0;
    pattern[12].duration0 = 1;
    pattern[12].level1 = 0;
    pattern[12].duration1 = 1;
    rmt_tx_channel_config_t config = {};
    config.gpio_num = static_cast<gpio_num_t>(board::kBuzzer);
    config.clk_src = RMT_CLK_SRC_DEFAULT;
    config.resolution_hz = 1000000;
    config.mem_block_symbols = 64;
    config.trans_queue_depth = 1;
    config.flags.io_loop_back = true;  // Enable actual-pad readback for diagnostics.
    rmt_copy_encoder_config_t copy_config = {};
    rmt_tx_event_callbacks_t callbacks = {};
    callbacks.on_trans_done = on_done;
    if (rmt_new_tx_channel(&config, &channel) != ESP_OK ||
        rmt_new_copy_encoder(&copy_config, &encoder) != ESP_OK ||
        rmt_tx_register_event_callbacks(channel, &callbacks, nullptr) != ESP_OK ||
        rmt_enable(channel) != ESP_OK) {
        force_low();
        return false;
    }
    if (xTaskCreatePinnedToCore(buzzer_task, "buzzer", 4096, nullptr, 10, nullptr, 0) != pdPASS) {
        force_low();
        return false;
    }
    ESP_LOGI("BUZZER", "Ready: Freenove GPIO14, active HIGH, hardware-timed pulses");
    return true;
}
