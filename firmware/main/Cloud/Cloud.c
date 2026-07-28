#include "Cloud.h"
#include "MIC_Speech.h"

#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "Provisioning.h"
#include "BAT_Driver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "PCM5101.h"
#include "spark_face.h"
#include "spark_emotion.h"

static const char *TAG = "CloudClient";

static esp_websocket_client_handle_t s_ws_client = NULL;
static TaskHandle_t s_sync_task_handle = NULL;
static TaskHandle_t s_audio_task_handle = NULL;
static bool s_cloud_running = false;
static int s_heartbeat_ref = 1;
static char *s_ws_rx_buf = NULL;
static int s_ws_rx_buf_len = 0;
static uint8_t *s_mp3_play_buf = NULL;

static void *cjson_spiram_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void cjson_spiram_free(void *ptr)
{
    heap_caps_free(ptr);
}

// Forward declarations
static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                     int32_t event_id, void *event_data);
static void cloud_sync_task(void *pvParameters);
static void parse_supabase_realtime_msg(const char *msg, size_t len);

esp_err_t Cloud_Start(void)
{
    if (s_cloud_running) {
        ESP_LOGW(TAG, "Cloud sync already running");
        return ESP_OK;
    }

    // Register cJSON allocator hooks to use SPIRAM
    static bool cjson_hooks_registered = false;
    if (!cjson_hooks_registered) {
        cJSON_Hooks hooks = {
            .malloc_fn = cjson_spiram_malloc,
            .free_fn = cjson_spiram_free
        };
        cJSON_InitHooks(&hooks);
        cjson_hooks_registered = true;
        ESP_LOGI(TAG, "cJSON hooks initialized to use SPIRAM");
    }

    const device_config_t *config = Provisioning_GetConfig();
    if (strlen(config->device_id) == 0) {
        ESP_LOGE(TAG, "Device not provisioned with a Cloud ID. Cannot start sync.");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Starting cloud database sync service...");
    s_cloud_running = true;

    // 1. Build WebSocket URI from Supabase REST URL
    // e.g. "https://abcdef.supabase.co" -> "wss://abcdef.supabase.co/realtime/v1/websocket?apikey=...&vsn=1.0.0"
    char ws_uri[600];
    const char *base_url = config->supabase_url;
    if (strncmp(base_url, "https://", 8) == 0) {
        snprintf(ws_uri, sizeof(ws_uri), "wss://%s/realtime/v1/websocket?apikey=%s&vsn=1.0.0", 
                 base_url + 8, config->supabase_anon_key);
    } else {
        snprintf(ws_uri, sizeof(ws_uri), "wss://%s/realtime/v1/websocket?apikey=%s&vsn=1.0.0", 
                 base_url, config->supabase_anon_key);
    }

    // 2. Initialize WebSocket Client
    esp_websocket_client_config_t ws_cfg = {
        .uri = ws_uri,
        .subprotocol = "graphql-ws", // standard phoenix protocol compatibility
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size = 2048, // reduced buffer size to 2KB to save memory
        .task_stack = 8192 // increased to 8KB because mbedTLS handshake requires large stack
    };

    s_ws_client = esp_websocket_client_init(&ws_cfg);
    if (!s_ws_client) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        s_cloud_running = false;
        return ESP_FAIL;
    }

    esp_websocket_register_events(s_ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);

    esp_err_t err = esp_websocket_client_start(s_ws_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start WebSocket client: %s", esp_err_to_name(err));
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
        s_cloud_running = false;
        return err;
    }

    // 3. Start Heartbeat & Diagnostics Task (statically allocated stack in SPIRAM)
    static StackType_t *s_sync_stack = NULL;
    static StaticTask_t *s_sync_task_buf = NULL;
    if (!s_sync_stack) {
        s_sync_stack = heap_caps_malloc(8192, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        s_sync_task_buf = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    }

    if (!s_sync_stack || !s_sync_task_buf) {
        ESP_LOGE(TAG, "Failed to allocate memory for static cloud sync task");
        Cloud_Stop();
        return ESP_ERR_NO_MEM;
    }

    s_sync_task_handle = xTaskCreateStaticPinnedToCore(
        cloud_sync_task,
        "cloud_sync_task",
        8192,
        NULL,
        3,
        s_sync_stack,
        s_sync_task_buf,
        1
    );

    if (!s_sync_task_handle) {
        ESP_LOGE(TAG, "Failed to create static cloud sync task");
        Cloud_Stop();
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Cloud sync started successfully. Listening for settings edits.");
    return ESP_OK;
}

void Cloud_Stop(void)
{
    if (!s_cloud_running) return;
    s_cloud_running = false;

    ESP_LOGI(TAG, "Stopping cloud sync service...");

    if (s_ws_client) {
        esp_websocket_client_stop(s_ws_client);
        esp_websocket_client_destroy(s_ws_client);
        s_ws_client = NULL;
    }

    if (s_ws_rx_buf) {
        heap_caps_free(s_ws_rx_buf);
        s_ws_rx_buf = NULL;
        s_ws_rx_buf_len = 0;
    }

    s_sync_task_handle = NULL;
}

esp_err_t Cloud_ReportDiagnostics(void)
{
    const device_config_t *config = Provisioning_GetConfig();
    if (strlen(config->device_id) == 0) return ESP_ERR_INVALID_STATE;

    // Allocate buffers dynamically from SPIRAM to save internal SRAM
    char *url = heap_caps_malloc(256, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *post_data = heap_caps_malloc(256, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *auth_header = heap_caps_malloc(600, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!url || !post_data || !auth_header) {
        ESP_LOGE(TAG, "Failed to allocate memory for diagnostics buffers");
        heap_caps_free(url);
        heap_caps_free(post_data);
        heap_caps_free(auth_header);
        return ESP_ERR_NO_MEM;
    }

    // PATCH /rest/v1/devices?id=eq.<uuid>
    snprintf(url, 256, "%s/rest/v1/devices?id=eq.%s", config->supabase_url, config->device_id);

    // Retrieve active Wi-Fi signal
    wifi_ap_record_t ap_info;
    int rssi = -100;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    // Convert battery voltage to a linear percentage range (3.3V-4.2V)
    float volts = BAT_Get_Volts();
    int battery = (int)((volts - 3.3f) / (4.2f - 3.3f) * 100.0f);
    if (battery > 100) battery = 100;
    if (battery < 0) battery = 0;

    // Calculate Uptime
    int64_t uptime = esp_timer_get_time() / 1000000;

    // Build ISO 8601 timestamp for last_seen_at
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm timeinfo;
    gmtime_r(&tv.tv_sec, &timeinfo);
    char iso_time[32];
    strftime(iso_time, sizeof(iso_time), "%Y-%m-%dT%H:%M:%SZ", &timeinfo);

    snprintf(post_data, 256, 
             "{\"is_online\":true,\"battery_level\":%d,\"wifi_signal_strength\":%d,\"uptime_seconds\":%lld,\"last_seen_at\":\"%s\"}",
             battery, rssi, uptime, iso_time);

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_PATCH,
        .timeout_ms = 10000, // Increase timeout to 10 seconds to accommodate TLS handshake latency
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 512,
        .buffer_size = 4096
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        heap_caps_free(url);
        heap_caps_free(post_data);
        heap_caps_free(auth_header);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "apikey", config->supabase_anon_key);

    snprintf(auth_header, 600, "Bearer %s", config->auth_token);
    esp_http_client_set_header(client, "Authorization", auth_header);

    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Diagnostics reported to cloud. HTTP Status: %d", status_code);
    } else {
        ESP_LOGE(TAG, "Error performing HTTP PATCH diagnostics report: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    heap_caps_free(url);
    heap_caps_free(post_data);
    heap_caps_free(auth_header);
    return err;
}

// ============================================================
// WEBSOCKET HANDLERS & MESSAGE PARSING
// ============================================================

static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                     int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    const device_config_t *config = Provisioning_GetConfig();

    switch (event_id) {
        case WEBSOCKET_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Realtime WebSocket connected! Subscribing to preferences channel...");
            
            // Join channel using Supabase Realtime v2 format:
            // Topic must be unique; the actual filter goes in the payload config.
            char sub_msg[700];
            snprintf(sub_msg, sizeof(sub_msg),
                     "{\"topic\":\"realtime:device_prefs_%s\","
                     "\"event\":\"phx_join\","
                     "\"payload\":{"
                       "\"config\":{"
                         "\"postgres_changes\":["
                           "{\"event\":\"UPDATE\","
                            "\"schema\":\"public\","
                            "\"table\":\"device_preferences\","
                            "\"filter\":\"device_id=eq.%s\"}"
                         "]"
                       "}"
                     "},"
                     "\"ref\":\"1\"}",
                     config->device_id, config->device_id);

            if (esp_websocket_client_is_connected(s_ws_client)) {
                esp_websocket_client_send_text(s_ws_client, sub_msg, strlen(sub_msg), portMAX_DELAY);
                ESP_LOGI(TAG, "Channel subscription request sent (Realtime v2 format).");
            }
            break;

        case WEBSOCKET_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Realtime WebSocket disconnected.");
            if (s_ws_rx_buf) {
                heap_caps_free(s_ws_rx_buf);
                s_ws_rx_buf = NULL;
                s_ws_rx_buf_len = 0;
            }
            break;

        case WEBSOCKET_EVENT_CLOSED:
            ESP_LOGI(TAG, "Realtime WebSocket closed.");
            if (s_ws_rx_buf) {
                heap_caps_free(s_ws_rx_buf);
                s_ws_rx_buf = NULL;
                s_ws_rx_buf_len = 0;
            }
            break;

        case WEBSOCKET_EVENT_DATA:
            if (data->op_code == WS_TRANSPORT_OPCODES_TEXT || data->op_code == 0) { // 0 is continuation frame
                if (data->payload_len > 0) {
                    if (data->payload_offset == 0) {
                        // First fragment or complete unfragmented frame
                        if (s_ws_rx_buf) {
                            heap_caps_free(s_ws_rx_buf);
                            s_ws_rx_buf = NULL;
                        }
                        s_ws_rx_buf = heap_caps_malloc(data->payload_len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                        if (s_ws_rx_buf == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for assembled WS message (%d bytes)", data->payload_len);
                            break;
                        }
                        s_ws_rx_buf_len = data->payload_len;
                    }
                    
                    if (s_ws_rx_buf && data->payload_offset + data->data_len <= s_ws_rx_buf_len) {
                        memcpy(s_ws_rx_buf + data->payload_offset, data->data_ptr, data->data_len);
                        
                        // Check if we have received the full message
                        if (data->payload_offset + data->data_len == s_ws_rx_buf_len) {
                            s_ws_rx_buf[s_ws_rx_buf_len] = '\0';
                            ESP_LOGI(TAG, "Assembled complete WebSocket message (%d bytes)", s_ws_rx_buf_len);
                            parse_supabase_realtime_msg(s_ws_rx_buf, s_ws_rx_buf_len);
                            heap_caps_free(s_ws_rx_buf);
                            s_ws_rx_buf = NULL;
                            s_ws_rx_buf_len = 0;
                        }
                    } else {
                        ESP_LOGW(TAG, "WS payload assembly mismatch or buffer not allocated. Offset: %d, Len: %d, BufLen: %d",
                                 data->payload_offset, data->data_len, s_ws_rx_buf_len);
                        if (s_ws_rx_buf) {
                            heap_caps_free(s_ws_rx_buf);
                            s_ws_rx_buf = NULL;
                            s_ws_rx_buf_len = 0;
                        }
                    }
                }
            }
            break;

        case WEBSOCKET_EVENT_ERROR:
            ESP_LOGE(TAG, "WebSocket error occurred");
            break;
    }
}
typedef struct {
    char url[512];
} audio_download_args_t;

static void audio_download_task(void *pvParameters)
{
    audio_download_args_t *args = (audio_download_args_t *)pvParameters;
    if (!args) {
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Audio download task started for URL: %s", args->url);
    
    esp_http_client_config_t config = {
        .url = args->url,
        .method = HTTP_METHOD_GET,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .buffer_size = 4096,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client for audio download");
        free(args);
        vTaskDelete(NULL);
        return;
    }
    
    // Set typical browser User-Agent so Translate/voicerss TTS doesn't reject us
    esp_http_client_set_header(client, "User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open HTTP connection: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        free(args);
        vTaskDelete(NULL);
        return;
    }
    
    int content_length = esp_http_client_fetch_headers(client);
    (void)content_length;
    
    // Allocate 128KB playback buffer in SPIRAM
    size_t max_mp3_size = 128 * 1024;
    uint8_t *download_buf = heap_caps_malloc(max_mp3_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!download_buf) {
        ESP_LOGE(TAG, "Failed to allocate download buffer in SPIRAM");
        esp_http_client_cleanup(client);
        free(args);
        vTaskDelete(NULL);
        return;
    }
    
    int read_bytes = 0;
    int total_bytes = 0;
    while (true) {
        read_bytes = esp_http_client_read(client, (char *)(download_buf + total_bytes), 2048);
        if (read_bytes <= 0) {
            break;
        }
        total_bytes += read_bytes;
        if (total_bytes + 2048 > max_mp3_size) {
            ESP_LOGW(TAG, "Audio response exceeded 128KB buffer, stopping download");
            break;
        }
    }
    
    esp_http_client_cleanup(client);
    
    if (total_bytes > 0) {
        ESP_LOGI(TAG, "Audio download complete. Total bytes: %d. Playing response from RAM...", total_bytes);
        
        // Free previously used buffer if any
        if (s_mp3_play_buf) {
            heap_caps_free(s_mp3_play_buf);
        }
        s_mp3_play_buf = download_buf;

        // Signal state machine that we're about to speak, then play audio
        MIC_SetConvState(CONV_STATE_SPEAKING);
        Play_Music_From_Buffer(s_mp3_play_buf, total_bytes);
    } else {
        ESP_LOGE(TAG, "Downloaded 0 bytes from response URL");
        heap_caps_free(download_buf);
    }
    
    free(args);
    s_audio_task_handle = NULL;
    vTaskDelete(NULL);
}

static void parse_supabase_realtime_msg(const char *msg, size_t len)
{
    // Make null-terminated copy for safe cJSON parsing from SPIRAM
    char *buf = heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) return;
    memcpy(buf, msg, len);
    buf[len] = '\0';

    cJSON *root = cJSON_Parse(buf);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON realtime message. Length: %d. Content snippet: %.100s", len, buf);
        heap_caps_free(buf);
        return;
    }

    cJSON *event = cJSON_GetObjectItem(root, "event");
    if (event && strcmp(event->valuestring, "postgres_changes") == 0) {
        cJSON *payload = cJSON_GetObjectItem(root, "payload");
        if (payload) {
            cJSON *data_obj = cJSON_GetObjectItem(payload, "data");
            if (data_obj) {
                cJSON *record = cJSON_GetObjectItem(data_obj, "record");
                if (record) {
                    ESP_LOGI(TAG, "Received postgres row update! Syncing settings...");

                    // 1. Parse Eye Color (Hex code, e.g. #00FFFF or #FF0000)
                    cJSON *color_item = cJSON_GetObjectItem(record, "eye_color");
                    uint32_t color = 0;
                    if (color_item && color_item->valuestring) {
                        if (color_item->valuestring[0] == '#') {
                            color = strtoul(color_item->valuestring + 1, NULL, 16);
                        } else {
                            color = strtoul(color_item->valuestring, NULL, 16);
                        }
                    }

                    // 2. Parse Brightness
                    cJSON *bright_item = cJSON_GetObjectItem(record, "brightness");
                    uint8_t brightness = (bright_item) ? (uint8_t)bright_item->valueint : 255;

                    // 3. Parse Volume
                    cJSON *vol_item = cJSON_GetObjectItem(record, "volume");
                    uint8_t volume = (vol_item) ? (uint8_t)vol_item->valueint : 255;

                    // 4. Parse Preset
                    cJSON *preset_item = cJSON_GetObjectItem(record, "personality_preset");
                    const char *preset = (preset_item) ? preset_item->valuestring : NULL;

                    // Update parameters locally in NVS
                    Provisioning_UpdatePersonalization(preset, color, brightness, volume);

                    // Dynamic hardware state updates:
                    if (color != 0) {
                        Spark_Face_SetColor(color);
                    }
                    if (brightness != 255) {
                        // call LCD brightness driver helper if available
                    }
                    if (volume != 255) {
                        // Call volume adjustment driver directly
                        Volume_adjustment(volume);
                    }

                    // 5. Parse Audio URL and trigger download/play
                    cJSON *audio_item = cJSON_GetObjectItem(record, "audio_url");
                    if (audio_item && audio_item->valuestring && strlen(audio_item->valuestring) > 0) {
                        audio_download_args_t *args = malloc(sizeof(audio_download_args_t));
                        if (args) {
                            strncpy(args->url, audio_item->valuestring, sizeof(args->url) - 1);
                            args->url[sizeof(args->url) - 1] = '\0';
                            
                            // Statically allocate stack in SPIRAM
                            static StackType_t *s_audio_stack = NULL;
                            static StaticTask_t *s_audio_task_buf = NULL;
                            if (!s_audio_stack) {
                                s_audio_stack = heap_caps_malloc(6144, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
                                s_audio_task_buf = heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
                            }

                            if (!s_audio_stack || !s_audio_task_buf) {
                                ESP_LOGE(TAG, "Failed to allocate static memory for audio download task");
                                free(args);
                            } else {
                                if (s_audio_task_handle != NULL) {
                                    ESP_LOGW(TAG, "Audio task already active. Deleting prior instance...");
                                    vTaskDelete(s_audio_task_handle);
                                    s_audio_task_handle = NULL;
                                }

                                s_audio_task_handle = xTaskCreateStaticPinnedToCore(
                                    audio_download_task,
                                    "audio_download",
                                    6144,
                                    args,
                                    3,
                                    s_audio_stack,
                                    s_audio_task_buf,
                                    1
                                );

                                if (!s_audio_task_handle) {
                                    ESP_LOGE(TAG, "Failed to create static audio download task");
                                    free(args);
                                } else {
                                    Spark_Emotion_Set("happy");
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);
    heap_caps_free(buf);
}

// Background task: Heartbeats + Health Reporting
static void cloud_sync_task(void *pvParameters)
{
    TickType_t last_report_time = xTaskGetTickCount();
    
    // Periodically post diagnostics every 60 seconds
    const TickType_t report_interval = pdMS_TO_TICKS(60000);
    // Periodically send Phoenix heartbeat every 25 seconds
    const TickType_t heartbeat_interval = pdMS_TO_TICKS(25000);

    TickType_t last_heartbeat_time = xTaskGetTickCount();

    // Initial delay to let network settle
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    ESP_LOGI(TAG, "Running initial diagnostics from cloud_sync_task. Free internal heap: %d bytes", 
             (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    Cloud_ReportDiagnostics();

    while (s_cloud_running) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        TickType_t now = xTaskGetTickCount();

        // 1. Send WebSocket Heartbeat
        if (now - last_heartbeat_time >= heartbeat_interval) {
            last_heartbeat_time = now;
            
            if (s_ws_client && esp_websocket_client_is_connected(s_ws_client)) {
                char hb[128];
                snprintf(hb, sizeof(hb), 
                         "{\"topic\":\"phoenix\",\"event\":\"heartbeat\",\"payload\":{},\"ref\":\"%d\"}", 
                         s_heartbeat_ref++);
                
                esp_websocket_client_send_text(s_ws_client, hb, strlen(hb), portMAX_DELAY);
                ESP_LOGD(TAG, "Sent Phoenix heartbeat.");
            }
        }

        // 2. Report diagnostics metrics to cloud
        if (now - last_report_time >= report_interval) {
            last_report_time = now;
            ESP_LOGI(TAG, "Running periodic diagnostics from cloud_sync_task. Free internal heap: %d bytes", 
                     (int)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
            Cloud_ReportDiagnostics();
        }
    }

    vTaskDelete(NULL);
}

// ============================================================
// AUTO-LINKING / REGISTRATION DISCOVERY
// ============================================================

static char s_response_buffer[1024];
static int s_response_len = 0;

static esp_err_t http_event_handle(esp_http_client_event_t *evt)
{
    switch(evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (s_response_len + evt->data_len < sizeof(s_response_buffer)) {
                memcpy(s_response_buffer + s_response_len, evt->data, evt->data_len);
                s_response_len += evt->data_len;
                s_response_buffer[s_response_len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

static void cloud_link_task(void *pvParameters)
{
    const device_config_t *config = Provisioning_GetConfig();
    char hw_id[18];
    Provisioning_GetHardwareId(hw_id, sizeof(hw_id));

    // Allocate large buffers dynamically from SPIRAM to save internal SRAM
    char *url = heap_caps_malloc(256, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *auth_header = heap_caps_malloc(600, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!url || !auth_header) {
        ESP_LOGE(TAG, "Failed to allocate memory for linking buffers");
        heap_caps_free(url);
        heap_caps_free(auth_header);
        vTaskDelete(NULL);
        return;
    }

    snprintf(url, 256, "%s/rest/v1/devices?hardware_id=eq.%s&select=id", 
             config->supabase_url, hw_id);

    ESP_LOGI(TAG, "Device linking task started. Polling url: %s", url);

    // Initial wait to let network settle
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (Provisioning_GetState() == PROV_STATE_WIFI_CONFIGURED) {
        s_response_len = 0;
        s_response_buffer[0] = '\0';

        esp_http_client_config_t http_cfg = {
            .url = url,
            .method = HTTP_METHOD_GET,
            .timeout_ms = 10000, // Increase timeout to 10 seconds to accommodate TLS handshake latency
            .event_handler = http_event_handle,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .buffer_size_tx = 512,
            .buffer_size = 4096
        };

        esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
        if (!client) {
            ESP_LOGE(TAG, "Failed to create link HTTP client");
            vTaskDelay(pdMS_TO_TICKS(10000));
            continue;
        }

        esp_http_client_set_header(client, "apikey", config->supabase_anon_key);
        
        snprintf(auth_header, 600, "Bearer %s", config->supabase_anon_key);
        esp_http_client_set_header(client, "Authorization", auth_header);

        esp_err_t err = esp_http_client_perform(client);
        if (err == ESP_OK) {
            int status_code = esp_http_client_get_status_code(client);
            if (status_code == 200 && s_response_len > 0) {
                cJSON *root = cJSON_Parse(s_response_buffer);
                if (root) {
                    if (cJSON_IsArray(root) && cJSON_GetArraySize(root) > 0) {
                        cJSON *item = cJSON_GetArrayItem(root, 0);
                        cJSON *id_item = cJSON_GetObjectItem(item, "id");
                        if (id_item && id_item->valuestring) {
                            ESP_LOGI(TAG, "SUCCESS! Device has been registered on cloud! UUID: %s", id_item->valuestring);
                            
                            // Save device_id and change state to fully provisioned
                            Provisioning_LinkCloud(id_item->valuestring, config->supabase_anon_key);
                            
                            // Start Supabase Realtime WebSocket client
                            Cloud_Start();
                            
                            cJSON_Delete(root);
                            esp_http_client_cleanup(client);
                            break; // Exit poll loop
                        }
                    }
                    cJSON_Delete(root);
                }
            } else {
                ESP_LOGI(TAG, "Polling registration status: status_code=%d", status_code);
            }
        } else {
            ESP_LOGE(TAG, "Link HTTP GET request failed: %s", esp_err_to_name(err));
        }

        esp_http_client_cleanup(client);
        
        // Wait 10 seconds before next poll
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    heap_caps_free(url);
    heap_caps_free(auth_header);
    ESP_LOGI(TAG, "Device linking task exiting.");
    vTaskDelete(NULL);
}

esp_err_t Cloud_StartLinkingTask(void)
{
    TaskHandle_t handle = NULL;
    BaseType_t ret = xTaskCreatePinnedToCore(
        cloud_link_task,
        "cloud_link_task",
        8192, // 8KB stack in internal RAM
        NULL,
        3,
        &handle,
        1
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create cloud link task");
        return ESP_FAIL;
    }
    return ESP_OK;
}

esp_err_t Cloud_SetListeningState(bool is_listening)
{
    const device_config_t *config = Provisioning_GetConfig();
    if (strlen(config->device_id) == 0) return ESP_ERR_INVALID_STATE;

    char *url = heap_caps_malloc(256, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *post_data = heap_caps_malloc(128, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    char *auth_header = heap_caps_malloc(600, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!url || !post_data || !auth_header) {
        heap_caps_free(url);
        heap_caps_free(post_data);
        heap_caps_free(auth_header);
        return ESP_ERR_NO_MEM;
    }

    snprintf(url, 256, "%s/rest/v1/devices?id=eq.%s", config->supabase_url, config->device_id);
    snprintf(post_data, 128, "{\"is_listening\":%s}", is_listening ? "true" : "false");

    esp_http_client_config_t http_cfg = {
        .url = url,
        .method = HTTP_METHOD_PATCH,
        .timeout_ms = 8000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .buffer_size_tx = 512,
        .buffer_size = 4096
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        heap_caps_free(url);
        heap_caps_free(post_data);
        heap_caps_free(auth_header);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "apikey", config->supabase_anon_key);
    snprintf(auth_header, 600, "Bearer %s", config->auth_token);
    esp_http_client_set_header(client, "Authorization", auth_header);
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        ESP_LOGI(TAG, "Listening state set to %s. Status: %d", is_listening ? "true" : "false", status_code);
    } else {
        ESP_LOGE(TAG, "Failed to update listening state: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    heap_caps_free(url);
    heap_caps_free(post_data);
    heap_caps_free(auth_header);
    return err;
}

void Cloud_SetPlayBuffer(uint8_t *buf)
{
    if (s_mp3_play_buf && s_mp3_play_buf != buf) {
        heap_caps_free(s_mp3_play_buf);
    }
    s_mp3_play_buf = buf;
}

