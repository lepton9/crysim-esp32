#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define PROV_NOTIFY_IDX 1

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

// Status sent from client task to UI task.
typedef struct {
    uint32_t         button1_presses;
    uint32_t         button2_presses;

    client_ui_mode_t ui_mode;
    char             ble_name[20];

    // Wi-Fi status snapshot for UI
    bool             wifi_connected;
    char             wifi_ssid[33];
    char             wifi_ip[16];
} client_status_t;

// Starts the client task.
// cmd_q: UI -> client commands.
// status_q: client -> UI status.
void client_task_start(QueueHandle_t cmd_q, QueueHandle_t status_q);
