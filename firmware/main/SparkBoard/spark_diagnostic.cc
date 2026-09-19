#include "spark_diagnostic.h"
#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2c.h>
#include <driver/i2s_std.h>
#include <cmath>
#include <climits>
#include <cstring>
#include <algorithm>

extern "C" {
#include "I2C_Driver.h"
#include "TCA9554PWR.h"
}

#define TAG "S_TIER_DIAGNOSTIC"

// I2S Microphone Pins (External INMP441 on Header — Waveshare Official)
#define DIAG_MIC_BCLK (GPIO_NUM_13)  // SCK -> Header Pin 1 (GPIO13)
#define DIAG_MIC_WS   (GPIO_NUM_12)  // WS  -> Header Pin 3 (GPIO12)
#define DIAG_MIC_DIN  (GPIO_NUM_3)   // SD  -> Header Pin 8 (GPIO3)

extern "C" void Spark_RunFullDiagnosticSuite(void) {
    SparkDiagnostic::RunFullDiagnosticSuite();
}

void SparkDiagnostic::RunFullDiagnosticSuite() {
    ESP_LOGI(TAG, "===============================================================================");
    ESP_LOGI(TAG, "      S-TIER ESP32-S3 HARDWARE & AUDIO FORENSIC DIAGNOSTIC SUITE START         ");
    ESP_LOGI(TAG, "===============================================================================");

    LogSystemHealthTelemetry();
    TestI2cBusScan();
    TestExioExpander();
    AnalyzeI2sRawMicStream();

    ESP_LOGI(TAG, "===============================================================================");
    ESP_LOGI(TAG, "       S-TIER ESP32-S3 HARDWARE & AUDIO FORENSIC DIAGNOSTIC SUITE END          ");
    ESP_LOGI(TAG, "===============================================================================");
}

void SparkDiagnostic::LogSystemHealthTelemetry() {
    ESP_LOGI(TAG, "--- [1/4] SYSTEM MEMORY & CORE TELEMETRY ---");
    size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t internal_min  = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    size_t psram_free    = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t dma_free      = heap_caps_get_free_size(MALLOC_CAP_DMA);

    ESP_LOGI(TAG, "  Internal 8-bit RAM Free: %zu bytes (Min ever: %zu bytes)", internal_free, internal_min);
    ESP_LOGI(TAG, "  Internal DMA RAM Free:   %zu bytes", dma_free);
    ESP_LOGI(TAG, "  External PSRAM Free:     %zu bytes", psram_free);
    ESP_LOGI(TAG, "  Current CPU Task Core:   Core %d", xPortGetCoreID());
    ESP_LOGI(TAG, "  Current Task Stack Free: %d words", uxTaskGetStackHighWaterMark(NULL));
}

void SparkDiagnostic::TestI2cBusScan() {
    ESP_LOGI(TAG, "--- [2/4] HARDWARE I2C BUS DISCOVERY (SCL=GPIO10, SDA=GPIO11) ---");

    int devices_found = 0;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_NUM_0, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);

        if (ret == ESP_OK) {
            devices_found++;
            const char* dev_name = "Unknown I2C Device";
            if (addr == 0x20) dev_name = "TCA9554PWR GPIO Expander (Power Controller)";
            else if (addr == 0x38) dev_name = "SPD2010 Display/Touch Controller";
            else if (addr == 0x51) dev_name = "PCF85063 RTC Clock";
            else if (addr == 0x6B) dev_name = "QMI8658 6-Axis Motion IMU";

            ESP_LOGI(TAG, "  [I2C ACK] Found device at address 0x%02X -> %s", addr, dev_name);
        }
    }

    if (devices_found == 0) {
        ESP_LOGE(TAG, "  [FAIL] NO I2C DEVICES ACKNOWLEDGED ON GPIO10/GPIO11! Check I2C bus initialization!");
    } else {
        ESP_LOGI(TAG, "  [SUCCESS] I2C Bus Scan Completed. Total devices detected: %d", devices_found);
    }
}

void SparkDiagnostic::TestExioExpander() {
    ESP_LOGI(TAG, "--- [3/4] EXIO EXPANDER (TCA9554PWR) REGISTRATION & POWER AUDIT ---");
    uint8_t input_reg = Read_EXIOS();
    ESP_LOGI(TAG, "  TCA9554 Input Register State: 0x%02X", input_reg);
    
    // Force EXIO pins output state to 0xFF (Power ON microphone, touch reset, display reset)
    Set_EXIOS(0xFF);
    ESP_LOGI(TAG, "  [ACTION] Executed Set_EXIOS(0xFF) to ensure full power to MSM261 MEMS microphone.");
}

void SparkDiagnostic::AnalyzeI2sRawMicStream() {
    ESP_LOGI(TAG, "--- [4/4] I2S RAW MEMS MICROPHONE FORENSIC SIGNAL ANALYSIS (STEREO) ---");

    i2s_chan_handle_t rx_handle = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  [FAIL] Failed to create I2S_NUM_1 channel: %s", esp_err_to_name(err));
        return;
    }

    // Use STEREO mode to read both L and R channels from INMP441
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = GPIO_NUM_NC,
            .bclk = DIAG_MIC_BCLK,
            .ws = DIAG_MIC_WS,
            .dout = GPIO_NUM_NC,
            .din = DIAG_MIC_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = (i2s_std_slot_mask_t)(I2S_STD_SLOT_LEFT | I2S_STD_SLOT_RIGHT);

    err = i2s_channel_init_std_mode(rx_handle, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "  [FAIL] Failed to init I2S_NUM_1 std mode: %s", esp_err_to_name(err));
        i2s_del_channel(rx_handle);
        return;
    }

    i2s_channel_enable(rx_handle);

    // Read buffer: stereo interleaved [L0, R0, L1, R1, ...]
    const int num_frames = 512;
    size_t buf_bytes = num_frames * 2 * sizeof(int32_t);  // *2 for L+R
    int32_t *raw_buf = (int32_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    assert(raw_buf);

    size_t bytes_read = 0;
    // Warmup reads
    for (int i = 0; i < 5; i++) {
        i2s_channel_read(rx_handle, raw_buf, buf_bytes, &bytes_read, pdMS_TO_TICKS(100));
    }

    // Capture diagnostic frame
    err = i2s_channel_read(rx_handle, raw_buf, buf_bytes, &bytes_read, pdMS_TO_TICKS(500));

    if (err != ESP_OK || bytes_read == 0) {
        ESP_LOGE(TAG, "  [FAIL] I2S Read failed! err=%s, bytes_read=%zu", esp_err_to_name(err), bytes_read);
        heap_caps_free(raw_buf);
        i2s_channel_disable(rx_handle);
        i2s_del_channel(rx_handle);
        return;
    }

    int samples_captured = bytes_read / sizeof(int32_t);
    int frames_captured = samples_captured / 2;
    ESP_LOGI(TAG, "  Read %d stereo samples (%d frames).", samples_captured, frames_captured);

    // Dump first 8 frames
    ESP_LOGI(TAG, "--- RAW STEREO FRAMES (first 8) ---");
    for (int f = 0; f < frames_captured && f < 8; f++) {
        ESP_LOGI(TAG, "  Frame[%2d] L=0x%08lX  R=0x%08lX",
            f, (unsigned long)raw_buf[f*2], (unsigned long)raw_buf[f*2+1]);
    }

    // Analyze each channel
    for (int ch = 0; ch < 2; ch++) {
        const char* ch_name = (ch == 0) ? "LEFT" : "RIGHT";
        int zero_count = 0;
        int32_t min_val = INT32_MAX, max_val = INT32_MIN;
        double energy = 0;

        for (int f = 0; f < frames_captured; f++) {
            int32_t s = raw_buf[f * 2 + ch];
            if (s == 0) zero_count++;
            if (s > max_val) max_val = s;
            if (s < min_val) min_val = s;
            int16_t v14 = (int16_t)(s >> 14);
            energy += (double)v14 * v14;
        }

        float rms = sqrtf(energy / frames_captured);
        ESP_LOGI(TAG, "  %s ch: Min=0x%08lX Max=0x%08lX Zeros=%d/%d RMS(>>14)=%.1f",
            ch_name, (unsigned long)min_val, (unsigned long)max_val, zero_count, frames_captured, rms);

        if (zero_count == frames_captured) {
            ESP_LOGE(TAG, "  [%s] ALL ZEROS — No data on this channel", ch_name);
        } else if (rms > 1.0f) {
            ESP_LOGI(TAG, "  [%s] ACTIVE — Mic data detected (RMS=%.1f)", ch_name, rms);
        }
    }

    heap_caps_free(raw_buf);
    i2s_channel_disable(rx_handle);
    i2s_del_channel(rx_handle);
}

