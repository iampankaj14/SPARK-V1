#pragma once

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "esp_wifi.h"
#include "nvs_flash.h" 
#include "esp_log.h"

#include <stdio.h>
#include <string.h>  // For memcpy
#include "esp_system.h"
#ifdef CONFIG_BT_ENABLED
#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_bt_main.h"
#endif

extern uint16_t BLE_NUM;
extern uint16_t WIFI_NUM;
extern bool Scan_finish;

void Wireless_Init(void);
void WIFI_Init(void *arg);
uint16_t WIFI_Scan(void);

#ifdef CONFIG_BT_ENABLED
void BLE_Init(void *arg);
uint16_t BLE_Scan(void);
#endif