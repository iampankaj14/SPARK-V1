#include "spark_display.h"
#include <esp_log.h>

#include "spark_emotion.h"
#include "spark_face.h"
#include "deskimon.h"
#include "LVGL_Driver.h"

#define TAG "SparkDisplay"

SparkDisplay::SparkDisplay() {
    width_ = 412;
    height_ = 412;
    setup_ui_called_ = true;
    ESP_LOGI(TAG, "SparkDisplay initialized with Spark faces.");
}

SparkDisplay::~SparkDisplay() {}

void SparkDisplay::SetStatus(const char* status) {
    ESP_LOGI(TAG, "SetStatus: %s", status ? status : "null");
    if (status) {
        Deskimon_SetStatus(status);
    }
}

void SparkDisplay::ShowNotification(const char* notification, int duration_ms) {
    ESP_LOGI(TAG, "Notification (%d ms): %s", duration_ms, notification ? notification : "null");
    if (notification) {
        Deskimon_ShowNotification(notification, duration_ms);
    }
}

void SparkDisplay::SetEmotion(const char* emotion) {
    if (!emotion) return;
    ESP_LOGI(TAG, "Setting Spark emotion to: %s", emotion);
    Spark_Emotion_Set(emotion);
    Deskimon_SetEmotion(emotion);
}

void SparkDisplay::SetChatMessage(const char* role, const char* content) {
    ESP_LOGI(TAG, "ChatMessage [%s]: %s", role ? role : "null", content ? content : "null");
    Deskimon_SetChatMessage(role, content);
}

void SparkDisplay::UpdateStatusBar(bool update_all) {
    // Spark does not have a status bar, but we can log metrics if needed.
}

void SparkDisplay::SetPowerSaveMode(bool on) {
    ESP_LOGI(TAG, "PowerSaveMode: %s", on ? "ON" : "OFF");
}

bool SparkDisplay::Lock(int timeout_ms) {
    return lvgl_port_lock((uint32_t)timeout_ms);
}

void SparkDisplay::Unlock() {
    lvgl_port_unlock();
}
