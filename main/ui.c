#include "ui.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_system.h"

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/task.h"

#include "display_s3.h"
#include "pin_config.h"
#include "client.h"
#include "widgets/lv_label.h"

static const char *TAG = "ui";

#define UI_LVGL_TICK_MS 5
#define BUTTON_HOLD_MS 2000
#define BUTTON_DEBOUNCE_MS 30

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
    ui->status_top_label = lv_label_create(lv_scr_act());;
    ui->ui_text_label = lv_label_create(lv_scr_act());;
    lv_label_set_text(ui->status_top_label, "");
    lv_label_set_text(ui->ui_text_label, "");
    lv_obj_align(ui->status_top_label, LV_ALIGN_TOP_LEFT, 10, 2);
    lv_obj_align(ui->ui_text_label, LV_ALIGN_TOP_LEFT, 10, 30);

    return ESP_OK;
}

static void render_ui(ui_t *ui) {
    if (!ui->ui_text_label || !ui->status_top_label) return;
    lv_label_set_text_fmt((lv_obj_t *)ui->status_top_label, "%s", ui->status_buf);
    lv_label_set_text_fmt((lv_obj_t *)ui->ui_text_label, "%s", ui->ui_text_buf);
}

void ui_tick(ui_t *ui) {
    if (!ui) return;
    ui->ticks++;
    render_ui(ui);
    lv_timer_handler();
}

typedef struct {
    QueueHandle_t cmd_q;
    QueueHandle_t status_q;
} ui_task_args_t;

static inline void send_cmd(QueueHandle_t q, client_cmd_type_t type) {
    if (!q) return;
    const client_cmd_t cmd = { .type = type };
    (void)xQueueSend(q, &cmd, 0);
}

typedef struct {
    bool    stable;
    bool    last_raw;
    uint8_t same_count;
} debounce_t;

static bool debounce_update(debounce_t *d, bool raw_pressed) {
    if (raw_pressed == d->last_raw) {
        if (d->same_count < 255) d->same_count++;
    } else {
        d->last_raw = raw_pressed;
        d->same_count = 0;
    }

    const uint8_t needed = (uint8_t)((BUTTON_DEBOUNCE_MS + LOOP_FREQ_MS - 1) / LOOP_FREQ_MS);
    if (d->same_count >= needed)
        d->stable = raw_pressed;

    return d->stable;
}

typedef struct {
    debounce_t db;
    bool       prev_pressed;
    bool       pressed;
    TickType_t press_start;
    bool       long_fired;
} button_t;

typedef struct {
    bool       pressed;
    bool       just_pressed;
    bool       just_released;
    TickType_t held_ticks;      // valid while pressed
    TickType_t press_ticks;     // valid on just_released
} button_sample_t;

static button_sample_t button_update(button_t *b, bool raw_pressed, TickType_t now) {
    button_sample_t s = { 0 };

    b->pressed = debounce_update(&b->db, raw_pressed);

    s.pressed = b->pressed;
    s.just_pressed = b->pressed && !b->prev_pressed;
    s.just_released = !b->pressed && b->prev_pressed;

    if (s.just_pressed) {
        b->press_start = now;
        b->long_fired = false;
    }
    if (s.just_released) {
        s.press_ticks = now - b->press_start;
        b->long_fired = false;
    }
    if (b->pressed)
        s.held_ticks = now - b->press_start;

    b->prev_pressed = b->pressed;
    return s;
}

void update_ui_buffers(ui_t *ui, const client_status_t *st) {
    switch (st->ui_mode) {
    case CLIENT_UI_NORMAL:
        // const char *wifi_state = st->wifi_connected ? "connected" :
        //                          (st->wifi_ssid[0] ? "not connected" : "not provisioned");
        const char *ssid = st->wifi_ssid[0] ? st->wifi_ssid : "(none)";
        const char *ip = st->wifi_connected ? (st->wifi_ip[0] ? st->wifi_ip : "(none)") : "-";
        snprintf(ui->status_buf, sizeof(ui->status_buf),
                 "SSID: %s - IP: %s",
                 ssid,
                 ip);
        snprintf(ui->ui_text_buf, sizeof(ui->ui_text_buf), "%s", st->data.data);
        break;
    case CLIENT_UI_PROV_PROMPT:
        snprintf(ui->ui_text_buf, sizeof(ui->ui_text_buf),
                 "Provision WiFi?\nB2: start\nHold B1: cancel\nHold B1+B2: reset");
        break;
    case CLIENT_UI_PROVISIONING:
        snprintf(ui->ui_text_buf, sizeof(ui->ui_text_buf),
                 "BLE Provisioning\nName: %s\nWiFi: %s\nHold B1: stop",
                 st->ble_name[0] ? st->ble_name : "(tbd)",
                 st->wifi_connected ? "connected" : "connecting");
        break;
    default:
        snprintf(ui->ui_text_buf, sizeof(ui->ui_text_buf), "mode: ?");
        break;
    }
}

static void ui_task(void *arg) {
    ui_task_args_t *a = (ui_task_args_t *)arg;
    QueueHandle_t cmd_q = a->cmd_q;
    QueueHandle_t status_q = a->status_q;

    ui_t ui = { 0 };

    ESP_ERROR_CHECK(ui_init(&ui));

    button_t b1 = { 0 };
    button_t b2 = { 0 };

    bool prev_both_pressed = false;
    TickType_t both_press_start = 0;
    bool both_long_fired = false;
    bool combo_active = false;

    bool have_status = false;
    client_status_t last_st = { 0 };

    while (true) {
        client_status_t st;
        if (xQueueReceive(status_q, &st, 0) == pdTRUE) {
            last_st = st;
            have_status = true;
        }

        const TickType_t now = xTaskGetTickCount();

        // Poll GPIO buttons
        const bool b1_raw = gpio_get_level((gpio_num_t)PIN_BUTTON_1) == 0;
        const bool b2_raw = gpio_get_level((gpio_num_t)PIN_BUTTON_2) == 0;
        const button_sample_t b1_s = button_update(&b1, b1_raw, now);
        const button_sample_t b2_s = button_update(&b2, b2_raw, now);

        const bool both_pressed = b1_s.pressed && b2_s.pressed;

        // Track combo hold
        if (both_pressed) combo_active = true;
        if (!b1_s.pressed && !b2_s.pressed) combo_active = false;

        if (both_pressed && !prev_both_pressed) {
            both_press_start = now;
            both_long_fired = false;
        }
        if (!both_pressed) both_long_fired = false;
        prev_both_pressed = both_pressed;

        // Hold both to reset + provision
        if (both_pressed && !both_long_fired && (now - both_press_start) >= pdMS_TO_TICKS(BUTTON_HOLD_MS)) {
            send_cmd(cmd_q, CMD_UI_RESET_AND_PROV);
            both_long_fired = true;
        }

        const client_ui_mode_t mode = have_status ? last_st.ui_mode : CLIENT_UI_NORMAL;
        switch (mode) {
        case CLIENT_UI_NORMAL:
            // Hold B1 to enter provisioning prompt
            if (!both_pressed && b1_s.pressed && !b1.long_fired && b1_s.held_ticks >= pdMS_TO_TICKS(BUTTON_HOLD_MS)) {
                send_cmd(cmd_q, CMD_UI_ENTER_PROV_PROMPT);
                b1.long_fired = true;
            }

            // Short press actions
            if (!combo_active && !both_long_fired) {
                if (b1_s.just_released) send_cmd(cmd_q, CMD_BUTTON1_PRESS);
                if (b2_s.just_released) send_cmd(cmd_q, CMD_BUTTON2_PRESS);
            }
            break;

        case CLIENT_UI_PROV_PROMPT:
            // Hold B1 to cancel
            if (!both_pressed && b1_s.pressed && !b1.long_fired && b1_s.held_ticks >= pdMS_TO_TICKS(BUTTON_HOLD_MS)) {
                send_cmd(cmd_q, CMD_UI_CANCEL_PROV);
                b1.long_fired = true;
            }
            // B2 short press to start provisioning
            if (!combo_active && !both_long_fired && b2_s.just_released)
                send_cmd(cmd_q, CMD_UI_START_PROV);
            break;

        case CLIENT_UI_PROVISIONING:
            // Hold B1 to cancel
            if (!both_pressed && b1_s.pressed && !b1.long_fired && b1_s.held_ticks >= pdMS_TO_TICKS(BUTTON_HOLD_MS)) {
                send_cmd(cmd_q, CMD_UI_CANCEL_PROV);
                b1.long_fired = true;
            }
            break;

        default:
            break;
        }

        if (have_status) update_ui_buffers(&ui, &last_st);

        ui_tick(&ui);
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
