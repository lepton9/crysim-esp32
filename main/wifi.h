#pragma once

#include <esp_system.h>
#include <esp_event.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#define BLE_PROV_SECURITY_VERSION (1)
#define BLE_PROV_POP ("s3blepop")
#define BLE_PROV_RECONN_ATTEMPTS (3)

// Provisioning BLE device name is derived from this prefix + MAC suffix.
// (See ble_prov_get_device_service_name())
#define BLE_PROV_DEVICE_NAME_PREFIX "PROV_"

void init_nvs(void);

void init_wifi();
void wifi_init_sta(void);

// App-level guard to stop our STA reconnect loop while provisioning runs.
void wifi_set_autoreconnect(bool enabled);
