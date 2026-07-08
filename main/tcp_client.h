#pragma once

#include "sdkconfig.h"

#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

#define TCP_CLIENT_MAX_REQUEST_LEN 128
#define TCP_CLIENT_MAX_RESPONSE_LEN 1024
#define TCP_CLIENT_REQ_QUEUE_LEN 4
#define TCP_CLIENT_RES_QUEUE_LEN 4
#define TCP_CLIENT_RESPONSE_TIMEOUT_MS 3000

typedef struct {
    uint32_t request_id;
    bool     expect_response;
    uint16_t len;
    char     data[TCP_CLIENT_MAX_REQUEST_LEN];
} tcp_client_request_t;

typedef struct {
    uint32_t request_id;
    int      err;
    uint16_t len;
    char     data[TCP_CLIENT_MAX_RESPONSE_LEN];
} tcp_client_response_t;

// Starts the TCP client task.
void tcp_client_start(void);

// Enqueue a request to send to the server.
// Returns false if the queue is full or parameters are invalid.
bool tcp_client_enqueue(const tcp_client_request_t *req, TickType_t timeout);

// Receive a response from the server.
// Returns false on timeout.
bool tcp_client_dequeue_response(tcp_client_response_t *out, TickType_t timeout);
