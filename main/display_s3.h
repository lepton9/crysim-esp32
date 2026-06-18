#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

esp_err_t display_init(bool usb_left);

void display_push_colors(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint16_t *data);

typedef void (*display_flush_done_cb_t)(void *ctx);
void display_set_flush_done_cb(display_flush_done_cb_t cb, void *ctx);
