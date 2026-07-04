#include "client.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "freertos/task.h"

#include <network_provisioning/manager.h>

#include "ble_prov.h"
#include "wifi.h"

static const char *TAG = "client";

static void update_wifi_status(client_status_t *st) {
    if (!st) return;

    st->wifi_connected = false;
    st->wifi_ssid[0] = 0;
    st->wifi_ip[0] = 0;

    wifi_ap_record_t ap = { 0 };
    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        st->wifi_connected = true;
        memcpy(st->wifi_ssid, ap.ssid, sizeof(ap.ssid));
        st->wifi_ssid[sizeof(st->wifi_ssid) - 1] = 0;
    } else {
        wifi_config_t cfg = { 0 };
        if (esp_wifi_get_config(WIFI_IF_STA, &cfg) == ESP_OK && cfg.sta.ssid[0]) {
            memcpy(st->wifi_ssid, cfg.sta.ssid, sizeof(cfg.sta.ssid));
            st->wifi_ssid[sizeof(st->wifi_ssid) - 1] = 0;
        }
    }

    if (st->wifi_connected) {
        esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
        esp_netif_ip_info_t ip_info = { 0 };
        if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK)
            snprintf(st->wifi_ip, sizeof(st->wifi_ip), IPSTR, IP2STR(&ip_info.ip));
    }
}

typedef struct {
    QueueHandle_t cmd_q;
    QueueHandle_t status_q;
} client_task_args_t;

typedef struct {
    TaskHandle_t client_task;
    const char * name_prefix;
    int          security;
    const char * pop;
} prov_task_args_t;

static void prov_task(void *arg) {
    prov_task_args_t *a = (prov_task_args_t *)arg;

    (void)ble_prov_start_provisioning(a->name_prefix, a->security, a->pop);

    // Notify the client task that provisioning has ended
    if (a->client_task) xTaskNotifyGiveIndexed(a->client_task, PROV_NOTIFY_IDX);
    vTaskDelete(NULL);
}

static void maybe_start_provisioning(
    client_status_t * st,
    bool *            prov_running,
    TaskHandle_t *    prov_task_handle,
    prov_task_args_t *prov_args,
    bool              reset_and_prov
    ) {
    if (!st || !prov_running || !prov_task_handle || !prov_args) return;

    st->ui_mode = CLIENT_UI_PROVISIONING;
    ble_prov_get_device_service_name(st->ble_name, sizeof(st->ble_name), BLE_PROV_DEVICE_NAME_PREFIX);

    if (*prov_running) return;

    if (reset_and_prov) {
        // Clear stored Wi-Fi credentials
        ESP_ERROR_CHECK(network_prov_mgr_reset_wifi_provisioning());
        ESP_ERROR_CHECK(esp_wifi_restore());

        wifi_mode_t mode = WIFI_MODE_NULL;
        ESP_ERROR_CHECK(esp_wifi_get_mode(&mode));
        if (mode != WIFI_MODE_STA && mode != WIFI_MODE_APSTA)
            ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

        wifi_config_t empty_cfg = { 0 };
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &empty_cfg));
    }

    wifi_set_autoreconnect(false);

    // Clear any stale completion notifications
    while (ulTaskNotifyTakeIndexed(PROV_NOTIFY_IDX, pdTRUE, 0) > 0) {
    }

    BaseType_t ok = xTaskCreate(prov_task, "ble_prov", 8192, prov_args, 5, prov_task_handle);
    *prov_running = (ok == pdPASS);
    if (!*prov_running)
        ESP_LOGE(TAG, "failed to start provisioning task");
}

// If provisioning succeeded, connect Wi-Fi
static void connect_wifi_maybe() {
    bool provisioned = false;
    if (ble_prov_is_provisioned(&provisioned) == ESP_OK && provisioned)
        wifi_init_sta();
}

static void client_task(void *arg) {
    client_task_args_t *a = (client_task_args_t *)arg;
    QueueHandle_t cmd_q = a->cmd_q;
    QueueHandle_t status_q = a->status_q;

    client_status_t st;

    memset(&st, 0, sizeof(st));
    st.ui_mode = CLIENT_UI_NORMAL;

    update_wifi_status(&st);

    xQueueOverwrite(status_q, &st);

    init_nvs();
    init_wifi();
    connect_wifi_maybe();

    bool prov_running = false;
    TaskHandle_t prov_task_handle = NULL;
    static prov_task_args_t prov_args;
    memset(&prov_args, 0, sizeof(prov_args));
    prov_args.client_task = xTaskGetCurrentTaskHandle();
    prov_args.name_prefix = BLE_PROV_DEVICE_NAME_PREFIX;
    prov_args.security = BLE_PROV_SECURITY_VERSION;
    prov_args.pop = BLE_PROV_POP;

    while (true) {
        // Check if provisioning ended
        if (ulTaskNotifyTakeIndexed(PROV_NOTIFY_IDX, pdTRUE, 0) > 0) {
            prov_running = false;
            prov_task_handle = NULL;

            wifi_set_autoreconnect(true);

            if (st.ui_mode == CLIENT_UI_PROVISIONING) {
                st.ui_mode = CLIENT_UI_NORMAL;
                st.ble_name[0] = 0;
            }

            connect_wifi_maybe();
        }

        client_cmd_t cmd;
        if (xQueueReceive(cmd_q, &cmd, pdMS_TO_TICKS(250)) == pdTRUE) {
            switch (cmd.type) {
            case CMD_BUTTON1_PRESS:
                st.button1_presses++;
                break;
            case CMD_BUTTON2_PRESS:
                st.button2_presses++;
                break;
            case CMD_UI_ENTER_PROV_PROMPT:
                st.ui_mode = CLIENT_UI_PROV_PROMPT;
                break;
            case CMD_UI_START_PROV:
                maybe_start_provisioning(&st, &prov_running, &prov_task_handle, &prov_args, false);
                break;
            case CMD_UI_CANCEL_PROV:
                st.ui_mode = CLIENT_UI_NORMAL;
                st.ble_name[0] = 0;

                if (prov_running)
                    ble_prov_stop_provisioning();
                break;
            case CMD_UI_RESET_AND_PROV:
                maybe_start_provisioning(&st, &prov_running, &prov_task_handle, &prov_args, true);
                break;
            default:
                break;
            }
        }

        update_wifi_status(&st);
        xQueueOverwrite(status_q, &st);
    }
}

void client_task_start(QueueHandle_t cmd_q, QueueHandle_t status_q) {
    static client_task_args_t args;

    memset(&args, 0, sizeof(args));
    args.cmd_q = cmd_q;
    args.status_q = status_q;

    BaseType_t ok = xTaskCreate(client_task, "client", 4096, &args, 5, NULL);
    if (ok != pdPASS)
        ESP_LOGE(TAG, "failed to start client task");
}
