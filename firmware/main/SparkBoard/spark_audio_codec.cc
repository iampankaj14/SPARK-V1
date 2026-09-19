#include "spark_audio_codec.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <cstring>
#include <algorithm>
#include <cmath>
#include <climits>

extern "C" {
#include "TCA9554PWR.h"
#include "PCM5101.h" // For Volume global and Volume_adjustment
}

#define TAG "SparkAudioCodec"

static SparkAudioCodec* g_spark_audio_codec_instance = nullptr;

extern "C" void InjectVirtualMicAudioFromC(const int16_t* data, int samples) {
    if (g_spark_audio_codec_instance) {
        g_spark_audio_codec_instance->InjectVirtualMicAudio(data, samples);
    }
}

// Speaker (PCM5101 / SmartElex) Pins
#define BSP_I2S_SCLK          (GPIO_NUM_48) 
#define BSP_I2S_LCLK          (GPIO_NUM_38) 
#define BSP_I2S_DOUT          (GPIO_NUM_47) // Onboard PCM5101 DAC → SPK+/SPK- → SmartElex Speaker

// INMP441 Digital MEMS Microphone Pins (Waveshare Official Recommendation)
#define MIC_I2S_BCLK          (GPIO_NUM_13) // SCK -> Header Pin 1 (GPIO13)
#define MIC_I2S_WS            (GPIO_NUM_12) // WS  -> Header Pin 3 (GPIO12)
#define MIC_I2S_DIN           (GPIO_NUM_3)  // SD  -> Header Pin 8 (GPIO3) — confirmed by Waveshare support

SparkAudioCodec::SparkAudioCodec(int input_sample_rate, int output_sample_rate) {
    g_spark_audio_codec_instance = this;
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

    // Run raw I2S hardware diagnostic at boot
    RunI2sHardwareTest();

    ESP_LOGI(TAG, "SparkAudioCodec initialized (TX: I2S_NUM_0, RX: I2S_NUM_1)");
}

void SparkAudioCodec::RunI2sHardwareTest() {
    ESP_LOGW(TAG, "=== I2S HARDWARE DIAGNOSTIC TEST (STEREO AUTO-DETECT) ===");
    
    // Give the mic 500ms to stabilize after power-on
    vTaskDelay(pdMS_TO_TICKS(500));
    
    // We read in STEREO mode, so samples are interleaved: [L0, R0, L1, R1, ...]
    const int STEREO_FRAMES = 128;  // 128 frames = 256 int32_t samples (L+R interleaved)
    const int TOTAL_SAMPLES = STEREO_FRAMES * 2;
    int32_t test_buf[TOTAL_SAMPLES];
    size_t bytes_read = 0;
    
    // Warmup reads - discard initial DMA garbage
    for (int w = 0; w < 3; w++) {
        i2s_channel_read(rx_handle_, test_buf, sizeof(test_buf), &bytes_read, pdMS_TO_TICKS(200));
    }
    
    // Actual test read
    bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_handle_, test_buf, sizeof(test_buf), &bytes_read, pdMS_TO_TICKS(500));
    
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S read FAILED: %s", esp_err_to_name(err));
        return;
    }
    
    int samples_read = bytes_read / sizeof(int32_t);
    int frames_read = samples_read / 2;
    ESP_LOGI(TAG, "Read %d stereo samples (%d frames, %d bytes)", samples_read, frames_read, (int)bytes_read);
    
    // Dump first 16 stereo frames as hex
    ESP_LOGW(TAG, "--- RAW STEREO I2S data (L/R interleaved, first 16 frames) ---");
    for (int f = 0; f < frames_read && f < 16; f++) {
        ESP_LOGW(TAG, "  Frame[%2d] L=0x%08lX  R=0x%08lX",
            f, (unsigned long)test_buf[f*2], (unsigned long)test_buf[f*2+1]);
    }
    
    // Analyze LEFT channel (even indices: 0, 2, 4, ...)
    int64_t sum_l = 0, sum_r = 0;
    int32_t min_l = INT32_MAX, max_l = INT32_MIN;
    int32_t min_r = INT32_MAX, max_r = INT32_MIN;
    int zero_l = 0, zero_r = 0;
    double energy_l = 0, energy_r = 0;
    
    for (int f = 0; f < frames_read; f++) {
        int32_t lval = test_buf[f*2];
        int32_t rval = test_buf[f*2+1];
        
        sum_l += lval;
        sum_r += rval;
        if (lval < min_l) min_l = lval;
        if (lval > max_l) max_l = lval;
        if (rval < min_r) min_r = rval;
        if (rval > max_r) max_r = rval;
        if (lval == 0) zero_l++;
        if (rval == 0) zero_r++;
        
        // Energy after >>14 shift
        int16_t l16 = (int16_t)(lval >> 14);
        int16_t r16 = (int16_t)(rval >> 14);
        energy_l += (double)l16 * l16;
        energy_r += (double)r16 * r16;
    }
    
    double rms_l = sqrt(energy_l / (frames_read ? frames_read : 1));
    double rms_r = sqrt(energy_r / (frames_read ? frames_read : 1));
    
    ESP_LOGW(TAG, "--- CHANNEL ANALYSIS ---");
    ESP_LOGW(TAG, "  LEFT  ch: Min=0x%08lX Max=0x%08lX Zeros=%d/%d RMS(>>14)=%.1f",
        (unsigned long)min_l, (unsigned long)max_l, zero_l, frames_read, rms_l);
    ESP_LOGW(TAG, "  RIGHT ch: Min=0x%08lX Max=0x%08lX Zeros=%d/%d RMS(>>14)=%.1f",
        (unsigned long)min_r, (unsigned long)max_r, zero_r, frames_read, rms_r);
    
    // Auto-detect which channel has actual mic data
    bool left_has_data = (zero_l < frames_read * 0.9) && (rms_l > 1.0);
    bool right_has_data = (zero_r < frames_read * 0.9) && (rms_r > 1.0);
    
    if (left_has_data && !right_has_data) {
        active_mic_channel_ = 0;  // LEFT
        ESP_LOGI(TAG, "AUTO-DETECT: INMP441 on LEFT channel (L/R=GND). active_mic_channel_=0");
    } else if (right_has_data && !left_has_data) {
        active_mic_channel_ = 1;  // RIGHT
        ESP_LOGI(TAG, "AUTO-DETECT: INMP441 on RIGHT channel (L/R=VDD?). active_mic_channel_=1");
    } else if (left_has_data && right_has_data) {
        // Both have data, pick higher RMS
        active_mic_channel_ = (rms_l >= rms_r) ? 0 : 1;
        ESP_LOGW(TAG, "AUTO-DETECT: Both channels have data! Using %s (higher RMS). active_mic_channel_=%d",
            active_mic_channel_ == 0 ? "LEFT" : "RIGHT", active_mic_channel_);
    } else {
        active_mic_channel_ = 0;  // Default to LEFT
        ESP_LOGE(TAG, "AUTO-DETECT: NO DATA on either channel! Mic may not be connected.");
        ESP_LOGE(TAG, "  Check: 1) INMP441 VDD connected to 3.3V  2) GND connected  3) L/R pin connected to GND");
        ESP_LOGE(TAG, "  Check: 4) SCK -> GPIO13 (Pin 1)  5) WS -> GPIO12 (Pin 3)  6) SD -> GPIO1 (Pin 10)");
    }
    
    // Show >>14 converted values from the active channel
    ESP_LOGW(TAG, "--- Active channel (%s) after >>14 (first 16 samples) ---",
        active_mic_channel_ == 0 ? "LEFT" : "RIGHT");
    for (int f = 0; f < frames_read && f < 16; f++) {
        int16_t val = (int16_t)(test_buf[f*2 + active_mic_channel_] >> 14);
        ESP_LOGW(TAG, "  [%2d] %6d (raw: 0x%08lX)",
            f, val, (unsigned long)test_buf[f*2 + active_mic_channel_]);
    }
    
    ESP_LOGW(TAG, "=== I2S HARDWARE DIAGNOSTIC TEST END ===");
}

SparkAudioCodec::~SparkAudioCodec() {
    DeinitializeI2sTx();
    DeinitializeI2sRx();
    if (rx_temp_buf_) {
        heap_caps_free(rx_temp_buf_);
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

    // Configure I2S RX for INMP441 in MONO LEFT mode
    // INMP441 with L/R=GND outputs data on LEFT channel (WS LOW phase)
    // Using Philips standard I2S protocol, 32-bit slot width for 24-bit mic data
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
    // INMP441 with L/R=GND — try RIGHT slot (ESP-IDF slot labeling can be inverted)
    // Using MSB mode (no 1-bit WS delay) which some INMP441 modules prefer
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_RIGHT;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle_, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle_));
    input_enabled_ = true;
    ESP_LOGI(TAG, "I2S RX initialized (MONO LEFT, 32-bit, 16kHz) for INMP441. BCLK:GPIO%d WS:GPIO%d DIN:GPIO%d",
        MIC_I2S_BCLK, MIC_I2S_WS, MIC_I2S_DIN);
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

#include <mbedtls/base64.h>
#include <esp_heap_caps.h>

void SparkAudioCodec::StartRecordingDiagnostic() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!diag_rec_buf_) {
        // Allocate 5 seconds of 16kHz audio in PSRAM (80,000 samples = 160 KB)
        diag_rec_buf_ = (int16_t*)heap_caps_malloc(80000 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!diag_rec_buf_) {
            diag_rec_buf_ = (int16_t*)malloc(32000 * sizeof(int16_t));
        }
    }
    diag_rec_index_ = 0;
    diag_recording_active_ = true;
    ESP_LOGI(TAG, "Diagnostic audio recording started (buffer ready)");
}

void SparkAudioCodec::StopAndDumpRecordingDiagnostic() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!diag_recording_active_ && diag_rec_index_ == 0) return;
    diag_recording_active_ = false;
    if (!diag_rec_buf_ || diag_rec_index_ <= 0) return;

    ESP_LOGI(TAG, "Dumping %d mic samples to serial diagnostic...", diag_rec_index_);
    printf("\n===MIC_RECORDING_START:%d===\n", diag_rec_index_);
    
    // Dump in 256-sample chunks (512 bytes raw -> ~684 base64 chars per line)
    const int CHUNK_SAMPLES = 256;
    unsigned char b64_out[1024];
    for (int offset = 0; offset < diag_rec_index_; offset += CHUNK_SAMPLES) {
        int chunk = (diag_rec_index_ - offset < CHUNK_SAMPLES) ? (diag_rec_index_ - offset) : CHUNK_SAMPLES;
        size_t olen = 0;
        mbedtls_base64_encode(b64_out, sizeof(b64_out), &olen, (const unsigned char*)&diag_rec_buf_[offset], chunk * sizeof(int16_t));
        b64_out[olen] = '\0';
        printf("%s\n", (char*)b64_out);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    printf("===MIC_RECORDING_END===\n\n");
    fflush(stdout);
    diag_rec_index_ = 0;
}

void SparkAudioCodec::InjectVirtualMicAudio(const int16_t* data, int samples) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Limit buffer to max 2 seconds (32000 samples) to prevent memory growth
    if (virtual_mic_buf_.size() > 32000) {
        virtual_mic_buf_.erase(virtual_mic_buf_.begin(), virtual_mic_buf_.begin() + (virtual_mic_buf_.size() - 32000));
    }
    virtual_mic_buf_.insert(virtual_mic_buf_.end(), data, data + samples);
}

int SparkAudioCodec::Read(int16_t* dest, int samples) {
    // Check if Virtual Mic audio from Phone is available
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!virtual_mic_buf_.empty()) {
            int to_copy = std::min(samples, (int)virtual_mic_buf_.size());
            std::copy(virtual_mic_buf_.begin(), virtual_mic_buf_.begin() + to_copy, dest);
            virtual_mic_buf_.erase(virtual_mic_buf_.begin(), virtual_mic_buf_.begin() + to_copy);

            if (to_copy < samples) {
                std::memset(dest + to_copy, 0, (samples - to_copy) * sizeof(int16_t));
            }
            return samples;
        }
    }

    if (!input_enabled_ || !rx_handle_) {
        std::memset(dest, 0, samples * sizeof(int16_t));
        return samples;
    }

    // MONO mode: each I2S read gives one 32-bit sample per frame
    if (rx_temp_buf_samples_ < samples) {
        if (rx_temp_buf_) heap_caps_free(rx_temp_buf_);
        rx_temp_buf_ = (int32_t*)heap_caps_malloc(samples * sizeof(int32_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        rx_temp_buf_samples_ = samples;
    }

    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(rx_handle_, rx_temp_buf_, samples * sizeof(int32_t), &bytes_read, pdMS_TO_TICKS(100));
    if (err != ESP_OK || bytes_read == 0) {
        std::memset(dest, 0, samples * sizeof(int16_t));
        return samples;
    }

    int read_samples = bytes_read / sizeof(int32_t);
    for (int i = 0; i < read_samples; i++) {
        // INMP441 outputs 24-bit audio sign-extended in 32-bit container
        // Shift right by 14 to convert to 16-bit signed PCM
        dest[i] = (int16_t)(rx_temp_buf_[i] >> 14);
    }

    if (read_samples < samples) {
        std::memset(dest + read_samples, 0, (samples - read_samples) * sizeof(int16_t));
    }

    // Capture diagnostic recording if active
    if (diag_recording_active_ && diag_rec_buf_) {
        for (int i = 0; i < read_samples && diag_rec_index_ < 80000; i++) {
            diag_rec_buf_[diag_rec_index_++] = dest[i];
        }
    }

    return samples;
}

int SparkAudioCodec::Write(const int16_t* data, int samples) {
    if (!output_enabled_ || !tx_handle_) {
        return 0;
    }

    // Software volume scaling with 4x gain boost for PCM5101 DAC (GPIO 47)
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
