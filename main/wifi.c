#include "protocomm_security.h"
#include <stdint.h>
#include <string.h>
#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#include "wifi.h"

static char const * const TAG = "wifi";

static bool autoreconnect = true;
static EventGroupHandle_t wifi_event_group = NULL;

EventGroupHandle_t wifi_event_group_get(void) {
    return wifi_event_group;
}

void wifi_set_autoreconnect(bool enabled) {
    autoreconnect = enabled;
}

static struct retry_num {
    uint32_t ap_not_found;
    uint32_t ap_auth_fail;
} _retry_num = {};

void init_nvs(void) {
    esp_err_t err = nvs_flash_init();

    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

static void _handle_wifi_sta_start(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data) {
    (void)arg;
    (void)event_base;
    (void)event_id;
    (void)event_data;

    if (wifi_event_group) xEventGroupClearBits(wifi_event_group, WIFI_GOT_IP_BIT);
    if (!autoreconnect) return;

    // Avoid spamming connect attempts if no SSID is configured
    wifi_config_t cfg = { 0 };
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK || cfg.sta.ssid[0] == 0)
        return;

    esp_wifi_connect();
}

static void _handle_wifi_sta_disconnect(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data) {
    if (wifi_event_group) xEventGroupClearBits(wifi_event_group, WIFI_GOT_IP_BIT);
    if (!autoreconnect) return;

    // Avoid spamming connect attempts if no SSID is configured
    wifi_config_t cfg = { 0 };
    if (esp_wifi_get_config(WIFI_IF_STA, &cfg) != ESP_OK || cfg.sta.ssid[0] == 0)
        return;

    wifi_event_sta_disconnected_t *disconnected = (wifi_event_sta_disconnected_t *)event_data;

    switch (disconnected->reason) {
    case WIFI_REASON_NO_AP_FOUND:
        ESP_LOGW(TAG, "WiFi AP not found");
        if (_retry_num.ap_not_found < BLE_PROV_RECONN_ATTEMPTS) {
            _retry_num.ap_not_found++;
            esp_wifi_connect();
            ESP_LOGI(TAG, "retry connect to WiFi ..");
        }
        break;
    case WIFI_REASON_AUTH_EXPIRE:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_BEACON_TIMEOUT:
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_ASSOC_FAIL:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        ESP_LOGW(TAG, "WiFi auth error");
        if (_retry_num.ap_auth_fail < BLE_PROV_RECONN_ATTEMPTS) {
            _retry_num.ap_auth_fail++;
            esp_wifi_connect();
            ESP_LOGI(TAG, "retry connect to WiFi ..");
        }
        break;
    default:
        esp_wifi_connect();
        break;
    }
}

static void _handle_sta_got_ip(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data) {
    ip_event_got_ip_t const * const event = (ip_event_got_ip_t *)event_data;

    ESP_LOGI(TAG, "IP addr " IPSTR, IP2STR(&event->ip_info.ip));
    _retry_num.ap_not_found = _retry_num.ap_auth_fail = 0;

    if (wifi_event_group) xEventGroupSetBits(wifi_event_group, WIFI_GOT_IP_BIT);

    // this would be a great place to start OTA Update
    //xTaskCreate(&ota_update_task, "ota_update_task", 8192, NULL, 5, NULL);
}

void init_wifi() {
    if (!wifi_event_group) {
        wifi_event_group = xEventGroupCreate();
        if (wifi_event_group) xEventGroupClearBits(wifi_event_group, WIFI_GOT_IP_BIT);
    }

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();  // init WiFi with configuration from non-volatile storage
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, &_handle_wifi_sta_start, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, &_handle_wifi_sta_disconnect, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &_handle_sta_got_ip, NULL));
}

void wifi_init_sta(void) {
    ESP_LOGI(TAG, "starting WiFi");

    // start Wi-Fi in station mode with credentials set during provisioning
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
}
