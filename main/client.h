#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define PROV_NOTIFY_IDX 1

#define TOKEN_LEN (64 + 1)

#define MAX_BALANCES 8
#define MAX_ASSETS 8
#define ASSET_SYM_LEN 8

#define STATE_FETCH_FREQ_S 5

// Commands sent from UI task to client task.
typedef enum {
    CMD_BUTTON1_PRESS        = 0,
    CMD_BUTTON2_PRESS        = 1,
    CMD_UI_ENTER_PROV_PROMPT = 2,
    CMD_UI_START_PROV        = 3,
    CMD_UI_CANCEL_PROV       = 4,
    CMD_UI_RESET_AND_PROV    = 5,
} client_cmd_type_t;

typedef struct {
    client_cmd_type_t type;
} client_cmd_t;

typedef enum {
    CLIENT_UI_NORMAL       = 0,
    CLIENT_UI_PROV_PROMPT  = 1,
    CLIENT_UI_PROVISIONING = 2,
} client_ui_mode_t;

typedef struct {
    uint32_t req_id;

    char     token[TOKEN_LEN];
    int      expires_at_s;

    uint32_t logging_in_id;
    bool     logged_in;

    // Most recent server state
    bool     have_state;
    int      last_state_ts;
    bool     fetching_state;

    // Summary
    double   equity_usd;
    double   net_deposits_usd;
    double   pnl_usd;
    double   fees_paid_usd;

    // Balances
    uint8_t  balances_len;
    struct {
        char   asset[ASSET_SYM_LEN];
        double amount;
    } balances[MAX_BALANCES];

    // Owned assets
    uint8_t assets_len;
    struct {
        char   asset[ASSET_SYM_LEN];
        double qty;
        double cost_basis_usd;
        double spot_price_usd;
        double market_value_usd;
        double unrealized_pnl_usd;
    } assets[MAX_ASSETS];
} tcp_client_status;

// Status sent from client task to UI task.
typedef struct {
    uint32_t          button1_presses;
    uint32_t          button2_presses;

    client_ui_mode_t  ui_mode;

    // Name for BLE provisioning
    char              ble_name[20];

    // Wi-Fi status snapshot for UI
    bool              wifi_connected;
    char              wifi_ssid[33];
    char              wifi_ip[16];

    // Data
    tcp_client_status data;
} client_status_t;

typedef struct {
    uint32_t id;
    char     token[TOKEN_LEN];
    char     method[64];
    char     params[128];
} server_request;

// Starts the client task.
// cmd_q: UI -> client commands.
// status_q: client -> UI status.
void client_task_start(QueueHandle_t cmd_q, QueueHandle_t status_q);
