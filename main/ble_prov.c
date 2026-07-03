#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_err.h>
#include <esp_netif.h>
#include <nvs_flash.h>
#include <nvs.h>
#include <network_provisioning/scheme_ble.h>
#include <network_provisioning/manager.h>

#include "ble_prov.h"
#include "network_provisioning/network_config.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof(*(a)))
#endif

static char const * const TAG = "ble_prov";

/* Signal Wi-Fi events on this event-group */
const int WIFI_PROV_END_EVENT = BIT0;
static EventGroupHandle_t _prov_event_group;

esp_err_t
ble_prov_is_provisioned(bool *provisioned) {
    *provisioned = false;

    wifi_config_t wifi_cfg;
    if (esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg) != ESP_OK)
        return ESP_FAIL;
    if (strlen((const char *)wifi_cfg.sta.ssid)) {
        *provisioned = true;
        ESP_LOGI(TAG, "Found SSID %s", (const char *)wifi_cfg.sta.ssid);
        //ESP_LOGI(TAG, "Found password %s", (const char*) wifi_cfg.sta.password);
    }
    return ESP_OK;
}

/* Event handler for catching system events */
static void prov_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    (void)event_base;
    switch (event_id) {
    case NETWORK_PROV_START:
        ESP_LOGI(TAG, "Provisioning started");
        break;
    case NETWORK_PROV_WIFI_CRED_RECV: {
        wifi_sta_config_t *wifi_sta_cfg = (wifi_sta_config_t *)event_data;

        ESP_LOGI(TAG, "Received SSID (%s), passwd (%s)",
                 (const char *)wifi_sta_cfg->ssid,
                 (const char *)wifi_sta_cfg->password);
        break;
    }
    case NETWORK_PROV_WIFI_CRED_FAIL: {
        network_prov_wifi_sta_fail_reason_t *reason = (network_prov_wifi_sta_fail_reason_t *)event_data;
        ESP_LOGE(TAG, "Provisioning failed!\n\tReason : %s" "\n\tPlease retry provisioning",
                 (*reason == NETWORK_PROV_WIFI_STA_AUTH_ERROR) ?

                 "Wi-Fi station authentication failed" : "Wi-Fi access-point not found");

        network_prov_mgr_reset_wifi_sm_state_on_failure();
        break;
    }
    case NETWORK_PROV_WIFI_CRED_SUCCESS:
        ESP_LOGI(TAG, "Wi-Fi provisioning successful");
        // User request: exit provisioning immediately on success.
        // Success is emitted on IP_EVENT_STA_GOT_IP, so we can stop the service right away.
        network_prov_mgr_stop_provisioning();
        break;

    case NETWORK_PROV_END:
        ESP_LOGI(TAG, "Wi-Fi Provisioning ended");
        // Service is already stopped when END is emitted; just release resources.
        network_prov_mgr_deinit();
        xEventGroupSetBits(_prov_event_group, WIFI_PROV_END_EVENT);
        break;
    default:
        break;
    }
}

void ble_prov_get_device_service_name(char *service_name, size_t max, const char *prefix) {
    if (!service_name || max == 0) return;

    uint8_t sta_mac[6];
    const char *pfx = (prefix && prefix[0]) ? prefix : "PROV_";

    esp_wifi_get_mac(WIFI_IF_STA, sta_mac);
    snprintf(service_name, max, "%s%02X%02X%02X",
             pfx, sta_mac[3], sta_mac[4], sta_mac[5]);
}

static void _get_device_service_name(char *service_name, size_t max, const char *prefix) {
    ble_prov_get_device_service_name(service_name, max, prefix);
}

void ble_prov_stop_provisioning(void) {
    network_prov_mgr_stop_provisioning();
}

/* Handlers for the optional provisioning endpoint registered by the application.
 * The data format can be chosen by applications. Here, we are using plain ascii text.
 * Applications can choose to use other formats like protobuf, JSON, XML, etc.
 */
esp_err_t _mqtt_config_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                               uint8_t **outbuf, ssize_t *outlen, void *priv_data) {
    if (inbuf) {
        //ESP_LOGI(TAG, "Received data: \"%.*s\"", inlen, (char *)inbuf);

        // store values in non volatile storage (flash)
        char *str = strndup((const char * const)inbuf, inlen);
        {
            if (strcmp(str, "null") == 0)
                str = strdup("\0");
            ESP_LOGI(TAG, "%s Received MQTT_URL: (%s)", __FUNCTION__, str);
            nvs_handle_t nvs_handle;
            ESP_ERROR_CHECK(nvs_open("storage", NVS_READWRITE, &nvs_handle));
            ESP_ERROR_CHECK(nvs_set_str(nvs_handle, "mqtt_url", str));
            ESP_ERROR_CHECK(nvs_commit(nvs_handle));
            nvs_close(nvs_handle);
        }
        free(str);
    }

    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}

esp_err_t _mqtt_status_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                               uint8_t **outbuf, ssize_t *outlen, void *priv_data) {
    ESP_LOGI(TAG, "%s called", __FUNCTION__);

    if (inbuf)
        ESP_LOGI(TAG, "%s Received data: %.*s", __FUNCTION__, inlen, (char *)inbuf);
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1; // +1 for NULL terminating byte

    return ESP_OK;
}

esp_err_t _ota_status_handler(uint32_t session_id, const uint8_t *inbuf, ssize_t inlen,
                              uint8_t **outbuf, ssize_t *outlen, void *priv_data) {
    ESP_LOGI(TAG, "%s called", __FUNCTION__);

    if (inbuf)
        ESP_LOGI(TAG, "%s Received data: %.*s", __FUNCTION__, inlen, (char *)inbuf);
    char response[] = "SUCCESS";
    *outbuf = (uint8_t *)strdup(response);
    if (*outbuf == NULL) {
        ESP_LOGE(TAG, "System out of memory");
        return ESP_ERR_NO_MEM;
    }
    *outlen = strlen(response) + 1; /* +1 for NULL terminating byte */

    return ESP_OK;
}

esp_err_t ble_prov_start_provisioning(const char *ble_device_name_prefix, int security, char const * const pop) {
    // register event handlers for WiFi, IP and Provisioning related events
    ESP_ERROR_CHECK(esp_event_handler_register(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));

    network_prov_mgr_config_t config = {
        .scheme               = network_prov_scheme_ble,
        //.scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM  // app doesn't require BT/BLE
        .scheme_event_handler = NETWORK_PROV_EVENT_HANDLER_NONE  // test
    };

    ESP_ERROR_CHECK(network_prov_mgr_init(config));
#if 0
    ESP_ERROR_CHECK(wifi_prov_mgr_disable_auto_stop(100));  //  the provisioning service will only be stopped after an explicit call to wifi_prov_mgr_stop_provisioning()
#endif

    // Always start provisioning when requested (reprovision supported).
    ESP_LOGI(TAG, "Starting provisioning");
    _prov_event_group = xEventGroupCreate();

    // Find the Device Service Name that we want (the `device name`)
    char service_name[12];
    _get_device_service_name(service_name, sizeof(service_name), ble_device_name_prefix);

    const char *service_key = NULL; // ignored

    // Endpoints for additional custom data
    // see: https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/provisioning/provisioning.html
    network_prov_mgr_endpoint_create("mqtt-config");
    network_prov_mgr_endpoint_create("mqtt-status");
    network_prov_mgr_endpoint_create("ota-status");

    // Start provisioning service
    // (de-init is trigged by the default event loop handler)
    ESP_LOGW(TAG, "Calling network_prov_mgr_start_provisioning() ..");
    ESP_ERROR_CHECK(network_prov_mgr_start_provisioning(security, pop, service_name, service_key));

    network_prov_mgr_endpoint_register("mqtt-config", _mqtt_config_handler, NULL);
    network_prov_mgr_endpoint_register("mqtt-status", _mqtt_status_handler, NULL);
    network_prov_mgr_endpoint_register("ota-status", _ota_status_handler, NULL);

    // Wait until provisioning is completed
    xEventGroupWaitBits(_prov_event_group, WIFI_PROV_END_EVENT, false, true, portMAX_DELAY);

    ESP_LOGW(TAG, "Provisioning done.");

    // Avoid accumulating handlers across multiple provisioning attempts.
    (void)esp_event_handler_unregister(NETWORK_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler);

    return ESP_OK;
}

// Application-specific context used by network provisioning handlers.
// The network_provisioning component declares `network_prov_ctx_t` as an opaque type.
struct network_prov_ctx {
    wifi_config_t wifi_cfg;
};

static wifi_config_t * _get_config(network_prov_ctx_t **ctx) {
    return *ctx ? &(*ctx)->wifi_cfg : NULL;
}

static wifi_config_t * _new_config(network_prov_ctx_t **ctx) {
    free(*ctx);
    (*ctx) = (network_prov_ctx_t *)calloc(1, sizeof(**ctx));
    return _get_config(ctx);
}

static void _free_config(network_prov_ctx_t **ctx) {
    free(*ctx);
    *ctx = NULL;
}

static esp_err_t _get_status_handler(network_prov_config_get_wifi_data_t *resp_data, network_prov_ctx_t **ctx) {
    memset(resp_data, 0, sizeof(network_prov_config_get_wifi_data_t));

    if (ble_prov_get_wifi_state(&resp_data->wifi_state) != ESP_OK) {
        ESP_LOGW(TAG, "Prov app not running");
        return ESP_FAIL;
    }

    if (resp_data->wifi_state == NETWORK_PROV_WIFI_STA_CONNECTED) {
        ESP_LOGI(TAG, "Connected state");

        /* IP Addr assigned to STA */
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"), &ip_info);
        esp_ip4addr_ntoa(&ip_info.ip, resp_data->conn_info.ip_addr, sizeof(resp_data->conn_info.ip_addr));

        /* AP information to which STA is connected */
        wifi_ap_record_t ap_info;
        esp_wifi_sta_get_ap_info(&ap_info);
        memcpy(resp_data->conn_info.bssid, (char *)ap_info.bssid, sizeof(ap_info.bssid));
        memcpy(resp_data->conn_info.ssid, (char *)ap_info.ssid, sizeof(ap_info.ssid));
        resp_data->conn_info.channel = ap_info.primary;
        resp_data->conn_info.auth_mode = ap_info.authmode;
    } else if (resp_data->wifi_state == NETWORK_PROV_WIFI_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected state");
        ble_prov_get_wifi_disconnect_reason(&resp_data->fail_reason);
    } else {
        ESP_LOGI(TAG, "Connecting ..");  // leave to main_app to handle this
    }
    return ESP_OK;
}

static esp_err_t _set_config_handler(const network_prov_config_set_wifi_data_t *req_data, network_prov_ctx_t **ctx) {
    wifi_config_t *wifi_cfg = _get_config(ctx);

    if (wifi_cfg)
        _free_config(ctx);

    wifi_cfg = _new_config(ctx);
    if (!wifi_cfg) {
        ESP_LOGE(TAG, "Unable to alloc wifi config");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Received WiFi credentials:\n\tssid %s \n\tpassword %s", req_data->ssid, req_data->password);

    strncpy((char *)wifi_cfg->sta.ssid, req_data->ssid, sizeof(wifi_cfg->sta.ssid));
    strlcpy((char *)wifi_cfg->sta.password, req_data->password, sizeof(wifi_cfg->sta.password));
    return ESP_OK;
}

static esp_err_t _apply_config_handler(network_prov_ctx_t **ctx) {
    wifi_config_t * const wifi_cfg = _get_config(ctx);

    if (!wifi_cfg) {
        ESP_LOGE(TAG, "WiFi config not set");
        return ESP_FAIL;
    }
    ble_prov_configure_sta(wifi_cfg);
    ESP_LOGI(TAG, "WiFi Credentials Applied");

    _free_config(ctx);
    return ESP_OK;
}

network_prov_config_handlers_t protocomm_handlers = {
    .wifi_get_status_handler   = _get_status_handler,
    .wifi_set_config_handler   = _set_config_handler,
    .wifi_apply_config_handler = _apply_config_handler,
    .ctx                       = NULL
};
