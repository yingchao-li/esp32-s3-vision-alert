#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "runtime.hpp"
#include <cstdio>
#include <cstring>
namespace vision {
namespace {
esp_netif_t *ap = nullptr, *station = nullptr;
bool started = false;
portMUX_TYPE wifi_mux = portMUX_INITIALIZER_UNLOCKED;
bool reconnect = false;
} // namespace
void wifi_poll() {
    static int64_t last = 0;
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&wifi_mux);
    bool attempt = reconnect;
    portEXIT_CRITICAL(&wifi_mux);
    if (!attempt || now - last < 15000000)
        return;
    last = now;
    wifi_ap_record_t record;
    if (esp_wifi_sta_get_ap_info(&record) != ESP_OK)
        esp_wifi_connect();
}
void wifi_ip(char *out, size_t len) {
    esp_netif_ip_info_t info = {};
    if (station)
        esp_netif_get_ip_info(station, &info);
    if (!info.ip.addr && ap)
        esp_netif_get_ip_info(ap, &info);
    std::snprintf(out, len, IPSTR, IP2STR(&info.ip));
}
bool wifi_configure(const char *ssid, const char *password, bool persist) {
    if (!started || std::strlen(ssid) > 32 || std::strlen(password) > 63 || !ssid[0])
        return false;
    wifi_config_t c = {};
    std::memcpy(c.sta.ssid, ssid, std::strlen(ssid));
    std::memcpy(c.sta.password, password, std::strlen(password));
    esp_wifi_disconnect();
    if (esp_wifi_set_config(WIFI_IF_STA, &c) != ESP_OK)
        return false;
    if (persist) {
        nvs_handle_t h;
        if (nvs_open("vision", NVS_READWRITE, &h) != ESP_OK)
            return false;
        bool ok = nvs_set_str(h, "ssid", ssid) == ESP_OK && nvs_set_str(h, "wifi_pass", password) == ESP_OK &&
                  nvs_commit(h) == ESP_OK;
        nvs_close(h);
        if (!ok)
            return false;
    }
    portENTER_CRITICAL(&wifi_mux);
    reconnect = true;
    portEXIT_CRITICAL(&wifi_mux);
    return esp_wifi_connect() == ESP_OK;
}
bool wifi_start() {
    if (esp_netif_init() != ESP_OK)
        return false;
    auto event = esp_event_loop_create_default();
    if (event != ESP_OK && event != ESP_ERR_INVALID_STATE)
        return false;
    ap = esp_netif_create_default_wifi_ap();
    station = esp_netif_create_default_wifi_sta();
    if (!ap || !station)
        return false;
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    init.nvs_enable = 0;
    if (esp_wifi_init(&init) != ESP_OK)
        return false;
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_wifi_set_ps(WIFI_PS_NONE);
    wifi_config_t c = {};
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    std::snprintf(reinterpret_cast<char *>(c.ap.ssid), sizeof(c.ap.ssid), "ESP32-Vision-%02X%02X", mac[4],
                  mac[5]);
    std::strncpy(reinterpret_cast<char *>(c.ap.password), CONFIG_VISION_AP_PASSWORD,
                 sizeof(c.ap.password) - 1);
    c.ap.authmode = WIFI_AUTH_WPA2_PSK;
    c.ap.max_connection = 3;
    c.ap.channel = 1;
    if (std::strlen(CONFIG_VISION_AP_PASSWORD) < 8) {
        health_error("AP password must contain >=8 characters");
        return false;
    }
    if (esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK || esp_wifi_set_config(WIFI_IF_AP, &c) != ESP_OK ||
        esp_wifi_start() != ESP_OK)
        return false;
    started = true;
    ESP_LOGI("WIFI", "Setup AP: %s, http://192.168.4.1 (WPA2 password in README)", c.ap.ssid);
    char ssid[33] = CONFIG_VISION_WIFI_SSID, password[64] = CONFIG_VISION_WIFI_PASSWORD;
    nvs_handle_t h;
    if (nvs_open("vision", NVS_READONLY, &h) == ESP_OK) {
        size_t n = sizeof(ssid), m = sizeof(password);
        nvs_get_str(h, "ssid", ssid, &n);
        nvs_get_str(h, "wifi_pass", password, &m);
        nvs_close(h);
    }
    if (ssid[0])
        wifi_configure(ssid, password, false);
    // Reconnection attempts happen in the low-priority monitor task; AI has no
    // dependency on network events or successful association.
    return true;
}
} // namespace vision
