#include "spark_awakening.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "SPARK_AWAKENING";
static bool s_is_active = false;

void Spark_Awakening_Init(void) {
    s_is_active = false;
    ESP_LOGI(TAG, "Spark Awakening Initialized");
}

bool Spark_Awakening_IsActive(void) {
    return s_is_active;
}

void Spark_Awakening_Start(void) {
    ESP_LOGI(TAG, "Boot awakening sequence skipped. Direct active UI mode.");
    s_is_active = false;
}

void Spark_Awakening_Update(uint32_t delta_ms) {
    (void)delta_ms;
    s_is_active = false;
}

void Spark_Awakening_OnNetworkReceived(const char* ssid, const char* user_name) {
    (void)ssid;
    (void)user_name;
}

void Spark_Awakening_Stop(void) {
    s_is_active = false;
    ESP_LOGI(TAG, "Spark Awakening Stopped");
}
