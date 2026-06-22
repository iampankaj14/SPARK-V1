#pragma once

#include <stdbool.h>

typedef enum {
    SPARK_STATE_BOOT,
    SPARK_STATE_IDLE,
    SPARK_STATE_LISTENING,
    SPARK_STATE_THINKING,
    SPARK_STATE_SPEAKING,
    SPARK_STATE_SLEEPING,
    SPARK_STATE_CHARGING,
    SPARK_STATE_UPDATING,
    SPARK_STATE_ERROR,
    SPARK_STATE_MAX
} spark_state_t;

typedef void (*spark_state_cb_t)(spark_state_t old_state, spark_state_t new_state);

void Spark_State_Init(void);
spark_state_t Spark_State_Get(void);
bool Spark_State_TransitionTo(spark_state_t next_state);
void Spark_State_RegisterCallback(spark_state_cb_t callback);
const char* Spark_State_ToString(spark_state_t state);
