#include "spark_intent.h"
#include "../MIC_Driver/MIC_Speech.h"
#include "esp_log.h"

#define TAG "SparkIntent"

void Spark_Intent_Init(void) {
    ESP_LOGI(TAG, "Intent Manager initialized");
}

void Spark_Intent_StartRecording(void) {
    MIC_StartRecordingManual();
}

void Spark_Intent_StopRecording(void) {
    MIC_SetConvState(CONV_STATE_IDLE);
}

bool Spark_Intent_IsRecording(void) {
    return (MIC_GetConvState() == CONV_STATE_LISTENING);
}
