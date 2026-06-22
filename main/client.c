#include "client.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "client";

typedef struct {
    QueueHandle_t cmd_q;
    QueueHandle_t status_q;
} client_task_args_t;

static void client_task(void *arg) {
    client_task_args_t *a = (client_task_args_t *)arg;
    QueueHandle_t cmd_q = a->cmd_q;
    QueueHandle_t status_q = a->status_q;

    client_status_t st = {
        .button1_presses = 0,
        .button2_presses = 0,
        .seq             = 0,
    };

    xQueueOverwrite(status_q, &st);

    while (true) {
        client_cmd_t cmd;
        if (xQueueReceive(cmd_q, &cmd, portMAX_DELAY) != pdTRUE) continue;

        switch (cmd.type) {
        case CMD_BUTTON1_PRESS:
            st.button1_presses++;
            break;
        case CMD_BUTTON2_PRESS:
            st.button2_presses++;
            break;
        default:
            break;
        }

        st.seq++;

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
