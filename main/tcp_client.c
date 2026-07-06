#include "tcp_client.h"

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include "sdkconfig.h"

#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

#include "lwip/inet.h"
#include "lwip/sockets.h"

#include "wifi.h"

static const char *TAG = "tcp_client";

static TaskHandle_t tcp_task_handle = NULL;

static QueueHandle_t req_q = NULL;
static QueueHandle_t res_q = NULL;

static bool send_all(int s, const void *buf, size_t len) {
    const uint8_t *p = (const uint8_t *)buf;
    size_t left = len;

    while (left > 0) {
        int n = send(s, p, left, 0);
        if (n < 0) return false;
        if (n == 0) return false;
        p += (size_t)n;
        left -= (size_t)n;
    }
    send(s, "\n", 1, 0);
    return true;
}

static void push_response(uint32_t request_id, int err, const char *data, uint16_t len) {
    if (!res_q) return;
    tcp_client_response_t r = { 0 };
    r.request_id = request_id;
    r.err = err;
    if (len > (uint16_t)(TCP_CLIENT_MAX_MSG_LEN - 1))
        len = (uint16_t)(TCP_CLIENT_MAX_MSG_LEN - 1);
    r.len = len;
    if (data && len) memcpy(r.data, data, len);
    r.data[r.len] = 0;
    (void)xQueueSend(res_q, &r, 0);
}

// Connect to the server. Set a timeout if connecting fails.
int connect_timeout(uint32_t timeout_ms) {
    struct sockaddr_in dest = { 0 };

    dest.sin_family = AF_INET;
    dest.sin_port = htons(CONFIG_TCP_CLIENT_SERVER_PORT);

    int ok = inet_pton(AF_INET, CONFIG_TCP_CLIENT_SERVER_IP, &dest.sin_addr);
    if (ok != 1) {
        ESP_LOGE(TAG, "invalid server ip: %s", CONFIG_TCP_CLIENT_SERVER_IP);
        vTaskDelay(pdMS_TO_TICKS(1000));
        return -1;
    }

    int s = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (s < 0) {
        ESP_LOGW(TAG, "socket failed: errno=%d (%s)", errno, strerror(errno));
        vTaskDelay(pdMS_TO_TICKS(timeout_ms));
        return s;
    }

    // Read timeout
    struct timeval tv = { .tv_sec = 0, .tv_usec = 200 * 1000 };
    (void)setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    // Send timeout
    struct timeval stv = { .tv_sec = 2, .tv_usec = 0 };
    (void)setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &stv, sizeof(stv));
    int yes = 1;
    (void)setsockopt(s, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof(yes));

    ESP_LOGI(TAG, "connecting to %s:%d", CONFIG_TCP_CLIENT_SERVER_IP, CONFIG_TCP_CLIENT_SERVER_PORT);
    if (connect(s, (struct sockaddr *)&dest, sizeof(dest)) != 0) {
        ESP_LOGW(TAG, "connect failed: errno=%d (%s)", errno, strerror(errno));
        close(s);
        vTaskDelay(pdMS_TO_TICKS(timeout_ms));
        return -1;
    }

    ESP_LOGI(TAG, "connected");
    return s;
}

static void tcp_client_task(void *arg) {
    (void)arg;

    EventGroupHandle_t wifi_eg = wifi_event_group_get();
    if (!wifi_eg) {
        ESP_LOGE(TAG, "wifi event group not initialized");
        vTaskDelete(NULL);
        return;
    }

    uint32_t reconnect_timeout_ms = 500;

    while (1) {
        // Block until Wi-Fi is up and we have an IP
        xEventGroupWaitBits(wifi_eg, WIFI_GOT_IP_BIT, pdFALSE, pdTRUE, portMAX_DELAY);

        int s = connect_timeout(reconnect_timeout_ms);
        if (s < 0) {
            reconnect_timeout_ms = reconnect_timeout_ms < 5000 ? reconnect_timeout_ms * 2 : 500;
            continue;
        }
        reconnect_timeout_ms = 500;

        // If we are waiting for a response
        bool cur_packet_waiting = false;
        uint32_t cur_packet_id = 0;
        TickType_t cur_packet_deadline = 0;

        char line_buf[TCP_CLIENT_MAX_MSG_LEN];
        size_t line_len = 0;

        // Main loop when connected to the server
        while (1) {
            // If Wi-Fi dropped, break and try to reconnect
            if ((xEventGroupGetBits(wifi_eg) & WIFI_GOT_IP_BIT) == 0) {
                ESP_LOGW(TAG, "wifi lost, disconnecting");
                break;
            }

            TickType_t now = xTaskGetTickCount();
            if (cur_packet_waiting && (int32_t)(now - cur_packet_deadline) > 0) {
                // Timeout waiting for the response
                push_response(cur_packet_id, ETIMEDOUT, NULL, 0);
                cur_packet_waiting = false;
                cur_packet_id = 0;
            }

            // Read available bytes
            char buf[128];
            int n = recv(s, buf, sizeof(buf), 0);
            if (n > 0) {
                for (int i = 0; i < n; i++) {
                    char ch = buf[i];
                    if (ch == '\n') {
                        if (line_len > 0 && line_buf[line_len - 1] == '\r') line_len--;

                        if (cur_packet_waiting) {
                            push_response(cur_packet_id, 0, line_buf, (uint16_t)line_len);
                            cur_packet_waiting = false;
                            cur_packet_id = 0;
                        }
                        line_len = 0;
                        continue;
                    }

                    if (line_len < (sizeof(line_buf) - 1))
                        line_buf[line_len++] = ch;
                }
            } else if (n == 0) {
                ESP_LOGW(TAG, "server closed");
                break;
            } else {
                if (errno != EWOULDBLOCK && errno != EAGAIN) {
                    ESP_LOGW(TAG, "recv failed: errno=%d (%s)", errno, strerror(errno));
                    break;
                }
            }

            // Only send a new request when we are not waiting for a reply
            if (cur_packet_waiting || !req_q) continue;

            tcp_client_request_t req;
            if (xQueueReceive(req_q, &req, pdMS_TO_TICKS(50)) == pdTRUE) {
                if (req.len > TCP_CLIENT_MAX_MSG_LEN) req.len = TCP_CLIENT_MAX_MSG_LEN;
                bool ok_send = send_all(s, req.data, req.len);
                if (!ok_send) {
                    ESP_LOGW(TAG, "send failed: errno=%d (%s)", errno, strerror(errno));
                    break;
                }

                if (req.expect_response) {
                    cur_packet_waiting = true;
                    cur_packet_id = req.request_id;
                    cur_packet_deadline = now + pdMS_TO_TICKS(TCP_CLIENT_RESPONSE_TIMEOUT_MS);
                }
            }
        }

        close(s);
        ESP_LOGI(TAG, "disconnected");

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

void tcp_client_start(void) {
    if (tcp_task_handle) return;

    if (!req_q) {
        req_q = xQueueCreate(TCP_CLIENT_REQ_QUEUE_LEN, sizeof(tcp_client_request_t));
        if (!req_q) {
            ESP_LOGE(TAG, "failed to create request queue");
            return;
        }
    }
    if (!res_q) {
        res_q = xQueueCreate(TCP_CLIENT_RES_QUEUE_LEN, sizeof(tcp_client_response_t));
        if (!res_q) {
            ESP_LOGE(TAG, "failed to create response queue");
            return;
        }
    }

    BaseType_t ok = xTaskCreate(tcp_client_task, "tcp_client", 4096, NULL, 5, &tcp_task_handle);
    if (ok != pdPASS) {
        tcp_task_handle = NULL;
        ESP_LOGE(TAG, "failed to start tcp client task");
    }
}

bool tcp_client_enqueue(const tcp_client_request_t *req, TickType_t timeout) {
    if (!req || !req_q) return false;
    if (req->len > TCP_CLIENT_MAX_MSG_LEN) return false;
    return xQueueSend(req_q, req, timeout) == pdTRUE;
}

bool tcp_client_dequeue_response(tcp_client_response_t *out, TickType_t timeout) {
    if (!out || !res_q) return false;
    return xQueueReceive(res_q, out, timeout) == pdTRUE;
}
