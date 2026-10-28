#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "runtime.hpp"
#include <cstring>
namespace vision {
namespace {
SemaphoreHandle_t mutex = nullptr;
std::array<WindowStats<128>, MetricCount> samples;
PerfSnapshot snapshot;
std::array<TaskHandle_t, 10> handles{};
uint32_t captures = 0, inferences = 0, drops = 0, preview_drops = 0;
uint32_t generation = 0;
} // namespace
bool perf_init() {
    mutex = xSemaphoreCreateMutex();
    return mutex != nullptr;
}
void perf_reset_window() {
    if (!mutex)
        return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    samples = {};
    snapshot.latency = {};
    ++generation;
    xSemaphoreGive(mutex);
}
void perf_sample(Metric m, float value) {
    if (!mutex)
        return;
    xSemaphoreTake(mutex, portMAX_DELAY);
    samples[m].add(value);
    xSemaphoreGive(mutex);
}
void perf_capture() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    ++captures;
    xSemaphoreGive(mutex);
}
void perf_inference() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    ++inferences;
    xSemaphoreGive(mutex);
}
void perf_drop(bool preview) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    if (preview)
        ++preview_drops;
    else
        ++drops;
    xSemaphoreGive(mutex);
}
void perf_task_register(const char *name, TaskHandle_t h) {
    xSemaphoreTake(mutex, portMAX_DELAY);
    for (unsigned i = 0; i < handles.size(); ++i)
        if (!handles[i]) {
            handles[i] = h;
            std::strncpy(snapshot.stacks[i].name, name, 15);
            ++generation;
            break;
        }
    xSemaphoreGive(mutex);
}
PerfSnapshot perf_get() {
    xSemaphoreTake(mutex, portMAX_DELAY);
    auto p = snapshot;
    xSemaphoreGive(mutex);
    return p;
}
void perf_task(void *) {
    uint32_t previous = 0;
    int64_t then = esp_timer_get_time();
    unsigned log = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        wifi_poll();
        int64_t now = esp_timer_get_time();
        // Copy rings while holding a task mutex; sort after releasing it, so
        // percentile work never blocks the inference producer for a sort.
        std::array<WindowStats<128>, MetricCount> copy;
        PerfSnapshot p;
        std::array<TaskHandle_t, 10> task_handles;
        xSemaphoreTake(mutex, portMAX_DELAY);
        uint32_t current_generation = generation;
        copy = samples;
        task_handles = handles;
        p = snapshot;
        p.captures = captures;
        p.inferences = inferences;
        p.dropped = drops;
        p.preview_dropped = preview_drops;
        xSemaphoreGive(mutex);
        p.fps = (p.inferences - previous) * 1000000.f / (now - then);
        previous = p.inferences;
        then = now;
        for (unsigned i = 0; i < MetricCount; ++i)
            p.latency[i] = copy[i].summarize();
        p.internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        p.internal_min = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
        p.psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        p.psram_largest = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
        // Application tasks do not delete themselves after registration on normal
        // operation; failure tasks suspend, preserving these handles.
        for (unsigned i = 0; i < task_handles.size(); ++i)
            if (task_handles[i])
                p.stacks[i].free_bytes = uxTaskGetStackHighWaterMark(task_handles[i]);
        xSemaphoreTake(mutex, portMAX_DELAY);
        if (current_generation == generation)
            snapshot = p;
        xSemaphoreGive(mutex);
        if (++log % 5 == 0)
            ESP_LOGI("PERF", "fps=%.2f net=%.2fms p95=%.2f drop=%lu SRAM=%lu PSRAM=%lu", double(p.fps),
                     double(p.latency[Inference].mean), double(p.latency[Inference].p95),
                     (unsigned long)p.dropped, (unsigned long)p.internal_free, (unsigned long)p.psram_free);
    }
}
} // namespace vision
