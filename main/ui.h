#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct ui {
    int button1;
    int button2;

    // Internal UI state
    void *label; // lv_obj_t*
    uint32_t ticks;
} ui_t;

// Initialize LVGL, register the display driver.
esp_err_t ui_init(ui_t *ui);

// Update inputs.
void ui_set_buttons(ui_t *ui, int b1_level, int b2_level);

// Updates the UI.
void ui_tick(ui_t *ui);
