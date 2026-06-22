#include "Cloud.h"
#include "MIC_Speech.h"
#include "PCM5101.h"
#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "Provisioning.h"
#include "spark_emotion.h"
#include "BAT_Driver.h"
#include "esp_wifi.h"


static const char *TAG = "CloudUpload";

// Direct voice API server URL — set via Provisioning or hardcode for dev
// Format: "http://192.168.1.100:3001" (no trailing slash)
static const char *s_voice_api_url = NULL;

void Cloud_SetVoiceApiUrl(const char *url)
{
    s_voice_api_url = url;
    ESP_LOGI(TAG, "Voice API URL set to: %s", url ? url : "(null)");
}


esp_err_t Cloud_UploadVoiceFile(const char *filepath)
{
    const device_config_t *config = Provisioning_GetConfig();
    if (strlen(config->device_id) == 0) {
        ESP_LOGE(TAG, "Device not provisioned. Cannot upload.");
        return ESP_ERR_INVALID_STATE;
    }

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open audio file %s for reading", filepath);
        return ESP_FAIL;
    }

    // Get file size
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    char *file_buf = heap_caps_malloc(fsize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!file_buf) {
        ESP_LOGE(TAG, "Failed to allocate %ld bytes in SPIRAM for file upload", fsize);
        fclose(f);
        return ESP_ERR_NO_MEM;
    }

    size_t read_bytes = fread(file_buf, 1, fsize, f);
    fclose(f);

    if (read_bytes != fsize) {
        ESP_LOGE(TAG, "Failed to read full file. Read %d of %ld bytes", read_bytes, fsize);
        heap_caps_free(file_buf);
        return ESP_FAIL;
    }

    // 1. Build Storage Upload URL:
    // POST /storage/v1/object/audio/queries/<device_id>_query.wav
    char *upload_url = heap_caps_malloc(512, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *auth_header = heap_caps_malloc(600, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!upload_url || !auth_header) {
        ESP_LOGE(TAG, "Failed to allocate URL/Auth buffers");
        heap_caps_free(file_buf);
        heap_caps_free(upload_url);
        heap_caps_free(auth_header);
        return ESP_ERR_NO_MEM;
    }

    snprintf(upload_url, 512, "%s/storage/v1/object/audio/queries/%s_query.wav", 
             config->supabase_url, config->device_id);

    ESP_LOGI(TAG, "Uploading voice file to: %s", upload_url);

    esp_http_client_config_t http_cfg = {
        .url = upload_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 4096,
        .buffer_size = 4096,
        .keep_alive_enable = true
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize upload HTTP client");
        heap_caps_free(file_buf);
        heap_caps_free(upload_url);
        heap_caps_free(auth_header);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_header(client, "apikey", config->supabase_anon_key);
    esp_http_client_set_header(client, "x-upsert", "true");

    snprintf(auth_header, 600, "Bearer %s", config->auth_token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_http_client_set_post_field(client, file_buf, fsize);

    esp_err_t err = esp_http_client_perform(client);
    int status_code = 0;
    if (err == ESP_OK) {
        status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Storage upload response code: %d", status_code);
    } else {
        ESP_LOGE(TAG, "Failed to perform storage upload: %s", esp_err_to_name(err));
    }

    heap_caps_free(file_buf);

    if (err != ESP_OK || (status_code != 200 && status_code != 201)) {
        ESP_LOGE(TAG, "Upload failed with HTTP status: %d", status_code);
        esp_http_client_cleanup(client);
        heap_caps_free(upload_url);
        heap_caps_free(auth_header);
        return ESP_FAIL;
    }

    // 2. Perform PATCH request to set devices.voice_query_url
    char *patch_url = upload_url; // reuse buffer
    snprintf(patch_url, 512, "%s/rest/v1/devices?id=eq.%s", config->supabase_url, config->device_id);

    char *post_data = heap_caps_malloc(512, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!post_data) {
        ESP_LOGE(TAG, "Failed to allocate PATCH payload buffer");
        esp_http_client_cleanup(client);
        heap_caps_free(patch_url);
        heap_caps_free(auth_header);
        return ESP_ERR_NO_MEM;
    }

    snprintf(post_data, 512, "{\"voice_query_url\":\"%s/storage/v1/object/public/audio/queries/%s_query.wav\"}",
             config->supabase_url, config->device_id);

    ESP_LOGI(TAG, "Patching device table (reusing client): %s", patch_url);

    // Reuse HTTP client for PATCH to avoid duplicate TLS handshake
    esp_http_client_set_url(client, patch_url);
    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "x-upsert", NULL); // Clear x-upsert header
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "PATCH query URL response status: %d", status_code);
    } else {
        ESP_LOGE(TAG, "Failed to perform PATCH query URL: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    heap_caps_free(patch_url);
    heap_caps_free(auth_header);
    heap_caps_free(post_data);

    return err;
}

typedef struct {
    char riff[4];           // "RIFF"
    uint32_t overall_size;   // file size - 8
    char wave[4];           // "WAVE"
    char fmt_chunk_marker[4]; // "fmt "
    uint32_t length_of_fmt;  // 16
    uint16_t format_type;    // 1 for PCM
    uint16_t channels;       // 1 for mono, 2 for stereo
    uint32_t sample_rate;    // 16000
    uint32_t byterate;       // sample_rate * channels * (bits_per_sample/8)
    uint16_t block_align;    // channels * (bits_per_sample/8)
    uint16_t bits_per_sample;// 16
    char data_chunk_header[4]; // "data"
    uint32_t data_size;      // number of bytes of PCM data
} __attribute__((packed)) wav_header_t;

esp_err_t Cloud_UploadVoiceBuffer(const int16_t *pcm_data, uint32_t num_samples)
{
    const device_config_t *config = Provisioning_GetConfig();
    if (strlen(config->device_id) == 0) {
        ESP_LOGE(TAG, "Device not provisioned. Cannot upload.");
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t data_size = num_samples * sizeof(int16_t);
    uint32_t wav_size = sizeof(wav_header_t) + data_size;

    // Allocate buffer in SPIRAM for WAV file
    char *wav_buf = heap_caps_malloc(wav_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wav_buf) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes in SPIRAM for WAV buffer", (int)wav_size);
        return ESP_ERR_NO_MEM;
    }

    // Construct WAV header
    wav_header_t *header = (wav_header_t *)wav_buf;
    memcpy(header->riff, "RIFF", 4);
    header->overall_size = data_size + 36;
    memcpy(header->wave, "WAVE", 4);
    memcpy(header->fmt_chunk_marker, "fmt ", 4);
    header->length_of_fmt = 16;
    header->format_type = 1; // PCM
    header->channels = 1; // Mono
    header->sample_rate = 16000;
    header->byterate = 16000 * 1 * 2;
    header->block_align = 1 * 2;
    header->bits_per_sample = 16;
    memcpy(header->data_chunk_header, "data", 4);
    header->data_size = data_size;

    // Copy PCM data
    memcpy(wav_buf + sizeof(wav_header_t), pcm_data, data_size);

    char *upload_url = heap_caps_malloc(512, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *auth_header = heap_caps_malloc(600, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!upload_url || !auth_header) {
        ESP_LOGE(TAG, "Failed to allocate URL/Auth buffers");
        heap_caps_free(wav_buf);
        heap_caps_free(upload_url);
        heap_caps_free(auth_header);
        return ESP_ERR_NO_MEM;
    }

    snprintf(upload_url, 512, "%s/storage/v1/object/audio/queries/%s_query.wav", 
             config->supabase_url, config->device_id);

    ESP_LOGI(TAG, "Uploading voice buffer from RAM to: %s", upload_url);

    esp_http_client_config_t http_cfg = {
        .url = upload_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 4096,
        .buffer_size = 4096,
        .keep_alive_enable = true
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize upload HTTP client");
        heap_caps_free(wav_buf);
        heap_caps_free(upload_url);
        heap_caps_free(auth_header);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_header(client, "apikey", config->supabase_anon_key);
    esp_http_client_set_header(client, "x-upsert", "true");

    snprintf(auth_header, 600, "Bearer %s", config->auth_token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_http_client_set_post_field(client, wav_buf, wav_size);

    esp_err_t err = esp_http_client_perform(client);
    int status_code = 0;
    if (err == ESP_OK) {
        status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Storage upload response code: %d", status_code);
    } else {
        ESP_LOGE(TAG, "Failed to perform storage upload: %s", esp_err_to_name(err));
    }

    heap_caps_free(wav_buf);

    if (err != ESP_OK || (status_code != 200 && status_code != 201)) {
        ESP_LOGE(TAG, "Upload failed with HTTP status: %d", status_code);
        esp_http_client_cleanup(client);
        heap_caps_free(upload_url);
        heap_caps_free(auth_header);
        return ESP_FAIL;
    }

    // 2. Perform PATCH request to set devices.voice_query_url
    char *patch_url = upload_url; // reuse buffer
    snprintf(patch_url, 512, "%s/rest/v1/devices?id=eq.%s", config->supabase_url, config->device_id);

    char *post_data = heap_caps_malloc(512, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!post_data) {
        ESP_LOGE(TAG, "Failed to allocate PATCH payload buffer");
        esp_http_client_cleanup(client);
        heap_caps_free(patch_url);
        heap_caps_free(auth_header);
        return ESP_ERR_NO_MEM;
    }

    snprintf(post_data, 512, "{\"voice_query_url\":\"%s/storage/v1/object/public/audio/queries/%s_query.wav\"}",
             config->supabase_url, config->device_id);

    ESP_LOGI(TAG, "Patching device table (reusing client): %s", patch_url);

    esp_http_client_set_url(client, patch_url);
    esp_http_client_set_method(client, HTTP_METHOD_PATCH);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "x-upsert", NULL);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "PATCH query URL response status: %d", status_code);
    } else {
        ESP_LOGE(TAG, "Failed to perform PATCH query URL: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    heap_caps_free(patch_url);
    heap_caps_free(auth_header);
    heap_caps_free(post_data);

    return err;
}

// ============================================================
// DIRECT VOICE API — POST WAV to server, receive MP3 response
// Eliminates Supabase storage round-trips (~4-9s latency savings)
// ============================================================

// HTTP event handler for collecting response data into SPIRAM buffer
typedef struct {
    uint8_t *response_buf;
    int response_len;
    int response_max;
} direct_voice_ctx_t;

static esp_err_t direct_voice_http_event(esp_http_client_event_t *evt)
{
    direct_voice_ctx_t *ctx = (direct_voice_ctx_t *)evt->user_data;
    if (!ctx) return ESP_OK;

    switch (evt->event_id) {
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI("LATENCY_AUDIT", "[LATENCY] HTTP Upload End: %lld ms", esp_timer_get_time() / 1000);
            break;
        case HTTP_EVENT_ON_DATA:
            if (ctx->response_len == 0) {
                ESP_LOGI("LATENCY_AUDIT", "[LATENCY] First Byte Received: %lld ms", esp_timer_get_time() / 1000);
            }
            if (ctx->response_buf && ctx->response_len + evt->data_len <= ctx->response_max) {
                memcpy(ctx->response_buf + ctx->response_len, evt->data, evt->data_len);
                ctx->response_len += evt->data_len;
            } else {
                ESP_LOGW(TAG, "Direct voice response buffer overflow or not allocated");
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

esp_err_t Cloud_UploadVoiceDirect(const int16_t *pcm_data, uint32_t num_samples)
{
    // If no direct server URL configured, fall back to Supabase path
    if (!s_voice_api_url || strlen(s_voice_api_url) == 0) {
        ESP_LOGI(TAG, "No Voice API URL configured. Using Supabase fallback.");
        return Cloud_UploadVoiceBuffer(pcm_data, num_samples);
    }

    const device_config_t *config = Provisioning_GetConfig();
    if (strlen(config->device_id) == 0) {
        ESP_LOGE(TAG, "Device not provisioned. Cannot upload.");
        return ESP_ERR_INVALID_STATE;
    }

    // 1. Build WAV in RAM (same as Cloud_UploadVoiceBuffer)
    uint32_t data_size = num_samples * sizeof(int16_t);
    uint32_t wav_size = sizeof(wav_header_t) + data_size;

    char *wav_buf = heap_caps_malloc(wav_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!wav_buf) {
        ESP_LOGE(TAG, "Failed to allocate %d bytes in SPIRAM for WAV buffer", (int)wav_size);
        return ESP_ERR_NO_MEM;
    }

    wav_header_t *header = (wav_header_t *)wav_buf;
    memcpy(header->riff, "RIFF", 4);
    header->overall_size = data_size + 36;
    memcpy(header->wave, "WAVE", 4);
    memcpy(header->fmt_chunk_marker, "fmt ", 4);
    header->length_of_fmt = 16;
    header->format_type = 1;
    header->channels = 1;
    header->sample_rate = 16000;
    header->byterate = 16000 * 1 * 2;
    header->block_align = 1 * 2;
    header->bits_per_sample = 16;
    memcpy(header->data_chunk_header, "data", 4);
    header->data_size = data_size;
    memcpy(wav_buf + sizeof(wav_header_t), pcm_data, data_size);

    ESP_LOGI("LATENCY_AUDIT", "[LATENCY] WAV Creation: %lld ms", esp_timer_get_time() / 1000);
    ESP_LOGI(TAG, "Built WAV buffer (%d bytes). POSTing directly to server...", (int)wav_size);

    // 2. Allocate response buffer in SPIRAM (128KB for MP3 response)
    size_t max_response_size = 128 * 1024;
    uint8_t *response_buf = heap_caps_malloc(max_response_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!response_buf) {
        ESP_LOGE(TAG, "Failed to allocate response buffer in SPIRAM");
        heap_caps_free(wav_buf);
        return ESP_ERR_NO_MEM;
    }

    direct_voice_ctx_t ctx = {
        .response_buf = response_buf,
        .response_len = 0,
        .response_max = (int)max_response_size
    };

    // 3. Build URL: <server_url>/api/voice
    char *url = heap_caps_malloc(512, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!url) {
        ESP_LOGE(TAG, "Failed to allocate URL buffer");
        heap_caps_free(wav_buf);
        heap_caps_free(response_buf);
        return ESP_ERR_NO_MEM;
    }
    snprintf(url, 512, "%s/api/voice", s_voice_api_url);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 30000,  // 30s timeout for AI processing
        .event_handler = direct_voice_http_event,
        .user_data = &ctx,
        .buffer_size_tx = 4096,
        .buffer_size = 4096,
        .disable_auto_redirect = true,
    };

    // Use TLS if server URL starts with https
    if (strncmp(s_voice_api_url, "https://", 8) == 0) {
        http_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    }

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize direct voice HTTP client");
        heap_caps_free(wav_buf);
        heap_caps_free(response_buf);
        heap_caps_free(url);
        return ESP_FAIL;
    }

    // Set headers
    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    esp_http_client_set_header(client, "X-Device-Id", config->device_id);

    // Fetch battery voltage
    float battery_volts = BAT_Get_Volts();
    char battery_str[16];
    snprintf(battery_str, sizeof(battery_str), "%.2f", battery_volts);
    esp_http_client_set_header(client, "X-Device-Battery", battery_str);

    // Fetch Wi-Fi status
    wifi_ap_record_t ap_info;
    int rssi = -100;
    char ssid[33] = "None";
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
        strncpy(ssid, (char *)ap_info.ssid, sizeof(ssid) - 1);
        ssid[sizeof(ssid) - 1] = '\0';
    }
    char rssi_str[16];
    snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);
    esp_http_client_set_header(client, "X-Device-Wifi-RSSI", rssi_str);
    esp_http_client_set_header(client, "X-Device-Wifi-SSID", ssid);

    // Fetch volume setting
    char vol_str[16];
    snprintf(vol_str, sizeof(vol_str), "%d", Volume);
    esp_http_client_set_header(client, "X-Device-Volume", vol_str);

    // Fetch boot count
    char boot_str[16];
    snprintf(boot_str, sizeof(boot_str), "%lu", (unsigned long)config->boot_count);
    esp_http_client_set_header(client, "X-Device-Boot-Count", boot_str);

    // Set WAV as POST body
    esp_http_client_set_post_field(client, wav_buf, wav_size);

    // 4. Perform the request — this blocks until response is received
    ESP_LOGI(TAG, "Sending WAV to %s ...", url);
    int64_t start_us = esp_timer_get_time();
    ESP_LOGI("LATENCY_AUDIT", "[LATENCY] HTTP Upload Start: %lld ms", start_us / 1000);

    esp_err_t err = esp_http_client_perform(client);
    int status_code = 0;
    
    if (err == ESP_OK) {
        ESP_LOGI("LATENCY_AUDIT", "[LATENCY] Full MP3 Received: %lld ms", esp_timer_get_time() / 1000);
        status_code = esp_http_client_get_status_code(client);
        int64_t elapsed_ms = (esp_timer_get_time() - start_us) / 1000;
        ESP_LOGI(TAG, "Direct voice response: HTTP %d, %d bytes MP3, %lld ms round-trip",
                 status_code, ctx.response_len, elapsed_ms);
    } else {
        ESP_LOGE(TAG, "Direct voice request failed: %s", esp_err_to_name(err));
    }

    // Free WAV buffer (no longer needed)
    heap_caps_free(wav_buf);
    esp_http_client_cleanup(client);
    heap_caps_free(url);

    // 5. Handle response
    if (err != ESP_OK || status_code != 200 || ctx.response_len == 0) {
        ESP_LOGW(TAG, "Direct path failed (HTTP %d, %d bytes). Falling back to Supabase...",
                 status_code, ctx.response_len);
        heap_caps_free(response_buf);
        // Rebuild WAV and use legacy path as fallback
        return Cloud_UploadVoiceBuffer(pcm_data, num_samples);
    }

    // 6. Play the MP3 response directly from the response buffer!
    ESP_LOGI(TAG, "Direct voice success! Playing %d byte MP3 response immediately.", ctx.response_len);
    
    MIC_SetConvState(CONV_STATE_SPEAKING);
    Spark_Emotion_Set("happy");
    Play_Music_From_Buffer(response_buf, ctx.response_len);

    // NOTE: response_buf ownership transfers to the audio player.
    // It will be freed when the next audio download or direct voice call happens
    // (via the s_mp3_play_buf pattern in Cloud.c audio_download_task).
    // For safety, we track it the same way:
    Cloud_SetPlayBuffer(response_buf);

    return ESP_OK;
}
