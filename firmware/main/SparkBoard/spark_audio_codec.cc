#include "spark_audio_codec.h"
#include <esp_log.h>
#include <cstring>
#include <algorithm>
#include <cmath>

extern "C" {
#include "TCA9554PWR.h"
#include "PCM5101.h" // For Volume global and Volume_adjustment
}

#define TAG "SparkAudioCodec"

// Speaker (PCM5101) Pins
#define BSP_I2S_SCLK          (GPIO_NUM_48) 
#define BSP_I2S_LCLK          (GPIO_NUM_38) 
#define BSP_I2S_DOUT          (GPIO_NUM_47) 

// Microphone Pins
#define MIC_I2S_BCLK          (GPIO_NUM_15)
#define MIC_I2S_WS            (GPIO_NUM_2)
#define MIC_I2S_DIN           (GPIO_NUM_39)

SparkAudioCodec::SparkAudioCodec(int input_sample_rate, int output_sample_rate) {
    duplex_ = true;
    input_reference_ = false;
    input_channels_ = 1;
    output_channels_ = 1; // Mono playback
    input_sample_rate_ = input_sample_rate;
    output_sample_rate_ = output_sample_rate;

    // Enforce matching rates
    assert(input_sample_rate_ == 16000);
    
    // Enable EXIO expander (gives power to microphone)
    Set_EXIOS(0xFF);

    InitializeI2sTx();
    InitializeI2sRx();

    ESP_LOGI(TAG, "SparkAudioCodec initialized (TX: I2S_NUM_0, RX: I2S_NUM_1)");
}

SparkAudioCodec::~SparkAudioCodec() {
    DeinitializeI2sTx();
    DeinitializeI2sRx();
    if (rx_temp_buf_) {
        free(rx_temp_buf_);
    }
}

void SparkAudioCodec::InitializeI2sTx() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, nullptr));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)output_sample_rate_),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = BSP_I2S_SCLK,
            .ws = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din = GPIO_NUM_NC,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle_));
    output_enabled_ = true;
    ESP_LOGI(TAG, "I2S TX initialized successfully.");
}

void SparkAudioCodec::InitializeI2sRx() {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, nullptr, &rx_handle_));

    // The microphone outputs 24-bit MSB-aligned data on a 32-bit slot
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG((uint32_t)input_sample_rate_),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = MIC_I2S_BCLK,
            .ws = MIC_I2S_WS,
            .dout = GPIO_NUM_NC,
            .din = MIC_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_BOTH;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    input_enabled_ = true;
    ESP_LOGI(TAG, "I2S RX initialized successfully.");
}

void SparkAudioCodec::DeinitializeI2sTx() {
    if (tx_handle_) {
        i2s_channel_disable(tx_handle_);
        i2s_del_channel(tx_handle_);
        tx_handle_ = nullptr;
    }
}

void SparkAudioCodec::DeinitializeI2sRx() {
    if (rx_handle_) {
        i2s_channel_disable(rx_handle_);
        i2s_del_channel(rx_handle_);
        rx_handle_ = nullptr;
    }
}

void SparkAudioCodec::SetOutputVolume(int volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    Volume_adjustment(volume);
    AudioCodec::SetOutputVolume(volume);
}

void SparkAudioCodec::EnableInput(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (enable == input_enabled_) return;
    
    if (rx_handle_) {
        if (enable) {
            i2s_channel_enable(rx_handle_);
        } else {
            i2s_channel_disable(rx_handle_);
        }
    }
    AudioCodec::EnableInput(enable);
}

void SparkAudioCodec::EnableOutput(bool enable) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (enable == output_enabled_) return;

    if (tx_handle_) {
        if (enable) {
            i2s_channel_enable(tx_handle_);
        } else {
            i2s_channel_disable(tx_handle_);
        }
    }
    AudioCodec::EnableOutput(enable);
}

int SparkAudioCodec::Read(int16_t* dest, int samples) {
    if (!input_enabled_ || !rx_handle_) {
        std::memset(dest, 0, samples * sizeof(int16_t));
        return samples;
    }

    // Allocate temporary RX buffer for stereo 32-bit I2S samples
    int stereo_samples = samples * 2;
    if (rx_temp_buf_samples_ < stereo_samples) {
        if (rx_temp_buf_) free(rx_temp_buf_);
        rx_temp_buf_ = (int32_t*)malloc(stereo_samples * sizeof(int32_t));
        rx_temp_buf_samples_ = stereo_samples;
    }

    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_handle_, rx_temp_buf_, stereo_samples * sizeof(int32_t), &bytes_read, portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S read error: %s", esp_err_to_name(err));
        std::memset(dest, 0, samples * sizeof(int16_t));
        return 0;
    }

    int read_samples = bytes_read / sizeof(int32_t);
    int read_frames = read_samples / 2;

    // Shift >> 16 to convert 32-bit I2S MEMS samples to 16-bit PCM (standard range)
    // Apply DC offset removal (high-pass filter) to the microphone channel.
    // We compute RMS for Left and Right channels to check which one is active.
    float sum_sq_left = 0.0f;
    float sum_sq_right = 0.0f;
    int count_left = 0;
    int count_right = 0;

    for (int i = 0; i < read_frames; ++i) {
        // Left Channel (Even index)
        int32_t raw_l = rx_temp_buf_[2 * i] >> 16;
        if (raw_l > 32767) raw_l = 32767;
        else if (raw_l < -32768) raw_l = -32768;
        sum_sq_left += (float)raw_l * raw_l;
        count_left++;

        // Right Channel (Odd index) - shift >> 13 to provide optimal 8x gain for 24-bit MEMS microphone
        int32_t raw_r = rx_temp_buf_[2 * i + 1] >> 13;
        if (raw_r > 32767) raw_r = 32767;
        else if (raw_r < -32768) raw_r = -32768;
        sum_sq_right += (float)raw_r * raw_r;
        count_right++;

        // Extract Right channel to dest with high-pass DC offset filter
        dc_offset_ = 0.98f * dc_offset_ + 0.02f * (float)raw_r;
        int32_t val = raw_r - (int32_t)dc_offset_;
        if (val > 32767) val = 32767;
        else if (val < -32768) val = -32768;
        dest[i] = (int16_t)val;
    }

    // Diagnostic logging once per second (approx 31 calls per sec at 16kHz/512 samples)
    static int log_counter = 0;
    log_counter++;
    if (log_counter >= 31) {
        log_counter = 0;
        float rms_l = sqrtf(sum_sq_left / (count_left > 0 ? count_left : 1));
        float rms_r = sqrtf(sum_sq_right / (count_right > 0 ? count_right : 1));
        ESP_LOGI("AUDIO_DIAG", "Stereo Channels - Left (Even) RMS: %.1f, Right (Odd) RMS: %.1f", rms_l, rms_r);
    }

    return read_frames;
}

int SparkAudioCodec::Write(const int16_t* data, int samples) {
    if (!output_enabled_ || !tx_handle_) {
        return 0;
    }

    // Software volume scaling
    // Scale factor: 0-100 Volume -> 0.0 - 4.0 scaling factor (consistent with original PCM5101.c)
    float volume_factor = (Volume / 100.0f) * 4.0f;
    int16_t* scaled_buf = (int16_t*)malloc(samples * sizeof(int16_t));
    if (!scaled_buf) return 0;

    for (int i = 0; i < samples; i++) {
        int32_t val = (int32_t)(data[i] * volume_factor);
        if (val > 32767) val = 32767;
        else if (val < -32768) val = -32768;
        scaled_buf[i] = (int16_t)val;
    }

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(tx_handle_, scaled_buf, samples * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    free(scaled_buf);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S write error: %s", esp_err_to_name(err));
        return 0;
    }

    return bytes_written / sizeof(int16_t);
}
