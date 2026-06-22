#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "lvgl.h"

typedef struct ui {
    // Internal UI state
    lv_obj_t *ui_text_label;
    uint32_t  ticks;

    // Context
    void *    ptr;
    struct {
        // Writes a UI string into the dst buffer.
        // Returns number of chars written.
        size_t (*get_ui_text)(void *ctx, char *dst, size_t dst_size);
    } vtable;
} ui_t;

// Initialize LVGL, register the display driver.
esp_err_t ui_init(ui_t *ui);

// Updates the UI.
void ui_tick(ui_t *ui);
