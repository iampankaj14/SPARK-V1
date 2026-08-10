#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void Spark_Emotion_Init(void);
void Spark_Emotion_Set(const char *emotion_tag);
void Spark_Emotion_ProcessIntent(const char *intent_name);

#ifdef __cplusplus
}
#endif
