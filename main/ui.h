#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "esp_err.h"

#include "lvgl.h"

#define LOOP_FREQ_MS 10

typedef struct ui {
    // Internal UI state
    lv_obj_t *status_top_label;
    lv_obj_t *ui_text_label;

    char      status_buf[128];
    char      ui_text_buf[256];

    uint32_t  ticks;
} ui_t;

// Initialize LVGL, register the display driver.
esp_err_t ui_init(ui_t *ui);

// Updates the UI.
void ui_tick(ui_t *ui);

// Starts the UI task.
void ui_task_start(QueueHandle_t cmd_q, QueueHandle_t status_q);
