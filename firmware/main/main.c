#include "Display_SPD2010.h"
#include "esp_timer.h"
#include "PCF85063.h"
#include "QMI8658.h"
#include "SD_MMC.h"
#include "Wireless.h"
#include "TCA9554PWR.h"
#include "deskimon.h"
#include "BAT_Driver.h"
#include "PWR_Key.h"
#include "PCM5101.h"
#include "MIC_Speech.h"
#include "spark_state.h"
#include "spark_hardware.h"
#include "spark_face.h"
#include "spark_animation.h"
#include "spark_emotion.h"
#include "spark_intent.h"
#include "spark_solar_system.h"
#include "spark_portal_travel.h"

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
    EXIO_Init();                    // Example Initialize EXIO
    Flash_Searching();
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
void app_main(void)
{
    Driver_Init();

    SD_Init();
    LCD_Init();
    
    // Initialize Wi-Fi first to guarantee it gets contiguous internal DMA memory 
    // before LVGL and Speech Models consume all internal RAM.
    Wireless_Init();
    
    LVGL_Init();
    Audio_Init();
    MIC_Speech_init(); // Turn mic back on for normal user interaction
    
    // Initialize Spark Core Managers
    Spark_State_Init();
    Spark_Hardware_Init();
    Spark_Face_Init();
    Spark_Anim_Init();
    Spark_Emotion_Init();
    Spark_Intent_Init();

    // Start the Deskimon Interface
    Deskimon_Start();
    
    // Initialize Cinematic Faces
    Spark_SolarSystem_Init(lv_scr_act());
    Spark_PortalTravel_Init(lv_scr_act());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        lv_timer_handler();
    }
}






