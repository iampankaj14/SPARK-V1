#include "Display_SPD2010.h"
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

void Driver_Loop(void *parameter)
{
    vTaskDelay(pdMS_TO_TICKS(5000));
    Wireless_Init();
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
    LVGL_Init();
    Audio_Init();
    MIC_Speech_init();
    
    // Initialize Spark Core Managers
    Spark_State_Init();
    Spark_Hardware_Init();
    Spark_Face_Init();
    Spark_Anim_Init();
    Spark_Emotion_Init();
    Spark_Intent_Init();

    // Start the Deskimon Interface
    Deskimon_Start();

    while (1) {
        // raise the task priority of LVGL and/or reduce the handler period can improve the performance
        vTaskDelay(pdMS_TO_TICKS(10));
        // The task running lv_timer_handler should have lower priority than that running `lv_tick_inc`
        lv_timer_handler();
    }
}






