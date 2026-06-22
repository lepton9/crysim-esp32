#pragma once

#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Commands sent from UI task to client task.
typedef enum {
    CMD_BUTTON1_PRESS = 0,
    CMD_BUTTON2_PRESS = 1,
} client_cmd_type_t;

typedef struct {
    client_cmd_type_t type;
} client_cmd_t;

// Status sent from client task to UI task.
typedef struct {
    uint32_t button1_presses;
    uint32_t button2_presses;
    uint32_t seq;
} client_status_t;

// Starts the client task.
// cmd_q: UI -> client commands.
// status_q: client -> UI status.
void client_task_start(QueueHandle_t cmd_q, QueueHandle_t status_q);
