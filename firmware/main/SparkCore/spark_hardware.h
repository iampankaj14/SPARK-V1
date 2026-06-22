#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SPARK_HW_EVENT_SHAKE,
    SPARK_HW_EVENT_TILT_UP,
    SPARK_HW_EVENT_TILT_DOWN,
    SPARK_HW_EVENT_TAP,
    SPARK_HW_EVENT_DOUBLE_TAP,
    SPARK_HW_EVENT_TRIPLE_TAP,
    SPARK_HW_EVENT_LONG_PRESS_5S
} spark_hw_event_t;

typedef void (*spark_hw_callback_t)(spark_hw_event_t event);

void Spark_Hardware_Init(void);
float Spark_Hardware_GetBatteryVolts(void);
int Spark_Hardware_GetBatteryPercent(void);
void Spark_Hardware_SetBacklight(uint8_t percentage);
void Spark_Hardware_RegisterCallback(spark_hw_callback_t callback);
