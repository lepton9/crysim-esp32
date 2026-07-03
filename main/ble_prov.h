#pragma once

#include <stddef.h>

#include "esp_wifi_types_generic.h"
#include <network_provisioning/network_config.h>

extern network_prov_config_handlers_t protocomm_handlers;

// Compute the BLE provisioning device name (service name) used in advertisements.
// Format: <prefix><last 3 bytes of STA MAC as hex>, e.g. "POOL_DDEEFF".
void ble_prov_get_device_service_name(char *service_name, size_t max, const char *prefix);

// Best-effort request to stop provisioning (if currently running).
// Typically triggers NETWORK_PROV_END.
void ble_prov_stop_provisioning(void);

/**
 * @brief   Get state of WiFi Station during provisioning
 *
 * @note    WiFi is initially configured as AP, when
 *          provisioning starts. After provisioning data
 *          is provided by user, the WiFi is reconfigured
 *          to run as both AP and Station.
 *
 * @param[out] state    Pointer to wifi_prov_sta_state_t variable to be filled
 *
 * @return
 *  - ESP_OK    : Successfully retrieved wifi state
 *  - ESP_FAIL  : Provisioning app not running
 */
esp_err_t ble_prov_get_wifi_state(network_prov_wifi_sta_state_t *state);

/**
 * @brief   Get reason code in case of WiFi station
 *          disconnection during provisioning
 *
 * @param[out] reason    Pointer to wifi_prov_sta_fail_reason_t variable to be filled
 *
 * @return
 *  - ESP_OK    : Successfully retrieved wifi disconnect reason
 *  - ESP_FAIL  : Provisioning app not running
 */
esp_err_t ble_prov_get_wifi_disconnect_reason(network_prov_wifi_sta_fail_reason_t *reason);

/**
 * @brief   Checks if device is provisioned
 * *
 * @param[out] provisioned  True if provisioned, else false
 *
 * @return
 *  - ESP_OK      : Retrieved provision state successfully
 *  - ESP_FAIL    : Failed to retrieve provision state
 */
esp_err_t ble_prov_is_provisioned(bool *provisioned);

/**
 * @brief   Runs WiFi as Station
 *
 * Configures the WiFi station mode to connect to the
 * SSID and password specified in config structure,
 * and starts WiFi to run as station
 *
 * @param[in] wifi_cfg  Pointer to WiFi cofiguration structure
 *
 * @return
 *  - ESP_OK      : WiFi configured and started successfully
 *  - ESP_FAIL    : Failed to set configuration
 */
esp_err_t ble_prov_configure_sta(wifi_config_t * const wifi_cfg);

/**
 * @brief   Start provisioning via Bluetooth
 *
 * @param[in] security  Security mode
 * @param[in] pop       Pointer to proof of possession (NULL if not present)
 *
 * @return
 *  - ESP_OK      : Provisioning started successfully
 *  - ESP_FAIL    : Failed to start
 */
esp_err_t ble_prov_start_provisioning(const char *ble_device_name_prefix, int security, char const * const pop);
