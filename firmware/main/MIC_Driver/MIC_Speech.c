#include "MIC_Speech.h"
#include "Cloud.h"
#include "spark_emotion.h"
#include "spark_state.h"
#include "PCM5101.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "driver/i2s_tdm.h"
#include "soc/soc_caps.h"
#include "esp_err.h"
#include "esp_log.h"

#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "esp_afe_sr_models.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"

#include "freertos/timers.h"

#define I2S_CHANNEL_NUM 1
#define USE_MULTINET_AS_WAKEWORD 1

// Follow-up listening configuration
#define FOLLOWUP_TIMEOUT_MS      15000   // 15-second follow-up window
#define PROCESSING_TIMEOUT_MS    30000   // 30-second max wait for AI response
#define SPEECH_ENERGY_THRESHOLD  250     // Energy threshold for speech detection
#define SPEECH_ONSET_SAMPLES     (16000 * 0.15f)  // 150ms of speech to confirm onset
#define SETTLING_DELAY_MS        300     // Post-playback settling time before listening
#define MIN_RECORDING_SECONDS    0.4f    // Minimum recording before silence check
#define SILENCE_DURATION_SECONDS 0.4f    // Consecutive silence to stop recording

static const char *TAG = "App/Speech";

static i2s_chan_handle_t                rx_handle = NULL;        // I2S rx channel handler
static AppSpeech MIC_Speech;
bool play_Music_Flag = 0;
uint8_t LCD_Backlight_original = 0;

// ============================================================
// CONVERSATION STATE MACHINE
// ============================================================
static volatile conv_state_t s_conv_state = CONV_STATE_IDLE;
static TimerHandle_t s_followup_timer = NULL;
static uint32_t s_processing_start_tick = 0;
static uint32_t s_listening_start_tick = 0;

// ============================================================
// VOICE RECORDING STATE
// ============================================================
static bool s_recording_active = false;
static int16_t *s_record_buf = NULL;
static uint32_t s_record_index = 0;
static const uint32_t s_record_max_samples = 16000 * 5; // 5 seconds of 16kHz audio

// Follow-up speech onset detection
static uint32_t s_consecutive_speech_samples = 0;
static bool s_settling_active = false;
static uint32_t s_settling_start_tick = 0;

typedef struct {
    char riff[4];           // "RIFF"
    uint32_t overall_size;   // file size - 8
    char wave[4];           // "WAVE"
    char fmt_chunk_marker[4]; // "fmt "
    uint32_t length_of_fmt;  // 16
    uint16_t format_type;    // 1 for PCM
    uint16_t channels;       // 1 for mono, 2 for stereo
    uint32_t sample_rate;    // 16000
    uint32_t byterate;       // sample_rate * channels * (bits_per_sample/8)
    uint16_t block_align;    // channels * (bits_per_sample/8)
    uint16_t bits_per_sample;// 16
    char data_chunk_header[4]; // "data"
    uint32_t data_size;      // number of bytes of PCM data
} __attribute__((packed)) wav_header_t;

// ============================================================
// PUBLIC API — STATE MACHINE
// ============================================================

conv_state_t MIC_GetConvState(void)
{
    return s_conv_state;
}

void MIC_SetConvState(conv_state_t new_state)
{
    ESP_LOGI(TAG, "State transition: %d -> %d", (int)s_conv_state, (int)new_state);
    s_conv_state = new_state;

    // Synchronize to centralized state manager
    switch (new_state) {
        case CONV_STATE_IDLE:
            Spark_State_TransitionTo(SPARK_STATE_IDLE);
            break;
        case CONV_STATE_LISTENING:
        case CONV_STATE_FOLLOWUP_LISTENING:
            Spark_State_TransitionTo(SPARK_STATE_LISTENING);
            break;
        case CONV_STATE_PROCESSING:
            Spark_State_TransitionTo(SPARK_STATE_THINKING);
            break;
        case CONV_STATE_SPEAKING:
            Spark_State_TransitionTo(SPARK_STATE_SPEAKING);
            break;
        default:
            break;
    }
}

// ============================================================
// FOLLOW-UP TIMER
// ============================================================

static void followup_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "Follow-up timeout (%d ms). Returning to IDLE.", FOLLOWUP_TIMEOUT_MS);
    MIC_SetConvState(CONV_STATE_IDLE);
    s_recording_active = false;
    s_consecutive_speech_samples = 0;
    
    // Re-enable wake word detection
    if (MIC_Speech.afe_handle && MIC_Speech.afe_data) {
        MIC_Speech.afe_handle->enable_wakenet(MIC_Speech.afe_data);
    }
    MIC_Speech.detected = false;
    LCD_Backlight = LCD_Backlight_original;
    Cloud_SetListeningState(false);
    Spark_Emotion_Set("normal");
}

static void start_followup_timer(void)
{
    if (s_followup_timer) {
        // xTimerReset will start the timer if not running, or restart it if running.
        // This guarantees only one timer is ever active.
        xTimerReset(s_followup_timer, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "Follow-up timer started/reset (%d ms)", FOLLOWUP_TIMEOUT_MS);
    }
}

static void cancel_followup_timer(void)
{
    if (s_followup_timer) {
        xTimerStop(s_followup_timer, pdMS_TO_TICKS(100));
        ESP_LOGI(TAG, "Follow-up timer cancelled");
    }
}

// ============================================================
// HELPER: Transition to IDLE (centralized cleanup)
// ============================================================

static void transition_to_idle(void)
{
    ESP_LOGI(TAG, "Transitioning to IDLE state");
    cancel_followup_timer();
    MIC_SetConvState(CONV_STATE_IDLE);
    s_recording_active = false;
    s_record_index = 0;
    s_consecutive_speech_samples = 0;
    s_settling_active = false;
    s_listening_start_tick = 0;

    // Re-enable wake word detection
    if (MIC_Speech.afe_handle && MIC_Speech.afe_data) {
        MIC_Speech.afe_handle->enable_wakenet(MIC_Speech.afe_data);
    }
    MIC_Speech.detected = false;
    LCD_Backlight = LCD_Backlight_original;
    Cloud_SetListeningState(false);
    Spark_Emotion_Set("normal");
}

// ============================================================
// HELPER: Start recording
// ============================================================

static void start_recording(void)
{
    s_record_index = 0;
    s_recording_active = true;
    s_consecutive_speech_samples = 0;
    ESP_LOGI("LATENCY_AUDIT", "[LATENCY] Recording Start: %lld ms", esp_timer_get_time() / 1000);
    ESP_LOGI(TAG, "Voice recording started (max %d seconds)...", (int)(s_record_max_samples / 16000));
}

// ============================================================
// WAV FILE WRITING
// ============================================================

static __attribute__((unused)) void write_wav_file(const char *filepath, int16_t *pcm_data, uint32_t num_samples)
{
    FILE *f = fopen(filepath, "wb");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s for writing", filepath);
        return;
    }
    
    wav_header_t header;
    memcpy(header.riff, "RIFF", 4);
    header.overall_size = num_samples * sizeof(int16_t) + 36;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt_chunk_marker, "fmt ", 4);
    header.length_of_fmt = 16;
    header.format_type = 1; // PCM
    header.channels = 1; // Mono
    header.sample_rate = 16000;
    header.byterate = 16000 * 1 * 2;
    header.block_align = 1 * 2;
    header.bits_per_sample = 16;
    memcpy(header.data_chunk_header, "data", 4);
    header.data_size = num_samples * sizeof(int16_t);
    
    fwrite(&header, 1, sizeof(header), f);
    fwrite(pcm_data, sizeof(int16_t), num_samples, f);
    fclose(f);
    ESP_LOGI(TAG, "Wrote WAV file to %s. Samples: %d", filepath, (int)num_samples);
}

// ============================================================
// VOICE UPLOAD TASK
// ============================================================

static void voice_upload_task(void *pvParameters)
{
    uint32_t num_samples = (uint32_t)pvParameters;
    ESP_LOGI(TAG, "Starting direct voice processing...");
    // Cloud_UploadVoiceDirect sends audio directly to the server,
    // receives MP3 response, and starts playback — all in one HTTP call.
    // Falls back to Supabase path if no server URL is configured.
    esp_err_t err = Cloud_UploadVoiceDirect(s_record_buf, num_samples);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Voice processing completed successfully.");
    } else {
        ESP_LOGE(TAG, "Voice processing failed. Returning to idle.");
        // On upload failure, go back to idle
        transition_to_idle();
    }
    vTaskDelete(NULL);
}

// ============================================================
// HELPER: Finish recording and start upload
// ============================================================

static void finish_recording_and_upload(uint32_t num_samples)
{
    s_recording_active = false;
    ESP_LOGI("LATENCY_AUDIT", "[LATENCY] Recording End: %lld ms", esp_timer_get_time() / 1000);
    MIC_SetConvState(CONV_STATE_PROCESSING);
    s_processing_start_tick = xTaskGetTickCount();
    
    ESP_LOGI(TAG, "Recording complete (%d samples). Uploading from RAM...", (int)num_samples);
    Spark_Emotion_Set("interest");
    
    // Create transient upload task
    xTaskCreatePinnedToCore(voice_upload_task, "voice_upload", 8192, (void *)num_samples, 5, NULL, 1);
}

// ============================================================
// I2S INITIALIZATION
// ============================================================

static esp_err_t i2s_init(i2s_port_t i2s_num, uint32_t sample_rate, int channel_format, int bits_per_chan)
{
    esp_err_t ret_val = ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(i2s_num, I2S_ROLE_MASTER);

    ret_val |= i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    i2s_std_config_t std_cfg = I2S_CONFIG_DEFAULT(16000, I2S_SLOT_MODE_MONO, I2S_DATA_BIT_WIDTH_32BIT);
    // std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;
    // std_cfg.clk_cfg.mclk_multiple = EXAMPLE_MCLK_MULTIPLE;   //The default is I2S_MCLK_MULTIPLE_256. If not using 24-bit data width, 256 should be enough
    ret_val |= i2s_channel_init_std_mode(rx_handle, &std_cfg);
    ret_val |= i2s_channel_enable(rx_handle);

    return ret_val;
}

// ============================================================
// FEED HANDLER — Mic data acquisition + recording + speech onset
// ============================================================

static void feed_handler(AppSpeech *self)
{
    esp_afe_sr_data_t *afe_data = self->afe_data;
    int audio_chunksize = self->afe_handle->get_feed_chunksize(afe_data);
    size_t samp_len = audio_chunksize;
    size_t samp_len_bytes = samp_len * I2S_CHANNEL_NUM * sizeof(int32_t);
    int32_t *i2s_buff = (int32_t *)malloc(samp_len_bytes);
    assert(i2s_buff);
    // Properly typed 16-bit buffer for AFE feed — avoids int32->int16 cast corruption
    int16_t *feed_buf = (int16_t *)malloc(samp_len * sizeof(int16_t));
    assert(feed_buf);
    size_t bytes_read;

    // VAD state for recording
    static uint32_t consecutive_silence_samples = 0;

    while (true)
    {
        i2s_channel_read(rx_handle, i2s_buff, samp_len_bytes, &bytes_read, portMAX_DELAY);

        // Convert 32-bit I2S samples to proper 16-bit samples
        for (int i = 0; i < samp_len; ++i)
        {
            feed_buf[i] = (int16_t)(i2s_buff[i] >> 14);
        }

        // ============================================================
        // AUDIO CONFLICT PREVENTION:
        // During SPEAKING state, discard all mic data.
        // This prevents Deskimon from hearing its own voice.
        // ============================================================
        if (s_conv_state == CONV_STATE_SPEAKING) {
            // Don't feed AFE, don't record. Complete mic mute.
            continue;
        }

        // ============================================================
        // FOLLOW-UP LISTENING: Speech onset detection
        // Monitor energy levels to detect when user starts speaking.
        // Only active in FOLLOWUP_LISTENING state when not yet recording.
        // ============================================================
        if (s_conv_state == CONV_STATE_FOLLOWUP_LISTENING && !s_recording_active) {
            // Handle post-playback settling delay
            if (s_settling_active) {
                uint32_t elapsed = (xTaskGetTickCount() - s_settling_start_tick) * portTICK_PERIOD_MS;
                if (elapsed < SETTLING_DELAY_MS) {
                    // Still settling — feed AFE but don't analyze energy
                    self->afe_handle->feed(afe_data, feed_buf);
                    continue;
                }
                s_settling_active = false;
                ESP_LOGI(TAG, "Post-playback settling complete. Listening for speech...");
            }

            // Calculate chunk energy
            long long chunk_sum = 0;
            for (int i = 0; i < samp_len; ++i) {
                chunk_sum += abs(feed_buf[i]);
            }
            float chunk_avg = (float)chunk_sum / samp_len;

            if (chunk_avg > SPEECH_ENERGY_THRESHOLD) {
                s_consecutive_speech_samples += samp_len;

                if (s_consecutive_speech_samples >= (uint32_t)SPEECH_ONSET_SAMPLES) {
                    // Speech confirmed! Transition to recording.
                    ESP_LOGI(TAG, "Speech detected during follow-up! Starting recording.");
                    cancel_followup_timer();
                    MIC_SetConvState(CONV_STATE_LISTENING);
                    start_recording();
                    Cloud_SetListeningState(true);
                    Spark_Emotion_Set("listening");
                }
            } else {
                s_consecutive_speech_samples = 0;
            }
        }

        // ============================================================
        // ACTIVE RECORDING: Buffer mic samples + VAD silence detection
        // ============================================================
        if (s_recording_active && s_record_buf) {
            if (s_record_index == 0) {
                consecutive_silence_samples = 0;
            }

            long long chunk_sum = 0;
            for (int i = 0; i < samp_len; ++i) {
                chunk_sum += abs(feed_buf[i]);
                if (s_record_index < s_record_max_samples) {
                    s_record_buf[s_record_index++] = feed_buf[i];
                }
            }

            float chunk_avg = (float)chunk_sum / samp_len;

            // Wait until minimum recording time has passed before checking for silence
            if (s_record_index > (uint32_t)(16000 * MIN_RECORDING_SECONDS)) {
                if (chunk_avg < SPEECH_ENERGY_THRESHOLD) {
                    consecutive_silence_samples += samp_len;
                } else {
                    consecutive_silence_samples = 0;
                }

                if (consecutive_silence_samples >= (uint32_t)(16000 * SILENCE_DURATION_SECONDS)) {
                    uint32_t final_samples = s_record_index;
                    ESP_LOGI(TAG, "Silence detected. Stopping recording at %d samples.", (int)final_samples);
                    finish_recording_and_upload(final_samples);
                }
            }

            // Buffer full — force stop
            if (s_recording_active && s_record_index >= s_record_max_samples) {
                ESP_LOGI(TAG, "Recording buffer full. Saving WAV file...");
                finish_recording_and_upload(s_record_max_samples);
            }
        }

        // Feed AFE for wake word / multinet detection (except during SPEAKING)
        self->afe_handle->feed(afe_data, feed_buf);
    }
    self->afe_handle->destroy(afe_data);
    if (i2s_buff) {
        free(i2s_buff);
        i2s_buff = NULL;
    }
    if (feed_buf) {
        free(feed_buf);
        feed_buf = NULL;
    }
    vTaskDelete(NULL);
}

// ============================================================
// DETECT HANDLER — State machine main loop
// ============================================================

static void detect_hander(AppSpeech *self)
{
    esp_afe_sr_data_t *afe_data = self->afe_data;
    int afe_chunksize = self->afe_handle->get_fetch_chunksize(afe_data);
#if defined(CONFIG_SR_MN_CN_MULTINET5_RECOGNITION_QUANT8) || defined(CONFIG_SR_MN_CN_MULTINET6_QUANT) || defined(CONFIG_SR_MN_CN_MULTINET6_AC_QUANT)
    char *mn_name = esp_srmodel_filter(self->models, ESP_MN_PREFIX, ESP_MN_CHINESE);
#else
    char *mn_name = esp_srmodel_filter(self->models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
#endif // CONFIG_IDF_TARGET_ESP32S3
    ESP_LOGI(TAG, "multinet:%s\n", mn_name);
    esp_mn_iface_t *multinet = esp_mn_handle_from_name(mn_name);
    model_iface_data_t *model_data = multinet->create(mn_name, 60000); // Wait up to 1 minute after waking up
    esp_mn_commands_update_from_sdkconfig(multinet, model_data); // Add speech commands from sdkconfig
    int mu_chunksize = multinet->get_samp_chunksize(model_data);
    assert(mu_chunksize == afe_chunksize);

    //print active speech commands
    multinet->print_active_speech_commands(model_data);
    ESP_LOGI(TAG, "Ready");

    self->detected = false;

    while (true)
    {
        // ==========================================================
        // STATE MACHINE
        // ==========================================================
        switch (s_conv_state) {

        // ----------------------------------------------------------
        // IDLE: Continuous MultiNet command spotter for "Spark" keywords or original WakeNet
        // ----------------------------------------------------------
        case CONV_STATE_IDLE: {
            afe_fetch_result_t* res = self->afe_handle->fetch(afe_data); 
            if (!res || res->ret_value == ESP_FAIL) {
                ESP_LOGE(TAG, "fetch error!\n");
                vTaskDelay(pdMS_TO_TICKS(50));
                break;
            }

#if USE_MULTINET_AS_WAKEWORD
            // Feed the audio chunk to MultiNet to scan for the custom keyword
            int64_t start_time = esp_timer_get_time();
            esp_mn_state_t mn_state = multinet->detect(model_data, res->data);
            int64_t end_time = esp_timer_get_time();
            int64_t duration_us = end_time - start_time;
            
            // Calculate task CPU core usage percentage (each chunk is afe_chunksize samples at 16kHz)
            float chunk_duration_us = (afe_chunksize * 1000000.0f) / 16000.0f;
            float task_cpu_usage = (duration_us / chunk_duration_us) * 100.0f;

            if (mn_state == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *mn_res = multinet->get_results(model_data);
                if (mn_res && mn_res->num > 0) {
                    int cmd_id = mn_res->command_id[0];
                    ESP_LOGI(TAG, "=== MULTINET DETECTED command_id: %d, phrase: %s, prob: %f ===", 
                             cmd_id, mn_res->string, mn_res->prob[0]);
                    
                    if (cmd_id == 5 || cmd_id == 6 || cmd_id == 7) {
                        ESP_LOGI(TAG, "=== SPARK WAKEWORD DETECTED! ===");
                        ESP_LOGI(TAG, "Detection Latency: %lld us (%f%% task CPU usage)", duration_us, task_cpu_usage);
                        ESP_LOGI(TAG, "RAM Stats - Free Heap: %lu bytes (Internal: %lu, SPIRAM: %lu)",
                                 (unsigned long)esp_get_free_heap_size(),
                                 (unsigned long)esp_get_free_internal_heap_size(),
                                 (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
                        
                        multinet->clean(model_data);
                        LCD_Backlight_original = LCD_Backlight;
                        
                        // Disable wake word and multinet detection during conversation
                        self->afe_handle->disable_wakenet(afe_data);
                        self->detected = true;
                        
                        // Visual + cloud feedback
                        LCD_Backlight = 35;
                        Cloud_SetListeningState(true);
                        Spark_Emotion_Set("ooh");
                        s_listening_start_tick = xTaskGetTickCount();
                        
                        // Transition directly to LISTENING and start recording
                        MIC_SetConvState(CONV_STATE_LISTENING);
                        start_recording();
                        ESP_LOGI(TAG, "State: IDLE -> LISTENING (via Spark trigger)");
                    }
                }
            }
#else
            if (res->wakeup_state == WAKENET_DETECTED) {
                ESP_LOGI(TAG, "=== WAKEWORD DETECTED ===\n");
                multinet->clean(model_data);
                LCD_Backlight_original = LCD_Backlight;
                MIC_SetConvState(CONV_STATE_WAKE_DETECTED);
                ESP_LOGI(TAG, "State: IDLE -> WAKE_DETECTED");
            }
#endif
            break;
        }

        // ----------------------------------------------------------
        // WAKE_DETECTED: Wait for channel verification
        // ----------------------------------------------------------
        case CONV_STATE_WAKE_DETECTED: {
            afe_fetch_result_t* res = self->afe_handle->fetch(afe_data); 
            if (!res || res->ret_value == ESP_FAIL) {
                ESP_LOGE(TAG, "fetch error during wake verification!\n");
                transition_to_idle();
                break;
            }

            if (res->wakeup_state == WAKENET_CHANNEL_VERIFIED) {
                ESP_LOGI(TAG, "AFE_FETCH_CHANNEL_VERIFIED, channel: %d\n", res->trigger_channel_id);
                
                // Disable wake word detection for conversation duration
                self->afe_handle->disable_wakenet(afe_data);
                self->detected = true;
                
                // Visual + cloud feedback
                LCD_Backlight = 35;
                Cloud_SetListeningState(true);
                Spark_Emotion_Set("listening");
                
                // Start recording
                MIC_SetConvState(CONV_STATE_LISTENING);
                start_recording();
                ESP_LOGI(TAG, "State: WAKE_DETECTED -> LISTENING");
            }
            break;
        }

        // ----------------------------------------------------------
        // LISTENING: Recording in progress (handled by feed_handler)
        // ----------------------------------------------------------
        case CONV_STATE_LISTENING: {
            if (s_listening_start_tick > 0 && 
                ((xTaskGetTickCount() - s_listening_start_tick) * portTICK_PERIOD_MS > 800)) {
                Spark_Emotion_Set("happy");
                s_listening_start_tick = 0;
            }
            // Must continually fetch from AFE so the feed_handler doesn't deadlock!
            afe_fetch_result_t* res = self->afe_handle->fetch(afe_data); 
            if (!res || res->ret_value == ESP_FAIL) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            break;
        }

        // ----------------------------------------------------------
        // PROCESSING: Waiting for AI response from server
        // ----------------------------------------------------------
        case CONV_STATE_PROCESSING: {
            // Safety timeout: if no response after 30 seconds, give up
            uint32_t elapsed = (xTaskGetTickCount() - s_processing_start_tick) * portTICK_PERIOD_MS;
            if (elapsed > PROCESSING_TIMEOUT_MS) {
                ESP_LOGW(TAG, "Processing timeout (%d ms). Returning to IDLE.", PROCESSING_TIMEOUT_MS);
                transition_to_idle();
                break;
            }
            // Must continually fetch from AFE so the feed_handler doesn't deadlock!
            afe_fetch_result_t* res = self->afe_handle->fetch(afe_data); 
            if (!res || res->ret_value == ESP_FAIL) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            break;
        }

        // ----------------------------------------------------------
        // SPEAKING: Audio response is playing (mic muted in feed_handler)
        // ----------------------------------------------------------
        case CONV_STATE_SPEAKING: {
            static uint32_t speaking_entry_tick = 0;
            static bool playback_started = false;

            if (speaking_entry_tick == 0) {
                speaking_entry_tick = xTaskGetTickCount();
                playback_started = false;
            }

            audio_player_state_t player_state = audio_player_get_state();
            if (player_state == AUDIO_PLAYER_STATE_PLAYING) {
                playback_started = true;
            }

            uint32_t elapsed_ms = (xTaskGetTickCount() - speaking_entry_tick) * portTICK_PERIOD_MS;
            bool finished = false;

            if (playback_started) {
                if (player_state != AUDIO_PLAYER_STATE_PLAYING) {
                    ESP_LOGI(TAG, "Audio response finished. Entering follow-up listening.");
                    finished = true;
                }
            } else {
                // If it hasn't started playing after 2 seconds, assume it failed or finished instantly
                if (elapsed_ms > 2000) {
                    ESP_LOGW(TAG, "Audio response failed to start within 2s. Transitioning to follow-up.");
                    finished = true;
                }
            }

            if (finished) {
                speaking_entry_tick = 0;
                playback_started = false;

                // Transition to follow-up listening
                MIC_SetConvState(CONV_STATE_FOLLOWUP_LISTENING);
                s_consecutive_speech_samples = 0;
                s_recording_active = false;

                // Start post-playback settling delay
                s_settling_active = true;
                s_settling_start_tick = xTaskGetTickCount();

                // Start the 15-second inactivity timer
                start_followup_timer();

                // Visual feedback
                Cloud_SetListeningState(true);
                Spark_Emotion_Set("listening");

                ESP_LOGI(TAG, "State: SPEAKING -> FOLLOWUP_LISTENING (settling for %d ms)", SETTLING_DELAY_MS);
            } else {
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;
        }

        // ----------------------------------------------------------
        // FOLLOWUP_LISTENING: Waiting for user to speak again
        // ----------------------------------------------------------
        case CONV_STATE_FOLLOWUP_LISTENING: {
            // Speech onset detection is handled in feed_handler.
            // Timer callback handles timeout -> IDLE transition.
            // feed_handler handles speech detected -> LISTENING transition.
            afe_fetch_result_t* res = self->afe_handle->fetch(afe_data); 
            if (!res || res->ret_value == ESP_FAIL) {
                vTaskDelay(pdMS_TO_TICKS(50));
            }
            break;
        }

        default:
            ESP_LOGE(TAG, "Unknown conversation state: %d", (int)s_conv_state);
            transition_to_idle();
            break;
        }

        if (self->detected && s_conv_state == CONV_STATE_IDLE) {
            // We shouldn't be detected AND idle normally,
            // but handle gracefully just in case
            self->afe_handle->enable_wakenet(afe_data);
            multinet->clean(model_data);
            self->detected = false;
        }
    }
    if (model_data) {
        multinet->destroy(model_data);
        model_data = NULL;
    }
    self->afe_handle->destroy(afe_data);
    vTaskDelete(NULL);
}

// ============================================================
// INITIALIZATION
// ============================================================

void MIC_Speech_init() 
{
    // Allocate recording buffer in external SPIRAM (160KB)
    s_record_buf = heap_caps_malloc(s_record_max_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!s_record_buf) {
        ESP_LOGE(TAG, "Failed to allocate record buffer in SPIRAM!");
    } else {
        ESP_LOGI(TAG, "Allocated 160KB record buffer in SPIRAM successfully.");
    }
    ESP_LOGI(TAG, "Speech Init RAM Stats - Free Heap: %lu bytes (Internal: %lu, SPIRAM: %lu)",
             (unsigned long)esp_get_free_heap_size(),
             (unsigned long)esp_get_free_internal_heap_size(),
             (unsigned long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
   
    // Create follow-up inactivity timer (one-shot, initially stopped)
    s_followup_timer = xTimerCreate(
        "followup_timer",
        pdMS_TO_TICKS(FOLLOWUP_TIMEOUT_MS),
        pdFALSE,       // One-shot timer (not auto-reload)
        NULL,
        followup_timer_callback
    );
    if (!s_followup_timer) {
        ESP_LOGE(TAG, "Failed to create follow-up timer!");
    } else {
        ESP_LOGI(TAG, "Follow-up timer created (%d ms timeout).", FOLLOWUP_TIMEOUT_MS);
    }

    MIC_Speech.afe_handle = &ESP_AFE_SR_HANDLE;

    MIC_Speech.detected = false;
    MIC_Speech.command = COMMAND_TIMEOUT;
    MIC_Speech.models = esp_srmodel_init("model");
    i2s_init(I2S_NUM_1, 16000, 2, 32);
    // sd_card_mount("/sdcard");
    afe_config_t afe_config = {
        .aec_init = true,
        .se_init = true,
        .vad_init = true,
        .wakenet_init = true,
        .voice_communication_init = false,
        .voice_communication_agc_init = false,
        .voice_communication_agc_gain = 15,
        .vad_mode = VAD_MODE_3,
        .wakenet_model_name = NULL,
        .wakenet_model_name_2 = NULL,
        .wakenet_mode = DET_MODE_90,
        .afe_mode = SR_MODE_LOW_COST,
        .afe_perferred_core = 1,
        .afe_perferred_priority = 5,
        .afe_ringbuf_size = 50,
        .memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM,
        .afe_linear_gain = 1.0,
        .agc_mode = AFE_MN_PEAK_AGC_MODE_2,
        .pcm_config = {
            .total_ch_num = 3,
            .mic_num = 2,
            .ref_num = 1,
            .sample_rate = 16000,
        },
        .debug_init = false,
        .debug_hook = {{AFE_DEBUG_HOOK_MASE_TASK_IN, NULL}, {AFE_DEBUG_HOOK_FETCH_TASK_IN, NULL}},
    };
    afe_config.aec_init = false;
    afe_config.se_init = false;
    afe_config.vad_init = false;
    afe_config.afe_ringbuf_size = 10;
    afe_config.pcm_config.total_ch_num = 1;
    afe_config.pcm_config.mic_num = 1;
    afe_config.pcm_config.ref_num = 0;
    afe_config.pcm_config.sample_rate = 16000;
#if USE_MULTINET_AS_WAKEWORD
    // Disable WakeNet in the AFE config
    afe_config.wakenet_init = false;
    afe_config.wakenet_model_name = NULL;
    ESP_LOGI(TAG, "Continuous MultiNet Wake Word Spotter enabled (WakeNet bypassed)");
#else
    afe_config.wakenet_model_name = esp_srmodel_filter(MIC_Speech.models, ESP_WN_PREFIX, NULL);
#endif
    MIC_Speech.afe_data = MIC_Speech.afe_handle->create_from_config(&afe_config);
    xTaskCreatePinnedToCore((TaskFunction_t)feed_handler, "App/SR/Feed", 4 * 1024, &MIC_Speech, 5, NULL, 1);
    xTaskCreatePinnedToCore((TaskFunction_t)detect_hander, "App/SR/Detect", 16 * 1024, &MIC_Speech, 5, NULL, 1);

    ESP_LOGI(TAG, "MIC Speech initialized with continuous conversation support.");
}

void MIC_StartRecordingManual(void)
{
    if (s_conv_state == CONV_STATE_IDLE || s_conv_state == CONV_STATE_FOLLOWUP_LISTENING) {
        ESP_LOGI(TAG, "Manual trigger: transitioning to LISTENING");
        cancel_followup_timer();
        if (MIC_Speech.afe_handle && MIC_Speech.afe_data) {
            MIC_Speech.afe_handle->disable_wakenet(MIC_Speech.afe_data);
        }
        MIC_Speech.detected = true;
        LCD_Backlight = 35;
        Cloud_SetListeningState(true);
        Spark_Emotion_Set("listening");
        
        MIC_SetConvState(CONV_STATE_LISTENING);
        start_recording();
    } else {
        ESP_LOGI(TAG, "Manual trigger ignored: state is %d", (int)s_conv_state);
    }
}
