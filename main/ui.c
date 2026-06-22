#include "ui.h"

#include <stdio.h>
#include <string.h>

#include "esp_system.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "display_s3.h"
#include "pin_config.h"
#include "client.h"

static const char *TAG = "ui";

#define UI_LVGL_TICK_MS 5

#define RETURN_IF_ERR(expr) do { \
            esp_err_t __err = (expr); \
            if (__err != ESP_OK) return __err; \
} while (0)

static lv_disp_draw_buf_t s_draw_buf;
static esp_timer_handle_t s_lv_tick_timer;

static lv_disp_drv_t disp_drv;

static void lv_tick_timer_cb(void *arg) {
    (void)arg;
    lv_tick_inc(UI_LVGL_TICK_MS);
}

static void on_flush_done(void *ctx) {
    lv_disp_drv_t *drv = (lv_disp_drv_t *)ctx;

    lv_disp_flush_ready(drv);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map) {
    (void)drv;
    display_push_colors(area->x1, area->y1, area->x2 + 1, area->y2 + 1, (const uint16_t *)color_map);
}

esp_err_t ui_init(ui_t *ui) {
    if (!ui) return ESP_ERR_INVALID_ARG;
    memset(ui, 0, sizeof(*ui));

    ESP_LOGI(TAG, "Init LVGL");
    lv_init();

    display_set_flush_done_cb(on_flush_done, &disp_drv);

    const int buf_px = DISPLAY_BUFFER_SIZE;
    lv_color_t *buf1 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    lv_color_t *buf2 = heap_caps_malloc(buf_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (!buf1 || !buf2) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffers");
        return ESP_ERR_NO_MEM;
    }
    lv_disp_draw_buf_init(&s_draw_buf, buf1, buf2, buf_px);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISPLAY_HOR_RES;
    disp_drv.ver_res = DISPLAY_VER_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &s_draw_buf;
    disp_drv.full_refresh = DISPLAY_FULLRESH;
    lv_disp_drv_register(&disp_drv);

    if (!s_lv_tick_timer) {
        const esp_timer_create_args_t targs = {
            .callback = &lv_tick_timer_cb,
            .name     = "lv_tick",
        };
        RETURN_IF_ERR(esp_timer_create(&targs, &s_lv_tick_timer));
        RETURN_IF_ERR(esp_timer_start_periodic(s_lv_tick_timer, UI_LVGL_TICK_MS * 1000));
    }

    // Setup UI layout
    ui->ui_text_label = lv_label_create(lv_scr_act());;
    lv_label_set_text(ui->ui_text_label, "");
    lv_obj_align(ui->ui_text_label, LV_ALIGN_TOP_LEFT, 10, 10);

    return ESP_OK;
}

static void render_ui(ui_t *ui, const char *buffer) {
    if (!ui->ui_text_label) return;
    const char *text = buffer ? buffer : "";
    lv_label_set_text_fmt((lv_obj_t *)ui->ui_text_label, "%s", text);
}

void ui_tick(ui_t *ui, const char *buffer) {
    if (!ui) return;
    ui->ticks++;
    render_ui(ui, buffer);
    lv_timer_handler();
}

typedef struct {
    QueueHandle_t cmd_q;
    QueueHandle_t status_q;
} ui_task_args_t;

static void ui_task(void *arg) {
    ui_task_args_t *a = (ui_task_args_t *)arg;
    QueueHandle_t cmd_q = a->cmd_q;
    QueueHandle_t status_q = a->status_q;

    ui_t ui = { 0 };

    ESP_ERROR_CHECK(ui_init(&ui));

    bool prev_b1_pressed = false;
    bool prev_b2_pressed = false;

    bool have_status = false;
    client_status_t last_st = { 0 };

    char buffer[128];
    buffer[0] = 0;

    while (true) {
        client_status_t st;
        if (xQueueReceive(status_q, &st, 0) == pdTRUE) {
            last_st = st;
            have_status = true;
        }

        // Poll GPIO button presses
        const bool b1_pressed = gpio_get_level((gpio_num_t)PIN_BUTTON_1) == 0;
        const bool b2_pressed = gpio_get_level((gpio_num_t)PIN_BUTTON_2) == 0;
        if (b1_pressed && !prev_b1_pressed) {
            const client_cmd_t cmd = { .type = CMD_BUTTON1_PRESS };
            xQueueSend(cmd_q, &cmd, 0);
        }
        if (b2_pressed && !prev_b2_pressed) {
            const client_cmd_t cmd = { .type = CMD_BUTTON2_PRESS };
            xQueueSend(cmd_q, &cmd, 0);
        }
        prev_b1_pressed = b1_pressed;
        prev_b2_pressed = b2_pressed;

        if (have_status) {
            snprintf(buffer, sizeof(buffer),
                     "ticks=%lu\nclient: b1=%lu" " b2=%lu" "\nseq=%lu",
                     ui.ticks,
                     last_st.button1_presses,
                     last_st.button2_presses,
                     last_st.seq);
        }

        ui_tick(&ui, buffer);
        vTaskDelay(pdMS_TO_TICKS(LOOP_FREQ_MS));
    }
}

void ui_task_start(QueueHandle_t cmd_q, QueueHandle_t status_q) {
    static ui_task_args_t ui_args;

    ui_args.cmd_q = cmd_q;
    ui_args.status_q = status_q;
    BaseType_t ok = xTaskCreate(ui_task, "ui", 6144, &ui_args, 5, NULL);
    if (ok != pdPASS) abort();
}
