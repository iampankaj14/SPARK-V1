#pragma once

#include "lvgl.h"

#include "LVGL_Driver.h"
#include "PCF85063.h"
#include "QMI8658.h"
#include "SD_MMC.h"
#include "BAT_Driver.h"
#include "Wireless.h"

void Deskimon_Start(void);
void Deskimon_SetEyeColor(uint32_t color_hex);
void Deskimon_SetEmotion(const char* emotion);