#include "ui.h"

#include <string.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "lvgl.h"

#include "display_s3.h"
#include "pin_config.h"

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

    // TODO: change ui
    ui->ui_text_label = lv_label_create(lv_scr_act());;
    lv_label_set_text(ui->ui_text_label, "");
    lv_obj_align(ui->ui_text_label, LV_ALIGN_TOP_LEFT, 10, 10);

    return ESP_OK;
}

void render_ui(ui_t *ui) {
    if (!ui->ui_text_label) return;


    int free_heap_bytes = esp_get_free_heap_size();

    lv_label_set_text_fmt((lv_obj_t *)ui->ui_text_label, "ticks=%lu\nb1=%d b2=%d\nfree=%d MB",
                          (unsigned long)ui->ticks, ui->button1, ui->button2, free_heap_bytes / 1000000);
}

void ui_set_buttons(ui_t *ui, int b1_level, int b2_level) {
    if (!ui) return;
    ui->button1 = b1_level;
    ui->button2 = b2_level;
}

void ui_tick(ui_t *ui) {
    if (!ui) return;
    ui->ticks++;

    render_ui(ui);

    lv_timer_handler();
}
