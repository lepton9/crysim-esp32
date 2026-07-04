#include "freertos/FreeRTOS.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "sdkconfig.h"

#include "pin_config.h"
#include "display_s3.h"
#include "ui.h"
#include "client.h"


#define USB_LEFT false

static const char *TAG = "main";

static void configure_input(gpio_num_t pin, bool pullup) {
    const gpio_config_t cfg = {
        .pin_bit_mask     = 1ULL << pin,
            .mode         = GPIO_MODE_INPUT,
            .pull_up_en   = pullup ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
    };

    ESP_ERROR_CHECK(gpio_config(&cfg));
}

void app_main(void) {
    ESP_ERROR_CHECK(display_init(USB_LEFT));
    ESP_LOGI(TAG, "display init done");

    configure_input((gpio_num_t)PIN_BUTTON_1, true);
    configure_input((gpio_num_t)PIN_BUTTON_2, true);

    QueueHandle_t cmd_q = xQueueCreate(8, sizeof(client_cmd_t));
    QueueHandle_t status_q = xQueueCreate(1, sizeof(client_status_t));

    client_task_start(cmd_q, status_q);
    ESP_LOGI(TAG, "client task started");

    ui_task_start(cmd_q, status_q);
    ESP_LOGI(TAG, "ui task started");
}
