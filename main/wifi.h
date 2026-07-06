#pragma once

#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>

#define BLE_PROV_SECURITY_VERSION (1)
#define BLE_PROV_POP ("s3blepop")
#define BLE_PROV_RECONN_ATTEMPTS (3)

// Provisioning BLE device name is derived from this prefix + MAC suffix.
#define BLE_PROV_DEVICE_NAME_PREFIX "PROV_"

// Set on IP_EVENT_STA_GOT_IP and cleared on disconnect.
#define WIFI_GOT_IP_BIT BIT0

void init_nvs(void);

void init_wifi();
void wifi_init_sta(void);

// App-level guard to stop our STA reconnect loop while provisioning runs.
void wifi_set_autoreconnect(bool enabled);

// Returns an EventGroup which exposes Wi-Fi connectivity state.
EventGroupHandle_t wifi_event_group_get(void);
