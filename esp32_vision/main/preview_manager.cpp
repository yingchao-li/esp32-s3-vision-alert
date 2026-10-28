#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "runtime.hpp"
#include <cstring>
namespace vision {
namespace {
constexpr size_t kJpegCapacity = 65536;
struct Slot {
    uint8_t *data = nullptr;
    size_t length = 0;
    FrameResult result;
    unsigned refs = 0;
    bool writing = false;
};
Slot slots[2];
int latest = -1;
uint8_t *raw = nullptr;
FrameResult pending;
int quality = 60;
bool busy = false, ready = false;
TaskHandle_t worker = nullptr;
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
int64_t last_offer = 0;
struct Writer {
    Slot *slot;
    bool overflow = false;
    size_t length = 0;
};
size_t jpeg_write(void *arg, size_t index, const void *data, size_t length) {
    auto &w = *static_cast<Writer *>(arg);
    if (index > kJpegCapacity || length > kJpegCapacity - index) {
        w.overflow = true;
        return 0;
    }
    std::memcpy(w.slot->data + index, data, length);
    w.length = std::max(w.length, index + length);
    return length;
}
void preview_task(void *) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        int target = -1;
        portENTER_CRITICAL(&mux);
        for (int i = 0; i < 2; ++i)
            if (i != latest && !slots[i].refs && !slots[i].writing) {
                target = i;
                slots[i].writing = true;
                break;
            }
        portEXIT_CRITICAL(&mux);
        if (target >= 0) {
            Writer w{&slots[target]};
            int64_t start = esp_timer_get_time();
            bool ok = fmt2jpg_cb(raw, 320 * 240 * 2, 320, 240, PIXFORMAT_RGB565, quality, jpeg_write, &w);
            perf_sample(Preview, (esp_timer_get_time() - start) / 1000.f);
            if (ok && !w.overflow && w.length) {
                slots[target].length = w.length;
                slots[target].result = pending;
                portENTER_CRITICAL(&mux);
                slots[target].writing = false;
                latest = target;
                portEXIT_CRITICAL(&mux);
            } else {
                portENTER_CRITICAL(&mux);
                slots[target].writing = false;
                portEXIT_CRITICAL(&mux);
                perf_drop(true);
            }
        } else
            perf_drop(true);
        portENTER_CRITICAL(&mux);
        busy = false;
        portEXIT_CRITICAL(&mux);
        vTaskDelay(1);
    }
}
} // namespace
bool preview_init() {
    raw = static_cast<uint8_t *>(heap_caps_malloc(320 * 240 * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    for (auto &s : slots)
        s.data = static_cast<uint8_t *>(heap_caps_malloc(kJpegCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!raw || !slots[0].data || !slots[1].data) {
        heap_caps_free(raw);
        raw = nullptr;
        for (auto &s : slots) {
            heap_caps_free(s.data);
            s.data = nullptr;
        }
        return false;
    }
    if (xTaskCreatePinnedToCore(preview_task, "preview", 8192, nullptr, 1, &worker, 0) != pdPASS) {
        heap_caps_free(raw);
        raw = nullptr;
        for (auto &s : slots) {
            heap_caps_free(s.data);
            s.data = nullptr;
        }
        return false;
    }
    perf_task_register("preview", worker);
    ready = true;
    return true;
}
void preview_offer(const FrameJob &f, const FrameResult &r, const Settings &s) {
    int fps = preview_fps(s.mode);
    int64_t now = esp_timer_get_time();
    if (!ready || !s.stream_enabled || fps == 0)
        return;
    portENTER_CRITICAL(&mux);
    bool due = now - last_offer >= 1000000 / fps;
    bool available = !busy && due;
    if (available) {
        busy = true;
        last_offer = now;
    }
    portEXIT_CRITICAL(&mux);
    if (!due)
        return;
    if (!available) {
        perf_drop(true);
        return;
    }
    std::memcpy(raw, f.fb->buf, 320 * 240 * 2);
    pending = r;
    quality = preview_quality(s.mode);
    xTaskNotifyGive(worker);
}
bool preview_acquire(PreviewLease &l) {
    auto settings = settings_get();
    if (!settings.stream_enabled || preview_fps(settings.mode) == 0)
        return false;
    portENTER_CRITICAL(&mux);
    if (latest < 0) {
        portEXIT_CRITICAL(&mux);
        return false;
    }
    auto &s = slots[latest];
    ++s.refs;
    l.jpeg = s.data;
    l.length = s.length;
    l.result = s.result;
    l.slot = latest;
    portEXIT_CRITICAL(&mux);
    return true;
}
void preview_release(PreviewLease &l) {
    portENTER_CRITICAL(&mux);
    if (l.slot >= 0 && l.slot < 2 && slots[l.slot].refs)
        --slots[l.slot].refs;
    portEXIT_CRITICAL(&mux);
    l = {};
}
} // namespace vision
