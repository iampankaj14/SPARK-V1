#include "Provisioning.h"

#include <string.h>
#include <stdio.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "dns_server.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "Provisioning";

// ============================================================
// NVS KEYS
// ============================================================
#define NVS_NAMESPACE       "deskimon_cfg"
#define NVS_KEY_WIFI_SSID   "wifi_ssid"
#define NVS_KEY_WIFI_PASS   "wifi_pass"
#define NVS_KEY_DEV_ID      "device_id"
#define NVS_KEY_AUTH_TOKEN   "auth_token"
#define NVS_KEY_SUPA_URL    "supa_url"
#define NVS_KEY_SUPA_KEY    "supa_key"
#define NVS_KEY_DEV_NAME    "dev_name"
#define NVS_KEY_EYE_COLOR   "eye_color"
#define NVS_KEY_BRIGHTNESS  "brightness"
#define NVS_KEY_VOLUME      "volume"
#define NVS_KEY_PROV_STATE  "prov_state"
#define NVS_KEY_BOOT_COUNT  "boot_count"

// ============================================================
// CAPTIVE PORTAL CONFIG
// ============================================================
#define AP_SSID_PREFIX      "DESKIMON"
#define AP_MAX_CONNECTIONS  4
#define AP_CHANNEL          1

// ============================================================
// WIFI EVENTS
// ============================================================
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1
#define WIFI_MAX_RETRY      5

// ============================================================
// INTERNAL STATE
// ============================================================
static device_config_t s_config;
static nvs_handle_t s_nvs_handle;
static httpd_handle_t s_httpd = NULL;
static EventGroupHandle_t s_wifi_event_group = NULL;
static int s_retry_count = 0;
static bool s_initialized = false;

// Forward declarations
static esp_err_t nvs_load_config(void);
static esp_err_t nvs_save_string(const char* key, const char* value);
static esp_err_t nvs_save_u8(const char* key, uint8_t value);
static esp_err_t nvs_save_u32(const char* key, uint32_t value);
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);
static esp_err_t start_http_server(void);
static void stop_http_server(void);

// ============================================================
// PUBLIC IMPLEMENTATION
// ============================================================

static void *cjson_spiram_malloc(size_t size)
{
    return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

static void cjson_spiram_free(void *ptr)
{
    heap_caps_free(ptr);
}

esp_err_t Provisioning_Init(void)
{
    if (s_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing provisioning system...");

    // Register cJSON allocator hooks to use SPIRAM globally
    static bool cjson_hooks_registered = false;
    if (!cjson_hooks_registered) {
        cJSON_Hooks hooks = {
            .malloc_fn = cjson_spiram_malloc,
            .free_fn = cjson_spiram_free
        };
        cJSON_InitHooks(&hooks);
        cjson_hooks_registered = true;
        ESP_LOGI(TAG, "cJSON hooks initialized to use SPIRAM globally");
    }

    // Open NVS
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &s_nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Load config from NVS
    memset(&s_config, 0, sizeof(s_config));
    err = nvs_load_config();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "No saved config found, using defaults");
        s_config.prov_state = PROV_STATE_UNPROVISIONED;
        s_config.eye_color = 0x00FFFF;
        s_config.brightness = 80;
        s_config.volume = 100;
        snprintf(s_config.device_name, sizeof(s_config.device_name), "My Deskimon");
    }

    // Increment boot count
    s_config.boot_count++;
    nvs_save_u32(NVS_KEY_BOOT_COUNT, s_config.boot_count);

    // Create WiFi event group
    s_wifi_event_group = xEventGroupCreate();

    // Register WiFi event handlers once globally
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                    &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                    &wifi_event_handler, NULL, NULL));

    s_initialized = true;
    ESP_LOGI(TAG, "Provisioning initialized. State: %d, Boot: %lu", 
             s_config.prov_state, (unsigned long)s_config.boot_count);

    return ESP_OK;
}

prov_state_t Provisioning_GetState(void)
{
    return s_config.prov_state;
}

const device_config_t* Provisioning_GetConfig(void)
{
    return &s_config;
}

esp_err_t Provisioning_SaveWiFi(const char* ssid, const char* password)
{
    if (!ssid || strlen(ssid) == 0) {
        ESP_LOGE(TAG, "Invalid SSID");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Saving Wi-Fi credentials for SSID: %s", ssid);

    strncpy(s_config.wifi_ssid, ssid, sizeof(s_config.wifi_ssid) - 1);
    strncpy(s_config.wifi_password, password ? password : "", sizeof(s_config.wifi_password) - 1);

    esp_err_t err = nvs_save_string(NVS_KEY_WIFI_SSID, s_config.wifi_ssid);
    err |= nvs_save_string(NVS_KEY_WIFI_PASS, s_config.wifi_password);

    if (err == ESP_OK) {
        s_config.prov_state = PROV_STATE_WIFI_CONFIGURED;
        nvs_save_u8(NVS_KEY_PROV_STATE, (uint8_t)s_config.prov_state);
        ESP_LOGI(TAG, "Wi-Fi credentials saved successfully");
    }

    return err;
}

esp_err_t Provisioning_LinkCloud(const char* device_id, const char* auth_token)
{
    if (!device_id || !auth_token) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Linking device to cloud account. ID: %s", device_id);

    strncpy(s_config.device_id, device_id, sizeof(s_config.device_id) - 1);
    strncpy(s_config.auth_token, auth_token, sizeof(s_config.auth_token) - 1);

    esp_err_t err = nvs_save_string(NVS_KEY_DEV_ID, s_config.device_id);
    err |= nvs_save_string(NVS_KEY_AUTH_TOKEN, s_config.auth_token);

    if (err == ESP_OK) {
        s_config.prov_state = PROV_STATE_FULLY_PROVISIONED;
        nvs_save_u8(NVS_KEY_PROV_STATE, (uint8_t)s_config.prov_state);
        ESP_LOGI(TAG, "Cloud link saved successfully");
    }

    return err;
}

esp_err_t Provisioning_SaveSupabase(const char* url, const char* anon_key)
{
    if (!url || !anon_key) return ESP_ERR_INVALID_ARG;

    strncpy(s_config.supabase_url, url, sizeof(s_config.supabase_url) - 1);
    strncpy(s_config.supabase_anon_key, anon_key, sizeof(s_config.supabase_anon_key) - 1);

    esp_err_t err = nvs_save_string(NVS_KEY_SUPA_URL, s_config.supabase_url);
    err |= nvs_save_string(NVS_KEY_SUPA_KEY, s_config.supabase_anon_key);

    return err;
}

esp_err_t Provisioning_UpdatePersonalization(const char* name, uint32_t eye_color,
                                              uint8_t brightness, uint8_t volume)
{
    if (name) {
        strncpy(s_config.device_name, name, sizeof(s_config.device_name) - 1);
        nvs_save_string(NVS_KEY_DEV_NAME, s_config.device_name);
    }
    if (eye_color != 0) {
        s_config.eye_color = eye_color;
        nvs_save_u32(NVS_KEY_EYE_COLOR, s_config.eye_color);
    }
    if (brightness != 255) {
        s_config.brightness = brightness;
        nvs_save_u8(NVS_KEY_BRIGHTNESS, s_config.brightness);
    }
    if (volume != 255) {
        s_config.volume = volume;
        nvs_save_u8(NVS_KEY_VOLUME, s_config.volume);
    }

    ESP_LOGI(TAG, "Personalization updated: name=%s color=0x%06lX bright=%d vol=%d",
             s_config.device_name, (unsigned long)s_config.eye_color,
             s_config.brightness, s_config.volume);

    return ESP_OK;
}

esp_err_t Provisioning_GetHardwareId(char* out_id, size_t max_len)
{
    if (!out_id || max_len < 18) return ESP_ERR_INVALID_ARG;

    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) return err;

    snprintf(out_id, max_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    return ESP_OK;
}

// ============================================================
// WI-FI CONNECTION (STA MODE)
// ============================================================

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < WIFI_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGW(TAG, "Wi-Fi disconnected. Retry %d/%d", s_retry_count, WIFI_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG, "Wi-Fi connection failed after %d retries", WIFI_MAX_RETRY);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Connected! IP: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

esp_err_t Provisioning_ConnectWiFi(void)
{
    if (strlen(s_config.wifi_ssid) == 0) {
        ESP_LOGE(TAG, "No Wi-Fi credentials saved");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to Wi-Fi: %s", s_config.wifi_ssid);

    // Clear event group bits before connecting
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // Configure STA
    wifi_config_t sta_config = {};
    strncpy((char*)sta_config.sta.ssid, s_config.wifi_ssid, sizeof(sta_config.sta.ssid) - 1);
    strncpy((char*)sta_config.sta.password, s_config.wifi_password, sizeof(sta_config.sta.password) - 1);
    sta_config.sta.threshold.authmode = strlen(s_config.wifi_password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    // Use STA-only mode for normal connection to save internal RAM
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    
    s_retry_count = 0;
    ESP_ERROR_CHECK(esp_wifi_start());

    // Wait for connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(15000));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connected successfully!");
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "Wi-Fi connection failed");
        return ESP_FAIL;
    }
}

// ============================================================
// CAPTIVE PORTAL (AP MODE + HTTP SERVER)
// ============================================================

esp_err_t Provisioning_StartCaptivePortal(void)
{
    ESP_LOGI(TAG, "Starting captive portal...");

    // Generate AP SSID with last 4 chars of MAC
    char hw_id[18];
    Provisioning_GetHardwareId(hw_id, sizeof(hw_id));
    char ap_ssid[33];
    snprintf(ap_ssid, sizeof(ap_ssid), "%s-%c%c%c%c", AP_SSID_PREFIX,
             hw_id[12], hw_id[13], hw_id[15], hw_id[16]);

    ESP_LOGI(TAG, "AP SSID: %s", ap_ssid);

    // Configure AP
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.ap.ssid, ap_ssid, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(ap_ssid);
    wifi_config.ap.max_connection = AP_MAX_CONNECTIONS;
    wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    wifi_config.ap.channel = AP_CHANNEL;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Start HTTP server for captive portal
    esp_err_t err = start_http_server();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
        return err;
    }

    // Start DNS server for captive portal redirection
    err = DnsServer_Start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start DNS server: %s", esp_err_to_name(err));
    }

    ESP_LOGI(TAG, "Captive portal running at http://192.168.4.1");
    return ESP_OK;
}

esp_err_t Provisioning_StopCaptivePortal(void)
{
    ESP_LOGI(TAG, "Stopping captive portal...");
    DnsServer_Stop();
    stop_http_server();
    esp_wifi_stop();
    return ESP_OK;
}

void Provisioning_FactoryReset(void)
{
    ESP_LOGW(TAG, "!!! FACTORY RESET !!!");
    nvs_erase_all(s_nvs_handle);
    nvs_commit(s_nvs_handle);
    esp_restart();
}

// ============================================================
// HTTP SERVER HANDLERS (Captive Portal API)
// ============================================================

// Serve the setup wizard HTML page
static esp_err_t http_get_root_handler(httpd_req_t *req)
{
    // The captive portal HTML is embedded via EMBED_FILES in CMakeLists
    extern const uint8_t portal_html_start[] asm("_binary_portal_html_start");
    extern const uint8_t portal_html_end[]   asm("_binary_portal_html_end");
    size_t portal_html_len = portal_html_end - portal_html_start;

    httpd_resp_set_type(req, "text/html");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, (const char*)portal_html_start, portal_html_len);
    return ESP_OK;
}

// API: Scan for Wi-Fi networks
static esp_err_t http_get_scan_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API: Wi-Fi scan requested");

    // Already in APSTA mode, no need to switch modes
    
    wifi_scan_config_t scan_config = {
        .show_hidden = false,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };
    esp_wifi_scan_start(&scan_config, true);

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count > 20) ap_count = 20;  // Limit results

    wifi_ap_record_t *ap_list = malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_list) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);

    // Build JSON response
    char *json_buf = malloc(4096);
    if (!json_buf) {
        free(ap_list);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
        return ESP_FAIL;
    }

    int offset = snprintf(json_buf, 4096, "{\"networks\":[");
    for (int i = 0; i < ap_count; i++) {
        offset += snprintf(json_buf + offset, 4096 - offset,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"auth\":%d}",
            i > 0 ? "," : "",
            (char*)ap_list[i].ssid,
            ap_list[i].rssi,
            ap_list[i].authmode);
    }
    snprintf(json_buf + offset, 4096 - offset, "]}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json_buf, strlen(json_buf));

    free(ap_list);
    free(json_buf);
    
    // Already in APSTA mode, keep it active
    
    return ESP_OK;
}

static volatile bool s_restart_requested = false;

static void restart_task(void *pvParameters)
{
    // Wait up to 8 seconds for the portal JS to call /api/restart,
    // or auto-restart after the safety timeout expires.
    for (int i = 0; i < 80; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (s_restart_requested) break;
    }
    ESP_LOGI(TAG, "Stopping captive portal before restart...");
    Provisioning_StopCaptivePortal();
    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "Rebooting device...");
    esp_restart();
    vTaskDelete(NULL);
}

// API: Save Wi-Fi credentials and connect
static esp_err_t http_post_connect_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API: Wi-Fi connect requested");

    char buf[256] = {0};
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }

    // Simple JSON parsing (ssid and password fields)
    char ssid[33] = {0};
    char password[65] = {0};
    
    // Extract ssid
    char *ssid_start = strstr(buf, "\"ssid\":\"");
    if (ssid_start) {
        ssid_start += 8;
        char *ssid_end = strchr(ssid_start, '"');
        if (ssid_end && (ssid_end - ssid_start) < (int)sizeof(ssid)) {
            memcpy(ssid, ssid_start, ssid_end - ssid_start);
        }
    }

    // Extract password
    char *pass_start = strstr(buf, "\"password\":\"");
    if (pass_start) {
        pass_start += 12;
        char *pass_end = strchr(pass_start, '"');
        if (pass_end && (pass_end - pass_start) < (int)sizeof(password)) {
            memcpy(password, pass_start, pass_end - pass_start);
        }
    }

    if (strlen(ssid) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing SSID");
        return ESP_FAIL;
    }

    // Disconnect from any current connection first
    esp_wifi_disconnect();

    // Clear event group bits before testing
    xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    // Configure STA interface temporarily for the test
    wifi_config_t wifi_config = {};
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char*)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.threshold.authmode = strlen(password) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    s_retry_count = 0;
    esp_err_t conn_err = esp_wifi_connect();
    if (conn_err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initiate Wi-Fi connection: %s", esp_err_to_name(conn_err));
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to initiate Wi-Fi connection\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    // Wait for connection with a timeout (10 seconds)
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE, pdMS_TO_TICKS(10000));

    if (!(bits & WIFI_CONNECTED_BIT)) {
        ESP_LOGE(TAG, "Wi-Fi test connection failed (timeout or wrong credentials)");
        esp_wifi_disconnect(); // Clean up connection attempt
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Incorrect password or network unreachable\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Wi-Fi test connection succeeded! Saving credentials...");

    // Save credentials
    esp_err_t err = Provisioning_SaveWiFi(ssid, password);
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"success\":false,\"error\":\"Failed to save\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true,\"message\":\"Credentials saved. Waiting for confirmation...\"}", HTTPD_RESP_USE_STRLEN);

    // Start a background restart task that waits for /api/restart or auto-restarts after 8s
    s_restart_requested = false;
    xTaskCreate(restart_task, "restart_task", 2048, NULL, 5, NULL);

    return ESP_OK;
}

// API: Get device info
static esp_err_t http_get_device_info_handler(httpd_req_t *req)
{
    char hw_id[18];
    Provisioning_GetHardwareId(hw_id, sizeof(hw_id));

    char json[256];
    snprintf(json, sizeof(json),
        "{\"hardware_id\":\"%s\",\"name\":\"%s\",\"firmware\":\"%s\",\"boot_count\":%lu}",
        hw_id, s_config.device_name, "1.0.0", (unsigned long)s_config.boot_count);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
    httpd_resp_send(req, json, strlen(json));
    return ESP_OK;
}

// API: Trigger device restart (called by portal JS when user is ready)
static esp_err_t http_post_restart_handler(httpd_req_t *req)
{
    ESP_LOGI(TAG, "API: Restart requested by portal");
    s_restart_requested = true;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"success\":true}", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// Redirect all unknown URLs to root (captive portal behavior)
static esp_err_t http_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static esp_err_t start_http_server(void)
{
    if (s_httpd != NULL) {
        ESP_LOGI(TAG, "HTTP server already running");
        return ESP_OK;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 10;
    config.stack_size = 4096;
    config.uri_match_fn = httpd_uri_match_wildcard;

    esp_err_t err = httpd_start(&s_httpd, &config);
    if (err != ESP_OK) return err;

    // Register URI handlers
    const httpd_uri_t root = {
        .uri = "/", .method = HTTP_GET, .handler = http_get_root_handler
    };
    const httpd_uri_t scan = {
        .uri = "/api/scan", .method = HTTP_GET, .handler = http_get_scan_handler
    };
    const httpd_uri_t connect_wifi = {
        .uri = "/api/connect", .method = HTTP_POST, .handler = http_post_connect_handler
    };
    const httpd_uri_t device_info = {
        .uri = "/api/device", .method = HTTP_GET, .handler = http_get_device_info_handler
    };
    const httpd_uri_t restart = {
        .uri = "/api/restart", .method = HTTP_POST, .handler = http_post_restart_handler
    };
    const httpd_uri_t redirect = {
        .uri = "/*", .method = HTTP_GET, .handler = http_redirect_handler
    };

    httpd_register_uri_handler(s_httpd, &root);
    httpd_register_uri_handler(s_httpd, &scan);
    httpd_register_uri_handler(s_httpd, &connect_wifi);
    httpd_register_uri_handler(s_httpd, &device_info);
    httpd_register_uri_handler(s_httpd, &restart);
    httpd_register_uri_handler(s_httpd, &redirect);  // Must be last (wildcard)

    ESP_LOGI(TAG, "HTTP server started with %d handlers", 5);
    return ESP_OK;
}

static void stop_http_server(void)
{
    if (s_httpd) {
        httpd_stop(s_httpd);
        s_httpd = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}

// ============================================================
// NVS HELPERS
// ============================================================

static esp_err_t nvs_load_config(void)
{
    size_t len;
    esp_err_t err;

    // Load strings
    #define LOAD_STR(key, dest) do { \
        len = sizeof(dest); \
        err = nvs_get_str(s_nvs_handle, key, dest, &len); \
        if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) return err; \
    } while(0)

    LOAD_STR(NVS_KEY_WIFI_SSID, s_config.wifi_ssid);
    LOAD_STR(NVS_KEY_WIFI_PASS, s_config.wifi_password);
    LOAD_STR(NVS_KEY_DEV_ID,    s_config.device_id);
    LOAD_STR(NVS_KEY_AUTH_TOKEN, s_config.auth_token);
    LOAD_STR(NVS_KEY_SUPA_URL,  s_config.supabase_url);
    LOAD_STR(NVS_KEY_SUPA_KEY,  s_config.supabase_anon_key);
    LOAD_STR(NVS_KEY_DEV_NAME,  s_config.device_name);

    #undef LOAD_STR

    // Fallback compile-time defaults for Supabase settings if empty
    if (strlen(s_config.supabase_url) == 0) {
        strncpy(s_config.supabase_url, "https://cnbwttjojlrconmargzh.supabase.co", sizeof(s_config.supabase_url) - 1);
    }
    if (strlen(s_config.supabase_anon_key) == 0) {
        strncpy(s_config.supabase_anon_key, "eyJhbGciOiJIUzI1NiIsInR5cCI6IkpXVCJ9.eyJpc3MiOiJzdXBhYmFzZSIsInJlZiI6ImNuYnd0dGpvamxyY29ubWFyZ3poIiwicm9sZSI6ImFub24iLCJpYXQiOjE3ODA1NTkxMTEsImV4cCI6MjA5NjEzNTExMX0.lnv5XcSBzLvbvVf-rLdq-ioOXUsKCBuoISrrwNKnw5w", sizeof(s_config.supabase_anon_key) - 1);
    }

    // Load integers
    uint8_t u8val;
    uint32_t u32val;

    if (nvs_get_u8(s_nvs_handle, NVS_KEY_PROV_STATE, &u8val) == ESP_OK)
        s_config.prov_state = (prov_state_t)u8val;
    if (nvs_get_u32(s_nvs_handle, NVS_KEY_EYE_COLOR, &u32val) == ESP_OK)
        s_config.eye_color = u32val;
    if (nvs_get_u8(s_nvs_handle, NVS_KEY_BRIGHTNESS, &u8val) == ESP_OK)
        s_config.brightness = u8val;
    if (nvs_get_u8(s_nvs_handle, NVS_KEY_VOLUME, &u8val) == ESP_OK)
        s_config.volume = u8val;
    if (nvs_get_u32(s_nvs_handle, NVS_KEY_BOOT_COUNT, &u32val) == ESP_OK)
        s_config.boot_count = u32val;

    return ESP_OK;
}

static esp_err_t nvs_save_string(const char* key, const char* value)
{
    esp_err_t err = nvs_set_str(s_nvs_handle, key, value);
    if (err == ESP_OK) err = nvs_commit(s_nvs_handle);
    if (err != ESP_OK) ESP_LOGE(TAG, "NVS save failed for '%s': %s", key, esp_err_to_name(err));
    return err;
}

static esp_err_t nvs_save_u8(const char* key, uint8_t value)
{
    esp_err_t err = nvs_set_u8(s_nvs_handle, key, value);
    if (err == ESP_OK) err = nvs_commit(s_nvs_handle);
    return err;
}

static esp_err_t nvs_save_u32(const char* key, uint32_t value)
{
    esp_err_t err = nvs_set_u32(s_nvs_handle, key, value);
    if (err == ESP_OK) err = nvs_commit(s_nvs_handle);
    return err;
}
