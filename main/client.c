#include "client.h"

#include <math.h>
#include <limits.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_err.h"
#include "esp_netif.h"
#include "freertos/task.h"

#include <network_provisioning/manager.h>
#include <time.h>

#include "ble_prov.h"
#include "lwipopts.h"
#include "wifi.h"
#include "tcp_client.h"
#include "protocol.h"

static const char *TAG = "client";

#define REQUEST_ID(id) id < UINT_MAX ? id + 1 : 1

int now_ts() {
    // TODO: get unix timestamp
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return tv.tv_sec;
}

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

void request_login(tcp_client_status *st) {
    st->req_id = REQUEST_ID(st->req_id);
    st->logging_in_id = st->req_id;

    tcp_client_request_t request = {
        .request_id      = st->req_id,
        .expect_response = true,
    };

    cJSON *json = build_request_json(st->req_id, st->token, "login");
    const char *username = CONFIG_TCP_CLIENT_USERNAME;
    const char *password = CONFIG_TCP_CLIENT_PASSWORD;
    cJSON *params = build_params_login(username, password);
    cJSON_AddItemToObject(json, "params", params);
    int n = json_string(json, request.data, TCP_CLIENT_MAX_REQUEST_LEN);
    cJSON_Delete(json);
    if (n <= 0) return;
    request.len = n;

    bool ok = tcp_client_enqueue(&request, 0);
    if (!ok) ESP_LOGE(TAG, "Login request enqueue failed");
}

void request_state(tcp_client_status *st) {
    st->req_id = REQUEST_ID(st->req_id);

    tcp_client_request_t request = {
        .request_id      = st->req_id,
        .expect_response = true,
    };

    cJSON *json = build_request_json(st->req_id, st->token, "state");
    int n = json_string(json, request.data, TCP_CLIENT_MAX_REQUEST_LEN);
    cJSON_Delete(json);
    if (n <= 0) return;
    request.len = n;

    bool ok = tcp_client_enqueue(&request, 0);
    if (ok) st->fetching_state = true;
    else ESP_LOGE(TAG, "State request enqueue failed");
}

static void str_copy(char *dst, size_t dst_len, const char *src) {
    if (!dst || dst_len == 0) return;
    if (!src) {
        dst[0] = 0;
        return;
    }
    strlcpy(dst, src, dst_len);
}

static bool json_get_double_any(cJSON *obj, const char *key, double *out) {
    if (!obj || !key || !out) return false;
    cJSON *it = cJSON_GetObjectItem(obj, key);
    if (!it) return false;
    if (cJSON_IsNumber(it)) {
        *out = it->valuedouble;
        return true;
    }
    if (cJSON_IsString(it) && it->valuestring) {
        char *end = NULL;
        double v = strtod(it->valuestring, &end);
        if (end == it->valuestring) return false;
        *out = v;
        return true;
    }
    return false;
}

static void clear_state(tcp_client_status *st) {
    if (!st) return;
    st->have_state = false;
    st->equity_usd = 0;
    st->net_deposits_usd = 0;
    st->pnl_usd = 0;
    st->fees_paid_usd = 0;
    st->balances_len = 0;
    st->assets_len = 0;
}

static void parse_state_result(tcp_client_status *st, cJSON *result, int now_s) {
    if (!st || !result) return;
    clear_state(st);

    (void)json_get_double_any(result, "equity_usd", &st->equity_usd);
    (void)json_get_double_any(result, "net_deposits_usd", &st->net_deposits_usd);
    (void)json_get_double_any(result, "pnl_usd", &st->pnl_usd);
    (void)json_get_double_any(result, "fees_paid_usd", &st->fees_paid_usd);

    cJSON *balances = cJSON_GetObjectItem(result, "balances");
    if (cJSON_IsArray(balances)) {
        const int n = cJSON_GetArraySize(balances);
        for (int i = 0; i < n && st->balances_len < MAX_BALANCES; i++) {
            cJSON *b = cJSON_GetArrayItem(balances, i);
            if (!cJSON_IsObject(b)) continue;
            const char *asset = cJSON_GetStringValue(cJSON_GetObjectItem(b, "asset"));
            double amount = 0;
            if (!json_get_double_any(b, "amount", &amount)) continue;
            str_copy(st->balances[st->balances_len].asset, sizeof(st->balances[st->balances_len].asset), asset);
            st->balances[st->balances_len].amount = amount;
            st->balances_len++;
        }
    }

    cJSON *assets = cJSON_GetObjectItem(result, "assets");
    if (cJSON_IsArray(assets)) {
        const int n = cJSON_GetArraySize(assets);
        for (int i = 0; i < n && st->assets_len < MAX_ASSETS; i++) {
            cJSON *a = cJSON_GetArrayItem(assets, i);
            if (!cJSON_IsObject(a)) continue;
            const char *asset = cJSON_GetStringValue(cJSON_GetObjectItem(a, "asset"));

            double qty = 0;
            if (!json_get_double_any(a, "qty", &qty)) continue;

            uint8_t idx = st->assets_len;
            str_copy(st->assets[idx].asset, sizeof(st->assets[idx].asset), asset);
            st->assets[idx].qty = qty;
            (void)json_get_double_any(a, "cost_basis_usd", &st->assets[idx].cost_basis_usd);
            (void)json_get_double_any(a, "spot_price_usd", &st->assets[idx].spot_price_usd);
            (void)json_get_double_any(a, "market_value_usd", &st->assets[idx].market_value_usd);
            (void)json_get_double_any(a, "unrealized_pnl_usd", &st->assets[idx].unrealized_pnl_usd);
            st->assets_len++;
        }
    }

    st->have_state = true;
    st->last_state_ts = now_s;
}

// Get and handle a response from the queue if there is one.
void try_get_response(client_status_t *st) {
    tcp_client_response_t response = { 0 };

    if (!tcp_client_dequeue_response(&response, 0)) return;

    cJSON *res = cJSON_ParseWithLength(response.data, response.len);
    if (!res) return;
    ESP_LOGI(TAG, "Response: %s", response.data);

    bool ok = cJSON_IsTrue(cJSON_GetObjectItem(res, "ok"));
    if (!ok) {
        cJSON *err = cJSON_GetObjectItem(res, "error");
        if (err) {
            ESP_LOGE(TAG, "Response error: %s", response.data);
            char *code = cJSON_GetStringValue(cJSON_GetObjectItem(err, "code"));
            if (strcmp(code, "unauthorized") == 0) st->data.logged_in = false;
        }
        goto end;
    }

    cJSON *result = cJSON_GetObjectItem(res, "result");
    const uint32_t req_id = response.request_id;

    // Login response
    if (req_id != 0 && req_id == st->data.logging_in_id) {
        char *token = cJSON_GetStringValue(cJSON_GetObjectItem(result, "token"));
        double expires_at_ms = 0;
        bool have_expires = json_get_double_any(result, "expires_at_ms", &expires_at_ms);
        if (token) {
            strlcpy(st->data.token, token, sizeof(st->data.token));
            st->data.logging_in_id = 0;
            st->data.logged_in = true;
        }
        if (have_expires) st->data.expires_at_s = (int)(expires_at_ms / 1000.0);
    } else {
        st->data.fetching_state = false;
        // State response
        if (cJSON_IsObject(result))
            parse_state_result(&st->data, result, now_ts());
    }
 end:
    cJSON_Delete(res);
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

    // Start the TCP client task. Waits if no wifi connected.
    tcp_client_start();

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


        int now = now_ts();
        // bool token_expired = st.data.expires_at_s < now;
        // ESP_LOGI(TAG, "%d %d", now, st.data.expires_at_s);

        // Get state or login if not logged in already
        if (st.data.logged_in && !st.data.fetching_state &&
            now - st.data.last_state_ts > STATE_FETCH_FREQ_S)
            request_state(&st.data);
        else if (!st.data.logged_in && st.data.logging_in_id == 0)
            request_login(&st.data);

        try_get_response(&st);

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
