#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the cloud database sync service.
 * Connects to Supabase via WebSockets and listens to preference changes.
 * Also starts a periodic heartbeat and diagnostics reporter.
 * 
 * @return ESP_OK on success
 */
esp_err_t Cloud_Start(void);

/**
 * @brief Stop the cloud database sync service.
 */
void Cloud_Stop(void);

/**
 * @brief Push current diagnostics to Supabase (Battery, RSSI, Uptime)
 * 
 * @return ESP_OK on success
 */
esp_err_t Cloud_ReportDiagnostics(void);

/**
 * @brief Update the device's listening state in Supabase.
 * 
 * @param is_listening True if wake word is active and device is listening for user voice.
 * @return ESP_OK on success
 */
esp_err_t Cloud_SetListeningState(bool is_listening);
esp_err_t Cloud_UploadVoiceFile(const char *filepath);
esp_err_t Cloud_UploadVoiceBuffer(const int16_t *pcm_data, uint32_t num_samples);

/**
 * @brief Upload voice audio directly to the server and play the MP3 response.
 * This bypasses Supabase storage entirely for ~4-9s latency savings.
 * Falls back to Cloud_UploadVoiceBuffer() if no server URL is configured.
 *
 * @param pcm_data   PCM audio samples (16-bit, 16kHz, mono)
 * @param num_samples Number of samples
 * @return ESP_OK on success
 */
esp_err_t Cloud_UploadVoiceDirect(const int16_t *pcm_data, uint32_t num_samples);

/**
 * @brief Set the direct voice API server URL.
 * @param url  Server URL, e.g. "http://192.168.1.100:3001"
 */
void Cloud_SetVoiceApiUrl(const char *url);

/**
 * @brief Register the play buffer for cleanup when next audio plays.
 * @param buf  Pointer to the allocated audio buffer
 */
void Cloud_SetPlayBuffer(uint8_t *buf);

/**
 * @brief Start background task to poll Supabase for device linking/registration
 * 
 * @return ESP_OK on success
 */
esp_err_t Cloud_StartLinkingTask(void);

#ifdef __cplusplus
}
#endif
