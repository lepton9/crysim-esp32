#include "protocol.h"

#include <stdbool.h>
#include <string.h>

#include "cJSON.h"

cJSON *build_request_json(int id, const char *token, const char *method) {
    cJSON *root = cJSON_CreateObject();
    if (!root) return NULL;

    cJSON_AddNumberToObject(root, "id", id);
    cJSON_AddStringToObject(root, "token", token ? token : "");
    cJSON_AddStringToObject(root, "method", method ? method : "");
    return root;
}

cJSON *build_params_login(const char *username, const char *password) {
    cJSON *params = cJSON_CreateObject();
    if (!params) return NULL;

    cJSON_AddStringToObject(params, "username", username ? username : "");
    cJSON_AddStringToObject(params, "password", password ? password : "");
    return params;
}

int json_string(cJSON *json, char *buf, size_t len) {
    if (!json || !buf || len == 0) return -1;
    bool ok = cJSON_PrintPreallocated(json, buf, (int)len, false);
    if (!ok) return -1;
    return (int)strlen(buf);
}
