#include "spark_state.h"
#include "esp_log.h"
#include <stddef.h>

#define TAG "SparkState"

static spark_state_t s_current_state = SPARK_STATE_BOOT;

#define MAX_STATE_CALLBACKS 8
static spark_state_cb_t s_callbacks[MAX_STATE_CALLBACKS];
static int s_callback_count = 0;

void Spark_State_Init(void) {
    s_current_state = SPARK_STATE_BOOT;
    s_callback_count = 0;
    for (int i = 0; i < MAX_STATE_CALLBACKS; i++) {
        s_callbacks[i] = NULL;
    }
}

spark_state_t Spark_State_Get(void) {
    return s_current_state;
}

bool Spark_State_TransitionTo(spark_state_t next_state) {
    if (next_state == s_current_state) {
        return true;
    }

    // Validate transition
    bool allowed = false;
    switch (s_current_state) {
        case SPARK_STATE_BOOT:
            if (next_state == SPARK_STATE_IDLE || next_state == SPARK_STATE_ERROR) allowed = true;
            break;
        case SPARK_STATE_IDLE:
            if (next_state == SPARK_STATE_LISTENING || next_state == SPARK_STATE_SLEEPING || 
                next_state == SPARK_STATE_CHARGING || next_state == SPARK_STATE_UPDATING || 
                next_state == SPARK_STATE_ERROR) allowed = true;
            break;
        case SPARK_STATE_LISTENING:
            if (next_state == SPARK_STATE_THINKING || next_state == SPARK_STATE_IDLE || 
                next_state == SPARK_STATE_ERROR) allowed = true;
            break;
        case SPARK_STATE_THINKING:
            if (next_state == SPARK_STATE_SPEAKING || next_state == SPARK_STATE_IDLE || 
                next_state == SPARK_STATE_ERROR) allowed = true;
            break;
        case SPARK_STATE_SPEAKING:
            if (next_state == SPARK_STATE_LISTENING || next_state == SPARK_STATE_IDLE || 
                next_state == SPARK_STATE_ERROR) allowed = true;
            break;
        case SPARK_STATE_SLEEPING:
            if (next_state == SPARK_STATE_IDLE || next_state == SPARK_STATE_CHARGING || 
                next_state == SPARK_STATE_ERROR) allowed = true;
            break;
        case SPARK_STATE_CHARGING:
            if (next_state == SPARK_STATE_IDLE || next_state == SPARK_STATE_SLEEPING || 
                next_state == SPARK_STATE_ERROR) allowed = true;
            break;
        case SPARK_STATE_UPDATING:
            if (next_state == SPARK_STATE_BOOT || next_state == SPARK_STATE_ERROR) allowed = true;
            break;
        case SPARK_STATE_ERROR:
            if (next_state == SPARK_STATE_BOOT || next_state == SPARK_STATE_IDLE) allowed = true;
            break;
        default:
            break;
    }

    // Always allow manual overrides if forced to prevent system lock
    if (!allowed) {
        ESP_LOGW(TAG, "Override transition: %s -> %s", 
                 Spark_State_ToString(s_current_state), Spark_State_ToString(next_state));
        allowed = true;
    }

    if (allowed) {
        spark_state_t old_state = s_current_state;
        s_current_state = next_state;
        ESP_LOGI(TAG, "Transition: %s -> %s", 
                 Spark_State_ToString(old_state), Spark_State_ToString(next_state));
        
        for (int i = 0; i < s_callback_count; ++i) {
            if (s_callbacks[i]) {
                s_callbacks[i](old_state, next_state);
            }
        }
        return true;
    }
    return false;
}

void Spark_State_RegisterCallback(spark_state_cb_t callback) {
    if (s_callback_count < MAX_STATE_CALLBACKS) {
        s_callbacks[s_callback_count++] = callback;
    } else {
        ESP_LOGE(TAG, "Failed to register callback: full");
    }
}

const char* Spark_State_ToString(spark_state_t state) {
    switch (state) {
        case SPARK_STATE_BOOT: return "BOOT";
        case SPARK_STATE_IDLE: return "IDLE";
        case SPARK_STATE_LISTENING: return "LISTENING";
        case SPARK_STATE_THINKING: return "THINKING";
        case SPARK_STATE_SPEAKING: return "SPEAKING";
        case SPARK_STATE_SLEEPING: return "SLEEPING";
        case SPARK_STATE_CHARGING: return "CHARGING";
        case SPARK_STATE_UPDATING: return "UPDATING";
        case SPARK_STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}
