#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void Deskimon_Start(void);
void Deskimon_SetEyeColor(uint32_t color_hex);
void Deskimon_SetEmotion(const char* emotion);
void Deskimon_SetStatus(const char* status);
void Deskimon_SetChatMessage(const char* role, const char* content);
void Deskimon_ShowNotification(const char* notification, int duration_ms);

#ifdef __cplusplus
}
#endif