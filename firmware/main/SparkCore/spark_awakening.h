#pragma once
#include "lvgl.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    AWAKENING_SCREEN_DORMANT_SIGNAL = 0,
    AWAKENING_SCREEN_FIRST_TRANSMISSION,
    AWAKENING_SCREEN_ORIGIN_SEARCH,
    AWAKENING_SCREEN_LOST_ARCHIVE,
    AWAKENING_SCREEN_MEMORY_FRAGMENTS,
    AWAKENING_SCREEN_HUMAN_DETECTION,
    AWAKENING_SCREEN_FIRST_CONTACT,
    AWAKENING_SCREEN_SYNCHRONIZATION,
    AWAKENING_SCREEN_BOND_FORMATION,
    AWAKENING_SCREEN_IDENTITY_RECOVERY,
    AWAKENING_SCREEN_BIRTH,
    AWAKENING_SCREEN_COMPLETE
} spark_awakening_screen_t;

void Spark_Awakening_Init(void);
void Spark_Awakening_Start(void);
void Spark_Awakening_Update(uint32_t delta_ms);
void Spark_Awakening_OnNetworkReceived(const char* ssid, const char* user_name);
void Spark_Awakening_Stop(void);
bool Spark_Awakening_IsActive(void);

#ifdef __cplusplus
}
#endif
