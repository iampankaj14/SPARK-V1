#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Start the DNS redirect server on port 53.
 * Redirects all DNS requests to the ESP32 Access Point IP (192.168.4.1).
 * 
 * @return ESP_OK on success
 */
esp_err_t DnsServer_Start(void);

/**
 * @brief Stop the DNS redirect server.
 */
void DnsServer_Stop(void);

#ifdef __cplusplus
}
#endif
