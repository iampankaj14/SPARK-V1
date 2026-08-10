#include "spark_board.h"
#include <esp_log.h>
#include <cJSON.h>

#define TAG "SparkBoard"

extern "C" {
#include "spark_hardware.h"
}

SparkBoard::SparkBoard() {
    ESP_LOGI(TAG, "SparkBoard singleton created.");
}

SparkBoard::~SparkBoard() {}

std::string SparkBoard::GetBoardType() {
    return "spark";
}

AudioCodec* SparkBoard::GetAudioCodec() {
    static SparkAudioCodec audio_codec(16000, 16000);
    return &audio_codec;
}

Display* SparkBoard::GetDisplay() {
    static SparkDisplay display;
    return &display;
}

bool SparkBoard::GetBatteryLevel(int &level, bool& charging, bool& discharging) {
    level = Spark_Hardware_GetBatteryPercent();
    charging = false;     // Default to false for simplicity
    discharging = true;   // Default to true
    return true;
}

std::string SparkBoard::GetBoardJson() {
    std::string json = R"({"type":"spark",)";
    json += R"("name":"Spark Companion Robot",)";
    json += R"("manufacturer":"Deskimon",)";
    json += R"("mac":")" + uuid_ + R"("})";
    return json;
}

DECLARE_BOARD(SparkBoard);
