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
#include "mbedtls/base64.h"
#include "cJSON.h"

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
    char intent_name[64];
} direct_voice_ctx_t;

static esp_err_t direct_voice_http_event(esp_http_client_event_t *evt)
{
    direct_voice_ctx_t *ctx = (direct_voice_ctx_t *)evt->user_data;
    if (!ctx) return ESP_OK;

    switch (evt->event_id) {
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI("LATENCY_AUDIT", "[LATENCY] HTTP Upload End: %lld ms", esp_timer_get_time() / 1000);
            break;
        case HTTP_EVENT_ON_HEADER:
            if (strcasecmp(evt->header_key, "X-Intent-Name") == 0) {
                strncpy(ctx->intent_name, evt->header_value, sizeof(ctx->intent_name) - 1);
                ctx->intent_name[sizeof(ctx->intent_name) - 1] = '\0';
                ESP_LOGI(TAG, "Extracted intent from header: %s", ctx->intent_name);
            }
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

static esp_err_t Cloud_ProcessVoiceDirectAPIs(const int16_t *pcm_data, uint32_t num_samples)
{
#if defined(CONFIG_DESKIMON_GEMINI_API_KEY) && strlen(CONFIG_DESKIMON_GEMINI_API_KEY) > 0
    ESP_LOGI(TAG, "Starting Direct ESP32-to-API processing (Gemini + TTS)...");

    // 1. Build WAV in RAM
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

    // 2. Base64 Encode WAV buffer
    size_t base64_len = 0;
    mbedtls_base64_encode(NULL, 0, &base64_len, (const unsigned char *)wav_buf, wav_size);
    char *base64_buf = heap_caps_malloc(base64_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!base64_buf) {
        ESP_LOGE(TAG, "Failed to allocate Base64 buffer (%d bytes)", (int)base64_len);
        heap_caps_free(wav_buf);
        return ESP_ERR_NO_MEM;
    }

    size_t out_len = 0;
    mbedtls_base64_encode((unsigned char *)base64_buf, base64_len + 1, &out_len, (const unsigned char *)wav_buf, wav_size);
    base64_buf[out_len] = '\0';
    heap_caps_free(wav_buf); // Free raw WAV to save RAM

    ESP_LOGI(TAG, "Encoded %d bytes WAV into %d bytes Base64", (int)wav_size, (int)out_len);

    // 3. Build JSON payload for Gemini API
    cJSON *root = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateArray();
    cJSON *content_item = cJSON_CreateObject();
    cJSON *parts = cJSON_CreateArray();

    cJSON *part_audio = cJSON_CreateObject();
    cJSON *inline_data = cJSON_CreateObject();
    cJSON_AddStringToObject(inline_data, "mimeType", "audio/wav");
    cJSON_AddStringToObject(inline_data, "data", base64_buf);
    cJSON_AddItemToObject(part_audio, "inlineData", inline_data);
    cJSON_AddItemToArray(parts, part_audio);

    cJSON *part_text = cJSON_CreateObject();
    cJSON_AddStringToObject(part_text, "text", 
        "You are Spark, a smart cute companion robot. Listen to the user audio and respond.\n"
        "Return ONLY a JSON object: {\"reply\": \"your reply\", \"intent\": \"GREETING/JOKE/ANGRY/LOVE/SAD/NORMAL\"}");
    cJSON_AddItemToArray(parts, part_text);

    cJSON_AddItemToObject(content_item, "parts", parts);
    cJSON_AddItemToArray(contents, content_item);
    cJSON_AddItemToObject(root, "contents", contents);

    char *json_post_body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    heap_caps_free(base64_buf);

    if (!json_post_body) {
        ESP_LOGE(TAG, "Failed to create Gemini JSON post body");
        return ESP_FAIL;
    }

    // 4. Send HTTP POST to Gemini API
    size_t gemini_res_max = 32 * 1024;
    uint8_t *gemini_res_buf = heap_caps_malloc(gemini_res_max, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!gemini_res_buf) {
        cJSON_free(json_post_body);
        return ESP_ERR_NO_MEM;
    }

    direct_voice_ctx_t gemini_ctx = {
        .response_buf = gemini_res_buf,
        .response_len = 0,
        .response_max = (int)gemini_res_max,
        .intent_name = {0}
    };

    char gemini_url[256];
    snprintf(gemini_url, sizeof(gemini_url),
             "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.0-flash:generateContent?key=%s",
             CONFIG_DESKIMON_GEMINI_API_KEY);

    esp_http_client_config_t gemini_http_cfg = {
        .url = gemini_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 20000,
        .event_handler = direct_voice_http_event,
        .user_data = &gemini_ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 4096,
        .buffer_size = 4096,
    };

    esp_http_client_handle_t client = esp_http_client_init(&gemini_http_cfg);
    if (!client) {
        cJSON_free(json_post_body);
        heap_caps_free(gemini_res_buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_post_body, strlen(json_post_body));

    ESP_LOGI(TAG, "Sending audio to Gemini API...");
    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(json_post_body);

    if (err != ESP_OK || status_code != 200 || gemini_ctx.response_len == 0) {
        ESP_LOGE(TAG, "Gemini API request failed: HTTP %d", status_code);
        heap_caps_free(gemini_res_buf);
        return ESP_FAIL;
    }

    gemini_res_buf[gemini_ctx.response_len] = '\0';
    ESP_LOGI(TAG, "Gemini API raw response (%d bytes)", gemini_ctx.response_len);

    cJSON *g_root = cJSON_Parse((char *)gemini_res_buf);
    heap_caps_free(gemini_res_buf);

    if (!g_root) {
        ESP_LOGE(TAG, "Failed to parse Gemini root JSON");
        return ESP_FAIL;
    }

    char reply_text[256] = {0};
    char intent_text[64] = "NORMAL";

    cJSON *candidates = cJSON_GetObjectItem(g_root, "candidates");
    if (candidates && cJSON_GetArraySize(candidates) > 0) {
        cJSON *cand0 = cJSON_GetArrayItem(candidates, 0);
        cJSON *content = cJSON_GetObjectItem(cand0, "content");
        if (content) {
            cJSON *parts_arr = cJSON_GetObjectItem(content, "parts");
            if (parts_arr && cJSON_GetArraySize(parts_arr) > 0) {
                cJSON *part0 = cJSON_GetArrayItem(parts_arr, 0);
                cJSON *text_obj = cJSON_GetObjectItem(part0, "text");
                if (text_obj && text_obj->valuestring) {
                    char *start = strchr(text_obj->valuestring, '{');
                    char *end = strrchr(text_obj->valuestring, '}');
                    if (start && end && end > start) {
                        size_t len = end - start + 1;
                        char *sub_json = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                        if (sub_json) {
                            memcpy(sub_json, start, len);
                            sub_json[len] = '\0';
                            cJSON *inner = cJSON_Parse(sub_json);
                            heap_caps_free(sub_json);

                            if (inner) {
                                cJSON *r_item = cJSON_GetObjectItem(inner, "reply");
                                cJSON *i_item = cJSON_GetObjectItem(inner, "intent");
                                if (r_item && r_item->valuestring) {
                                    strncpy(reply_text, r_item->valuestring, sizeof(reply_text) - 1);
                                }
                                if (i_item && i_item->valuestring) {
                                    strncpy(intent_text, i_item->valuestring, sizeof(intent_text) - 1);
                                }
                                cJSON_Delete(inner);
                            }
                        }
                    } else {
                        strncpy(reply_text, text_obj->valuestring, sizeof(reply_text) - 1);
                    }
                }
            }
        }
    }
    cJSON_Delete(g_root);

    if (strlen(reply_text) == 0) {
        ESP_LOGE(TAG, "No valid reply text extracted from Gemini");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Gemini processed! Reply: \"%s\", Intent: \"%s\"", reply_text, intent_text);

    // 5. Call TTS API
    size_t max_mp3_size = 128 * 1024;
    uint8_t *mp3_buf = heap_caps_malloc(max_mp3_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!mp3_buf) return ESP_ERR_NO_MEM;

    direct_voice_ctx_t tts_ctx = {
        .response_buf = mp3_buf,
        .response_len = 0,
        .response_max = (int)max_mp3_size,
        .intent_name = {0}
    };

    char tts_url[256];
#if defined(CONFIG_DESKIMON_TTS_ELEVENLABS) && defined(CONFIG_DESKIMON_ELEVENLABS_API_KEY)
    snprintf(tts_url, sizeof(tts_url), "https://api.elevenlabs.io/v1/text-to-speech/%s",
             CONFIG_DESKIMON_ELEVENLABS_VOICE_ID);
    
    cJSON *tts_root = cJSON_CreateObject();
    cJSON_AddStringToObject(tts_root, "text", reply_text);
    cJSON_AddStringToObject(tts_root, "model_id", "eleven_monolingual_v1");
    char *tts_post_body = cJSON_PrintUnformatted(tts_root);
    cJSON_Delete(tts_root);

    esp_http_client_config_t tts_cfg = {
        .url = tts_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .event_handler = direct_voice_http_event,
        .user_data = &tts_ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 1024,
        .buffer_size = 4096,
    };

    client = esp_http_client_init(&tts_cfg);
    if (!client) {
        cJSON_free(tts_post_body);
        heap_caps_free(mp3_buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "xi-api-key", CONFIG_DESKIMON_ELEVENLABS_API_KEY);
    esp_http_client_set_header(client, "accept", "audio/mpeg");
    esp_http_client_set_post_field(client, tts_post_body, strlen(tts_post_body));

    ESP_LOGI(TAG, "Calling ElevenLabs TTS API...");
    err = esp_http_client_perform(client);
    status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(tts_post_body);

#elif defined(CONFIG_DESKIMON_TTS_VOICERSS) && defined(CONFIG_DESKIMON_VOICERSS_API_KEY)
    snprintf(tts_url, sizeof(tts_url), "https://api.voicerss.org/?key=%s&hl=en-us&c=MP3&f=16khz_16bit_mono&src=%s",
             CONFIG_DESKIMON_VOICERSS_API_KEY, reply_text);

    esp_http_client_config_t tts_cfg = {
        .url = tts_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .event_handler = direct_voice_http_event,
        .user_data = &tts_ctx,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 4096,
    };

    client = esp_http_client_init(&tts_cfg);
    if (!client) {
        heap_caps_free(mp3_buf);
        return ESP_FAIL;
    }
    err = esp_http_client_perform(client);
    status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
#else
    ESP_LOGE(TAG, "No TTS provider or API key configured");
    heap_caps_free(mp3_buf);
    return ESP_FAIL;
#endif

    if (err != ESP_OK || status_code != 200 || tts_ctx.response_len == 0) {
        ESP_LOGE(TAG, "TTS API request failed: HTTP %d", status_code);
        heap_caps_free(mp3_buf);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Direct API pipeline complete! Received %d bytes MP3.", tts_ctx.response_len);

    // 6. Play MP3 and set emotion
    MIC_SetConvState(CONV_STATE_SPEAKING);
    Spark_Emotion_ProcessIntent(intent_text);
    Play_Music_From_Buffer(mp3_buf, tts_ctx.response_len);
    Cloud_SetPlayBuffer(mp3_buf);

    return ESP_OK;
#else
    return ESP_FAIL;
#endif
}

esp_err_t Cloud_UploadVoiceDirect(const int16_t *pcm_data, uint32_t num_samples)
{
#if defined(CONFIG_DESKIMON_DIRECT_APIS) && defined(CONFIG_DESKIMON_GEMINI_API_KEY)
    if (strlen(CONFIG_DESKIMON_GEMINI_API_KEY) > 0) {
        esp_err_t api_err = Cloud_ProcessVoiceDirectAPIs(pcm_data, num_samples);
        if (api_err == ESP_OK) return ESP_OK;
        ESP_LOGW(TAG, "Direct API mode failed/fallback to custom server / Supabase");
    }
#endif

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
        .response_max = (int)max_response_size,
        .intent_name = {0}
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
    if (strlen(ctx.intent_name) > 0) {
        Spark_Emotion_ProcessIntent(ctx.intent_name);
    } else {
        Spark_Emotion_Set("happy");
    }
    Play_Music_From_Buffer(response_buf, ctx.response_len);

    // NOTE: response_buf ownership transfers to the audio player.
    // It will be freed when the next audio download or direct voice call happens
    // (via the s_mp3_play_buf pattern in Cloud.c audio_download_task).
    // For safety, we track it the same way:
    Cloud_SetPlayBuffer(response_buf);

    return ESP_OK;
}
