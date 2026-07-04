#include "display_s3.h"

#include "sdkconfig.h"

#if CONFIG_LILYGO_T_DISPLAY_S3

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"

#include "esp_err.h"
#include "esp_lcd_io_i80.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_log.h"

#include "pin_config.h"

static const char *TAG = "display";

#define LCD_PIXEL_CLOCK_HZ (10 * 1000 * 1000)

#define RETURN_IF_ERR(expr) do { \
            esp_err_t __err = (expr); \
            if (__err != ESP_OK) return __err; \
} while (0)

static esp_lcd_panel_io_handle_t s_io_handle;
static esp_lcd_panel_handle_t s_panel_handle;

typedef struct {
    display_flush_done_cb_t cb;
    void *                  ctx;
} flush_done_hook_t;

static flush_done_hook_t s_flush_hook;

void display_set_flush_done_cb(display_flush_done_cb_t cb, void *ctx) {
    s_flush_hook.cb = cb;
    s_flush_hook.ctx = ctx;
}

static bool on_color_trans_done(esp_lcd_panel_io_handle_t      panel_io,
                                esp_lcd_panel_io_event_data_t *edata,
                                void *                         user_ctx) {
    (void)panel_io;
    (void)edata;

    flush_done_hook_t *hook = (flush_done_hook_t *)user_ctx;
    if (hook && hook->cb)
        hook->cb(hook->ctx);
    return false;
}

void display_push_colors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *data) {
    // x2/y2 are exclusive end coordinates.
    esp_lcd_panel_draw_bitmap(s_panel_handle, x1, y1, x2, y2, data);
}

static esp_err_t gpio_output_set(gpio_num_t pin, int level) {
    const gpio_config_t cfg = {
        .pin_bit_mask     = 1ULL << pin,
            .mode         = GPIO_MODE_OUTPUT,
            .pull_up_en   = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type    = GPIO_INTR_DISABLE,
    };

    RETURN_IF_ERR(gpio_config(&cfg));
    RETURN_IF_ERR(gpio_set_level(pin, level));
    return ESP_OK;
}

esp_err_t display_init(bool usb_left) {
    ESP_LOGI(TAG, "Init display");

    RETURN_IF_ERR(gpio_output_set(BOARD_POWERON, 1));
    RETURN_IF_ERR(gpio_output_set((gpio_num_t)BOARD_TFT_BL, 1));

    RETURN_IF_ERR(gpio_output_set((gpio_num_t)BOARD_TFT_RD, 1));

    esp_lcd_i80_bus_handle_t i80_bus = NULL;
    const esp_lcd_i80_bus_config_t bus_config = {
        .dc_gpio_num        = (gpio_num_t)BOARD_TFT_DC,
        .wr_gpio_num        = (gpio_num_t)BOARD_TFT_WR,
        .clk_src            = LCD_CLK_SRC_DEFAULT,
        .data_gpio_nums     = {
            (gpio_num_t)BOARD_TFT_DATA0,
            (gpio_num_t)BOARD_TFT_DATA1,
            (gpio_num_t)BOARD_TFT_DATA2,
            (gpio_num_t)BOARD_TFT_DATA3,
            (gpio_num_t)BOARD_TFT_DATA4,
            (gpio_num_t)BOARD_TFT_DATA5,
            (gpio_num_t)BOARD_TFT_DATA6,
            (gpio_num_t)BOARD_TFT_DATA7,
        },
        .bus_width          = 8,
        .max_transfer_bytes = AMOLED_WIDTH * 100 * sizeof(uint16_t),
    };
    RETURN_IF_ERR(esp_lcd_new_i80_bus(&bus_config, &i80_bus));

    const esp_lcd_panel_io_i80_config_t io_config = {
        .cs_gpio_num         = (gpio_num_t)BOARD_TFT_CS,
        .pclk_hz             = LCD_PIXEL_CLOCK_HZ,
        .trans_queue_depth   = 5,
        .on_color_trans_done = on_color_trans_done,
        .user_ctx            = &s_flush_hook,
        .lcd_cmd_bits        = 8,
        .lcd_param_bits      = 8,
        .dc_levels           = {
            .dc_idle_level   = 0,
            .dc_cmd_level    = 0,
            .dc_dummy_level  = 0,
            .dc_data_level   = 1,
        },
    };
    RETURN_IF_ERR(esp_lcd_new_panel_io_i80(i80_bus, &io_config, &s_io_handle));

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = (gpio_num_t)BOARD_TFT_RST,
        .rgb_ele_order  = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
    };
    RETURN_IF_ERR(esp_lcd_new_panel_st7789(s_io_handle, &panel_config, &s_panel_handle));

    RETURN_IF_ERR(esp_lcd_panel_reset(s_panel_handle));
    RETURN_IF_ERR(esp_lcd_panel_init(s_panel_handle));

    RETURN_IF_ERR(esp_lcd_panel_invert_color(s_panel_handle, false));
    RETURN_IF_ERR(esp_lcd_panel_swap_xy(s_panel_handle, true));

    const bool mirror_x = !usb_left;
    const bool mirror_y = usb_left;
    RETURN_IF_ERR(esp_lcd_panel_mirror(s_panel_handle, mirror_x, mirror_y));

    const int offset = 35;
    RETURN_IF_ERR(esp_lcd_panel_set_gap(s_panel_handle, 0, offset));
    RETURN_IF_ERR(esp_lcd_panel_disp_on_off(s_panel_handle, true));

    return ESP_OK;
}

#else

esp_err_t display_init(bool usb_left) {
    (void)usb_left;
    return ESP_ERR_NOT_SUPPORTED;
}

void display_push_colors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *data) {
    (void)x1;
    (void)y1;
    (void)x2;
    (void)y2;
    (void)data;
}

void display_set_flush_done_cb(display_flush_done_cb_t cb, void *ctx) {
    (void)cb;
    (void)ctx;
}

#endif
