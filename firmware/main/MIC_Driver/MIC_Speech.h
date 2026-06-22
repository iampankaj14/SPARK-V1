#pragma once

#include "esp_afe_sr_iface.h"
#include "esp_process_sdkconfig.h"
#include "model_path.h"
#include "Display_SPD2010.h"

#define I2S_CONFIG_DEFAULT(sample_rate, channel_fmt, bits_per_chan) { \
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate), \
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits_per_chan, channel_fmt), \
        .gpio_cfg = { \
            .mclk = GPIO_NUM_NC, \
            .bclk = GPIO_NUM_15, \
            .ws   = GPIO_NUM_2, \
            .dout = GPIO_NUM_NC, \
            .din  = GPIO_NUM_39, \
            .invert_flags = { \
                .mclk_inv = false, \
                .bclk_inv = false, \
                .ws_inv   = false, \
            }, \
        }, \
    }
typedef enum
{
    COMMAND_TIMEOUT = -2,
    COMMAND_NOT_DETECTED = -1,

    COMMAND_ID1 = 0,
    COMMAND_ID2 = 1,
    COMMAND_ID3 = 2,
    COMMAND_ID4 = 3,
    COMMAND_ID5 = 4,
    COMMAND_ID6 = 5,
} command_word_t;

// ============================================================
// CONVERSATION STATE MACHINE
// ============================================================
typedef enum {
    CONV_STATE_IDLE = 0,            // Wake word detection active
    CONV_STATE_WAKE_DETECTED,       // Wake word just fired, waiting for channel verify
    CONV_STATE_LISTENING,           // Recording user speech (VAD active)
    CONV_STATE_PROCESSING,          // Audio uploaded, waiting for AI response
    CONV_STATE_SPEAKING,            // Playing audio response (mic muted)
    CONV_STATE_FOLLOWUP_LISTENING,  // Post-response listening window (15s timer)
} conv_state_t;

typedef struct {
    const esp_afe_sr_iface_t *afe_handle;
    esp_afe_sr_data_t *afe_data;
    srmodel_list_t *models;
    bool detected;
    command_word_t command;
} AppSpeech;

// Core init
void MIC_Speech_init(void);

// Conversation state machine API (called from Cloud module)
conv_state_t MIC_GetConvState(void);
void MIC_SetConvState(conv_state_t new_state);
void MIC_StartRecordingManual(void);


