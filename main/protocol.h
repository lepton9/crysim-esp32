#pragma once

#include <stddef.h>

typedef struct cJSON cJSON;

// Builds the base request object: {"id":..., "token":"...", "method":"..."}
// Caller owns the returned cJSON.
cJSON *build_request_json(int id, const char *token, const char *method);

// Builds login params: {"username":"...", "password":"..."}
// Caller owns the returned cJSON.
cJSON *build_params_login(const char *username, const char *password);

// Serialize JSON into buf. Returns number of bytes written.
int json_string(cJSON *json, char *buf, size_t len);
