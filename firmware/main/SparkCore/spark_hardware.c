#include "spark_hardware.h"
#include "../QMI8658/QMI8658.h"
#include "../BAT_Driver/BAT_Driver.h"
#include "../PWR_Key/PWR_Key.h"
#include "../LCD_Driver/Display_SPD2010.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

#define TAG "SparkHardware"

#define MAX_HW_CALLBACKS 4
static spark_hw_callback_t s_callbacks[MAX_HW_CALLBACKS];
static int s_callback_count = 0;

static float s_last_accel_x = 0;
static float s_last_accel_y = 0;
static float s_last_accel_z = 0;

static void spark_hardware_task(void *param) {
    vTaskDelay(pdMS_TO_TICKS(6000)); // wait for init to settle
    while (1) {
        // Read accelerometer data (already updated by QMI8658_Loop in Driver_Loop)
        float dx = Accel.x - s_last_accel_x;
        float dy = Accel.y - s_last_accel_y;
        float dz = Accel.z - s_last_accel_z;
        float move_amt = (dx * dx) + (dy * dy) + (dz * dz);
        
        s_last_accel_x = Accel.x;
        s_last_accel_y = Accel.y;
        s_last_accel_z = Accel.z;

        bool tilted_up = (Accel.y > 0.6f);
        bool shaking = (move_amt > 1.5f);

        if (tilted_up) {
            for (int i = 0; i < s_callback_count; i++) {
                if (s_callbacks[i]) s_callbacks[i](SPARK_HW_EVENT_TILT_UP);
            }
        } else {
            // Can support other tilt directions if needed
        }

        if (shaking) {
            for (int i = 0; i < s_callback_count; i++) {
                if (s_callbacks[i]) s_callbacks[i](SPARK_HW_EVENT_SHAKE);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void Spark_Hardware_Init(void) {
    s_callback_count = 0;
    for (int i = 0; i < MAX_HW_CALLBACKS; i++) {
        s_callbacks[i] = NULL;
    }
    
    // Spawn hardware events polling task
    xTaskCreatePinnedToCore(spark_hardware_task, "spark_hw_task", 4096, NULL, 3, NULL, 0);
}

float Spark_Hardware_GetBatteryVolts(void) {
    return BAT_Get_Volts();
}

int Spark_Hardware_GetBatteryPercent(void) {
    float volts = BAT_Get_Volts();
    int battery = (int)((volts - 3.3f) / (4.2f - 3.3f) * 100.0f);
    if (battery > 100) battery = 100;
    if (battery < 0) battery = 0;
    return battery;
}

void Spark_Hardware_SetBacklight(uint8_t percentage) {
    LCD_Backlight = percentage;
}

void Spark_Hardware_RegisterCallback(spark_hw_callback_t callback) {
    if (s_callback_count < MAX_HW_CALLBACKS) {
        s_callbacks[s_callback_count++] = callback;
    } else {
        ESP_LOGE(TAG, "Hardware callback registry full!");
    }
}
