#pragma once

#include <stddef.h>

typedef struct {
    int button1;
    int button2;
} client_t;

size_t get_ui_text(void *ptr, char *dst, size_t dst_size);

// Update inputs.
void set_buttons(client_t *client, int b1_level, int b2_level);
