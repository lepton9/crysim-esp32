#include "client.h"

#include <stdio.h>

size_t get_ui_text(void *ptr, char *dst, size_t dst_size) {
    if (!ptr || !dst || dst_size == 0) return 0;
    client_t *client = (client_t *)ptr;

    int n = snprintf(dst, dst_size, "b1=%d b2=%d", client->button1, client->button2);
    return (n < 0) ? 0u : (size_t)n;
}

void set_buttons(client_t *client, int b1_level, int b2_level) {
    if (!client) return;
    client->button1 = b1_level;
    client->button2 = b2_level;
}
