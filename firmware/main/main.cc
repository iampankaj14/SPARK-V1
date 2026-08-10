#include <esp_log.h>
#include <esp_err.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <driver/gpio.h>
#include <esp_event.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "application.h"
#include "esp_lcd_panel_io.h"

extern "C" {
#include "Display_SPD2010.h"
#include "PCF85063.h"
#include "QMI8658.h"
#include "SD_MMC.h"
#include "TCA9554PWR.h"
#include "deskimon.h"
#include "BAT_Driver.h"
#include "PWR_Key.h"
#include "PCM5101.h"

#include "spark_hardware.h"
#include "spark_face.h"
#include "spark_animation.h"
#include "spark_emotion.h"
#include "spark_solar_system.h"
#include "spark_portal_travel.h"
}

#define TAG "main"

static void lvgl_task(void *param) {
    while (1) {
        if (lvgl_port_lock(10)) {
            uint32_t time_till_next = lv_timer_handler();
            lvgl_port_unlock();
            if (time_till_next < 10) time_till_next = 10;
            else if (time_till_next > 30) time_till_next = 30;
            vTaskDelay(pdMS_TO_TICKS(time_till_next));
        } else {
            vTaskDelay(pdMS_TO_TICKS(5));
        }
    }
    vTaskDelete(NULL);
}

void Driver_Loop(void *parameter)
{
    while(1)
    {
        QMI8658_Loop();
        PCF85063_Loop();
        BAT_Get_Volts();
        PWR_Loop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    vTaskDelete(NULL);
}

void Driver_Init(void)
{
    PWR_Init();
    BAT_Init();
    I2C_Init();
    EXIO_Init();                    // Initialize EXIO
    
    // Initialize NVS flash for WiFi configuration
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Erasing NVS flash to fix corruption");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    PCF85063_Init();
    QMI8658_Init();
    xTaskCreatePinnedToCore(
        Driver_Loop, 
        "Other Driver task",
        4096, 
        NULL, 
        3, 
        NULL, 
        0);
}

extern "C" void app_main(void)
{
    Driver_Init();

    SD_Init();
    LCD_Init();
    
    // Initialize LVGL
    LVGL_Init();
    
    // Initialize Spark Core Managers
    Spark_Hardware_Init();
    Spark_Face_Init();
    Spark_Anim_Init();
    Spark_Emotion_Init();

    // Start the Deskimon Interface
    Deskimon_Start();

    // Create a background task for LVGL handling so main loop doesn't block it
    xTaskCreatePinnedToCore(
        lvgl_task,
        "LVGL Task",
        8192,
        NULL,
        5,
        NULL,
        1
    );

    // Initialize and run the Xiaozhi Voice AI application
    auto& app = Application::GetInstance();
    app.Initialize();
    app.Run();  // This function runs the main event loop and never returns
}

extern "C" void Spark_StartListening(void) {
    Application::GetInstance().StartListening();
}
