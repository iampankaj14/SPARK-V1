#pragma once

/**
 * @file Provisioning.h
 * @brief DESKIMON Provisioning & NVS Configuration Manager
 * 
 * Manages device provisioning state, Wi-Fi credentials storage,
 * cloud account linking, and device configuration in NVS.
 * 
 * Architecture:
 *   Boot → Check NVS → [Provisioned?] → Yes → Connect STA
 *                                      → No  → Start AP + Captive Portal
 */

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================
// PROVISIONING STATES
// ============================================================
typedef enum {
    PROV_STATE_UNPROVISIONED = 0,  // Fresh device, no credentials
    PROV_STATE_WIFI_CONFIGURED,     // Wi-Fi creds saved, not yet linked to cloud
    PROV_STATE_FULLY_PROVISIONED,   // Wi-Fi + cloud account linked
    PROV_STATE_ERROR                // Provisioning error
} prov_state_t;

// ============================================================
// DEVICE CONFIGURATION (stored in NVS)
// ============================================================
typedef struct {
    // Wi-Fi
    char wifi_ssid[33];         // Max 32 chars + null
    char wifi_password[65];     // Max 64 chars + null
    
    // Cloud
    char device_id[37];         // UUID format (36 chars + null)
    char supabase_url[128];     // Supabase project URL
    char supabase_anon_key[256]; // Supabase anon key
    char auth_token[512];       // User auth JWT token
    
    // Personalization
    char device_name[32];       // User-given name for this DESKIMON
    uint32_t eye_color;         // Eye color as hex (e.g., 0x00FFFF)
    uint8_t brightness;         // LCD brightness 0-100
    uint8_t volume;             // Speaker volume 0-100
    
    // State
    prov_state_t prov_state;    // Current provisioning state
    uint32_t boot_count;        // Number of boots
} device_config_t;

// ============================================================
// PUBLIC API
// ============================================================

/**
 * @brief Initialize the provisioning system
 * Loads config from NVS or initializes defaults.
 * Must be called before any other provisioning functions.
 * 
 * @return ESP_OK on success
 */
esp_err_t Provisioning_Init(void);

/**
 * @brief Get the current provisioning state
 * @return Current provisioning state
 */
prov_state_t Provisioning_GetState(void);

/**
 * @brief Get a pointer to the current device configuration
 * @return Pointer to the device config (read-only)
 */
const device_config_t* Provisioning_GetConfig(void);

/**
 * @brief Save Wi-Fi credentials and attempt connection
 * 
 * @param ssid     Wi-Fi SSID
 * @param password Wi-Fi password
 * @return ESP_OK on success, ESP_FAIL if save fails
 */
esp_err_t Provisioning_SaveWiFi(const char* ssid, const char* password);

/**
 * @brief Link device to a cloud account
 * 
 * @param device_id     Unique device UUID
 * @param auth_token    User's auth JWT token
 * @return ESP_OK on success
 */
esp_err_t Provisioning_LinkCloud(const char* device_id, const char* auth_token);

/**
 * @brief Save Supabase connection details
 * 
 * @param url       Supabase project URL
 * @param anon_key  Supabase anonymous key
 * @return ESP_OK on success
 */
esp_err_t Provisioning_SaveSupabase(const char* url, const char* anon_key);

/**
 * @brief Update device personalization settings
 * 
 * @param name       Device name (NULL to skip)
 * @param eye_color  Eye color hex (0 to skip)
 * @param brightness Brightness 0-100 (255 to skip)
 * @param volume     Volume 0-100 (255 to skip)
 * @return ESP_OK on success
 */
esp_err_t Provisioning_UpdatePersonalization(const char* name, uint32_t eye_color, 
                                              uint8_t brightness, uint8_t volume);

/**
 * @brief Start the captive portal (AP mode + HTTP server)
 * Called when device is not provisioned.
 * 
 * @return ESP_OK on success
 */
esp_err_t Provisioning_StartCaptivePortal(void);

/**
 * @brief Stop the captive portal and switch to STA mode
 * Called after successful provisioning.
 * 
 * @return ESP_OK on success
 */
esp_err_t Provisioning_StopCaptivePortal(void);

/**
 * @brief Connect to the saved Wi-Fi network
 * 
 * @return ESP_OK on success, ESP_FAIL if no credentials saved
 */
esp_err_t Provisioning_ConnectWiFi(void);

/**
 * @brief Factory reset — erase all NVS data and restart
 */
void Provisioning_FactoryReset(void);

/**
 * @brief Get the device hardware ID (MAC address)
 * 
 * @param out_id    Buffer to write the hardware ID string
 * @param max_len   Max buffer length (needs at least 18 bytes for MAC)
 * @return ESP_OK on success
 */
esp_err_t Provisioning_GetHardwareId(char* out_id, size_t max_len);

#ifdef __cplusplus
}
#endif
