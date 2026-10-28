#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "model_manager.hpp"
#include "runtime.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
extern const uint8_t dashboard_html_start[] asm("_binary_dashboard_html_start");
extern const uint8_t dashboard_html_end[] asm("_binary_dashboard_html_end");
namespace vision {
namespace {
void number(cJSON *j, const char *key, double v) { cJSON_AddNumberToObject(j, key, v); }
void boolean(cJSON *j, const char *key, bool v) { cJSON_AddBoolToObject(j, key, v); }
cJSON *json_frame(const FrameResult &r) {
    auto *j = cJSON_CreateObject();
    if (!j)
        return nullptr;
    number(j, "frame_id", r.frame_id);
    number(j, "timestamp_us", r.captured_us);
    number(j, "completed_us", r.completed_us);
    number(j, "inference_ms", r.inference_ms);
    number(j, "width", r.width);
    number(j, "height", r.height);
    auto *objects = cJSON_AddArrayToObject(j, "objects");
    for (unsigned i = 0; i < r.count; ++i) {
        const auto &t = r.tracks[i];
        auto *o = cJSON_CreateObject();
        if (!o)
            continue;
        cJSON_AddItemToArray(objects, o);
        number(o, "tracking_id", t.id);
        number(o, "class_id", static_cast<unsigned>(t.cls));
        cJSON_AddStringToObject(o, "class_name", class_name(t.cls));
        number(o, "confidence", t.score);
        number(o, "seen_us", t.seen_us);
        boolean(o, "matched", t.matched);
        boolean(o, "confirmed", t.confirmed);
        double box[] = {t.box.x1, t.box.y1, t.box.x2, t.box.y2};
        cJSON_AddItemToObject(o, "box", cJSON_CreateDoubleArray(box, 4));
        number(o, "vx_normalized_per_s", t.vx);
        number(o, "vy_normalized_per_s", t.vy);
        number(o, "area_change_per_s", t.area_rate);
        number(o, "zone", t.zone);
        number(o, "roi_event", t.event);
        number(o, "risk", t.risk);
    }
    return j;
}
cJSON *json_settings(const Settings &s) {
    auto *j = cJSON_CreateObject();
    if (!j)
        return nullptr;
    boolean(j, "ai_enabled", s.ai_enabled);
    boolean(j, "buzzer_enabled", s.buzzer_enabled);
    boolean(j, "stream_enabled", s.stream_enabled);
    number(j, "mode", static_cast<unsigned>(s.mode));
    number(j, "confidence", s.confidence);
    number(j, "beep_ms", s.beep_ms);
    number(j, "gap_ms", s.gap_ms);
    number(j, "alert_interval_ms", s.alert_interval_ms);
    number(j, "confirm_frames", s.confirm_frames);
    number(j, "roi_top", s.roi_top);
    number(j, "roi_bottom", s.roi_bottom);
    number(j, "left_boundary", s.left_boundary);
    number(j, "right_boundary", s.right_boundary);
    number(j, "danger_y", s.danger_y);
    int order[] = {s.priority[0], s.priority[1], s.priority[2], s.priority[3]};
    cJSON_AddItemToObject(j, "priority", cJSON_CreateIntArray(order, 4));
    return j;
}
esp_err_t send_json(httpd_req_t *req, cJSON *j) {
    if (!j)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
    char *text = cJSON_PrintUnformatted(j);
    cJSON_Delete(j);
    if (!text)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "JSON allocation failed");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    auto e = httpd_resp_send(req, text, HTTPD_RESP_USE_STRLEN);
    cJSON_free(text);
    return e;
}
esp_err_t index(httpd_req_t *r) {
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_set_hdr(r, "Cache-Control", "no-store");
    httpd_resp_set_hdr(r, "Content-Security-Policy",
                       "default-src 'self'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; img-src "
                       "'self' blob:; connect-src 'self'");
    return httpd_resp_send(r, reinterpret_cast<const char *>(dashboard_html_start),
                           dashboard_html_end - dashboard_html_start);
}
esp_err_t status_handler(httpd_req_t *req) {
    auto *j = cJSON_CreateObject();
    if (!j)
        return send_json(req, j);
    auto h = health_get();
    auto p = perf_get();
    auto b = buzzer_status();
    char ip[20];
    wifi_ip(ip, sizeof(ip));
    cJSON_AddStringToObject(j, "model", ModelManager::name());
    cJSON_AddStringToObject(j, "model_family", ModelManager::family());
    boolean(j, "four_class_available", ModelManager::four_classes() && h.model);
    boolean(j, "three_class_available", ModelManager::three_classes() && h.model);
    boolean(j, "cone_model_deployed", ModelManager::four_classes() && h.model);
    cJSON_AddStringToObject(j, "model_status",
                            !h.model ? "MODEL NOT READY; AUTOMATIC ALERTS DISABLED"
                            : ModelManager::four_classes()
                                ? "FOUR_CLASS_WEIGHTS_LOADED"
                            : ModelManager::three_classes()
                                ? "THREE_CLASS: PERSON/BICYCLE/CAR; TRAFFIC CONE NOT DEPLOYED"
                                : "PARTIAL: PERSON ONLY; CONE/BICYCLE/CAR MODEL NOT DEPLOYED");
    auto *deployed = cJSON_AddArrayToObject(j, "deployed_classes");
    if (h.model) {
        if (ModelManager::four_classes()) {
            for (const char *n : {"Person", "Traffic Cone", "Bicycle", "Car"})
                cJSON_AddItemToArray(deployed, cJSON_CreateString(n));
        } else if (ModelManager::three_classes()) {
            for (const char *n : {"Person", "Bicycle", "Car"})
                cJSON_AddItemToArray(deployed, cJSON_CreateString(n));
        } else {
            cJSON_AddItemToArray(deployed, cJSON_CreateString("Person"));
        }
    }
    boolean(j, "camera_ok", h.camera);
    boolean(j, "model_ok", h.model);
    number(j, "sensor_pid", h.sensor_pid);
    cJSON_AddStringToObject(j, "sensor_name",
                            h.sensor_pid == GC0308_PID   ? "GC0308"
                            : h.sensor_pid == OV3660_PID ? "OV3660"
                                                         : "unknown");
    cJSON_AddStringToObject(j, "camera_format", "RGB565");
    number(j, "camera_width", 320);
    number(j, "camera_height", 240);
    number(j, "camera_framebuffer_count", 2);
    number(j, "camera_framebuffer_bytes", 320 * 240 * 2);
    number(j, "camera_valid_frames", p.captures);
    cJSON_AddStringToObject(j, "preview_encoder", "ESP32 software RGB565 to JPEG");
    cJSON_AddStringToObject(j, "camera_validation",
                            p.captures ? "frame layout/timestamp checked; image content requires user test"
                                       : "no validated capture yet; init alone does not verify image data");
    number(j, "uptime_s", esp_timer_get_time() / 1000000.0);
    number(j, "errors", h.errors);
    number(j, "reset_reason", h.reset_reason);
    boolean(j, "previous_boot_watchdog",
            h.reset_reason == ESP_RST_TASK_WDT || h.reset_reason == ESP_RST_INT_WDT ||
                h.reset_reason == ESP_RST_WDT);
    cJSON_AddStringToObject(j, "last_error", h.last_error);
    cJSON_AddStringToObject(j, "ip", ip);
    number(j, "flash_bytes", h.flash_bytes);
    number(j, "psram_bytes", h.psram_bytes);
    cJSON_AddItemToObject(j, "settings", json_settings(settings_get()));
    cJSON_AddItemToObject(j, "result", json_frame(result_get()));
    auto *perf = cJSON_AddObjectToObject(j, "performance");
    number(perf, "ai_fps", p.fps);
    number(perf, "captured", p.captures);
    number(perf, "inferences", p.inferences);
    number(perf, "frame_drops", p.dropped);
    number(perf, "preview_drops", p.preview_dropped);
    cJSON_AddStringToObject(perf, "drop_scope",
                            "application queues/stale frames only; sensor/driver losses not measurable");
    cJSON_AddNullToObject(perf, "driver_drops");
    cJSON_AddNullToObject(perf, "cpu_usage_percent");
    number(perf, "internal_free", p.internal_free);
    number(perf, "internal_min", p.internal_min);
    number(perf, "psram_free", p.psram_free);
    number(perf, "psram_largest", p.psram_largest);
    number(perf, "stats_window_samples", 128);
    auto *latency = cJSON_AddObjectToObject(perf, "latency");
    for (unsigned i = 0; i < MetricCount; ++i) {
        auto *m = cJSON_AddObjectToObject(latency, metric_names[i]);
        auto &d = p.latency[i];
        number(m, "count", d.count);
        number(m, "mean", d.mean);
        number(m, "p50", d.p50);
        number(m, "p95", d.p95);
        number(m, "p99", d.p99);
        number(m, "max", d.max);
    }
    auto *stacks = cJSON_AddObjectToObject(perf, "stack_free_bytes");
    for (const auto &s : p.stacks)
        if (s.name[0])
            number(stacks, s.name, s.free_bytes);
    auto *buzz = cJSON_AddObjectToObject(j, "buzzer");
    boolean(buzz, "active", b.active);
    boolean(buzz, "test_mode", b.test);
    boolean(buzz, "pin_high", b.pin_high);
    cJSON_AddStringToObject(buzz, "class", class_name(b.cls));
    cJSON_AddStringToObject(buzz, "current_alert_class", b.active ? class_name(b.cls) : "None");
    number(buzz, "beep_count", b.beeps);
    number(buzz, "last_alert_us", b.last_alert_us);
    number(buzz, "completed_sequences", b.completed);
    number(buzz, "cancelled_sequences", b.cancelled);
#if CONFIG_VISION_DL_DUAL_CORE
    cJSON_AddStringToObject(j, "scheduling", "ESP-DL dual-core workers + bounded pipeline");
#else
    cJSON_AddStringToObject(j, "scheduling", "single-core ESP-DL on CPU1 + pipeline on CPU0");
#endif
    return send_json(req, j);
}
esp_err_t frame_handler(httpd_req_t *req) {
    PreviewLease l;
    if (!preview_acquire(l)) {
        httpd_resp_set_status(req, "503 Service Unavailable");
        return httpd_resp_send(req, "Preview disabled/not ready", HTTPD_RESP_USE_STRLEN);
    }
    struct Guard {
        PreviewLease &l;
        ~Guard() { preview_release(l); }
    } guard{l};
    auto *meta = json_frame(l.result);
    if (!meta)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Metadata allocation");
    char *json = cJSON_PrintUnformatted(meta);
    cJSON_Delete(meta);
    if (!json)
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Metadata allocation");
    size_t n = std::strlen(json);
    uint8_t prefix[] = {uint8_t(n), uint8_t(n >> 8), uint8_t(n >> 16), uint8_t(n >> 24)};
    httpd_resp_set_type(req, "application/octet-stream");
    httpd_resp_set_hdr(req, "Cache-Control", "no-store");
    auto e = httpd_resp_send_chunk(req, reinterpret_cast<const char *>(prefix), 4);
    if (e == ESP_OK)
        e = httpd_resp_send_chunk(req, json, n);
    cJSON_free(json);
    if (e == ESP_OK)
        e = httpd_resp_send_chunk(req, reinterpret_cast<const char *>(l.jpeg), l.length);
    if (e == ESP_OK)
        e = httpd_resp_send_chunk(req, nullptr, 0);
    return e;
}
cJSON *read_json(httpd_req_t *req) {
    if (req->content_len <= 0 || req->content_len > 2048)
        return nullptr;
    char type[64];
    if (httpd_req_get_hdr_value_str(req, "Content-Type", type, sizeof(type)) != ESP_OK ||
        std::strncmp(type, "application/json", 16))
        return nullptr;
    char body[2049];
    int done = 0;
    while (done < req->content_len) {
        int n = httpd_req_recv(req, body + done, req->content_len - done);
        if (n <= 0)
            return nullptr;
        done += n;
    }
    body[done] = 0;
    return cJSON_ParseWithLength(body, done);
}
bool number_field(cJSON *j, const char *name, double &v) {
    auto *x = cJSON_GetObjectItemCaseSensitive(j, name);
    if (!x)
        return true;
    if (!cJSON_IsNumber(x) || !std::isfinite(x->valuedouble))
        return false;
    v = x->valuedouble;
    return true;
}
esp_err_t config_handler(httpd_req_t *req) {
    auto *j = read_json(req);
    if (!j)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Expected JSON <=2048 bytes");
    Settings s = settings_get();
    bool ok = true;
    auto flag = [&](const char *k, bool &v) {
        auto *x = cJSON_GetObjectItemCaseSensitive(j, k);
        if (x) {
            if (!cJSON_IsBool(x))
                ok = false;
            else
                v = cJSON_IsTrue(x);
        }
    };
    auto real = [&](const char *k, float &v) {
        double n = v;
        if (!number_field(j, k, n))
            ok = false;
        else
            v = n;
    };
    auto integer = [&](const char *k, auto &v) {
        double n = v;
        if (!number_field(j, k, n) || n < 0 || n > 60000 || std::floor(n) != n)
            ok = false;
        else
            v = static_cast<decltype(v + 0)>(n);
    };
    flag("ai_enabled", s.ai_enabled);
    flag("buzzer_enabled", s.buzzer_enabled);
    flag("stream_enabled", s.stream_enabled);
    unsigned mode = static_cast<unsigned>(s.mode);
    integer("mode", mode);
    if (mode > 2)
        ok = false;
    s.mode = static_cast<Mode>(mode);
    real("confidence", s.confidence);
    real("roi_top", s.roi_top);
    real("roi_bottom", s.roi_bottom);
    real("left_boundary", s.left_boundary);
    real("right_boundary", s.right_boundary);
    real("danger_y", s.danger_y);
    integer("beep_ms", s.beep_ms);
    integer("gap_ms", s.gap_ms);
    integer("alert_interval_ms", s.alert_interval_ms);
    integer("confirm_frames", s.confirm_frames);
    auto *order = cJSON_GetObjectItemCaseSensitive(j, "priority");
    if (order) {
        if (!cJSON_IsArray(order) || cJSON_GetArraySize(order) != 4)
            ok = false;
        else
            for (int i = 0; i < 4; ++i) {
                auto *n = cJSON_GetArrayItem(order, i);
                if (!cJSON_IsNumber(n) || n->valuedouble < 0 || n->valuedouble > 3 ||
                    std::floor(n->valuedouble) != n->valuedouble)
                    ok = false;
                else
                    s.priority[i] = n->valueint;
            }
    }
    if (ok)
        ok = settings_set(s);
    bool persist = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "persist"));
    cJSON_Delete(j);
    if (!ok)
        return httpd_resp_send_err(
            req, HTTPD_400_BAD_REQUEST,
            "Invalid range/ROI/priority; interval must exceed max sequence by >=150ms");
    if (persist && !settings_save())
        return httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                   "RAM applied; NVS save failed, no erase performed");
    return send_json(req, json_settings(s));
}
esp_err_t test_handler(httpd_req_t *req) {
    auto *j = read_json(req);
    if (!j)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON required");
    auto *c = cJSON_GetObjectItemCaseSensitive(j, "class_id");
    bool ok = cJSON_IsNumber(c) && c->valuedouble >= 0 && c->valuedouble <= 3 &&
              std::floor(c->valuedouble) == c->valuedouble && buzzer_test(static_cast<ClassId>(c->valueint));
    cJSON_Delete(j);
    if (!ok)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Muted, busy, queue full or invalid class");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_sendstr(req, "{\"test_only\":true,\"ai_detection\":false}");
}
esp_err_t wifi_handler(httpd_req_t *req) {
    auto *j = read_json(req);
    if (!j)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON required");
    auto *ssid = cJSON_GetObjectItemCaseSensitive(j, "ssid"),
         *pass = cJSON_GetObjectItemCaseSensitive(j, "password");
    bool ok = cJSON_IsString(ssid) && cJSON_IsString(pass) &&
              wifi_configure(ssid->valuestring, pass->valuestring,
                             cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(j, "persist")));
    cJSON_Delete(j);
    if (!ok)
        return httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "WiFi config/save failed");
    return httpd_resp_sendstr(req, "WiFi connection requested; setup AP remains available");
}
esp_err_t csv_handler(httpd_req_t *req) {
    auto p = perf_get();
    httpd_resp_set_type(req, "text/csv");
    httpd_resp_set_hdr(req, "Content-Disposition", "attachment; filename=vision-performance.csv");
    httpd_resp_sendstr_chunk(req, "metric,count,mean,p50,p95,p99,max\n");
    char line[256];
    for (unsigned i = 0; i < MetricCount; ++i) {
        auto d = p.latency[i];
        std::snprintf(line, sizeof(line), "%s,%lu,%.4f,%.4f,%.4f,%.4f,%.4f\n", metric_names[i],
                      (unsigned long)d.count, double(d.mean), double(d.p50), double(d.p95), double(d.p99),
                      double(d.max));
        if (httpd_resp_sendstr_chunk(req, line) != ESP_OK)
            return ESP_FAIL;
    }
    std::snprintf(line, sizeof(line),
                  "ai_fps,1,%.4f,,,,\nframe_drops,1,%lu,,,,\npreview_drops,1,%lu,,,,\ninternal_free,1,%lu,,,,"
                  "\npsram_free,1,%lu,,,,\n",
                  double(p.fps), (unsigned long)p.dropped, (unsigned long)p.preview_dropped,
                  (unsigned long)p.internal_free, (unsigned long)p.psram_free);
    httpd_resp_sendstr_chunk(req, line);
    return httpd_resp_send_chunk(req, nullptr, 0);
}
} // namespace
bool web_start() {
    httpd_config_t c = HTTPD_DEFAULT_CONFIG();
    c.core_id = 0;
    c.task_priority = 2;
    c.stack_size = 10240;
    c.max_uri_handlers = 8;
    c.max_open_sockets = 3;
    c.lru_purge_enable = true;
    c.recv_wait_timeout = 3;
    c.send_wait_timeout = 3;
    httpd_handle_t server = nullptr;
    if (httpd_start(&server, &c) != ESP_OK)
        return false;
    const httpd_uri_t routes[] = {
        {.uri = "/", .method = HTTP_GET, .handler = index, .user_ctx = nullptr},
        {.uri = "/api/status", .method = HTTP_GET, .handler = status_handler, .user_ctx = nullptr},
        {.uri = "/api/frame", .method = HTTP_GET, .handler = frame_handler, .user_ctx = nullptr},
        {.uri = "/api/config", .method = HTTP_POST, .handler = config_handler, .user_ctx = nullptr},
        {.uri = "/api/buzzer-test", .method = HTTP_POST, .handler = test_handler, .user_ctx = nullptr},
        {.uri = "/api/wifi", .method = HTTP_POST, .handler = wifi_handler, .user_ctx = nullptr},
        {.uri = "/api/performance.csv", .method = HTTP_GET, .handler = csv_handler, .user_ctx = nullptr}};
    for (const auto &route : routes)
        if (httpd_register_uri_handler(server, &route) != ESP_OK) {
            httpd_stop(server);
            return false;
        }
    auto h = xTaskGetHandle("httpd");
    if (h)
        perf_task_register("httpd", h);
    return true;
}
} // namespace vision
