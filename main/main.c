#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "pin_config.h"
#include "display_s3.h"
#include "ui.h"
#include "client.h"

#define USB_LEFT false
#define LOOP_FREQ_MS 10

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


    client_t client;
    ui_t ui;

    ESP_ERROR_CHECK(ui_init(&ui));

    ui.ptr = &client;
    ui.vtable.get_ui_text = &get_ui_text;

    configure_input((gpio_num_t)PIN_BUTTON_1, true);
    configure_input((gpio_num_t)PIN_BUTTON_2, true);

    while (true) {
        const int b1 = gpio_get_level((gpio_num_t)PIN_BUTTON_1);
        const int b2 = gpio_get_level((gpio_num_t)PIN_BUTTON_2);
        set_buttons(&client, b1, b2);
        ui_tick(&ui);

        vTaskDelay(pdMS_TO_TICKS(LOOP_FREQ_MS));
    }
}
