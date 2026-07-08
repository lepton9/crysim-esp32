#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#include "lvgl.h"

#define LOOP_FREQ_MS 10

typedef enum {
    UI_VIEW_PORTFOLIO = 0,
    UI_VIEW_MARKET    = 1,
} ui_view_t;

typedef struct ui {
    uint32_t  ticks;

    // The current UI view in normal mode
    ui_view_t view;

    // Internal UI state
    lv_obj_t *status_top_label;
    lv_obj_t *summary_label;
    lv_obj_t *assets_label;

    // Buffers for the labels
    char      status_buf[128];
    char      summary_buf[256];
    char      assets_buf[512];
} ui_t;

// Initialize LVGL, register the display driver.
esp_err_t ui_init(ui_t *ui);

// Updates the UI.
void ui_tick(ui_t *ui);

// Starts the UI task.
void ui_task_start(QueueHandle_t cmd_q, QueueHandle_t status_q);
